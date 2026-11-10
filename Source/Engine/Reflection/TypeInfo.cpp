#include "pch.h"

#include "Engine/Reflection/TypeInfo.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/pair.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/MemoryBlock.h"
#include "Core/Name/hashed_string.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionConstants.h"
#include "Engine/Reflection/ReflectionEditorMetadata.h"
#include "Engine/Reflection/ReflectionEnumNames.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Reflection/TypeRegistry.h"

/**
 * @file TypeInfo.cpp
 * @brief 형식의 리플렉션 정보 — 부모 · 조상 · 상속 프로퍼티 캐시입니다.
 */
namespace sw
{
    namespace
    {
        struct TypeInfoInternal
        {
            /** @brief 조상 테이블에 적는 이름입니다. FQN 의 intern 인덱스이고, 없으면 짧은 이름의 인덱스입니다. 둘 다 없으면 None 입니다. */
            static uint32 canonicalNameIndex( const TypeInfo& type )
            {
                constexpr PredefinedNameType kNone = PredefinedNameType::NameType_None;
                return type._fullyQualifiedName.isPredefinedType( kNone ) ? type._name.getIndex() : type._fullyQualifiedName.getIndex();
            }

            /**
             * @brief 두 TypeInfo 가 같은 타입을 말하는지 판단합니다. 포인터가 달라도 이름이 같으면 같은 타입입니다.
             * @details 레지스트리는 FQN 하나당 항목 하나지만, 레지스트리 **밖**에 사본이 있을 수 있다
             *          (테스트 목의 손으로 만든 `StaticType()` 이 자기 사본을 반환한다). 포인터를 따라가다가
             *          그 사본을 만나면 이름으로 한 번 더 본다. intern 인덱스 정수 비교 한 번이다.
             */
            static bool isSameTypeName( const TypeInfo& lhs, const TypeInfo& rhs )
            {
                // 단계마다 부르는 곳이라 intern 테이블은 만지지 않는다. `empty()` 는 길이를 보려고 테이블을
                // 읽는다(단계당 캐시 라인 둘). 인덱스가 None 이 아니면 이름이 있는 것으로 보고, 인덱스가
                // 같을 때(체인이 끝나는 적중)만 빈 이름을 걸러 낸다.
                constexpr PredefinedNameType kNone = PredefinedNameType::NameType_None;
                if ( lhs._fullyQualifiedName.isPredefinedType( kNone ) == false )
                    return lhs._fullyQualifiedName == rhs._fullyQualifiedName && lhs._fullyQualifiedName.empty() == false;
                return lhs._name.isPredefinedType( kNone ) == false && lhs._name == rhs._name && lhs._name.empty() == false;
            }
        };
    } // namespace

    void TypeInfo::setPropertyTable( const PropertyDesc* pDesc, const size_t count )
    {
        _listProperty.clear();
        _listProperty.reserve( count );
        for ( size_t descIndex = 0; descIndex < count; ++descIndex )
        {
            _listProperty.emplace_back( pDesc[descIndex] );
        }
    }

    const string* TypeInfo::findCustomMeta( const hashed_string& key ) const noexcept
    {
        return _metadata->getEditor().findCustomMeta( key );
    }

    string_view TypeInfo::getCategory() const noexcept
    {
        return _metadata->getEditor()._category;
    }

    const utf8* TypeInfo::getDisplayName() const noexcept
    {
        const string& displayName = _metadata->getEditor()._displayName;
        return displayName.empty() ? _name.c_str() : displayName.c_str();
    }

    string_view TypeInfo::getTooltip() const noexcept
    {
        return _metadata->getEditor()._tooltip;
    }

    bool TypeInfo::isHiddenInMenu() const noexcept
    {
        return _metadata->getEditor()._bHideInMenu != SW_FALSE;
    }

