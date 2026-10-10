/**
 * @file ReflectionTypes.h
 * @brief 리플렉션용 프로퍼티 / enum / 함수 / 타입 메타데이터
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionConstants.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct ReflectEventOps;
    struct ReflectTypeOps;

    class ValidationContext;

    /** @brief 검증 함수를 부릅니다(생성 코드가 만든다 — `PROPERTY( Validate = fn )` · `REFLECT( Validate = fn )`). */
    using ReflectValidateFunction = void ( * )( const void* pInstance, ValidationContext& context );

    class Component;
    class GameObject;

    /// @brief 함수의 네트워크 역할입니다. 목록은 PredefinedFunctionNetRole.xxx 에 있습니다.
    enum class FunctionNetRole : uint8
    {
#define REGISTER_FUNCTION_NET_ROLE( Name ) Name,
#include "Core/Predefined/PredefinedFunctionNetRole.xxx"

#undef REGISTER_FUNCTION_NET_ROLE
    };

    /// @brief REFLECT() 클래스/구조체 저작 메타
    struct SW_API TypeMetadata
    {
#if !defined( SW_SHIPPING )
        string                               _category;
        string                               _displayName;
        string                               _tooltip;
        unordered_map<hashed_string, string> _mapCustomMeta;
        uint8                                _bHideInMenu   : 1; ///< "Add Component" 메뉴 숨김
        [[maybe_unused]] uint8               _reservedFlags : 7;
#else
        [[maybe_unused]] uint8 _reservedEmpty;
#endif

        TypeMetadata() noexcept;

        /** @brief 커스텀 메타데이터 태그를 조회합니다. (Shipping 빌드에서는 nullptr) */
        const string* findCustomMeta( const hashed_string& key ) const noexcept
        {
#if !defined( SW_SHIPPING )
            auto iter = _mapCustomMeta.find( key );
            return ( iter != _mapCustomMeta.end() ) ? &iter->second : nullptr;
#else
            (void)key;
            return nullptr;
#endif
        }
    };
} // namespace sw

namespace sw
{
    /// @brief PROPERTY() 저작 메타 (카테고리, 기본값, 범위, XML 속성)
    struct SW_API PropertyMetadata
    {
#if !defined( SW_SHIPPING )
        string                               _category;
        string                               _displayName;
        string                               _tooltip;
        unordered_map<hashed_string, string> _mapCustomMeta;
        /** @brief `EditCondition = "…"` — 인스펙터가 이 식이 거짓이면 막는다(`PropertyEditCondition`). */
        string _editCondition;
        /** @brief `FileFilter = "*.png;*.dds"` — 경로 칸이 받는 파일입니다. */
        string _fileFilter;
        /** @brief `UiMin` · `UiMax` — 인스펙터 슬라이더 · 드래그 범위입니다. 허용 범위(`_minRange` · `_maxRange`)와 따로입니다. */
        float32                _uiMinRange;
        float32                _uiMaxRange;
        uint8                  _bHasUiMinRange      : 1;
        uint8                  _bHasUiMaxRange      : 1;
        uint8                  _bEditConditionHides : 1; ///< 조건이 거짓이면 막지 않고 숨긴다
        uint8                  _bColorHdr           : 1; ///< 색을 HDR(1 을 넘는 값)로 고친다
        uint8                  _bMultiline          : 1; ///< 여러 줄 글 칸
        [[maybe_unused]] uint8 _reservedDisplay     : 3;
#endif
        /**
         * @brief 에셋이 이 프로퍼티를 생략할 때 쓰는 저작 기본값 (PROPERTY(Default="...")).
         * @details XML/JSON/Binary 역직렬화가 적용합니다. C++ 멤버 초기화자와는 별개이므로 둘을 맞춰 두십시오.
         */
        string _defaultValue;
        /** @brief 소프트 에셋 힌트 (PROPERTY(AssetPath) / AssetType="Texture"). */
        string _assetType;
        /** @brief `RepNotify = fn` — 받은 값으로 바꾼 뒤 부르는 같은 타입의 메서드 이름입니다. 부르는 길은 `PropertyInfo::_pRepNotify` 입니다. */
        hashed_string _repNotify;
        /** @brief `Validate = fn` — 검증 함수 이름입니다(결과 메시지 · 문서용). 부르는 길은 `PropertyInfo::_pValidate` 입니다. */
        hashed_string _validate;
        float32       _minRange;
        float32       _maxRange;
        /**
         * @brief 아래 · 위 경계가 각각 적혀 있는가(`PROPERTY( Min = … )` · `Max = …`). 적힌 쪽만 막는다.
         * @details 표시가 하나면 `Min = 0` 만 적은 프로퍼티의 위 경계가 기본값 1 로 남아 인스펙터가 그 값을 1 에서 막는다.
         *          슬라이더는 둘 다 있을 때만 그린다(`hasFullRange`).
         */
        uint8 _bHasMinRange : 1;
        uint8 _bHasMaxRange : 1;
        uint8 _bReadOnly    : 1;
        /** @brief 부모 엘리먼트의 XML attribute로 직렬화 (PROPERTY(XMLAttribute)). */
        uint8 _bXMLAttribute : 1;
        uint8 _bAssetPath    : 1;
        /** @brief 값이 ReflectAny(또는 type+blob 다형 페이로드)입니다. */
        uint8 _bPolymorphic : 1;
        /** @brief 직렬화(JSON/XML/Binary/Diff)의 저장 · 로드 대상에서 뺍니다(Transient / NonSerialized). */
        uint8 _bTransient : 1;
        /**
         * @brief 값이 비어 있으면 **쓸 때 생략**합니다 (PROPERTY(SkipIfEmpty)).
         * @details 리플렉션 직렬화는 기본적으로 모든 PROPERTY 를 씁니다. 그래야 "파일에 없음" 과
         *          "명시적으로 비어 있음" 이 구분되기 때문입니다. 다만 선택적 필드(쓰지 않는 셰이더
         *          스테이지 진입점 같은 것)까지 모두 쓰면 파일이 읽기 어려워집니다. 이 플래그는
         *          그 판단을 **스키마가 명시**하게 합니다. 생략해도 좋다고 선언한 필드만 생략되므로
         *          모호함이 생기지 않습니다.
         * @note 읽기에는 영향이 없습니다. 없으면 멤버 초기값이 그대로 남습니다.
         */
        uint8 _bSkipIfEmpty : 1;
#if !defined( SW_SHIPPING )
        /** @brief 에디터 인스펙터에서 숨깁니다(HideInInspector). */
        uint8 _bHideInInspector : 1;
#else
        [[maybe_unused]] uint8 _reservedFlags : 1;
#endif
        /** @brief 네트워크 복제 대상입니다(`Replicated` · `RepNotify`). 네트워크 계층이 `PropertyRoleUtil::collectReplicatedProperties` 로 모읍니다. */
        uint8 _bReplicated : 1;
        /**
         * @brief 세이브 대상입니다(`SaveGame`). 타입(기반 포함)에 이것이 하나라도 있으면 세이브 직렬화(`SerializeContext::setSaveGameOnly`)가
         *        이것만 씁니다 — 하나도 없는 타입은 전부 씁니다(`TypeInfo::hasSaveGameProperty`).
         */
        uint8 _bSaveGame : 1;
        /** @brief 시퀀서 값 트랙이 섞을 수 있습니다(`Interp` — 숫자 · float2/3/4 · quaternion 만, 파서가 막는다). */
        uint8                  _bInterp       : 1;
        [[maybe_unused]] uint8 _reservedRoles : 5;

        /** @brief 범위 · 플래그를 끈 기본값으로 만듭니다. */
        PropertyMetadata() noexcept;

        /** @brief 아래 · 위 경계가 둘 다 있으면(슬라이더로 그릴 수 있으면) true 입니다. */
        bool hasFullRange() const noexcept { return _bHasMinRange != SW_FALSE && _bHasMaxRange != SW_FALSE; }

        /** @brief 슬라이더 범위(`UiMin` · `UiMax`)가 둘 다 있으면 true 입니다. Shipping 에는 없습니다. */
        bool hasFullUiRange() const noexcept
        {
#if !defined( SW_SHIPPING )
            return _bHasUiMinRange != SW_FALSE && _bHasUiMaxRange != SW_FALSE;
#else
            return false;
#endif
        }

