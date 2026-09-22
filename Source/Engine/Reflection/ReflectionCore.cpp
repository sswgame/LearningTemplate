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
            /** @brief 조상 표에 적는 이름 — FQN 의 intern 인덱스, 없으면 짧은 이름의. 둘 다 없으면 None. */
            static uint32 canonicalNameIndex( const TypeInfo& type )
            {
                constexpr PredefinedNameType kNone = PredefinedNameType::NameType_None;
                return type._fullyQualifiedName.isPredefinedType( kNone ) ? type._name.getIndex() : type._fullyQualifiedName.getIndex();
            }

            /**
             * @brief 두 TypeInfo 가 같은 타입을 말하는지 — 포인터가 달라도 이름이 같으면 같은 타입.
             * @details 레지스트리는 FQN 하나당 항목 하나지만, 레지스트리 **밖**에 사본이 있을 수 있다
             *          (테스트 목의 손으로 만든 `StaticType()` 이 자기 사본을 돌려준다). 포인터 걷기가
             *          그 사본을 만나면 이름으로 한 번 더 본다 — intern 인덱스 정수 비교 한 번이다.
             */
            static bool isSameTypeName( const TypeInfo& lhs, const TypeInfo& rhs )
            {
                // 걸음마다 부르는 자리라 intern 테이블은 만지지 않는다 — `empty()` 는 길이를 보려고 테이블을
                // 읽는다(걸음당 캐시 라인 둘). 인덱스가 None 이 아니면 이름이 있는 것으로 보고, 인덱스가
                // 같을 때(사슬이 끝나는 적중)만 빈 이름을 걸러 낸다.
                constexpr PredefinedNameType kNone = PredefinedNameType::NameType_None;
                if ( lhs._fullyQualifiedName.isPredefinedType( kNone ) == false )
                    return lhs._fullyQualifiedName == rhs._fullyQualifiedName && lhs._fullyQualifiedName.empty() == false;
                return lhs._name.isPredefinedType( kNone ) == false && lhs._name == rhs._name && lhs._name.empty() == false;
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
        , _pParentType{ nullptr }
        , _typeId{ 0 }
        , _arrAncestorNameIndex{}
        , _ancestorDepth{ constants::reflection::kAncestorDepthUnknown }
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
        , _pParentType{ nullptr }
        , _typeId{ other._typeId }
        , _arrAncestorNameIndex{}
        , _ancestorDepth{ constants::reflection::kAncestorDepthUnknown }
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
        , _pParentType{ nullptr }
        , _typeId{ other._typeId }
        , _arrAncestorNameIndex{}
        , _ancestorDepth{ constants::reflection::kAncestorDepthUnknown }
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
        _pParentType.store( nullptr, std::memory_order_relaxed );
        clearAncestorDisplay();
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
        _pParentType.store( nullptr, std::memory_order_relaxed );
        clearAncestorDisplay();
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

        const TypeInfo*             pParent      = getParentType();
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
        : _generation{ 0 }
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
        // 사슬이 바뀌었을 수 있다 — 재등록은 부모를 바꿀 수 있고, 새 타입은 누군가의 비어 있던 부모일 수 있다.
        // 조상 표를 전부 비운다. 배치 끝의 buildLookupCaches 나 첫 상속 검사가 다시 세운다.
        for ( const auto& [storedFqn, storedInfo] : _mapFqnToClassType )
        {
            (void)storedFqn;
            storedInfo.clearAncestorDisplay();
        }
        if ( stored._name.empty() == false && stored._name != canonicalKey )
        {
            _mapAliasToFqn.insert_or_assign( stored._name, canonicalKey );
            _mapHashToCanonicalName.insert_or_assign( stored._name.getHash(), canonicalName );
        }
        // 표가 커졌으면 원소가 옮겨졌다 — 밖에서 들고 있던 포인터(TypeLookupCache)는 이제 무효다.
        _generation.fetch_add( 1, std::memory_order_acq_rel );
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
            // 부모 포인터부터 — 아래 두 캐시가 부모를 따라가고, 캐스트의 핫패스가 이것만 본다.
            pType->resolveParentType();
            pType->buildLookupCache();
            (void)pType->getPropertiesWithBase();
        }
        // 부모 포인터가 **전부** 풀린 뒤에 조상 표를 세운다 — 표는 사슬 끝까지 따라가므로 한 바퀴 뒤여야 한다.
        for ( const TypeInfo* pType : listType )
        {
            if ( pType != nullptr )
                (void)pType->buildAncestorDisplay();
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
        // 지운 자리에 마지막 원소가 옮겨 왔다 — 남은 타입이 풀어 둔 부모 포인터가 그 원소를 가리키고
        // 있었을 수 있다. 전부 비우고, 다음 조회(또는 다음 배치의 buildLookupCaches)가 다시 푼다.
        for ( const auto& [fqn, info] : _mapFqnToClassType )
        {
            (void)fqn;
            info.clearParentType();
            info.clearAncestorDisplay();
        }
        _generation.fetch_add( 1, std::memory_order_acq_rel );
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
        // 별칭이 생기면 같은 이름의 답이 nullptr 에서 타입으로 바뀔 수 있다.
        _generation.fetch_add( 1, std::memory_order_acq_rel );

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

    const TypeInfo* TypeLookupCache::find( const hashed_string& fqn ) const
    {
        const TypeRegistry& registry   = engine::getTypeRegistry();
        const uint32        generation = registry.getGeneration();
        if ( _generation.load( std::memory_order_acquire ) == generation )
            return _pType.load( std::memory_order_relaxed );

        const TypeInfo* pType = registry.findType( fqn );
        _pType.store( pType, std::memory_order_relaxed );
        _generation.store( generation, std::memory_order_release );
        return pType;
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

    const TypeInfo* TypeInfo::getParentType() const
    {
        const TypeInfo* pParent = _pParentType.load( std::memory_order_relaxed );
        if ( pParent != nullptr || _parentFQN.empty() )
            return pParent;

        // 배치 밖에서 등록된 타입이거나 해제로 비워진 뒤다 — 이름으로 풀어 적어 둔다. 부모가 아직
        // 등록되지 않았으면(모듈 로드 순서) nullptr 를 남겨 다음 호출이 다시 찾게 한다.
        pParent = engine::getTypeRegistry().findType( _parentFQN );
        if ( pParent == this )
            pParent = nullptr;
        _pParentType.store( pParent, std::memory_order_relaxed );
        return pParent;
    }

    void TypeInfo::resolveParentType() const
    {
        _pParentType.store( nullptr, std::memory_order_relaxed );
        (void)getParentType();
    }

    bool TypeInfo::isDerivedFrom( const hashed_string& targetFqn ) const
    {
        // **순환에서 멈춘다.** 걸음 수를 세는 것이 방문 목록보다 싸고(할당 없음), 체인은 보통 다섯을
        // 넘지 않는다. 부모가 미등록이어도 `_parentFQN` 이 같으면 파생으로 본다 — 모듈이 아직 안
        // 올라온 동안 이름으로 묻는 쪽(직렬화)이 그것에 기대 왔다.
        const TypeInfo* pCurrent = this;
        for ( uint32 depth = 0; depth < constants::reflection::kMaxParentChainDepth && pCurrent != nullptr; ++depth )
        {
            if ( pCurrent->_fullyQualifiedName == targetFqn || pCurrent->_name == targetFqn )
                return true;
            if ( pCurrent->_parentFQN.empty() )
                return false;
            if ( pCurrent->_parentFQN == targetFqn )
                return true;
            pCurrent = pCurrent->getParentType();
        }
        return false;
    }

    bool TypeInfo::isDerivedFrom( const TypeInfo* pTarget ) const
    {
        if ( pTarget == nullptr )
            return false;
        if ( pTarget == this )
            return true;

        // 조상 표 — 둘 다 표가 있으면 로드 둘과 비교 하나로 끝난다. 아직 안 세운 쪽은 여기서 세운다(배치 밖에서
        // 등록된 타입 · 레지스트리 밖 사본 · 해제 뒤 첫 조회). 세울 수 없는 쪽은 아래 걷기가 답한다.
        uint8 selfDepth = _ancestorDepth.load( std::memory_order_acquire );
        if ( selfDepth == constants::reflection::kAncestorDepthUnknown && buildAncestorDisplay() )
            selfDepth = _ancestorDepth.load( std::memory_order_acquire );
        uint8 targetDepth = pTarget->_ancestorDepth.load( std::memory_order_acquire );
        if ( targetDepth == constants::reflection::kAncestorDepthUnknown && pTarget->buildAncestorDisplay() )
            targetDepth = pTarget->_ancestorDepth.load( std::memory_order_acquire );
        if ( selfDepth < constants::reflection::kAncestorDisplayDepth && targetDepth < constants::reflection::kAncestorDisplayDepth )
        {
            return targetDepth <= selfDepth && _arrAncestorNameIndex[targetDepth].load( std::memory_order_relaxed ) ==
                                                   ReflectionCoreInternal::canonicalNameIndex( *pTarget );
        }

        // 표가 없는 쪽(이름 없음 · 순환 · 표보다 깊은 사슬)은 부모 포인터를 걷는다.
        const TypeInfo* pCurrent = this;
        for ( uint32 depth = 0; depth < constants::reflection::kMaxParentChainDepth && pCurrent != nullptr; ++depth )
        {
            if ( pCurrent == pTarget || ReflectionCoreInternal::isSameTypeName( *pCurrent, *pTarget ) )
                return true;
            pCurrent = pCurrent->getParentType();
        }
        return false;
    }

    bool TypeInfo::buildAncestorDisplay() const
    {
        constexpr uint32 kDepth = constants::reflection::kAncestorDisplayDepth;
        constexpr uint32 kNone  = static_cast<uint32>( PredefinedNameType::NameType_None );

        // 자기부터 위로 이름을 모은다. 순환은 표 깊이에서 걸린다. **안 풀리는 부모는 사슬의 끝이다** — `Component`
        // 처럼 REFLECT 가 아닌 기반은 이름만 적혀 있고 등록되지 않는데, 예전 걷기는 그 이름을 실패 캐스트마다
        // 잠금 잡고 레지스트리에서 찾았다(찾을 수 없으니 캐시도 안 됐다). 부모가 나중에 등록되면 registerClass 가
        // 표를 전부 비우므로 그때 다시 이어진다.
        uint32          arrChain[kDepth];
        uint32          chainCount = 0;
        const TypeInfo* pCurrent   = this;
        while ( pCurrent != nullptr )
        {
            const uint32 nameIndex = ReflectionCoreInternal::canonicalNameIndex( *pCurrent );
            if ( chainCount == kDepth || nameIndex == kNone )
            {
                _ancestorDepth.store( constants::reflection::kAncestorDepthNone, std::memory_order_release );
                return false;
            }
            arrChain[chainCount++] = nameIndex;
            if ( pCurrent->_parentFQN.empty() )
                break;
            pCurrent = pCurrent->getParentType();
        }

        // 루트가 0 번 칸이 되도록 뒤집어 적고, 깊이는 마지막에 publish 한다 — 깊이를 본 쪽은 칸이 다 채워진 뒤다.
        for ( uint32 index = 0; index < chainCount; ++index )
            _arrAncestorNameIndex[index].store( arrChain[chainCount - 1 - index], std::memory_order_relaxed );
        _ancestorDepth.store( static_cast<uint8>( chainCount - 1 ), std::memory_order_release );
        return true;
    }

    const PropertyInfo* TypeInfo::findPropertyInHierarchy( const hashed_string& propNameOrAlias ) const
    {
        // 재귀였다 — 부모 체인이 순환하면 스택이 넘친다. 걸음 수를 세는 루프로 걷는다.
        const TypeInfo* pCurrent = this;
        for ( uint32 depth = 0; depth < constants::reflection::kMaxParentChainDepth && pCurrent != nullptr; ++depth )
        {
            const PropertyInfo* pProp = pCurrent->findProperty( propNameOrAlias );
            if ( pProp != nullptr )
                return pProp;
            pCurrent = pCurrent->getParentType();
        }
        return nullptr;
    }
} // namespace sw