    TypeInfo::TypeInfo() noexcept
        : _size{ 0 }
        , _destroyInstance{ nullptr }
        , _addComponent{ nullptr }
        , _pValidate{ nullptr }
        , _pFixedCodec{ nullptr }
        , _name{}
        , _fullyQualifiedName{}
        , _parentFQN{}
        , _moduleName{}
        , _listProperty{}
        , _listMethod{}
        , _listEvent{}
        , _metadata{}
        , _listPropertyWithBase{}
        , _mapNameToPropertyWithBase{}
        , _mapNameToProperty{}
        , _mapNameToMethod{}
        , _pParentType{ nullptr }
        , _parentMissGeneration{ 0 }
        , _typeID{ 0 }
        , _bAbstract{ SW_FALSE }
        , _bStatic{ SW_FALSE }
        , _bPrimitive{ SW_FALSE }
        , _bIsCacheBuilt{ SW_FALSE }
        , _bListPropertyWithBaseBuilt{ SW_FALSE }
        , _bIsPODFastPath{ SW_FALSE }
        , _bHasSaveGameProperty{ SW_FALSE }
        , _bHasValidator{ SW_FALSE }
        , _bDerivedFlagsBuilt{ SW_FALSE }
        , _reservedCacheFlags{ 0 }
        , _reservedPadding{ 0, 0 }
    {
    }

    TypeInfo::TypeInfo( const TypeInfo& other )
        : TypeInfo()
    {
        *this = other;
    }

    TypeInfo::TypeInfo( TypeInfo&& other ) noexcept
        : TypeInfo()
    {
        *this = std::move( other );
    }

    void TypeInfo::clearInheritedProperties()
    {
        _listPropertyWithBase.clear();
        _mapNameToPropertyWithBase.clear();
        _bListPropertyWithBaseBuilt = SW_FALSE;
        _bDerivedFlagsBuilt         = SW_FALSE;
    }

    void TypeInfo::rebuildDerivedCaches()
    {
        // 자기 이름 맵 — 자기 선언만 본다.
        _mapNameToProperty.clear();
        _mapNameToMethod.clear();
        _mapNameToProperty.reserve( _listProperty.size() * 2 );
        _mapNameToMethod.reserve( _listMethod.size() );
        for ( const PropertyInfo& propertyInfo : _listProperty )
        {
            _mapNameToProperty[propertyInfo._name] = &propertyInfo;
            for ( const hashed_string& alias : propertyInfo._listAlias )
            {
                if ( alias.empty() == false )
                    _mapNameToProperty[alias] = &propertyInfo;
            }
        }
        for ( const FunctionInfo& method : _listMethod )
        {
            _mapNameToMethod[method._hashName] = &method;
        }
        _bIsCacheBuilt = SW_TRUE;

        clearInheritedProperties();
        if ( _parentFQN.empty() )
        {
            _bListPropertyWithBaseBuilt = SW_TRUE; // 부모가 없으면 `getPropertiesWithBase` 는 자기 목록을 본다
            return;
        }

        // 사슬을 모아 기반부터 단계마다 **자기 선언 목록**을 접는다. 같은 타입이 사슬에 두 번 나오면 순환이다 — 자기 것만 둔다.
        const TypeInfo* arrChain[constant::reflection::kMaxParentChainDepth];
        const uint32    chainLength = collectTypeChain( arrChain );
        bool            bLoop       = false;
        for ( uint32 outer = 0; outer < chainLength && bLoop == false; ++outer )
        {
            for ( uint32 inner = outer + 1; inner < chainLength && bLoop == false; ++inner )
            {
                bLoop = arrChain[outer] == arrChain[inner];
            }
        }
        if ( bLoop )
            SW_LOG_ERROR_ID( "Reflection.ParentChainLoop", "Reflection parent chain loops at '%#' - returning own properties only.", _fullyQualifiedName.c_str() );
        const uint32 depth = bLoop ? 1 : chainLength;

        unordered_map<hashed_string, uint32> mapNameToIndex;
        for ( uint32 level = depth; level > 0; --level )
        {
            for ( const PropertyInfo& prop : arrChain[level - 1]->_listProperty )
            {
                const auto [iter, bInserted] = mapNameToIndex.emplace( prop._name, static_cast<uint32>( _listPropertyWithBase.size() ) );
                if ( bInserted )
                    _listPropertyWithBase.push_back( &prop );
                else
                    _listPropertyWithBase[iter->second] = &prop; // 파생이 같은 이름을 제자리에서 덮는다
            }
        }

        // 이름과 별칭에서 항목으로 가는 맵을 목록과 함께 만든다. 뒤의 항목(파생)이 앞(기반)을 덮는다.
        _mapNameToPropertyWithBase.reserve( _listPropertyWithBase.size() * 2 );
        for ( const PropertyInfo* pProp : _listPropertyWithBase )
        {
            _mapNameToPropertyWithBase[pProp->_name] = pProp;
            for ( const hashed_string& alias : pProp->_listAlias )
            {
                if ( alias.empty() == false )
                    _mapNameToPropertyWithBase[alias] = pProp;
            }
        }
        _bListPropertyWithBaseBuilt = SW_TRUE;
    }

