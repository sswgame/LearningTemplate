#include "pch.h"

#include "Engine/Reflection/ReflectionCore.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionConstants.h"

namespace sw
{
    namespace
    {
        struct ReflectionCoreInternal
        {
            /**
             * @brief 부모 체인을 걸을 때 **이미 지나온 타입이면 멈추라고** 알려 줍니다.
             * @details `_parentFQN` 은 코드젠이 적는 값이지만 `registerClass` 는 공개 API 이고
             *          그 값을 검사하지 않는다. 모듈이 따로따로 등록되는 핫리로드에서는 A→B→A
             *          가 만들어질 수 있고, 그러면 체인을 거는 쪽이 멈추지 않는다(루프면 행,
             *          재귀면 스택 오버플로). 체인은 보통 다섯을 넘지 않으므로 지나온 것을
             *          적어 두는 값이 싸다.
             * @return 처음 보는 타입이면 true(계속 걸어도 된다), 이미 지나왔으면 false.
             */
            static bool markVisitedOrStop( vector<const TypeInfo*>& inoutListVisited, const TypeInfo* pType )
            {
                for ( const TypeInfo* pVisited : inoutListVisited )
                {
                    if ( pVisited == pType )
                        return false;
                }
                inoutListVisited.push_back( pType );
                return true;
            }