        /** @brief 커스텀 메타데이터 태그를 조회합니다. (Shipping 빌드에서는 nullptr) */
        const string* findCustomMeta( const hashed_string& key ) const noexcept
        {
#if !defined( SW_SHIPPING )
            auto iter = _mapCustomMeta.find( key );
            return ( iter != _mapCustomMeta.end() ) ? &iter->second : nullptr;
#else
            (void)key;
            return nullptr;
#endif
        }
    };
} // namespace sw

namespace sw
{
    /// @brief FUNCTION() 저작 메타 (카테고리, NetRole, static/const)
    struct SW_API FunctionMetadata
    {
#if !defined( SW_SHIPPING )
        string                               _category;
        string                               _displayName;
        string                               _tooltip;
        string                               _editorPreview; ///< `FUNCTION( EditorPreview = "..." )` — 에디터 뷰포트 미리보기가 이 종류로 메서드를 찾습니다
        unordered_map<hashed_string, string> _mapCustomMeta;
#endif
        FunctionNetRole _netRole;
        uint8           _bReliable    : 1;
        uint8           _bValidate    : 1;
        uint8           _bConstructor : 1; ///< REFLECT 타입의 생성자 호출기(objPtr 에 placement new)
        uint8           _bStatic      : 1; ///< C++ static 멤버 함수
        uint8           _bConst       : 1; ///< const 멤버 함수
#if !defined( SW_SHIPPING )
        uint8                  _bCallInEditor : 1; ///< 에디터 인스펙터에 실행 버튼을 노출합니다
        [[maybe_unused]] uint8 _reserved      : 2;
#else
        [[maybe_unused]] uint8 _reserved : 3;
#endif

        /** @brief 플래그를 끈 기본값으로 만듭니다. */
        FunctionMetadata() noexcept;

        /** @brief 커스텀 메타데이터 태그를 조회합니다. (Shipping 빌드에서는 nullptr) */
        const string* findCustomMeta( const hashed_string& key ) const noexcept
        {
#if !defined( SW_SHIPPING )
            auto iter = _mapCustomMeta.find( key );
            return ( iter != _mapCustomMeta.end() ) ? &iter->second : nullptr;
#else
            (void)key;
            return nullptr;
#endif
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 재귀 컨테이너 스키마 (vector&lt;vector&lt;T&gt;&gt;, map&lt;K,vector&lt;V&gt;&gt;, …).
     */
    struct NestedContainerInfo
    {
        ContainerKind                   _kind = ContainerKind::None;
        hashed_string                   _typeName; ///< 컨테이너 TypeInfo 이름 (vector, unordered_map, …)
        hashed_string                   _elementTypeName;
        hashed_string                   _keyTypeName;
        shared_ptr<IContainerWrapper>   _wrapper;
        shared_ptr<NestedContainerInfo> _elementNested; ///< 원소 · 값 자체가 컨테이너일 때
    };
} // namespace sw

namespace sw
{
    /// @brief 리플렉션 프로퍼티: 오프셋, 타입, 별칭, 컨테이너 래퍼
    struct SW_API PropertyInfo
    {
        using PropertyBindingDelegate = Delegate<void( const PropertyInfo& prop, const void* pInstance )>;
        /**
         * @brief 값이 객체 **밖**에 있는 프로퍼티의 값 주소를 돌려주는 함수입니다(`PROPERTY` 를 참조를 돌려주는 메서드에 붙인 것).
         * @details 받는 것은 오프셋이 기준으로 삼는 객체 주소(`getRawPtr` 의 인자와 같다)이고, 돌려주는 것은 그 인스턴스의 값
         *          자리입니다. 씬 컴포넌트의 로컬 TRS 가 첫 예입니다 — 값은 트랜스폼 저장소의 칸에 있고 이름(`_localPosition`)은 그대로라,
         *          직렬화기 · 인스펙터 · 비교 도구가 바뀐 것 없이 그 자리를 읽고 씁니다.
         */
        using ValueAccessor = void* (*)( void* pInstance );
        /**
         * @brief `RepNotify` 함수를 부릅니다(생성 코드가 만든다). @p pOldValue 는 바뀌기 전 값의 자리(프로퍼티의 선언 타입)이고,
         *        함수가 이전 값을 받지 않으면 쓰지 않습니다.
         */
        using RepNotifyFunction = void ( * )( void* pInstance, const void* pOldValue );

        shared_ptr<IContainerWrapper>   _containerWrapper;
        shared_ptr<NestedContainerInfo> _nestedContainer; ///< 컨테이너일 때 전체 중첩 사슬
        mutable PropertyBindingDelegate _onPropertyBoundChanged;

        /** @brief 값이 객체 밖에 있으면 그 자리를 찾는 함수입니다. nullptr 이면 값은 `인스턴스 + _offset` 에 있습니다. */
        ValueAccessor _pValueAccessor;
        /** @brief `RepNotify` 를 부르는 함수입니다. 없으면 nullptr 입니다(`PropertyRoleUtil::callRepNotify`). */
        RepNotifyFunction _pRepNotify;
        /** @brief `Validate = fn` 을 부르는 함수입니다. 없으면 nullptr 입니다(`ReflectionValidation::validateObject`). */
        ReflectValidateFunction _pValidate;
        size_t                  _offset;

        hashed_string         _name;
        hashed_string         _typeName;
        hashed_string         _elementTypeName;
        hashed_string         _keyTypeName;
        vector<hashed_string> _listAlias; ///< PROPERTY(Alias=…) 로 적은 다른 키들(이름을 바꾸기 전의 키)
        PropertyMetadata      _metadata;

        mutable uint32 _cachedNameHash;

        uint32                 _bitOffset;
        ContainerKind          _containerKind;
        uint8                  _bitMask;
        uint8                  _bIsContainer  : 1;
        uint8                  _bIsBitField   : 1;
        [[maybe_unused]] uint8 _reservedFlags : 6;

        /** @brief 오프셋 0, 컨테이너가 아닌 상태로 만듭니다. */
        PropertyInfo() noexcept;

        /** @brief 이름·타입·오프셋으로 프로퍼티를 채웁니다. */
        PropertyInfo( hashed_string name, hashed_string typeName, size_t offset,
                      bool bIsContainer = false, ContainerKind containerKind = ContainerKind::None,
                      hashed_string elementTypeName = {}, hashed_string keyTypeName = {},
                      shared_ptr<IContainerWrapper> containerWrapper = nullptr,
                      hashed_string                 alias            = {} );

        /** @brief 이름 해시를 반환합니다. 없으면 계산해 캐시합니다. */
        uint32 getNameHash() const noexcept
        {
            if ( _cachedNameHash == 0 && _name.empty() == false )
                _cachedNameHash = _name.getHash();
            return _cachedNameHash;
        }

        /** @brief 이름이나 별칭 중 하나의 해시와 일치하면 true 입니다. */
        bool matchesNameHash( const uint32 nameOrAliasHash ) const noexcept
        {
            if ( getNameHash() == nameOrAliasHash )
                return true;
            if ( _listAlias.empty() )
                return false;
            for ( const hashed_string& alias : _listAlias )
            {
                if ( alias.empty() == false && alias.getHash() == nameOrAliasHash )
                    return true;
            }
            return false;
        }

        /** @brief 이름이나 별칭과 일치하면 true 입니다. */
        bool matchesName( const hashed_string& nameOrAlias ) const
        {
            if ( _name == nameOrAlias )
                return true;
            if ( _listAlias.empty() )
                return false;
            for ( const hashed_string& alias : _listAlias )
            {
                if ( alias == nameOrAlias )
                    return true;
            }
            return false;
        }

        /** @brief 이 프로퍼티가 직렬화 가능한 컨테이너 래퍼를 가지고 있는지 반환합니다. */
        bool hasContainerWrapper() const noexcept
        {
            return _containerWrapper != nullptr;
        }

        /** @brief 단일 컨테이너와 중첩 컨테이너를 똑같이 다루기 위한 모양 정보를 반환합니다. */
        NestedContainerInfo getContainerShape() const;