    void TypeInfo::invalidateDerivedCaches()
    {
        clearInheritedProperties();
        _mapNameToProperty.clear();
        _mapNameToMethod.clear();
        clearParentType();
        clearAncestorDisplay();
        _bIsCacheBuilt = SW_FALSE;
    }

    TypeInfo& TypeInfo::operator=( const TypeInfo& other )
    {
        if ( this == &other )
            return *this;

        _size               = other._size;
        _destroyInstance    = other._destroyInstance;
        _addComponent       = other._addComponent;
        _pValidate          = other._pValidate;
        _pFixedCodec        = other._pFixedCodec;
        _name               = other._name;
        _fullyQualifiedName = other._fullyQualifiedName;
        _parentFQN          = other._parentFQN;
        _moduleName         = other._moduleName;
        _listProperty       = other._listProperty;
        _listMethod         = other._listMethod;
        _listEvent          = other._listEvent;
        _metadata           = other._metadata;
        _typeID             = other._typeID;
        _bAbstract          = other._bAbstract;
        _bStatic            = other._bStatic;
        _bPrimitive         = other._bPrimitive;

        invalidateDerivedCaches();

        return *this;
    }

    TypeInfo& TypeInfo::operator=( TypeInfo&& other ) noexcept
    {
        if ( this == &other )
            return *this;

        _size               = other._size;
        _destroyInstance    = other._destroyInstance;
        _addComponent       = other._addComponent;
        _pValidate          = other._pValidate;
        _pFixedCodec        = other._pFixedCodec;
        _name               = other._name;
        _fullyQualifiedName = other._fullyQualifiedName;
        _parentFQN          = other._parentFQN;
        _moduleName         = other._moduleName;
        _listProperty       = std::move( other._listProperty );
        _listMethod         = std::move( other._listMethod );
        _listEvent          = std::move( other._listEvent );
        _metadata           = std::move( other._metadata );
        _typeID             = other._typeID;
        _bAbstract          = other._bAbstract;
        _bStatic            = other._bStatic;
        _bPrimitive         = other._bPrimitive;

        invalidateDerivedCaches();

        other._typeID          = 0;
        other._size            = 0;
        other._destroyInstance = nullptr;
        other._addComponent    = nullptr;
        other._bIsCacheBuilt   = SW_FALSE;
        other.invalidateDerivedCaches(); // 옮겨 간 목록을 가리키던 맵을 남기지 않는다

        return *this;
    }

    bool TypeInfo::hasSaveGameProperty() const
    {
        SW_ASSERT( _bDerivedFlagsBuilt == SW_TRUE && "판정을 구하지 않은 TypeInfo 다 — 레지스트리 밖 사본은 computeDerivedFlags 를 먼저 부른다" );
        return _bHasSaveGameProperty == SW_TRUE;
    }

    bool TypeInfo::usesPODCopyFastPath() const
    {
        SW_ASSERT( _bDerivedFlagsBuilt == SW_TRUE && "판정을 구하지 않은 TypeInfo 다 — 레지스트리 밖 사본은 computeDerivedFlags 를 먼저 부른다" );
        return _bIsPODFastPath == SW_TRUE;
    }

    bool TypeInfo::hasValidator() const
    {
        SW_ASSERT( _bDerivedFlagsBuilt == SW_TRUE && "판정을 구하지 않은 TypeInfo 다 — 레지스트리 밖 사본은 computeDerivedFlags 를 먼저 부른다" );
        return _bHasValidator == SW_TRUE;
    }