            /**
             * @brief canonical FQN 의 네임스페이스를 alias 앞에 붙입니다.
             * @details REFLECT(Alias=Foo) 는 리프 이름만 적으므로, registerClass 가 FQN·리프를
             *          모두 등록하는 것과 맞추려면 별칭도 FQN 형태를 함께 등록해야 합니다.
             * @return 네임스페이스가 없거나 alias 가 이미 한정되어 있으면 빈 문자열.
             */
            static string qualifyAliasWithNamespace( const utf8* pAliasName, const utf8* pCanonicalName )
            {
                const string_view alias{ pAliasName };
                const string_view canonical{ pCanonicalName };
                if ( alias.find( constants::reflection::kScopeDelimiter ) != string_view::npos )
                    return {};

                const size_t lastScope = canonical.rfind( constants::reflection::kScopeDelimiter );
                if ( lastScope == string_view::npos )
                    return {};

                string qualified{ canonical.substr( 0, lastScope + 2 ) };
                qualified.append( alias.data(), alias.size() );
                return qualified;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    TypeMetadata::TypeMetadata() noexcept
#if !defined( SW_SHIPPING )
        : _category{ constants::reflection::kDefaultCategory }
        , _displayName{}
        , _tooltip{}
        , _mapCustomMeta{}
        , _bHideInMenu{ SW_FALSE }
        , _reservedFlags{ 0 }
#else
        : _reservedEmpty{ 0 }
#endif
    {
    }

    PropertyMetadata::PropertyMetadata() noexcept
#if !defined( SW_SHIPPING )
        : _category{ constants::reflection::kDefaultCategory }
        , _displayName{}
        , _tooltip{}
        , _mapCustomMeta{}
        , _defaultValue{}
#else
        : _defaultValue{}
#endif
        , _assetType{}
        , _minRange{ 0.0f }
        , _maxRange{ 1.0f }
        , _bHasRange{ SW_FALSE }
        , _bReadOnly{ SW_FALSE }
        , _bXmlAttribute{ SW_FALSE }
        , _bAssetPath{ SW_FALSE }
        , _bPolymorphic{ SW_FALSE }
        , _bTransient{ SW_FALSE }
        , _bSkipIfEmpty{ SW_FALSE }
#if !defined( SW_SHIPPING )
        , _bHideInInspector{ SW_FALSE }
#else
        , _reservedFlags{ 0 }
#endif
    {
    }

    FunctionMetadata::FunctionMetadata() noexcept
#if !defined( SW_SHIPPING )
        : _category{ constants::reflection::kDefaultCategory }
        , _displayName{}
        , _tooltip{}
        , _mapCustomMeta{}
        , _netRole{ FunctionNetRole::Local }
#else
        : _netRole{ FunctionNetRole::Local }
#endif
        , _bReliable{ SW_FALSE }
        , _bValidate{ SW_FALSE }
        , _bConstructor{ SW_FALSE }
        , _bStatic{ SW_FALSE }
        , _bConst{ SW_FALSE }
#if !defined( SW_SHIPPING )
        , _bCallInEditor{ SW_FALSE }
        , _reserved{ 0 }
#else
        , _reserved{ 0 }
#endif
    {
    }

    PropertyInfo::PropertyInfo() noexcept
        : _containerWrapper{ nullptr }
        , _nestedContainer{ nullptr }
        , _onPropertyBoundChanged{}
        , _offset{ 0 }
        , _name{}
        , _typeName{}
        , _elementTypeName{}
        , _keyTypeName{}
        , _listAlias{}
        , _metadata{}
        , _cachedNameHash{ 0 }
        , _bitOffset{ 0 }
        , _containerKind{ ContainerKind::None }
        , _bitMask{ MathUtil::MaxUInt8 }
        , _bIsContainer{ SW_FALSE }
        , _bIsBitField{ SW_FALSE }
        , _reservedFlags{ 0 } {}

    NestedContainerInfo PropertyInfo::getContainerShape() const
    {
        if ( _nestedContainer != nullptr )
            return *_nestedContainer;

        NestedContainerInfo flat{};
        flat._kind            = _containerKind;
        flat._typeName        = _typeName;
        flat._elementTypeName = _elementTypeName;
        flat._keyTypeName     = _keyTypeName;
        flat._wrapper         = _containerWrapper;
        return flat;
    }

    PropertyInfo::PropertyInfo( hashed_string name, hashed_string typeName, size_t offset,
                                bool bIsContainer, ContainerKind containerKind,
                                hashed_string elementTypeName, hashed_string keyTypeName,
                                shared_ptr<IContainerWrapper> containerWrapper,
                                hashed_string                 alias )
        : _containerWrapper{ std::move( containerWrapper ) }
        , _nestedContainer{ nullptr }
        , _onPropertyBoundChanged{}
        , _offset{ offset }
        , _name{ name }
        , _typeName{ typeName }
        , _elementTypeName{ elementTypeName }
        , _keyTypeName{ keyTypeName }
        , _listAlias{}
        , _metadata{}
        , _cachedNameHash{ 0 }
        , _bitOffset{ 0 }
        , _containerKind{ containerKind }
        , _bitMask{ MathUtil::MaxUInt8 }
        , _bIsContainer{ static_cast<uint8>( bIsContainer ? SW_TRUE : SW_FALSE ) }
        , _bIsBitField{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
        if ( alias.empty() == false )
            _listAlias.push_back( alias );
    }

    EnumInfo::EnumInfo() noexcept
        : _mapNameToValue{}
        , _mapValueToName{}
#if !defined( SW_SHIPPING )
        , _mapCustomMeta{}
#endif
        , _name{}
        , _fullyQualifiedName{}
        , _moduleName{}
        , _invalidValue{ 0 }
        , _countValue{ 0 }
        , _size{ sizeof( int32 ) }
        , _bIsBitFlag{ SW_FALSE }
        , _bHasInvalid{ SW_FALSE }
        , _bHasCount{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }

    TypeInfo::TypeInfo() noexcept
        : _size{ 0 }
        , _destroyInstance{ nullptr }
        , _name{}
        , _fullyQualifiedName{}
        , _parentFQN{}
        , _moduleName{}
        , _listProperty{}
        , _listMethod{}
        , _metadata{}
        , _listPropertyWithBase{}
        , _mapNameToProperty{}
        , _mapNameToMethod{}
        , _typeId{ 0 }
        , _bAbstract{ SW_FALSE }
        , _bStatic{ SW_FALSE }
        , _bPrimitive{ SW_FALSE }
        , _bIsCacheBuilt{ SW_FALSE }
        , _bIsPODFastPath{ SW_FALSE }
        , _bIsPODCalculated{ SW_FALSE }
        , _bListPropertyWithBaseBuilt{ SW_FALSE }
        , _bBuildingPropertyWithBase{ SW_FALSE }
        , _reservedPadding{ 0, 0, 0 } {}

    TypeInfo::TypeInfo( const TypeInfo& other )
        : _size{ other._size }
        , _destroyInstance{ other._destroyInstance }
        , _name{ other._name }
        , _fullyQualifiedName{ other._fullyQualifiedName }
        , _parentFQN{ other._parentFQN }
        , _moduleName{ other._moduleName }
        , _listProperty{ other._listProperty }
        , _listMethod{ other._listMethod }
        , _metadata{ other._metadata }
        , _listPropertyWithBase{}
        , _mapNameToProperty{}
        , _mapNameToMethod{}
        , _typeId{ other._typeId }
        , _bAbstract{ other._bAbstract }
        , _bStatic{ other._bStatic }
        , _bPrimitive{ other._bPrimitive }
        , _bIsCacheBuilt{ SW_FALSE }
        , _bIsPODFastPath{ SW_FALSE }
        , _bIsPODCalculated{ SW_FALSE }
        , _bListPropertyWithBaseBuilt{ SW_FALSE }
        , _bBuildingPropertyWithBase{ SW_FALSE }
        , _reservedPadding{ 0, 0, 0 }
    {
    }

    TypeInfo::TypeInfo( TypeInfo&& other ) noexcept
        : _size{ other._size }
        , _destroyInstance{ other._destroyInstance }
        , _name{ other._name }
        , _fullyQualifiedName{ other._fullyQualifiedName }
        , _parentFQN{ other._parentFQN }
        , _moduleName{ other._moduleName }
        , _listProperty{ std::move( other._listProperty ) }
        , _listMethod{ std::move( other._listMethod ) }
        , _metadata{ std::move( other._metadata ) }
        , _listPropertyWithBase{}
        , _mapNameToProperty{}
        , _mapNameToMethod{}
        , _typeId{ other._typeId }
        , _bAbstract{ other._bAbstract }
        , _bStatic{ other._bStatic }
        , _bPrimitive{ other._bPrimitive }
        , _bIsCacheBuilt{ SW_FALSE }
        , _bIsPODFastPath{ SW_FALSE }
        , _bIsPODCalculated{ SW_FALSE }
        , _bListPropertyWithBaseBuilt{ SW_FALSE }
        , _bBuildingPropertyWithBase{ SW_FALSE }
        , _reservedPadding{ 0, 0, 0 }
    {
        other._typeId          = 0;
        other._size            = 0;
        other._destroyInstance = nullptr;
        other._bIsCacheBuilt   = SW_FALSE;
    }

    TypeInfo& TypeInfo::operator=( const TypeInfo& other )
    {
        if ( this == &other )
            return *this;

        _size               = other._size;
        _destroyInstance    = other._destroyInstance;
        _name               = other._name;
        _fullyQualifiedName = other._fullyQualifiedName;
        _parentFQN          = other._parentFQN;
        _moduleName         = other._moduleName;
        _listProperty       = other._listProperty;
        _listMethod         = other._listMethod;
        _metadata           = other._metadata;
        _typeId             = other._typeId;
        _bAbstract          = other._bAbstract;
        _bStatic            = other._bStatic;
        _bPrimitive         = other._bPrimitive;

        _listPropertyWithBase.clear();
        _mapNameToProperty.clear();
        _mapNameToMethod.clear();
        _bIsCacheBuilt              = SW_FALSE;
        _bIsPODFastPath             = SW_FALSE;
        _bIsPODCalculated           = SW_FALSE;
        _bListPropertyWithBaseBuilt = SW_FALSE;
        _bBuildingPropertyWithBase  = SW_FALSE;

        return *this;
    }

    TypeInfo& TypeInfo::operator=( TypeInfo&& other ) noexcept
    {
        if ( this == &other )
            return *this;

        _size               = other._size;
        _destroyInstance    = other._destroyInstance;
        _name               = other._name;
        _fullyQualifiedName = other._fullyQualifiedName;
        _parentFQN          = other._parentFQN;
        _moduleName         = other._moduleName;
        _listProperty       = std::move( other._listProperty );
        _listMethod         = std::move( other._listMethod );
        _metadata           = std::move( other._metadata );
        _typeId             = other._typeId;
        _bAbstract          = other._bAbstract;
        _bStatic            = other._bStatic;
        _bPrimitive         = other._bPrimitive;

        _listPropertyWithBase.clear();
        _mapNameToProperty.clear();
        _mapNameToMethod.clear();
        _bIsCacheBuilt              = SW_FALSE;
        _bIsPODFastPath             = SW_FALSE;
        _bIsPODCalculated           = SW_FALSE;
        _bListPropertyWithBaseBuilt = SW_FALSE;
        _bBuildingPropertyWithBase  = SW_FALSE;

        other._typeId          = 0;
        other._size            = 0;
        other._destroyInstance = nullptr;
        other._bIsCacheBuilt   = SW_FALSE;

        return *this;
    }

    bool TypeInfo::usesPodCopyFastPath() const
    {
        if ( _bIsPODCalculated == SW_TRUE )
            return _bIsPODFastPath == SW_TRUE;

        TypeRegistry& registry = engine::getTypeRegistry();

        static const hashed_string kArrDynamicTypes[] = {
            hashed_string{ PredefinedNameType::NameType_string },
            hashed_string{ PredefinedNameType::NameType_hashed_string },
        };

        _bIsPODFastPath = SW_TRUE;
        for ( const PropertyInfo& prop : _listProperty )
        {
            if ( prop._bIsContainer == SW_TRUE || prop._containerKind != ContainerKind::None )
            {
                _bIsPODFastPath = SW_FALSE;
                break;
            }

            bool bIsDynamicType = false;
            for ( const hashed_string& dynamicType : kArrDynamicTypes )
            {
                if ( registry.isType( prop._typeName, dynamicType ) )
                {
                    bIsDynamicType = true;
                    break;
                }
            }

            if ( bIsDynamicType )
            {
                _bIsPODFastPath = SW_FALSE;
                break;
            }
        }
        _bIsPODCalculated = SW_TRUE;
        return _bIsPODFastPath;
    }

    const vector<PropertyInfo>& TypeInfo::getPropertiesWithBase() const
    {
        if ( _parentFQN.empty() )
            return _listProperty;

        if ( _bListPropertyWithBaseBuilt == SW_TRUE )
            return _listPropertyWithBase;

        // **순환에서 멈춘다.** 이 타입에서 이미 짓는 중인데 다시 들어왔다는 것은 부모 체인이
        // 돌아왔다는 뜻이다 — 더 올라가면 스택이 넘친다. 자기 것만 돌려주고 끊는다.
        if ( _bBuildingPropertyWithBase == SW_TRUE )
        {
            SW_LOG_ERROR( "Reflection parent chain loops at '%#' — returning own properties only.",
                          _fullyQualifiedName.c_str() );
            return _listProperty;
        }
        _bBuildingPropertyWithBase = SW_TRUE;

        const TypeInfo*             pParent      = engine::getTypeRegistry().findType( _parentFQN );
        const vector<PropertyInfo>* pParentProps = ( pParent != nullptr ) ? &pParent->getPropertiesWithBase() : nullptr;
        const size_t                totalCount   = ( pParentProps != nullptr ? pParentProps->size() : 0 ) + _listProperty.size();

        _listPropertyWithBase.clear();
        _listPropertyWithBase.reserve( totalCount );
        if ( pParentProps != nullptr )
            _listPropertyWithBase = *pParentProps;

        for ( const PropertyInfo& prop : _listProperty )
        {
            bool replaced{ false };
            for ( PropertyInfo& existing : _listPropertyWithBase )
            {
                if ( existing._name == prop._name )
                {
                    existing = prop;
                    replaced = true;
                    break;
                }
            }
            if ( replaced == false )
                _listPropertyWithBase.push_back( prop );
        }

        _bListPropertyWithBaseBuilt = SW_TRUE;
        _bBuildingPropertyWithBase  = SW_FALSE;
        return _listPropertyWithBase;
    }

    namespace generated
    {
        void forceLinkBuiltinTypes();
    } // namespace generated

    TypeRegistry::TypeRegistry()
    {
        generated::forceLinkBuiltinTypes();
    }
    TypeRegistry::~TypeRegistry() = default;

    static atomic<uint32> _s_typeIdCounter{ 0 }; // Local Runtime Index (Not Serialized, 100% Cross-Platform Safe)

    void TypeRegistry::registerClass( const TypeInfo& info )
    {
        std::unique_lock<std::shared_mutex> lock{ _mutex };

        TypeInfo stored = info;
        if ( _activeModuleName.empty() == false )
            stored._moduleName = _activeModuleName;
        else if ( stored._moduleName.empty() )
            stored._moduleName = hashed_string( constants::reflection::kDefaultModuleName );

        // 타입 하나가 곧 TypeInfo 하나다. 예전엔 FQN 키와 짧은 이름 키에 **각각 복사본**을 넣어서,
        // 같은 타입이라도 `findType("sw::Foo")` 와 `findType("Foo")` 가 서로 다른 포인터를 돌려줬다.
        // `const TypeInfo*` 를 키로 쓰는 쪽(GameObjectManager 의 컴포넌트 풀)은 그 둘을 다른 타입으로
        // 보고 조회에 실패했고, 풀에서 꺼낸 메모리를 힙 해제로 반납해 힙을 깨뜨렸다.
        const hashed_string canonicalKey =
            stored._fullyQualifiedName.empty() == false ? stored._fullyQualifiedName : stored._name;

        auto existingIt = _mapFqnToClassType.find( canonicalKey );
        if ( existingIt != _mapFqnToClassType.end() )
            stored._typeId = existingIt->second._typeId;
        else
            stored._typeId = _s_typeIdCounter.fetch_add( 1, std::memory_order_relaxed ) + 1;

        const hashed_string canonicalName = stored._name.empty() == false ? stored._name : stored._fullyQualifiedName;

        // 여기서 캐시를 만들어 두지 않는다 — **다음 등록에서 맵이 커지면 날아간다.**
        // `sw::unordered_map` 은 밀집 배열이라 커질 때 원소를 옮기고, `TypeInfo` 이동 생성자는
        // `mutable` 캐시를 비운다. 그래서 캐시는 **배치 등록이 끝난 뒤** 한 번에 만든다
        // (`buildLookupCaches`).
        _mapFqnToClassType.insert_or_assign( canonicalKey, stored );
        _mapHashToCanonicalName.insert_or_assign( canonicalKey.getHash(), canonicalName );
        if ( stored._name.empty() == false && stored._name != canonicalKey )
        {
            _mapAliasToFqn.insert_or_assign( stored._name, canonicalKey );
            _mapHashToCanonicalName.insert_or_assign( stored._name.getHash(), canonicalName );
        }
    }

    void TypeRegistry::registerEnum( const EnumInfo& info )
    {
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        EnumInfo                            stored = info;
        if ( _activeModuleName.empty() == false )
            stored._moduleName = _activeModuleName;
        else if ( stored._moduleName.empty() )
            stored._moduleName = hashed_string( constants::reflection::kDefaultModuleName );

        _mapNameToEnum.insert_or_assign( stored._fullyQualifiedName, stored );
        if ( stored._name.empty() == false && stored._name != stored._fullyQualifiedName )
            _mapNameToEnum.insert_or_assign( stored._name, stored );
    }

    void TypeRegistry::registerPendingTypes( string_view moduleName, TypeRegistrar* pClassHead, EnumRegistrar* pEnumHead )
    {
        _activeModuleName = hashed_string( moduleName.data(), static_cast<uint32>( moduleName.size() ) );

        TypeRegistrar* pCurrClass = pClassHead;
        while ( pCurrClass != nullptr )
        {
            if ( pCurrClass->_registerFunc != nullptr )
                pCurrClass->_registerFunc( *this );
            pCurrClass = pCurrClass->_pNext;
        }

        EnumRegistrar* pCurrEnum = pEnumHead;
        while ( pCurrEnum != nullptr )
        {
            if ( pCurrEnum->_registerFunc != nullptr )
                pCurrEnum->_registerFunc( *this );
            pCurrEnum = pCurrEnum->_pNext;
        }

        _activeModuleName = hashed_string();

        // 이 배치의 마지막 삽입까지 끝난 지금 캐시를 만든다 — 여기가 아직 단일 스레드다.
        buildLookupCaches();
    }

    void TypeRegistry::buildLookupCaches() const
    {
        vector<const TypeInfo*> listType;
        {
            std::shared_lock<std::shared_mutex> lock{ _mutex };
            listType.reserve( _mapFqnToClassType.size() );
            for ( const auto& [fqn, info] : _mapFqnToClassType )
            {
                (void)fqn;
                listType.push_back( &info );
            }
        }

        // **잠금 밖에서** 만든다 — getPropertiesWithBase 가 부모를 찾으려고 레지스트리를 다시
        // 잠그는데, shared_mutex 는 재귀가 아니라서 잠금 안에서 부르면 그 자리에서 멈춘다.
        for ( const TypeInfo* pType : listType )
        {
            if ( pType == nullptr )
                continue;
            pType->buildLookupCache();
            (void)pType->getPropertiesWithBase();
        }
    }

#if !defined( SW_SHIPPING )
    void TypeRegistry::unregisterTypesByModule( string_view moduleName )
    {
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        hashed_string                       hashModule( moduleName.data(), static_cast<uint32>( moduleName.size() ) );

        for ( auto it = _mapFqnToClassType.begin(); it != _mapFqnToClassType.end(); )
        {
            if ( it->second._moduleName == hashModule )
                it = _mapFqnToClassType.erase( it );
            else
                ++it;
        }

        // 사라진 타입을 가리키던 별칭도 같이 걷어낸다. 남겨두면 조회가 빈 항목을 타고 nullptr 를 낸다.
        for ( auto it = _mapAliasToFqn.begin(); it != _mapAliasToFqn.end(); )
        {
            if ( _mapFqnToClassType.find( it->second ) == _mapFqnToClassType.end() )
                it = _mapAliasToFqn.erase( it );
            else
                ++it;
        }

        for ( auto it = _mapNameToEnum.begin(); it != _mapNameToEnum.end(); )
        {
            if ( it->second._moduleName == hashModule )
                it = _mapNameToEnum.erase( it );
            else
                ++it;
        }

        _mapHashToCanonicalName.clear();
        for ( const auto& [fqn, info] : _mapFqnToClassType )
        {
            const hashed_string canonicalName = info._name.empty() == false ? info._name : info._fullyQualifiedName;
            _mapHashToCanonicalName.insert_or_assign( fqn.getHash(), canonicalName );
        }
        for ( const auto& [alias, fqn] : _mapAliasToFqn )
        {
            const auto typeIt = _mapFqnToClassType.find( fqn );
            if ( typeIt == _mapFqnToClassType.end() )
                continue;
            const TypeInfo&     info          = typeIt->second;
            const hashed_string canonicalName = info._name.empty() == false ? info._name : info._fullyQualifiedName;
            _mapHashToCanonicalName.insert_or_assign( alias.getHash(), canonicalName );
        }
    }
#endif

    void TypeRegistry::registerTypeAlias( const utf8* pAliasName, const utf8* pCanonicalName )
    {
        if ( pAliasName == nullptr || pCanonicalName == nullptr )
            return;
        if ( StringUtil::equals( pAliasName, pCanonicalName ) )
            return;

        std::unique_lock<std::shared_mutex> lock{ _mutex };

        // pCanonicalName 자체가 짧은 이름(=별칭)일 수 있으니 FQN 까지 한 번 더 따라간다.
        hashed_string canonicalKey{ pCanonicalName };
        if ( _mapFqnToClassType.find( canonicalKey ) == _mapFqnToClassType.end() )
        {
            const auto redirectIt = _mapAliasToFqn.find( canonicalKey );
            if ( redirectIt == _mapAliasToFqn.end() )
                return;
            canonicalKey = redirectIt->second;
        }

        const auto typeIt = _mapFqnToClassType.find( canonicalKey );
        if ( typeIt == _mapFqnToClassType.end() )
            return;

        // insert_or_assign: 핫리로드 재등록 시 옛 별칭이 남지 않게 함.
        const TypeInfo&     stored        = typeIt->second;
        const hashed_string canonicalName = stored._name.empty() == false ? stored._name : stored._fullyQualifiedName;
        const hashed_string aliasHash{ pAliasName };

        _mapAliasToFqn.insert_or_assign( aliasHash, canonicalKey );
        _mapHashToCanonicalName.insert_or_assign( aliasHash.getHash(), canonicalName );

        const string qualified = ReflectionCoreInternal::qualifyAliasWithNamespace( pAliasName, pCanonicalName );
        if ( qualified.empty() == false )
        {
            const hashed_string qualHash{ qualified.c_str() };
            _mapAliasToFqn.insert_or_assign( qualHash, canonicalKey );
            _mapHashToCanonicalName.insert_or_assign( qualHash.getHash(), canonicalName );
        }
    }

    void TypeRegistry::registerEnumAlias( const utf8* pAliasName, const utf8* pCanonicalName )
    {
        if ( pAliasName == nullptr || pCanonicalName == nullptr )
            return;
        if ( StringUtil::equals( pAliasName, pCanonicalName ) )
            return;

        std::unique_lock<std::shared_mutex> lock{ _mutex };
        auto                                it = _mapNameToEnum.find( hashed_string( pCanonicalName ) );
        if ( it == _mapNameToEnum.end() )
            return;

        const EnumInfo stored = it->second;
        _mapNameToEnum.insert_or_assign( hashed_string( pAliasName ), stored );

        const string qualified = ReflectionCoreInternal::qualifyAliasWithNamespace( pAliasName, pCanonicalName );
        if ( qualified.empty() == false )
            _mapNameToEnum.insert_or_assign( hashed_string( qualified.c_str() ), stored );
    }

    const TypeInfo* TypeRegistry::findType( const hashed_string& nameOrFqn ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };

        const auto it = _mapFqnToClassType.find( nameOrFqn );
        if ( it != _mapFqnToClassType.end() )
            return &it->second;

        const auto aliasIt = _mapAliasToFqn.find( nameOrFqn );
        if ( aliasIt == _mapAliasToFqn.end() )
            return nullptr;

        const auto canonicalIt = _mapFqnToClassType.find( aliasIt->second );
        return canonicalIt != _mapFqnToClassType.end() ? &canonicalIt->second : nullptr;
    }