        /** @brief 값 변경 콜백을 바인딩합니다. */
        void bindOnChanged( PropertyBindingDelegate delegate ) const { _onPropertyBoundChanged = std::move( delegate ); }

        /** @brief 비트필드의 그 비트 하나만 1 로 세우는 함수입니다(생성 코드가 만든다). 받는 것은 0 으로 채운, 그 타입 크기의 자리입니다. */
        using SetBitFunction = void ( * )( void* pInstance );

        /**
         * @brief 비트필드의 바이트 · 마스크를 **이 빌드 구성의 실제 레이아웃에서** 찾아 이 프로퍼티를 비트필드로 만듭니다.
         * @details 0 으로 채운 자리에 그 비트만 세우는 함수를 불러, 바뀐 바이트와 비트를 읽는다(언리얼 `FBoolProperty` 가 UHT 의
         *          SetBit 함수로 하는 것과 같다). 주의: 파서가 libclang 으로 잰 바이트를 쓰면 안 된다 — 파서는 Debug 정의
         *          (`SW_DEBUG` · `_DEBUG`)를 모른 채 재므로, Debug 에서 커지는 멤버(`sw::string` 의 경쟁 검사 자리 · 반복자 디버그) 뒤의
         *          비트필드가 엉뚱한 바이트를 가리키고 씬을 읽을 때마다 그 자리의 다른 필드를 덮어쓴다. 다른 프로퍼티는 `offsetof` 다.
         * @param ownerSize 이 프로퍼티를 가진 타입의 크기(`sizeof`)
         * @param pSetBit   그 비트만 세우는 함수
         * @return 바이트 하나의 비트 하나를 찾았으면 true. 아니면 오류를 남기고, 읽으면 false · 쓰면 무시되는 비트필드로 둡니다 —
         *         엉뚱한 바이트를 건드리지 않게.
         */
        bool resolveBitField( size_t ownerSize, SetBitFunction pSetBit );

        /** @brief 인스턴스의 프로퍼티 값을 읽습니다(비트필드 지원). */
        template <typename T, typename ObjectType>
        T getValue( const ObjectType* pInstance ) const
        {
            if ( _bIsBitField == SW_TRUE )
            {
                const uint8* pByte = reinterpret_cast<const uint8*>( pInstance ) + _offset;
                const bool   bVal  = ( ( *pByte & _bitMask ) != 0 );
                if constexpr ( std::is_same_v<T, bool> )
                    return bVal;
                else
                    return static_cast<T>( bVal ? 1 : 0 );
            }

            const T* pPtr = static_cast<const T*>( getRawPtr( static_cast<const void*>( pInstance ) ) );
            return *pPtr;
        }

        /** @brief 인스턴스의 프로퍼티 값을 쓰고 옵저버 · 바인딩에 알립니다(비트필드 지원). */
        template <typename T, typename ObjectType>
        void setValue( ObjectType* pInstance, const T& newValue ) const
        {
            if ( _bIsBitField == SW_TRUE )
            {
                uint8* pByte = reinterpret_cast<uint8*>( pInstance ) + _offset;
                bool   bVal  = false;
                // 비트필드는 1비트 불리언 플래그뿐이다. 구조체 타입(float3 …)으로 부르면 이 분기는 닿지 않지만 컴파일은 되어야 한다 —
                // `static_cast<T>( 0 )` 은 구조체에서 컴파일되지 않으므로 bool 일 때만 쓴다.
                if constexpr ( std::is_same_v<T, bool> )
                    bVal = newValue;
                else if constexpr ( std::is_same_v<T, float32> )
                    bVal = ( MathUtil::nearEqual( newValue, 0.0f ) == false );
                else if constexpr ( std::is_same_v<T, float64> )
                    bVal = ( MathUtil::nearEqual( newValue, 0.0 ) == false );
                else if constexpr ( std::is_arithmetic_v<T> || std::is_enum_v<T> )
                    bVal = ( newValue != static_cast<T>( 0 ) );
                else
                    return;

                if ( bVal )
                    *pByte |= _bitMask;
                else
                    *pByte &= static_cast<uint8>( ~_bitMask );

                if constexpr ( std::is_base_of_v<IPropertyObserver, ObjectType> )
                {
                    IPropertyObserver* pObserver = static_cast<IPropertyObserver*>( pInstance );
                    pObserver->onPropertyChanged( _name );
                }

                if ( _onPropertyBoundChanged.isBound() )
                    _onPropertyBoundChanged( *this, pInstance );
                return;
            }

            T* pPtr = static_cast<T*>( getRawPtr( static_cast<void*>( pInstance ) ) );
            if constexpr ( std::is_same_v<T, float32> || std::is_same_v<T, float64> )
            {
                if ( MathUtil::nearEqual( *pPtr, newValue ) )
                    return;
            }
            else if ( *pPtr == newValue )
                return;

            *pPtr = newValue;
            if constexpr ( std::is_base_of_v<IPropertyObserver, ObjectType> )
            {
                IPropertyObserver* pObserver = static_cast<IPropertyObserver*>( pInstance );
                pObserver->onPropertyChanged( _name );
            }

            if ( _onPropertyBoundChanged.isBound() )
                _onPropertyBoundChanged( *this, pInstance );
        }

        /** @brief 값 포인터입니다(비트필드는 nullptr). 자리는 `getRawPtr` 가 정합니다. */
        template <typename T>
        T* getValuePtr( void* pInstance ) const
        {
            if ( _bIsBitField == SW_TRUE )
                return nullptr;
            return static_cast<T*>( getRawPtr( pInstance ) );
        }

        /** @brief 값 포인터입니다(비트필드는 nullptr). 자리는 `getRawPtr` 가 정합니다. */
        template <typename T>
        const T* getValuePtr( const void* pInstance ) const
        {
            if ( _bIsBitField == SW_TRUE )
                return nullptr;
            return static_cast<const T*>( getRawPtr( pInstance ) );
        }

        /** @brief 값이 객체 밖에 있으면(`_pValueAccessor`) true 입니다. 이런 프로퍼티는 오프셋이 0 이고 뜻이 없습니다. */
        bool hasValueAccessor() const noexcept { return _pValueAccessor != nullptr; }

        /**
         * @brief 인스턴스 기준 프로퍼티 값의 원시 메모리 시작 포인터를 반환합니다.
         * @details 보통은 `인스턴스 + _offset` 이고, 값이 객체 밖에 있는 프로퍼티는 `_pValueAccessor` 가 찾은 자리입니다.
         *          값을 만지는 길(`getValue` · `setValue` · `getValuePtr` · 직렬화기)은 모두 여기를 지납니다.
         */
        void* getRawPtr( void* pInstance ) const noexcept
        {
            if ( _pValueAccessor != nullptr )
                return _pValueAccessor( pInstance );
            return reinterpret_cast<utf8*>( pInstance ) + _offset;
        }

        /** @brief 인스턴스 기준 프로퍼티 값의 원시 메모리 const 시작 포인터를 반환합니다. */
        const void* getRawPtr( const void* pInstance ) const noexcept
        {
            if ( _pValueAccessor != nullptr )
                return _pValueAccessor( const_cast<void*>( pInstance ) );
            return reinterpret_cast<const utf8*>( pInstance ) + _offset;
        }

        /** @brief 커스텀 메타데이터 태그를 조회합니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept
        {
            return _metadata.findCustomMeta( key );
        }
    };
} // namespace sw

namespace sw
{
    /// @brief 등록된 enum: 이름↔값, Flags, Invalid/Count 센티널
    struct SW_API EnumInfo
    {
        unordered_map<hashed_string, int64> _mapNameToValue;
        unordered_map<int64, hashed_string> _mapValueToName;
#if !defined( SW_SHIPPING )
        unordered_map<hashed_string, string> _mapCustomMeta;
#endif
        hashed_string          _name;
        hashed_string          _fullyQualifiedName;
        hashed_string          _moduleName;
        int64                  _invalidValue{ 0 };
        int64                  _countValue{ 0 };
        uint8                  _size{ sizeof( int32 ) };
        uint8                  _bIsBitFlag    : 1;
        uint8                  _bHasInvalid   : 1;
        uint8                  _bHasCount     : 1;
        uint8                  _bIsSigned     : 1; ///< 밑바탕 타입이 부호 있는가 — 좁은 값을 메모리에서 읽을 때 확장 방식을 정한다
        [[maybe_unused]] uint8 _reservedFlags : 4;

        /** @brief 빈 이름↔값 맵으로 만듭니다. */
        EnumInfo() noexcept;