    void TypeInfo::computeDerivedFlags()
    {
        TypeRegistry& registry = engine::getSubsystem<TypeRegistry>();

        static const hashed_string kArrDynamicTypes[] = {
            hashed_string{ PredefinedNameType::NameType_string },
            hashed_string{ PredefinedNameType::NameType_hashed_string },
        };

        // 부모의 프로퍼티까지 본다. 자기 것만 보면 부모가 문자열이나 컨테이너를 가진 타입도 통째 복사로 간다.
        bool bPOD      = true;
        bool bSaveGame = false;
        for ( const PropertyInfo& prop : getPropertiesWithBase() )
        {
            if ( prop._metadata->_bSaveGame == SW_TRUE )
                bSaveGame = true;
            if ( bPOD == false )
                continue;
            // 값이 객체 밖에 있는 프로퍼티는 객체를 통째로 복사해도 따라오지 않는다(그 위치를 찾는 슬롯 번호가 복사돼 두 객체가 한 위치를 나눠 쓴다).
            const bool bNotPODProperty = prop._bIsContainer == SW_TRUE || prop._containerKind != ContainerKind::None || prop.hasValueAccessor();
            if ( bNotPODProperty )
            {
                bPOD = false;
                continue;
            }
            for ( const hashed_string& dynamicType : kArrDynamicTypes )
            {
                if ( registry.isType( prop._typeName, dynamicType ) )
                {
                    bPOD = false;
                    break;
                }
            }
        }
        _bIsPODFastPath       = bPOD ? SW_TRUE : SW_FALSE;
        _bHasSaveGameProperty = bSaveGame ? SW_TRUE : SW_FALSE;
        _bHasValidator        = ReflectionValidation::computeHasValidator( *this ) ? SW_TRUE : SW_FALSE;
        _bDerivedFlagsBuilt   = SW_TRUE;
    }

    PropertyListView TypeInfo::getPropertiesWithBase() const
    {
        if ( _parentFQN.empty() )
            return PropertyListView{ _listProperty.data(), _listProperty.size() };
        if ( _bListPropertyWithBaseBuilt == SW_FALSE )
            SW_FATAL( "TypeInfo %# has a parent but its inherited property list was not built - only TypeRegistry-owned types (rebuildDerivedCaches) can list inherited properties",
                      _fullyQualifiedName.c_str() );
        return PropertyListView{ _listPropertyWithBase.data(), _listPropertyWithBase.size() };
    }

    const TypeInfo* TypeInfo::getParentType() const
    {
        const TypeInfo* pParent = _pParentType.load( std::memory_order_relaxed );
        if ( pParent != nullptr || _parentFQN.empty() )
            return pParent;

        // 찾지 못한 이름은 세대가 같은 동안 다시 찾지 않는다. 등록되지 않는 기반(`Component`)을 부모로 둔 타입은 모두
        // 여기로 오므로 그때마다 레지스트리를 잠그고 찾지 않는다. 등록이나 해제가 세대를 올리면 한 번 더 찾는다.
        const uint32 generation = TypeRegistry::getGeneration();
        if ( _parentMissGeneration.load( std::memory_order_acquire ) == generation )
            return nullptr;

        pParent = engine::getSubsystem<TypeRegistry>().findType( _parentFQN );
        if ( pParent == this )
            pParent = nullptr;
        if ( pParent != nullptr )
            _pParentType.store( pParent, std::memory_order_relaxed );
        else
            _parentMissGeneration.store( generation, std::memory_order_release );
        return pParent;
    }

    void TypeInfo::resolveParentType() const
    {
        clearParentType();
        (void)getParentType();
    }