    const EnumInfo* TypeRegistry::findEnum( const hashed_string& nameOrFqn ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        auto                                it = _mapNameToEnum.find( nameOrFqn );
        return it != _mapNameToEnum.end() ? &it->second : nullptr;
    }

    hashed_string TypeRegistry::canonicalTypeNameByHash( const uint32 nameHash ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        auto                                it = _mapHashToCanonicalName.find( nameHash );
        return it != _mapHashToCanonicalName.end() ? it->second : hashed_string{};
    }

    const utf8* TypeRegistry::enumToString( const hashed_string& enumName, int64 value ) const
    {
        const EnumInfo* pInfo = findEnum( enumName );
        return pInfo != nullptr ? pInfo->valueToCString( value ) : nullptr;
    }

    bool TypeRegistry::enumFromString( const hashed_string& enumName, string_view name, int64& outValue ) const
    {
        const EnumInfo* pInfo = findEnum( enumName );
        if ( pInfo == nullptr )
        {
            outValue = 0;
            return false;
        }
        if ( pInfo->tryParse( name, outValue ) && pInfo->isValidValue( outValue ) )
            return true;
        outValue = ( pInfo->_bHasInvalid != SW_FALSE ) ? pInfo->_invalidValue : 0;
        return false;
    }

    bool TypeRegistry::hasFlag( const hashed_string& enumName, int64 flags, int64 contains ) const
    {
        const EnumInfo* pInfo = findEnum( enumName );
        if ( pInfo == nullptr || pInfo->_bIsBitFlag == SW_FALSE )
            return false;
        return ( flags & contains ) == contains;
    }