        /** @brief 커스텀 메타데이터 태그를 조회합니다. (Shipping 빌드에서는 nullptr) */
        const string* findCustomMeta( const hashed_string& key ) const noexcept
        {
#if !defined( SW_SHIPPING )
            auto iter = _mapCustomMeta.find( key );
            return ( iter != _mapCustomMeta.end() ) ? &iter->second : nullptr;
#else
            (void)key;
            return nullptr;
#endif
        }

        /**
         * @brief 메모리 포인터에서 실제 enum 크기만큼 안전하게 읽어 int64로 반환합니다.
         * @details 밑바탕 타입의 부호대로 넓힌다 — 이름표(`_mapValueToName`)의 값도 같은 규칙(생성 코드의 `static_cast<int64>( 열거자 )`)
         *          이다. 둘이 어긋나면 `uint8` 의 0x80 · `int8` 의 -1 같은 값이 이름을 잃는다.
         */
        int64 readValueFromMemory( const void* pPtr ) const noexcept
        {
            if ( pPtr == nullptr )
                return 0;
            const bool bSigned = _bIsSigned != SW_FALSE;
            switch ( _size )
            {
                case 1:
                    return bSigned ? static_cast<int64>( *static_cast<const int8*>( pPtr ) ) : static_cast<int64>( *static_cast<const uint8*>( pPtr ) );
                case 2:
                    return bSigned ? static_cast<int64>( *static_cast<const int16*>( pPtr ) ) : static_cast<int64>( *static_cast<const uint16*>( pPtr ) );
                case 8:
                    return *static_cast<const int64*>( pPtr );
                case 4:
                default:
                    return bSigned ? static_cast<int64>( *static_cast<const int32*>( pPtr ) ) : static_cast<int64>( *static_cast<const uint32*>( pPtr ) );
            }
        }

        /** @brief int64 값을 실제 enum 크기만큼만 메모리에 안전하게 씁니다. */
        void writeValueToMemory( void* pPtr, int64 val ) const noexcept
        {
            if ( pPtr == nullptr )
                return;
            switch ( _size )
            {
                case 1:
                {
                    *static_cast<uint8*>( pPtr ) = static_cast<uint8>( val );
                    break;
                }
                case 2:
                {
                    *static_cast<uint16*>( pPtr ) = static_cast<uint16>( val );
                    break;
                }
                case 4:
                {
                    *static_cast<int32*>( pPtr ) = static_cast<int32>( val );
                    break;
                }
                case 8:
                {
                    *static_cast<int64*>( pPtr ) = val;
                    break;
                }
                default:
                {
                    *static_cast<int32*>( pPtr ) = static_cast<int32>( val );
                    break;
                }
            }
        }

        /** @brief ENUM(Invalid/Count) 센티널을 반영해 유효한 값인지 판단합니다. 메타가 없으면 true 입니다. */
        bool isValidValue( int64 value ) const noexcept
        {
            if ( _bHasInvalid != SW_FALSE && value == _invalidValue )
                return false;
            if ( _bHasCount != SW_FALSE && value >= _countValue )
                return false;
            return true;
        }

        /** @brief 문자열로 변환합니다. */
        hashed_string toString( int64 val ) const
        {
            auto iter = _mapValueToName.find( val );
            return iter != _mapValueToName.end() ? iter->second : hashed_string();
        }

        /** @brief 비트플래그 값을 이름 문자열로 바꿉니다. */
        hashed_string toStringFlags( int64 val ) const
        {
            if ( _bIsBitFlag == SW_FALSE )
                return toString( val );

            if ( val == 0 )
            {
                auto iter = _mapValueToName.find( 0 );
                return iter != _mapValueToName.end() ? iter->second : hashed_string( constant::reflection::kNone );
            }

            vector<hashed_string> listName;
            (void)collectFlagNames( val, listName ); // 이름이 덮지 못한 비트는 글에 적지 않는다
            string result;
            result.reserve( 64 );
            for ( const hashed_string& name : listName )
            {
                if ( result.empty() == false )
                    result += constant::reflection::kFlagSeparator;
                result += name.c_str();
            }

            return hashed_string( result.c_str() );
        }

        /**
         * @brief 비트플래그 값에 켜진 열거자들의 이름을 모으고, 이름이 덮지 못한 비트를 돌려줍니다.
         * @details 0 이 아닌 값 가운데 비트가 모두 켜진 것이 이름입니다(`_mapValueToName` 만 쓴다 — ValueAlias 는 겹쳐 적지 않는다). 글(`toStringFlags`)과
         *          바이너리(`SerializerUtil` 의 열거자 정체성 인코딩)가 **같은 이름**을 적도록 규칙을 여기 하나에 둡니다.
         */
        int64 collectFlagNames( int64 value, vector<hashed_string>& outListName ) const
        {
            outListName.clear();
            int64 coveredBits{ 0 };
            for ( const auto& [bitValue, name] : _mapValueToName )
            {
                if ( bitValue != 0 && ( value & bitValue ) == bitValue )
                {
                    outListName.push_back( name );
                    coveredBits |= bitValue;
                }
            }
            return value & ~coveredBits;
        }

        /**
         * @brief 이름 해시로 열거자 값을 찾습니다 — 정본 이름과 ValueAlias 모두. 바이너리는 열거자를 이 해시로 싣습니다(값이 아니라 이름이 정체성이다).
         * @details 표식 값(`Invalid` · `Count`)도 받습니다 — `tryParseText` 와 같은 규칙(필드에 든 값을 적은 그대로 읽는다). 이름 해시는 대소문자를
         *          가리지 않습니다(`hashed_string` — 글 읽기도 대소문자를 가리지 않는다). ValueAlias 로 남긴 이전 이름도 새 열거자로 읽힙니다
         *          (실제 게임 데이터가 생긴 뒤 이름을 바꿀 때의 창구 — 그 전에는 데이터를 새 이름으로 다시 쓴다).
         */
        [[nodiscard]] bool findValueByNameHash( uint32 nameHash, int64& outValue ) const
        {
            for ( const auto& [nameKey, enumValue] : _mapNameToValue )
            {
                if ( nameKey.getHash() == nameHash )
                {
                    outValue = enumValue;
                    return true;
                }
            }
            return false;
        }

        /** @brief 값에 해당하는 intern 된 enumerator 이름입니다. Invalid/Count 센티널이면 nullptr 입니다. */
        const utf8* valueToCString( int64 value ) const
        {
            if ( isValidValue( value ) == false )
                return nullptr;
            const hashed_string name = ( _bIsBitFlag != SW_FALSE ) ? toStringFlags( value ) : toString( value );
            return name.empty() ? nullptr : name.c_str();
        }

        /** @brief 이름을 값으로 바꿉니다. 대소문자는 무시하고, 비트플래그는 `A|B` 도 허용합니다. */
        [[nodiscard]] bool tryParse( string_view name, int64& outValue ) const
        {
            if ( name.empty() )
                return false;

            {
                // 찾기만 한다 — 열거자 이름은 등록 때 intern 됐으므로 답은 같고, 에셋의 모르는 글은 전역 이름 표에 들어가지 않는다.
                const hashed_string key = hashed_string::findInterned( name );
                const auto          it  = _mapNameToValue.find( key );
                if ( it != _mapNameToValue.end() )
                {
                    if ( isValidValue( it->second ) == false )
                        return false;
                    outValue = it->second;
                    return true;
                }
            }

            for ( const auto& [nameKey, enumValue] : _mapNameToValue )
            {
                if ( StringUtil::equals( name, nameKey.view(), true ) )
                {
                    if ( isValidValue( enumValue ) == false )
                        return false;
                    outValue = enumValue;
                    return true;
                }
            }

            int64 flagsValue{ 0 };
            if ( _bIsBitFlag == SW_FALSE || tryParseFlags( name, flagsValue ) == false || isValidValue( flagsValue ) == false )
                return false;
            outValue = flagsValue;
            return true;
        }

