#include "pch.h"

#include "Engine/Object/Component/ComponentDefaults.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        struct ComponentDefaultsInternal
        {
            static void pushLookupName( vector<string>& inoutListName, const string& name )
            {
                if ( name.empty() )
                    return;
                for ( const string& existing : inoutListName )
                {
                    if ( existing == name )
                        return;
                }
                inoutListName.push_back( name );
            }

            static void collectLookupNames( const TypeInfo& typeInfo, vector<string>& inoutListName )
            {
                string typeNameStr = typeInfo._name.c_str() ? typeInfo._name.c_str() : "";
                pushLookupName( inoutListName, typeNameStr );

                string                stripped    = typeNameStr;
                constexpr string_view kCompSuffix = "Component";
                constexpr string_view kDataSuffix = "Data";
                if ( StringUtil::endsWith( stripped, kCompSuffix ) )
                    stripped = stripped.substr( 0, stripped.size() - kCompSuffix.size() );
                else if ( StringUtil::endsWith( stripped, kDataSuffix ) )
                    stripped = stripped.substr( 0, stripped.size() - kDataSuffix.size() );
                pushLookupName( inoutListName, stripped );
            }

            /**
             * @brief 기반 타입부터 파생 타입까지의 TypeInfo 를 **뿌리 → 파생** 순서로 모읍니다.
             * @details 기본값은 `<Component>` 처럼 기반 이름으로도 적을 수 있어야 한다(모든 컴포넌트에
             *          공통으로 거는 값). 예전에는 그 적용이 `Component` 생성자에서 일어났는데, 기반
             *          생성자 시점에는 가상 `getTypeInfo()` 가 파생으로 디스패치되지 않아 **언제나
             *          `Component` 노드 하나만** 적용됐다(중간 기반은 한 번도 적용된 적이 없다).
             *          생성자에서 그 호출을 걷어냈으므로, 체인 적용은 여기서 제대로 한다 — 뿌리부터
             *          적용해 파생이 마지막에 덮어쓴다.
             */
            static void collectTypeChain( const TypeInfo& typeInfo, vector<const TypeInfo*>& outListType )
            {
                outListType.clear();

                const TypeInfo* pCursor = &typeInfo;
                while ( pCursor != nullptr )
                {
                    outListType.push_back( pCursor );

                    const hashed_string parentName = pCursor->_parentFQN;
                    if ( parentName.empty() )
                        break;

                    const TypeInfo* pParent = engine::getTypeRegistry().findType( parentName );
                    if ( pParent == nullptr || pParent == pCursor )
                        break;

                    // 순환 방지 — 이미 담은 타입이면 멈춘다.
                    bool bAlready = false;
                    for ( const TypeInfo* pSeen : outListType )
                    {
                        if ( pSeen == pParent )
                        {
                            bAlready = true;
                            break;
                        }
                    }
                    if ( bAlready )
                        break;

                    pCursor = pParent;
                }

                // 뿌리 → 파생 순서로 뒤집는다.
                for ( size_t head = 0, tail = outListType.size(); head + 1 < tail; ++head, --tail )
                {
                    const TypeInfo* pTemp = outListType[head];
                    outListType[head]     = outListType[tail - 1];
                    outListType[tail - 1] = pTemp;
                }
            }

            static XmlNode findDefaultsNode( XmlNode defaultsNode, const vector<string>& listName )
            {
                for ( const string& name : listName )
                {
                    if ( name.empty() )
                        continue;
                    XmlNode node = defaultsNode.child( name.c_str() );
                    if ( node.isValid() )
                        return node;
                }
                return {};
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ComponentDefaults::ComponentDefaults()
        : _defaultsDoc{}
        , _customDefaultsPath{}
        , _defaultsMutex{}
        , _bDefaultsLoaded{ false }
    {
    }

    ComponentDefaults::~ComponentDefaults()
    {
    }

    void ComponentDefaults::ensureDefaultsLoaded()
    {
        // 이중 검사 잠금이다 — 깃발이 원자적이어야 성립한다. acquire 로 읽어야 `true` 를 본
        // 스레드가 그 앞에서 지어진 `_defaultsDoc` 도 함께 본다.
        if ( _bDefaultsLoaded.load( std::memory_order_acquire ) )
            return;

        std::scoped_lock<mutex> lock{ _defaultsMutex };
        if ( _bDefaultsLoaded.load( std::memory_order_relaxed ) )
            return;
        if ( _customDefaultsPath.empty() )
            return;

        string absPath;
        if ( _defaultsDoc.loadResource( _customDefaultsPath.c_str(), &absPath ) )
            _bDefaultsLoaded.store( true, std::memory_order_release );
    }

    void ComponentDefaults::apply( void* pInstance, const TypeInfo& typeInfo, const TypeInfo* pAliasTypeInfo )
    {
        if ( pInstance == nullptr )
            return;

        ensureDefaultsLoaded();
        if ( _bDefaultsLoaded.load( std::memory_order_acquire ) == false )
            return;

        // **`reloadDefaults()` 는 컴포넌트를 만드는 중에 부르면 안 된다.** 여기서부터 문서를
        // 락 없이 읽는다(컴포넌트 생성마다 도는 자리라 잠그면 직렬화된다). 다시 읽는 일은
        // 개발 중 한 번씩 일어나는 일이므로, 그 순간에 생성이 돌지 않게 하는 것은 부르는
        // 쪽의 몫이다.

        XmlNode root = _defaultsDoc.root( "GameData" );
        if ( root.isValid() == false )
            return;

        XmlNode defaultsNode = root.child( "Defaults" );
        if ( defaultsNode.isValid() == false )
            return;

        // 뿌리 기반 타입부터 적용해 파생이 마지막에 덮어쓴다. 별칭 타입은 파생과 같은 단계로 본다.
        vector<const TypeInfo*> listType;
        ComponentDefaultsInternal::collectTypeChain( typeInfo, listType );

        for ( const TypeInfo* pLevelType : listType )
        {
            if ( pLevelType == nullptr )
                continue;

            vector<string> listName;
            ComponentDefaultsInternal::collectLookupNames( *pLevelType, listName );
            if ( pAliasTypeInfo != nullptr && pLevelType == &typeInfo )
                ComponentDefaultsInternal::collectLookupNames( *pAliasTypeInfo, listName );

            const XmlNode levelNode = ComponentDefaultsInternal::findDefaultsNode( defaultsNode, listName );
            if ( levelNode.isValid() == false )
                continue;

            applyNodeToProperties( pInstance, *pLevelType, levelNode );
        }
    }

    void ComponentDefaults::applyNodeToProperties( void* pInstance, const TypeInfo& typeInfo, const XmlNode& compNode )
    {
        typeInfo.forEachProperty( [&]( const PropertyInfo& prop )
        {
            const utf8* pPropName = prop._name.c_str();
            if ( pPropName == nullptr )
                return;
            const utf8* pAttrVal = compNode.attribute( pPropName );
            if ( pAttrVal == nullptr )
            {
                for ( const hashed_string& alias : prop._listAlias )
                {
                    const utf8* pAliasName = alias.c_str();
                    if ( pAliasName == nullptr )
                        continue;
                    pAttrVal = compNode.attribute( pAliasName );
                    if ( pAttrVal != nullptr )
                        break;
                }
            }
            if ( pAttrVal == nullptr )
                return;

            void* pPropPtr = prop.getRawPtr( pInstance );
            parseTextValueCoerced( pPropPtr, prop._typeName, pAttrVal, SerializeContext::getDefault() );
        } );
    }

    void ComponentDefaults::apply( Component* pComp, const TypeInfo& typeInfo )
    {
        apply( pComp, typeInfo, nullptr );
    }

    void ComponentDefaults::setPath( string_view path )
    {
        std::scoped_lock<mutex> lock{ _defaultsMutex };
        _customDefaultsPath = path;
        _bDefaultsLoaded.store( false, std::memory_order_release );
    }

    string ComponentDefaults::getPath() const
    {
        // **값으로 돌려준다.** `string_view` 를 주면 락을 놓은 뒤의 뷰가 되고, 그 사이
        // `setPath` 가 문자열을 갈아 끼우면 사라진 버퍼를 가리킨다.
        std::scoped_lock<mutex> lock{ _defaultsMutex };
        return _customDefaultsPath;
    }

    void ComponentDefaults::reload()
    {
        std::scoped_lock<mutex> lock{ _defaultsMutex };
        _bDefaultsLoaded.store( false, std::memory_order_release );
    }

    void ComponentDefaults::applyDefaults( void* pInstance, const TypeInfo& typeInfo, const TypeInfo* pAliasTypeInfo )
    {
        if ( engine::areEngineServicesBound() )
            engine::getComponentDefaults().apply( pInstance, typeInfo, pAliasTypeInfo );
    }

    void ComponentDefaults::applyDefaults( Component* pComp, const TypeInfo& typeInfo )
    {
        if ( engine::areEngineServicesBound() )
            engine::getComponentDefaults().apply( pComp, typeInfo );
    }

    void ComponentDefaults::setDefaultsPath( string_view path )
    {
        if ( engine::areEngineServicesBound() )
            engine::getComponentDefaults().setPath( path );
    }

    string ComponentDefaults::getDefaultsPath()
    {
        if ( engine::areEngineServicesBound() )
            return engine::getComponentDefaults().getPath();
        return {};
    }

    void ComponentDefaults::reloadDefaults()
    {
        if ( engine::areEngineServicesBound() )
            engine::getComponentDefaults().reload();
    }
} // namespace sw