    TaskValue TypeRegistry::invokeMethod( void* pInstance, const hashed_string& classFqn, const hashed_string& methodName, const TaskArgs& args ) const
    {
        const TypeInfo* pTypeInfo = findType( classFqn );
        if ( pTypeInfo != nullptr )
        {
            const FunctionInfo* pFunc = pTypeInfo->findMethod( methodName );
            if ( pFunc != nullptr && pFunc->_invoker.isBound() )
                return pFunc->_invoker( pInstance, args );
        }
        return TaskValue{};
    }

    TypeRegistrar*& TypeRegistrar::getHead()
    {
        static TypeRegistrar* s_pHead{ nullptr };
        return s_pHead;
    }

    TypeRegistrar::TypeRegistrar( void ( *registerFunc )( TypeRegistry& ) )
        : TypeRegistrar( registerFunc, getHead() )
    {
    }

    TypeRegistrar::TypeRegistrar( void ( *registerFunc )( TypeRegistry& ), TypeRegistrar*& pModuleHead )
        : _registerFunc{ registerFunc }
        , _pNext{ nullptr }
    {
        _pNext      = pModuleHead;
        pModuleHead = this;
    }

    EnumRegistrar*& EnumRegistrar::getHead()
    {
        static EnumRegistrar* s_pHead{ nullptr };
        return s_pHead;
    }