    bool TypeInfo::isDerivedFrom( const hashed_string& targetFqn ) const
    {
        // **순환에서 멈춘다.** 걸음 수를 세는 것이 방문 목록보다 싸고(할당 없음), 체인은 보통 다섯을
        // 넘지 않는다. 부모가 미등록이어도 `_parentFQN` 이 같으면 파생으로 본다. 모듈이 아직 로드되지
        // 않은 동안 이름으로 묻는 쪽(직렬화)이 그것에 기댄다.
        const TypeInfo* pCurrent = this;
        for ( uint32 depth = 0; depth < constant::reflection::kMaxParentChainDepth && pCurrent != nullptr; ++depth )
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

    // `castTo` 는 `TypeInfo` 를 전방 선언만 보고 그 주소를 조상 표로 읽는다(`TypeCastHeader::of`) — 표가 맨 앞 멤버여야 한다.
    static_assert( offsetof( TypeInfo, _castHeader ) == 0, "TypeInfo::_castHeader must be the first member (castTo reads it through a forward-declared TypeInfo)" );

    bool isTypeDerivedFromSlow( const TypeInfo* pType, const TypeInfo* pTarget )
    {
        return pType->isDerivedFromSlow( pTarget );
    }

    bool TypeInfo::isDerivedFromSlow( const TypeInfo* pTarget ) const
    {
        // 헤더의 인라인 버전이 nullptr, 자기 자신, 양쪽 모두 테이블이 있는 경우를 걸렀다. 아직 만들지 않은 쪽은 여기서
        // 만든다(배치 밖에서 등록된 타입, 레지스트리 밖 사본, 해제 뒤 첫 조회). 만들 수 없는 쪽은 아래 순회가 답한다.
        uint8 selfDepth = _castHeader._ancestorDepth.load( std::memory_order_acquire );
        if ( selfDepth == constant::reflection::kAncestorDepthUnknown && computeAncestorDisplay() )
            selfDepth = _castHeader._ancestorDepth.load( std::memory_order_acquire );
        uint8 targetDepth = pTarget->_castHeader._ancestorDepth.load( std::memory_order_acquire );
        if ( targetDepth == constant::reflection::kAncestorDepthUnknown && pTarget->computeAncestorDisplay() )
            targetDepth = pTarget->_castHeader._ancestorDepth.load( std::memory_order_acquire );
        if ( selfDepth < constant::reflection::kAncestorDisplayDepth && targetDepth < constant::reflection::kAncestorDisplayDepth )
        {
            // pTarget 의 자기 슬롯이 곧 pTarget 의 이름이다. 이름을 다시 계산하지 않는다.
            return targetDepth <= selfDepth && _castHeader._arrAncestorNameIndex[targetDepth].load( std::memory_order_relaxed ) ==
                                                   pTarget->_castHeader._arrAncestorNameIndex[targetDepth].load( std::memory_order_relaxed );
        }

        // 테이블이 없는 쪽(이름 없음, 순환, 테이블보다 깊은 체인)은 부모 포인터를 따라간다.
        const TypeInfo* pCurrent = this;
        for ( uint32 depth = 0; depth < constant::reflection::kMaxParentChainDepth && pCurrent != nullptr; ++depth )
        {
            if ( pCurrent == pTarget || TypeInfoInternal::isSameTypeName( *pCurrent, *pTarget ) )
                return true;
            pCurrent = pCurrent->getParentType();
        }
        return false;
    }

    void TypeInfo::clearContent()
    {
        _listProperty.clear();
        _listMethod.clear();
        _listEvent.clear();
        clearInheritedProperties();
        _mapNameToProperty.clear();
        _mapNameToMethod.clear();
        _metadata                   = {};
        _destroyInstance            = nullptr;
        _addComponent               = nullptr;
        _bIsCacheBuilt              = SW_FALSE;
        _bListPropertyWithBaseBuilt = SW_TRUE; // 묘비는 빈 목록이 답이다 — 든 포인터로 물어도 멈추지 않는다
        _pValidate                  = nullptr;
        _pFixedCodec                = nullptr;
    }

    bool TypeInfo::computeAncestorDisplay() const
    {
        constexpr uint32 kDepth = constant::reflection::kAncestorDisplayDepth;
        constexpr uint32 kNone  = static_cast<uint32>( PredefinedNameType::NameType_None );

        // 자기부터 위로 이름을 모은다. 순환은 테이블 깊이에서 걸린다. **찾을 수 없는 부모는 체인의 끝이다.** `Component`
        // 처럼 등록되지 않은 기반은 이름만 적혀 있으므로, 그 이름을 실패한 캐스트마다 잠그고 레지스트리에서 찾지 않는다.
        // 부모가 나중에 등록되면 registerClass 가 테이블을 모두 비우므로 그때 다시 이어진다.
        uint32          arrChain[kDepth];
        uint32          chainCount = 0;
        const TypeInfo* pCurrent   = this;
        while ( pCurrent != nullptr )
        {
            const uint32 nameIndex = TypeInfoInternal::canonicalNameIndex( *pCurrent );
            if ( chainCount == kDepth || nameIndex == kNone )
            {
                _castHeader._ancestorDepth.store( constant::reflection::kAncestorDepthNone, std::memory_order_release );
                return false;
            }
            arrChain[chainCount++] = nameIndex;
            if ( pCurrent->_parentFQN.empty() )
                break;
            pCurrent = pCurrent->getParentType();
        }

        // 루트가 0 번 슬롯이 되도록 뒤집어 적고, 깊이는 마지막에 publish 한다. 깊이를 본 쪽은 슬롯이 다 채워진 뒤에 보게 된다.
        for ( uint32 index = 0; index < chainCount; ++index )
        {
            _castHeader._arrAncestorNameIndex[index].store( arrChain[chainCount - 1 - index], std::memory_order_relaxed );
        }
        _castHeader._ancestorDepth.store( static_cast<uint8>( chainCount - 1 ), std::memory_order_release );
        return true;
    }

    uint32 TypeInfo::collectTypeChain( const TypeInfo* ( &outArrType )[constant::reflection::kMaxParentChainDepth] ) const
    {
        uint32          depth    = 0;
        const TypeInfo* pCurrent = this;
        while ( pCurrent != nullptr && depth < constant::reflection::kMaxParentChainDepth )
        {
            outArrType[depth++] = pCurrent;
            if ( pCurrent->_parentFQN.empty() )
                break;
            pCurrent = pCurrent->getParentType();
        }
        return depth;
    }

    const EventInfo* TypeInfo::findEventInHierarchy( const hashed_string& eventName ) const
    {
        const TypeInfo* arrChain[constant::reflection::kMaxParentChainDepth];
        const uint32    depth = collectTypeChain( arrChain );
        for ( uint32 level = 0; level < depth; ++level )
        {
            const EventInfo* pEvent = arrChain[level]->findEvent( eventName );
            if ( pEvent != nullptr )
                return pEvent;
        }
        return nullptr;
    }

    const FunctionInfo* TypeInfo::findMethodInHierarchy( const hashed_string& methodName ) const
    {
        const TypeInfo* arrChain[constant::reflection::kMaxParentChainDepth];
        const uint32    depth = collectTypeChain( arrChain );
        for ( uint32 level = 0; level < depth; ++level )
        {
            const FunctionInfo* pMethod = arrChain[level]->findMethod( methodName );
            if ( pMethod != nullptr )
                return pMethod;
        }
        return nullptr;
    }

    const PropertyInfo* TypeInfo::findPropertyInHierarchy( const hashed_string& propNameOrAlias ) const
    {
        // 부모가 없으면 자기 목록만 본다.
        if ( _parentFQN.empty() )
            return findProperty( propNameOrAlias );
        const PropertyListView listWithBase = getPropertiesWithBase();

        // 작으면 선형으로 찾는다. 뒤(파생)부터 보아 병합 규칙(파생이 이긴다)과 같은 결과를 낸다.
        if ( listWithBase.size() <= constant::reflection::kLinearSearchThreshold )
        {
            for ( size_t index = listWithBase.size(); index > 0; --index )
            {
                const PropertyInfo& prop = listWithBase[index - 1];
                if ( prop.matchesName( propNameOrAlias ) )
                    return &prop;
            }
            return nullptr;
        }

        const auto it = _mapNameToPropertyWithBase.find( propNameOrAlias );
        return it != _mapNameToPropertyWithBase.end() ? it->second : nullptr;
    }
} // namespace sw
