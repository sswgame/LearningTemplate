#include "pch.h"

#include "Engine/Object/Component/ComponentDefaults.h"

#include "Core/Concurrency/mutex.h"

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
        if ( _bDefaultsLoaded )
            return;
        std::scoped_lock<mutex> lock{ _defaultsMutex };
        if ( _bDefaultsLoaded )
            return;
        if ( _customDefaultsPath.empty() )
            return;

        string absPath;
        if ( _defaultsDoc.loadResource( _customDefaultsPath.c_str(), &absPath ) )
            _bDefaultsLoaded = true;
    }

    void ComponentDefaults::apply( void* pInstance, const TypeInfo& typeInfo, const TypeInfo* pAliasTypeInfo )
    {
        if ( pInstance == nullptr )
            return;

        ensureDefaultsLoaded();
        if ( _bDefaultsLoaded == false )
            return;

        XmlNode root = _defaultsDoc.root( "GameData" );
        if ( root.isValid() == false )
            return;

        XmlNode defaultsNode = root.child( "Defaults" );
        if ( defaultsNode.isValid() == false )
            return;

        vector<string> listName;
        ComponentDefaultsInternal::collectLookupNames( typeInfo, listName );
        if ( pAliasTypeInfo != nullptr )
            ComponentDefaultsInternal::collectLookupNames( *pAliasTypeInfo, listName );

        XmlNode compNode = ComponentDefaultsInternal::findDefaultsNode( defaultsNode, listName );
        if ( compNode.isValid() == false )
            return;

        typeInfo.forEachProperty( [&]( const PropertyInfo& prop )
        {
            const utf8* pPropName = prop._name.c_str();
            if ( pPropName == nullptr )
                return;
            const utf8* pAttrVal = compNode.attr( pPropName );
            if ( pAttrVal == nullptr )
            {
                for ( const hashed_string& alias : prop._listAlias )
                {
                    const utf8* pAliasName = alias.c_str();
                    if ( pAliasName == nullptr )
                        continue;
                    pAttrVal = compNode.attr( pAliasName );
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
        _bDefaultsLoaded    = false;
    }

    string_view ComponentDefaults::getPath() const
    {
        std::scoped_lock<mutex> lock{ _defaultsMutex };
        return _customDefaultsPath;
    }

    void ComponentDefaults::reload()
    {
        std::scoped_lock<mutex> lock{ _defaultsMutex };
        _bDefaultsLoaded = false;
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

    string_view ComponentDefaults::getDefaultsPath()
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