    EnumRegistrar::EnumRegistrar( void ( *registerFunc )( TypeRegistry& ) )
        : EnumRegistrar( registerFunc, getHead() )
    {
    }

    EnumRegistrar::EnumRegistrar( void ( *registerFunc )( TypeRegistry& ), EnumRegistrar*& pModuleHead )
        : _registerFunc{ registerFunc }
        , _pNext{ nullptr }
    {
        _pNext      = pModuleHead;
        pModuleHead = this;
    }
} // namespace sw
namespace sw
{

    bool TypeInfo::isDerivedFrom( const hashed_string& targetFqn ) const
    {
        if ( _fullyQualifiedName == targetFqn || _name == targetFqn )
            return true;
        if ( _parentFQN.empty() )
            return false;

        // **순환에서 멈춘다.** 여기는 `while` 이라 순환이면 영원히 돈다(재귀가 아니므로 스택도
        // 넘지 않고 그냥 멈춰 선다 — 더 알아채기 어렵다). 체인은 보통 다섯을 넘지 않으므로
        // 방문한 것을 적어 두고 다시 만나면 끊는다. `ComponentDefaults::collectTypeChain` 이
        // 이미 같은 일을 하고 있었는데 이쪽으로 옮겨지지 않았다.
        const TypeRegistry&     registry = engine::getTypeRegistry();
        const TypeInfo*         pCurrent = this;
        vector<const TypeInfo*> listVisited;
        listVisited.reserve( 8 );

        while ( pCurrent != nullptr && pCurrent->_parentFQN.empty() == false )
        {
            if ( ReflectionCoreInternal::markVisitedOrStop( listVisited, pCurrent ) == false )
                return false;

            if ( pCurrent->_parentFQN == targetFqn )
                return true;
            pCurrent = registry.findType( pCurrent->_parentFQN );
            if ( pCurrent != nullptr && ( pCurrent->_fullyQualifiedName == targetFqn || pCurrent->_name == targetFqn ) )
                return true;
        }
        return false;
    }

    const PropertyInfo* TypeInfo::findPropertyInHierarchy( const hashed_string& propNameOrAlias ) const
    {
        // 재귀였다 — 부모 체인이 순환하면 스택이 넘친다. 루프로 바꾸고 방문한 것을 적어 둔다.
        const TypeRegistry&     registry = engine::getTypeRegistry();
        const TypeInfo*         pCurrent = this;
        vector<const TypeInfo*> listVisited;
        listVisited.reserve( 8 );

        while ( pCurrent != nullptr )
        {
            if ( ReflectionCoreInternal::markVisitedOrStop( listVisited, pCurrent ) == false )
                return nullptr;

            const PropertyInfo* pProp = pCurrent->findProperty( propNameOrAlias );
            if ( pProp != nullptr )
                return pProp;

            if ( pCurrent->_parentFQN.empty() )
                return nullptr;
            pCurrent = registry.findType( pCurrent->_parentFQN );
        }
        return nullptr;
    }
} // namespace sw