        /**
         * @brief 에셋 · 설정 텍스트를 값으로 읽습니다 — 이름(대소문자 무시), 비트플래그면 `A | B`, 그리고 **알려진 값의 숫자**까지.
         * @details 모르는 이름 · 모르는 플래그 토큰이 하나라도 있으면 false 다 — 아는 토큰만 남기거나 0 으로 읽지 않는다. 에셋 · 설정 글을
         *          값으로 읽는 길은 이것 하나다(직렬화기 · 머티리얼).
         *
         *          `tryParse` 와 달리 표식 값(`Invalid` · `Count`)도 받는다. 직렬화는 필드에 든 값을 이름으로 적으므로(`Key::Unknown` 은
         *          "바인딩 없음" 이다) 읽기도 그 이름을 받아야 왕복이 맞는다. 이름을 해시로 바꾸지 않는다 — 에셋 글을 전역 이름표에 넣지
         *          않는다.
         */
        [[nodiscard]] bool tryParseText( string_view text, int64& outValue ) const
        {
            const string_view trimmed = StringUtil::trim( text );
            // 비트플래그의 빈 글은 "아무 비트도 없음" 이다(손으로 쓴 에셋). 쓰는 쪽은 0 을 `None` 으로 적는다(`toStringFlags`).
            if ( trimmed.empty() && _bIsBitFlag != SW_FALSE )
            {
                outValue = 0;
                return true;
            }
            for ( const auto& [nameKey, enumValue] : _mapNameToValue )
            {
                if ( StringUtil::equals( trimmed, nameKey.view(), true ) )
                {
                    outValue = enumValue;
                    return true;
                }
            }
            if ( _bIsBitFlag != SW_FALSE && tryParseFlags( trimmed, outValue ) )
                return true;
            int64 number{ 0 };
            if ( StringUtil::parseInt64( trimmed, number ) == false )
                return false;
            if ( _bIsBitFlag != SW_FALSE )
            {
                int64 knownBits{ 0 };
                for ( const auto& [bitValue, name] : _mapValueToName )
                {
                    knownBits |= bitValue;
                }
                if ( ( number & ~knownBits ) != 0 )
                    return false;
            }
            else if ( _mapValueToName.find( number ) == _mapValueToName.end() )
            {
                return false;
            }
            outValue = number;
            return true;
        }

    private:
        /** @brief `A | B` 를 읽습니다. 토큰마다 알려진 이름(대소문자 무시)이어야 합니다 — 하나라도 모르면 false. 표식 값 검사는 부르는 쪽이 합니다. */
        [[nodiscard]] bool tryParseFlags( string_view text, int64& outValue ) const
        {
            int64  result{ 0 };
            size_t startPos{ 0 };
            bool   bAnyToken = false;
            while ( startPos <= text.size() )
            {
                const size_t      delimiterPos = text.find( '|', startPos );
                const size_t      endPos       = ( delimiterPos != string_view::npos ) ? delimiterPos : text.size();
                const string_view token        = StringUtil::trim( text.substr( startPos, endPos - startPos ) );
                if ( token.empty() == false )
                {
                    bool bKnown = false;
                    for ( const auto& [nameKey, enumValue] : _mapNameToValue )
                    {
                        if ( StringUtil::equals( token, nameKey.view(), true ) )
                        {
                            result |= enumValue;
                            bKnown = true;
                            break;
                        }
                    }
                    // `None` 은 0 열거자가 없는 플래그에 `toStringFlags` 가 0 을 적는 이름이다 — 읽을 때도 0 이어야 왕복이 맞는다.
                    if ( bKnown == false && StringUtil::equals( token, constant::reflection::kNone, true ) == false )
                        return false;
                    bAnyToken = true;
                }
                if ( delimiterPos == string_view::npos )
                    break;
                startPos = delimiterPos + 1;
            }
            if ( bAnyToken == false )
                return false;
            outValue = result;
            return true;
        }

    public:
    };
} // namespace sw

namespace sw
{
    /** @brief 함수 · 이벤트의 인자 하나입니다. 순서는 선언 순서입니다. */
    struct SW_API FunctionParameterInfo
    {
        string _name;     ///< 선언에 적힌 이름. 이름 없이 적었으면 비어 있다
        string _typeName; ///< 정규 타입 이름(`int32` · `string` · `DamageEvent`) — const · 참조는 벗겼다
        /** @brief 기본 인자의 C++ 식 그대로(`1.0f` · `"idle"` · `Mode::Fast`). 없으면 비어 있다. `ReflectionInvoke` 가 인자를 빼먹은 호출에 이것을 읽어 넣는다. */
        string _defaultValue;
        /** @brief 이 인자 타입의 이름 · 값 변환(`ReflectTypeOpsOf<T>::kOps`)입니다. 이벤트는 `EventInfo::setOps` 가 채웁니다. */
        const ReflectTypeOps* _pType;

        FunctionParameterInfo() noexcept;
        FunctionParameterInfo( string name, string typeName, string defaultValue, const ReflectTypeOps* pType );

        /** @brief 기본 인자가 있으면 true 입니다. */
        bool hasDefaultValue() const noexcept { return _defaultValue.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /// @brief 리플렉션 메서드: 이름, 시그니처, invoker
    struct SW_API FunctionInfo
    {
        string                        _name;
        hashed_string                 _hashName;
        string                        _returnTypeName; ///< 정규 타입 이름(예: void, int32)
        vector<FunctionParameterInfo> _listParameter;  ///< 선언 순서대로의 인자(이름 · 타입 · 기본 인자)
        FunctionMetadata              _metadata;
        /**
         * @brief 인자 묶음을 받아 부르는 함수입니다. 인자는 **그 C++ 타입 그대로**(`args.get<T>`) 넣어야 합니다 — 타입이 다르면 Debug 는 단언,
         *        Release 는 잘못 읽습니다. 타입을 모르는 쪽(콘솔 · 비주얼 스크립팅)은 `ReflectionInvoke::call` 로 부릅니다(인자를 바꿔 넣는다).
         */
        Delegate<TaskValue( void*, const TaskArgs& )> _invoker;
        /** @brief 반환 타입의 이름 · 값 변환입니다. void · 생성자면 nullptr 입니다. */
        const ReflectTypeOps* _pReturnType = nullptr;

        /** @brief 인자 개수입니다. */
        uint32 getParameterCount() const noexcept { return static_cast<uint32>( _listParameter.size() ); }

        /** @brief 커스텀 메타데이터 태그를 조회합니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept
        {
            return _metadata.findCustomMeta( key );
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 리플렉션 이벤트 — `PROPERTY()` 를 붙인 멀티캐스트 델리게이트 필드(`MulticastDelegate<void( Args... )>`)입니다.
     * @details 값이 아니라 구독 목록이라 직렬화 · 인스펙터 값 편집에 들지 않습니다. 이름으로 찾아(`TypeInfo::findEventInHierarchy`) 타입을 모른 채
     *          묶고 · 풀고 · 부릅니다(`ReflectionInvoke::bindEvent` · `broadcastEvent`). 에디터 · 비주얼 스크립팅 · 기믹 배선이 쓰는 자리입니다.
     */
    struct SW_API EventInfo
    {
        hashed_string                 _name;
        vector<FunctionParameterInfo> _listParameter; ///< 이름은 선언에서, 변환 표는 `setOps` 가 템플릿 인자에서
        PropertyMetadata              _metadata;      ///< 표시 메타(Category · DisplayName · Tooltip · Meta · HideInInspector)만 쓴다
        /** @brief 묶기 · 풀기 · 부르기 · 인자 변환 표입니다(`ReflectEventOpsOf<필드 타입>::kOps`). */
        const ReflectEventOps* _pOps;
        size_t                 _offset; ///< 인스턴스 안 델리게이트 필드의 자리

        EventInfo() noexcept;

        /** @brief 표를 걸고, 인자 변환 표를 템플릿 인자에서 채웁니다. 선언에서 이름을 못 읽은 인자도 자리는 채웁니다. */
        void setOps( const ReflectEventOps* pOps );

        /** @brief 인스턴스 안 델리게이트 필드의 자리입니다. */
        void* getEventPtr( void* pInstance ) const noexcept { return reinterpret_cast<utf8*>( pInstance ) + _offset; }
        /** @brief 인스턴스 안 델리게이트 필드의 자리입니다. */
        const void* getEventPtr( const void* pInstance ) const noexcept { return reinterpret_cast<const utf8*>( pInstance ) + _offset; }
        /** @brief 인자 개수입니다. */
        uint32 getParameterCount() const noexcept { return static_cast<uint32>( _listParameter.size() ); }
    };
} // namespace sw

namespace sw
{
    /// @brief 등록된 타입: FQN, 프로퍼티/메서드, 생성 가능 여부
    struct SW_API TypeInfo
    {
        size_t _size;
        /** @brief `$ctor` 로 placement new 한 인스턴스를 파괴합니다. 없으면 nullptr 입니다. */
        void ( *_destroyInstance )( void* ) = nullptr;
        /**
         * @brief 이 타입의 컴포넌트를 @p pOwner 에 붙여 만듭니다(`GameObject::addComponentTo<T>`). 만들 수 있는 컴포넌트 타입이 아니면 nullptr 입니다.
         * @details 언리얼 `UClass` 가 리플렉션 정보와 생성(`ClassConstructor`)을 함께 드는 것과 같은 자리입니다. 이름으로 컴포넌트를 만드는 길
         *          (`GameObjectManager::addComponentByName` — 씬 · 프리팹 로드 · 에디터)은 이 칸 하나를 봅니다. 코드젠이 추상이 아닌 컴포넌트
         *          타입마다 채웁니다. 모듈 코드를 가리키므로 모듈 해제(`clearContent`)가 비우고, 같은 FQN 의 재등록이 새 이미지의 주소로 덮습니다.
         */
        Component* ( *_addComponent )( GameObject* pOwner );
        /** @brief `REFLECT( Validate = fn )` — 타입 검증 함수입니다. 없으면 nullptr 입니다. 모듈 코드라 `clearContent` 가 비웁니다. */
        ReflectValidateFunction                                   _pValidate;
        hashed_string                                             _name;
        hashed_string                                             _fullyQualifiedName;
        hashed_string                                             _parentFQN;
        hashed_string                                             _moduleName;
        vector<PropertyInfo>                                      _listProperty;
        vector<FunctionInfo>                                      _listMethod;
        vector<EventInfo>                                         _listEvent; ///< 이 타입이 선언한 이벤트(기반의 것은 `findEventInHierarchy` 가 걷는다)
        TypeMetadata                                              _metadata;
        mutable vector<PropertyInfo>                              _listPropertyWithBase;
        mutable unordered_map<hashed_string, const PropertyInfo*> _mapNameToPropertyWithBase; ///< 계층 병합 목록의 이름 · 별칭 → 항목. 목록과 함께 만듭니다
        mutable unordered_map<hashed_string, const PropertyInfo*> _mapNameToProperty;
        mutable unordered_map<hashed_string, const FunctionInfo*> _mapNameToMethod;
        /**
         * @brief `_parentFQN` 을 한 번 풀어 둔 부모 `TypeInfo` 입니다. 없거나 아직 풀지 못했으면 nullptr 입니다.
         * @details `isDerivedFrom` 이 조상마다 `findType(_parentFQN)`(shared_mutex 잠금 + 해시맵 조회)을 부르지 않도록,
         *          등록 배치 끝(`TypeRegistry::buildLookupCaches`)에서 한 번 풀어 둡니다. 걷는 일은 포인터 역참조 몇 번입니다.
         *
         *          **해제 때 비워집니다.** `TypeInfo` 의 주소는 고정이라 옮겨지지는 않지만, 모듈 해제 뒤에는
         *          묘비가 된 부모를 가리킬 수 있습니다. 그래서 해제가 남은 타입 모두의 이 칸을 비우고, 비어 있으면
         *          `getParentType()` 이 이름으로 다시 풉니다. 원자값인 이유: 배치 밖에서 등록된 타입(테스트)은 첫
         *          조회가 여러 스레드에서 동시에 올 수 있고, 같은 값을 쓰는 경쟁이라 relaxed 로 충분합니다.
         */
        mutable atomic<const TypeInfo*> _pParentType;
        /**
         * @brief `_parentFQN` 을 이름으로 풀어 봤지만 풀지 못했던 타입 표 세대입니다. 0 이면 아직 해 보지 않았습니다.
         * @details 등록되지 않은 기반은 이름만 적혀 있습니다. 부모를 묻는 자리(계층 프로퍼티 조회 · 이름 걷기 · 기본값의 사슬 수집)마다
         *          레지스트리를 잠그고 다시 찾지 않도록, 같은 세대면 답이 같으므로 세대를 적어 두고 등록 · 해제로 세대가 바뀔 때만 다시 찾습니다.
         */
        mutable atomic<uint32> _parentMissGeneration;
        uint32                 _typeId;
        /**
         * @brief 루트부터 자기까지의 **이름(FQN 의 intern 인덱스)** 을 깊이 순서로 적은 조상 표입니다. `_ancestorDepth` 가 자기 칸입니다.
         * @details 캐스트의 핫패스가 이것만 봅니다: `표[pTarget 의 깊이] == pTarget 의 이름`. 포인터가 아니라 이름이라, 레지스트리 밖
         *          사본(테스트 목의 손으로 만든 `StaticType()`)도 같은 이름이면 같은 타입으로 봅니다. 걷기의 `isSameTypeName` 과
         *          같은 규칙입니다. `_typeId` 를 쓰지 않는 이유도 그 사본입니다. 사본은 자기 id 를 따로 가집니다. 등록 · 해제로
         *          사슬이 바뀔 수 있으면 `TypeRegistry` 가 깊이를 `kAncestorDepthUnknown` 으로 비우고, 배치 끝(`buildLookupCaches`)
         *          이나 첫 상속 검사가 다시 세웁니다.
         *          원자값인 이유는 `_pParentType` 과 같습니다. 첫 조회는 여러 스레드에서 올 수 있고 같은 값을 씁니다. 등록과
         *          캐스트가 겹치는 것은 `_pParentType` 과 마찬가지로 전제하지 않습니다(모듈 로드는 단일 스레드).
         */
        mutable atomic<uint32> _arrAncestorNameIndex[constant::reflection::kAncestorDisplayDepth];
        /** @brief 조상 표에서 자기 칸의 깊이. Unknown 이면 아직 안 세웠고, None 이면 세울 수 없어 부모 포인터를 걷는다. */
        mutable atomic<uint8> _ancestorDepth;
        /**
         * @brief 레지스트리에 살아 있는 타입인지 나타냅니다. 모듈 해제는 항목을 지우지 않고 이것을 내립니다(묘비).
         * @details `TypeInfo` 의 주소는 고정입니다. 레지스트리가 `unique_ptr` 로 들고, 표가 커져도 옮기지 않으며, 해제해도
         *          지우지 않습니다. 그래서 `TypeLookupCache` 와 `_pParentType` 은 세대 검사 없이 포인터를 그대로 쓰고, 이
         *          플래그 하나로 "해제됐나" 를 봅니다. 같은 FQN 이 다시 등록되면 같은 객체에 덮어써 되살립니다. 레지스트리
         *          밖에서 만든 사본은 항상 살아 있습니다.
         */
        mutable atomic<uint8> _bAlive;
        /** @brief REFLECT(Abstract) 또는 C++ 추상 클래스입니다. 생성할 수 없습니다(UCLASS(Abstract)). */
        uint8 _bAbstract : 1;
        /** @brief REFLECT(Static) 타입(함수 라이브러리)입니다. FunctionMetadata::_bStatic 과는 다릅니다. */
        uint8 _bStatic : 1;
        /** @brief 내장 스칼라 · 문자열입니다(int32, bool, sw::string, …). REFLECT 코드젠이 없습니다. */
        uint8         _bPrimitive                 : 1;
        mutable uint8 _bIsCacheBuilt              : 1;
        mutable uint8 _bIsPODFastPath             : 1;
        mutable uint8 _bIsPODCalculated           : 1;
        mutable uint8 _bListPropertyWithBaseBuilt : 1;
        /**
         * @brief `getPropertiesWithBase()` 가 지금 이 타입에서 재귀 중인지 나타냅니다.
         * @details 부모 체인이 순환하면(`_parentFQN` 이 자기 자신이나 자손을 가리키면) 그 재귀가
         *          돌아오지 않아 스택이 넘칩니다. `_parentFQN` 은 코드젠이 적는 값이지만
         *          `registerClass` 는 공개 API 이고 그 값을 검사하지 않으며, 모듈이 따로따로
         *          등록되는 핫 리로드에서는 A→B→A 가 만들어질 수 있습니다.
         */
        mutable uint8 _bBuildingPropertyWithBase : 1;
        /** @brief `hasSaveGameProperty` 의 답과 그것을 구했는지입니다. 상속 목록 캐시와 함께 비웁니다. */
        mutable uint8 _bHasSaveGameProperty : 1;
        mutable uint8 _bSaveGameCalculated  : 1;
        /** @brief `ReflectionValidation::hasValidator` 의 답과 그것을 구했는지입니다. 상속 목록 캐시와 함께 비웁니다. */
        mutable uint8          _bHasValidator        : 1;
        mutable uint8          _bValidatorCalculated : 1;
        [[maybe_unused]] uint8 _reservedCacheFlags   : 4;
        [[maybe_unused]] uint8 _reservedPadding[2];

        /** @brief 빈 TypeInfo 를 만듭니다. */
        TypeInfo() noexcept;
        ~TypeInfo() = default;
        TypeInfo( const TypeInfo& other );
        TypeInfo( TypeInfo&& other ) noexcept;
        TypeInfo& operator=( const TypeInfo& other );
        TypeInfo& operator=( TypeInfo&& other ) noexcept;

        /** @brief 커스텀 메타데이터 태그를 조회합니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept
        {
            return _metadata.findCustomMeta( key );
        }

        /** @brief 카테고리를 반환합니다. */
        const string& getCategory() const noexcept
        {
#if !defined( SW_SHIPPING )
            return _metadata._category;
#else
            static const string s_empty;
            return s_empty;
#endif
        }

        /** @brief 표시 이름을 반환합니다. DisplayName 메타가 없으면 C++ 타입 이름을 반환합니다. */
        const utf8* getDisplayName() const noexcept
        {
#if !defined( SW_SHIPPING )
            if ( _metadata._displayName.empty() == false )
                return _metadata._displayName.c_str();
#endif
            return _name.c_str();
        }

        /** @brief 툴팁 문자열을 반환합니다. */
        const string& getTooltip() const noexcept
        {
#if !defined( SW_SHIPPING )
            return _metadata._tooltip;
#else
            static const string s_empty;
            return s_empty;
#endif
        }

        /** @brief "Add Component" 메뉴에서 숨겨야 하는지 여부를 반환합니다. */
        bool isHiddenInMenu() const noexcept
        {
#if !defined( SW_SHIPPING )
            return _metadata._bHideInMenu != SW_FALSE;
#else
            return false;
#endif
        }

        /** @brief 팩토리/$ctor가 인스턴스를 만들 수 있으면 true. */
        bool canConstruct() const noexcept { return _bAbstract == SW_FALSE && _bStatic == SW_FALSE && _bPrimitive == SW_FALSE; }

        /** @brief REFLECT 없이 등록된 내장 타입(int32, float32, …)이면 true 입니다. */
        bool isPrimitive() const noexcept { return _bPrimitive != SW_FALSE; }

        /** @brief 직렬화/복사 시 memcpy POD 경로를 쓸 수 있으면 true. */
        bool usesPodCopyFastPath() const;
        /**
         * @brief 상속분까지 `SaveGame` 프로퍼티가 하나라도 있으면 true 입니다 — 세이브 직렬화가 그것만 쓰는 타입(옵트인)인가.
         * @details 상속 목록 캐시와 같이 비우고 다시 구합니다(`clearInheritedProperties`).
         */
        bool hasSaveGameProperty() const;
        /** @brief 이 타입이 targetFqn이거나 그 파생이면 true. 부모가 미등록이어도 `_parentFQN` 이 같으면 true. */
        bool isDerivedFrom( const hashed_string& targetFqn ) const;
        /**
         * @brief 이 타입이 pTarget 이거나 그 파생이면 true 입니다. 조상 표가 있으면 로드 둘과 비교 하나로 끝나며, 여기 인라인입니다.
         * @details 캐스트의 핫패스라 DLL 경계를 넘지 않습니다. 둘 다 표가 서 있으면 `표[pTarget 의 깊이] == pTarget 의
         *          이름` 으로 끝납니다. 표가 아직 없거나 세울 수 없는 쪽은 `isDerivedFromSlow` 가 세우거나 부모 포인터를
         *          걷습니다. 레지스트리 밖의 사본(테스트 목의 `StaticType()`)도 이름이 같으면 같은 타입으로 봅니다.
         *          pTarget 이 nullptr 이면 false 입니다.
         */
        bool isDerivedFrom( const TypeInfo* pTarget ) const
        {
            if ( pTarget == nullptr )
                return false;
            if ( pTarget == this )
                return true;
            const uint8 selfDepth   = _ancestorDepth.load( std::memory_order_acquire );
            const uint8 targetDepth = pTarget->_ancestorDepth.load( std::memory_order_acquire );
            if ( selfDepth < constant::reflection::kAncestorDisplayDepth && targetDepth < constant::reflection::kAncestorDisplayDepth )
            {
                return targetDepth <= selfDepth && _arrAncestorNameIndex[targetDepth].load( std::memory_order_relaxed ) ==
                                                       pTarget->_arrAncestorNameIndex[targetDepth].load( std::memory_order_relaxed );
            }
            return isDerivedFromSlow( pTarget );
        }
        /** @brief 표가 없을 때의 경로입니다. 세울 수 있으면 세워서 답하고, 아니면 부모 포인터를 걷습니다. */
        bool isDerivedFromSlow( const TypeInfo* pTarget ) const;
        /** @brief 부모 TypeInfo 입니다. 풀어 둔 것이 없으면 이름으로 찾아 적어 둡니다. 부모가 없거나 미등록이면 nullptr 입니다. */
        const TypeInfo* getParentType() const;
        /** @brief 부모 포인터를 이름으로 다시 풉니다. 등록 배치 끝에서 `TypeRegistry` 가 부릅니다. */
        void resolveParentType() const;
        /** @brief 부모 포인터와 "풀지 못했다" 는 기억을 비웁니다. 해제 뒤(부모가 사라졌을 수 있습니다) `TypeRegistry` 가 부릅니다. */
        void clearParentType() const
        {
            _pParentType.store( nullptr, std::memory_order_relaxed );
            _parentMissGeneration.store( 0, std::memory_order_relaxed );
        }

        /** @brief 조상 표가 세워져 있으면 true 입니다. 상속 검사가 걷지 않고 O(1) 로 답합니다. */
        bool hasAncestorDisplay() const
        {
            return _ancestorDepth.load( std::memory_order_acquire ) < constant::reflection::kAncestorDisplayDepth;
        }
        /**
         * @brief 부모 포인터를 따라 조상 표를 세웁니다. 사슬이 모두 풀려 있고 표 깊이 안이면 true 입니다.
         * @details 이름이 없거나, 순환이거나, 표보다 깊으면 `kAncestorDepthNone` 을 적고 false 를 반환합니다. 그 타입은 다음
         *          등록 · 해제까지 부모 포인터 걷기로 답합니다. 풀리지 않는 부모(REFLECT 가 아닌 기반 · 아직 올라오지 않은
         *          모듈)는 사슬의 끝으로 봅니다. 그 부모가 등록되는 순간 `registerClass` 가 표를 비웁니다.
         *          등록 배치 끝에서 `TypeRegistry` 가 부르고, 배치 밖 타입은 첫 상속 검사가 부릅니다.
         */
        bool buildAncestorDisplay() const;
        /** @brief 조상 표를 비웁니다. 사슬이 바뀔 수 있는 등록 · 해제 뒤에 `TypeRegistry` 가 부릅니다. */
        void clearAncestorDisplay() const
        {
            _ancestorDepth.store( constant::reflection::kAncestorDepthUnknown, std::memory_order_relaxed );
        }
        /**
         * @brief 부모에게서 **복사해 온** 캐시(상속 포함 목록과 그 이름 맵 · POD 판정)를 비웁니다. 다음 조회가 지금 부모로 다시 만듭니다.
         * @details 등록 · 해제 뒤에 `TypeRegistry` 가 모든 타입에 부릅니다. 이것이 없으면 기반이 다시 등록되거나 내려가도 **다른 모듈의
         *          파생 타입**이 옛 기반의 프로퍼티(옛 오프셋 · 내려간 모듈 코드를 가리키는 접근자)를 계속 내놓는다.
         */
        void clearInheritedProperties() const;
        /**
         * @brief 선언에서 **파생된** 캐시(이름 맵 · 상속 포함 목록 · 부모 포인터 · 조상 표 · POD 판정)를 모두 비웁니다.
         * @details 복사 · 이동 대입이 함께 씁니다. 캐시를 더하면 여기서 비울 것 — 빠뜨리면 대입된 타입이 **옛 타입의 캐시**로 답합니다
         *          (이름 조회가 다른 오프셋을 줍니다).
         */
        void invalidateDerivedCaches();
        /** @brief 레지스트리에 살아 있으면 true 입니다. 모듈 해제가 내리고 재등록이 올립니다. 사본은 항상 true 입니다. */
        bool isAlive() const { return _bAlive.load( std::memory_order_acquire ) != SW_FALSE; }
        /**
         * @brief 묘비로 남길 때 모듈이 든 내용을 비웁니다. 이름 · id · 모듈만 남습니다.
         * @details 프로퍼티 · 메서드 목록은 그 모듈의 코드(델리게이트 · `$ctor` · 소멸 함수)를 가리킵니다. 모듈이 내려간 뒤에
         *          이것을 파괴하면(재등록 대입 · 레지스트리 소멸) 사라진 코드를 부릅니다. 그래서 해제 시점, **모듈이 아직 살아 있을 때**
         *          여기서 비웁니다.
         */
        void clearContent();

        /**
         * @brief 자신과 상속한 기반의 프로퍼티입니다(단일 부모 체인).
         * @details 자식의 이름이 부모를 덮어씁니다. 단일 상속에서 안전합니다.
         */
        const vector<PropertyInfo>& getPropertiesWithBase() const;

        /**
         * @brief 이름→프로퍼티/메서드 조회 캐시를 만듭니다.
         * @warning **여러 스레드가 동시에 부르면 안 됩니다.** `mutable` 맵 둘을 잠금 없이 채웁니다.
         *          두 스레드가 같은 `TypeInfo` 를 처음 조회하면 같은 맵에 동시에 삽입합니다.
         *          그래서 `TypeRegistry::buildLookupCaches()` 가 **등록 배치가 끝난 직후 단일
         *          스레드에서** 한 번 만듭니다. 등록된 타입을 병렬로 조회하는 것은 그 뒤이므로 안전합니다.
         *          (등록하지 않은 임시 사본은 만든 스레드가 알아서 씁니다.)
         */
        void buildLookupCache() const
        {
            if ( _bIsCacheBuilt != SW_FALSE )
                return;

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
        }

        /** @brief 조회 캐시가 이미 만들어져 있는지 반환합니다 (`buildLookupCaches` 뒤에는 true). */
        bool isLookupCacheBuilt() const { return _bIsCacheBuilt != SW_FALSE; }

        /** @brief 이름 또는 alias로 프로퍼티를 찾습니다. */
        const PropertyInfo* findProperty( const hashed_string& propertyNameOrAlias ) const
        {
            if ( _listProperty.size() <= constant::reflection::kLinearSearchThreshold )
            {
                for ( const PropertyInfo& propertyInfo : _listProperty )
                {
                    if ( propertyInfo.matchesName( propertyNameOrAlias ) )
                        return &propertyInfo;
                }
                return nullptr;
            }

            buildLookupCache();
            auto it = _mapNameToProperty.find( propertyNameOrAlias );
            return it != _mapNameToProperty.end() ? it->second : nullptr;
        }

        /** @brief 이름으로 메서드를 찾습니다. */
        const FunctionInfo* findMethod( const hashed_string& methodName ) const
        {
            if ( _listMethod.size() <= constant::reflection::kLinearSearchThreshold )
            {
                for ( const FunctionInfo& method : _listMethod )
                {
                    if ( method._hashName == methodName )
                        return &method;
                }
                return nullptr;
            }

            buildLookupCache();
            auto it = _mapNameToMethod.find( methodName );
            return it != _mapNameToMethod.end() ? it->second : nullptr;
        }

        /**
         * @brief 현재 클래스와 부모 상속 체인에서 프로퍼티를 찾습니다. 계층 병합 목록과 함께 만든 맵 하나로 찾습니다.
         * @details `getPropertiesWithBase()` 가 만들어 둔 맵을 한 번 봅니다. 파생이 기반과 같은
         *          이름을 다시 적으면 파생이 이깁니다(병합 규칙과 같습니다).
         */
        const PropertyInfo* findPropertyInHierarchy( const hashed_string& propertyNameOrAlias ) const;

        /**
         * @brief 프로퍼티를 순회합니다. bIncludeBase 가 true 이면 상속 체인을 포함합니다.
         * @details **직렬화는 true 를 써야 합니다.** 기본값 false 로 부르면 **상속된 PROPERTY 를 저장도 로드도 하지
         *          않습니다** — 컴포넌트에서는 그것이 곧 `SceneComponent` 의 트랜스폼이라 씬 · 프리팹 · Undo 스냅샷에서
         *          메시 · 스프라이트 · 카메라의 위치가 사라집니다.
         *          `getPropertiesWithBase()` 는 기반 먼저, 파생이 같은 이름을 **덮어쓰는** 순서로
         *          평탄화하고 캐시하므로 중복 방출은 없습니다.
         * @note 상속 체인을 **부르는 쪽이 직접 도는** 코드(`ComponentDefaults` 가 레벨마다 자기 XML
         *       노드를 찾아 적용합니다)는 false 가 맞습니다. true 로 주면 같은 값을 여러 노드에서
         *       중복 적용합니다.
         */
        template <typename Func>
        void forEachProperty( Func&& func, bool bIncludeBase = false ) const
        {
            if ( bIncludeBase == false )
            {
                for ( const PropertyInfo& propertyInfo : _listProperty )
                {
                    func( propertyInfo );
                }
            }
            else
            {
                for ( const PropertyInfo& propertyInfo : getPropertiesWithBase() )
                {
                    func( propertyInfo );
                }
            }
        }

        /** @brief 메서드를 순회합니다. */
        template <typename Func>
        void forEachMethod( Func&& func ) const
        {
            for ( const FunctionInfo& method : _listMethod )
            {
                func( method );
            }
        }

        /** @brief 이 타입이 선언한 이벤트를 이름으로 찾습니다. */
        const EventInfo* findEvent( const hashed_string& eventName ) const
        {
            for ( const EventInfo& event : _listEvent )
            {
                if ( event._name == eventName )
                    return &event;
            }
            return nullptr;
        }

        /** @brief 자기부터 기반으로 올라가며 이벤트를 이름으로 찾습니다(파생이 이긴다). */
        const EventInfo* findEventInHierarchy( const hashed_string& eventName ) const;
        /** @brief 자기부터 기반으로 올라가며 메서드를 이름으로 찾습니다(파생이 이긴다). */
        const FunctionInfo* findMethodInHierarchy( const hashed_string& methodName ) const;

        /** @brief 기반부터 파생 순서로 이벤트를 순회합니다. */
        template <typename Func>
        void forEachEventWithBase( Func&& func ) const
        {
            const TypeInfo* arrChain[constant::reflection::kMaxParentChainDepth];
            uint32          depth = collectTypeChain( arrChain );
            while ( depth > 0 )
            {
                --depth;
                for ( const EventInfo& event : arrChain[depth]->_listEvent )
                {
                    func( event );
                }
            }
        }

        /** @brief 자기부터 기반까지의 사슬을 @p outArrType 에 담고 길이를 돌려줍니다(풀리지 않는 부모에서 끝나고, 순환이면 상한에서 멈춘다). */
        uint32 collectTypeChain( const TypeInfo* ( &outArrType )[constant::reflection::kMaxParentChainDepth] ) const;
    };

} // namespace sw
