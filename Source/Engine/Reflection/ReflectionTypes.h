/**
 * @file ReflectionTypes.h
 * @brief 리플렉션용 프로퍼티 / enum / 함수 / 타입 정보
 * @details `GameObject.h` · `Component.h` 를 거쳐 TU 약 1,200 개가 읽는 헤더다. 속성 키를 더할 때마다 바뀌는 메타데이터 구조
 *          (`TypeMetadata` · `PropertyMetadata` · `FunctionMetadata`)는 `ReflectionMetadata.h` 에 있고, 여기서는 `ReflectMetadataBox` 로만 든다.
 *          속성 키 · 메타데이터 칸을 더할 때는 `ReflectionMetadata.h` 만 고친다 — 이 헤더에 메타데이터 칸을 읽는 인라인 본문을 두지 않는다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionConstants.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Reflection/TextAddress.h"

namespace sw
{
    struct FixedCodec;
    struct ReflectEventOps;
    struct ReflectTypeOps;

    class ValidationContext;

    /** @brief `PROPERTY( Validate = fn )` 과 `REFLECT( Validate = fn )` 의 검증 함수 타입입니다. 생성 코드가 이 함수를 만듭니다. */
    using ReflectValidateFunction = void ( * )( const void* pInstance, ValidationContext& context );
    /** @brief 필드 이름 꼴 범위 경계(`PROPERTY( Min = "-_halfSize" )`)를 인스턴스에서 읽는 함수 타입입니다. 생성 코드가 이 함수를 만듭니다(`PropertyMetadata::_pReadMinRange`). */
    using PropertyRangeReadFunction = float64 ( * )( const void* pInstance );

    class Component;
    class GameObject;

    /// @brief 함수의 네트워크 역할입니다. 목록은 PredefinedFunctionNetRole.xxx 에 있습니다.
    enum class FunctionNetRole : uint8
    {
#define REGISTER_FUNCTION_NET_ROLE( Name ) Name,
#include "Core/Predefined/PredefinedFunctionNetRole.xxx"

#undef REGISTER_FUNCTION_NET_ROLE
    };

    struct FunctionMetadata;
    struct PropertyMetadata;
    struct TypeMetadata;

    /**
     * @brief 리플렉션 메타데이터(`ReflectionMetadata.h`)를 값처럼 들되, 그 정의를 모르는 TU 에서도 복사 · 이동 · 소멸할 수 있게 하는 상자입니다.
     * @details 속성 키를 더할 때마다 바뀌는 메타데이터 구조를 이 헤더(`GameObject.h` · `Component.h` 를 거쳐 TU 약 1,200 개가 읽는다)에서 빼려는 것이다(계획 BuildSpeed 12).
     *          복사와 소멸은 만들 때 적은 함수 표로 하므로 메타데이터 정의가 필요한 것은 기본 생성뿐이다. 기본 생성은 `ReflectionCore.cpp` 의
     *          `TypeInfo` · `PropertyInfo` · `FunctionInfo` · `EventInfo` 생성자만 한다(함수 표가 엔진 모듈 코드를 가리켜 게임 모듈을 내려도 남는다).
     *          값 의미다 — 복사는 깊은 복사다. 이동한 뒤의 원본은 소멸과 대입만 할 수 있다. 칸은 `->` 로 읽고(`prop._metadata->_bTransient`) 그 TU 는 `ReflectionMetadata.h` 를 include 한다.
     */
    template <typename T>
    class ReflectMetadataBox
    {
    public:
        /** @brief 기본값 메타데이터를 만듭니다. `T` 의 정의가 보여야 합니다. */
        ReflectMetadataBox()
            : _pValue{ sw_new T{} }
            , _pOps{ &getOps() }
        {
        }
        ~ReflectMetadataBox()
        {
            if ( _pValue != nullptr )
                _pOps->_pDestroy( _pValue );
        }
        ReflectMetadataBox( const ReflectMetadataBox& other )
            : _pValue{ other._pOps->_pClone( *other._pValue ) }
            , _pOps{ other._pOps }
        {
        }
        ReflectMetadataBox( ReflectMetadataBox&& other ) noexcept
            : _pValue{ other._pValue }
            , _pOps{ other._pOps }
        {
            other._pValue = nullptr;
        }
        ReflectMetadataBox& operator=( const ReflectMetadataBox& other )
        {
            if ( this != &other )
            {
                T* pCopy = other._pOps->_pClone( *other._pValue );
                if ( _pValue != nullptr )
                    _pOps->_pDestroy( _pValue );
                _pValue = pCopy;
                _pOps   = other._pOps;
            }
            return *this;
        }
        ReflectMetadataBox& operator=( ReflectMetadataBox&& other ) noexcept
        {
            std::swap( _pValue, other._pValue );
            std::swap( _pOps, other._pOps );
            return *this;
        }

        T*       operator->() noexcept { return _pValue; }
        const T* operator->() const noexcept { return _pValue; }
        T&       operator*() noexcept { return *_pValue; }
        const T& operator*() const noexcept { return *_pValue; }

    private:
        /** @brief 정의를 아는 곳(기본 생성)에서 한 번 만드는 복사 · 소멸 함수 표입니다. */
        struct Ops
        {
            T* ( *_pClone )( const T& value );
            void ( *_pDestroy )( T* pValue );
        };

        static T* cloneValue( const T& value )
        {
            return sw_new T{ value };
        }
        static void destroyValue( T* pValue )
        {
            sw_delete( pValue );
        }
        static const Ops& getOps()
        {
            static constexpr Ops s_ops{ &cloneValue, &destroyValue };
            return s_ops;
        }

        T*         _pValue; ///< 이동한 뒤에만 nullptr 이다
        const Ops* _pOps;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 컨테이너가 중첩된 프로퍼티의 스키마입니다(`vector&lt;vector&lt;T&gt;&gt;`, `map&lt;K,vector&lt;V&gt;&gt;` 등).
     */
    struct NestedContainerInfo
    {
        ContainerKind                   _kind = ContainerKind::None;
        hashed_string                   _typeName; ///< 컨테이너 TypeInfo 의 이름이다(vector, unordered_map 등)
        hashed_string                   _elementTypeName;
        hashed_string                   _keyTypeName;
        shared_ptr<IContainerWrapper>   _wrapper;
        shared_ptr<NestedContainerInfo> _elementNested; ///< 원소나 값이 컨테이너일 때 쓴다
    };
} // namespace sw

namespace sw
{
    /// @brief 프로퍼티 하나의 리플렉션 정보입니다. 오프셋, 타입, 별칭, 컨테이너 래퍼를 담습니다.
    struct SW_API PropertyInfo
    {
        using PropertyBindingDelegate = Delegate<void( const PropertyInfo& prop, ObjectAddress instance )>;
        /**
         * @brief 값이 객체 **밖**에 있는 프로퍼티의 값 주소를 반환하는 함수입니다. 참조를 반환하는 메서드에 `PROPERTY` 를 붙이면 생성됩니다.
         * @details 인자는 오프셋의 기준이 되는 객체 주소(`getRawPtr` 의 인자와 같다)이고, 반환값은 그 인스턴스의 값 주소다.
         *          씬 컴포넌트의 로컬 TRS 가 첫 번째 예다. 값은 트랜스폼 저장소의 슬롯에 있지만 이름(`_localPosition`)은 그대로라서,
         *          직렬화기, 인스펙터, 비교 도구가 코드를 바꾸지 않고 그 위치를 읽고 쓴다.
         */
        using ValueAccessor = void* (*)( void* pInstance );
        /**
         * @brief `RepNotify` 함수를 호출하는 함수입니다. 생성 코드가 만듭니다.
         * @details @p pOldValue 는 바뀌기 전 값의 주소이고, 타입은 프로퍼티의 선언 타입이다. 함수가 이전 값을 받지 않으면 쓰지 않는다.
         */
        using RepNotifyFunction = void ( * )( void* pInstance, const void* pOldValue );

        shared_ptr<IContainerWrapper>   _containerWrapper;
        shared_ptr<NestedContainerInfo> _nestedContainer; ///< 컨테이너일 때 중첩 구조 전체다
        mutable PropertyBindingDelegate _onPropertyBoundChanged;

        /** @brief 값이 객체 밖에 있을 때 그 주소를 찾는 함수입니다. nullptr 이면 값은 `인스턴스 + _offset` 에 있습니다. */
        ValueAccessor _pValueAccessor;
        /** @brief `RepNotify` 를 부르는 함수입니다. 없으면 nullptr 입니다(`PropertyRoleUtil::callRepNotify`). */
        RepNotifyFunction _pRepNotify;
        /** @brief `Validate = fn` 을 부르는 함수입니다. 없으면 nullptr 입니다(`ReflectionValidation::validateObject`). */
        ReflectValidateFunction _pValidate;

        hashed_string _name;
        hashed_string _typeName;
        hashed_string _elementTypeName;
        hashed_string _keyTypeName;
        /**
         * @brief 타입 핸들(`TComponentHandle<T>`)이 가리키는 컴포넌트 타입 `T` 의 이름입니다. 타입 핸들이 아니면 비어 있습니다.
         * @details 값(`_typeName`) 또는 시퀀스 원소(`_elementTypeName`)는 내장 `ComponentHandle` 로 기록되고, 대상 타입은 여기에만 있습니다.
         *          파서가 값 하나와 시퀀스 원소만 허용하므로 필드당 하나면 충분합니다(`docs/plans/TypedComponents.md` 3.7).
         */
        hashed_string                        _handleTargetTypeName;
        vector<hashed_string>                _listAlias; ///< `PROPERTY( Alias = … )` 로 적은 다른 키 목록이다. 이름을 바꾸기 전의 키를 적는다
        ReflectMetadataBox<PropertyMetadata> _metadata;  ///< `PROPERTY()` 에 적은 메타데이터(`ReflectionMetadata.h`)

        mutable uint32 _cachedNameHash;

        uint32                 _bitOffset;
        ContainerKind          _containerKind;
        uint8                  _bIsContainer  : 1;
        uint8                  _bIsBitField   : 1;
        [[maybe_unused]] uint8 _reservedFlags : 6;

        /** @brief 오프셋 0, 컨테이너가 아닌 상태로 만듭니다. */
        PropertyInfo() noexcept;

        /** @brief 이름, 타입, 오프셋으로 프로퍼티를 채웁니다. */
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
            return getNameHash() == nameOrAliasHash || matchesRedirectHash( nameOrAliasHash );
        }

        /**
         * @brief 옛 이름(`PROPERTY( Alias = … )`) 중 하나의 해시와 일치하면 true 입니다. 지금 이름은 보지 않습니다.
         * @details 태그 스트림 읽기는 모든 프로퍼티의 지금 이름을 먼저 보고, 없을 때만 이것을 봅니다(`SerializerUtil::findPropertyIndexByNameHash`).
         */
        bool matchesRedirectHash( const uint32 oldNameHash ) const noexcept
        {
            return VectorUtil::containsIf( _listAlias, [&]( const hashed_string& alias )
            { return alias.empty() == false && alias.getHash() == oldNameHash; } );
        }

        /** @brief 이름이나 별칭과 일치하면 true 입니다. */
        bool matchesName( const hashed_string& nameOrAlias ) const
        {
            if ( _name == nameOrAlias )
                return true;
            if ( _listAlias.empty() )
                return false;
            return VectorUtil::contains( _listAlias, nameOrAlias );
        }

        /** @brief 이 프로퍼티가 직렬화 가능한 컨테이너 래퍼를 가지고 있는지 반환합니다. */
        bool hasContainerWrapper() const noexcept
        {
            return _containerWrapper != nullptr;
        }

        /** @brief 단일 컨테이너와 중첩 컨테이너를 똑같이 다루기 위한 구조 정보를 반환합니다. */
        NestedContainerInfo getContainerShape() const;

        /** @brief 값이 바뀔 때 부르는 콜백을 등록합니다. */
        void bindOnChanged( PropertyBindingDelegate delegate ) const { _onPropertyBoundChanged = std::move( delegate ); }

        /** @brief 비트필드에서 그 비트 하나만 1 로 세우는 함수입니다. 생성 코드가 만듭니다. 인자는 0 으로 채운, 그 타입 크기의 메모리입니다. */
        using SetBitFunction = void ( * )( void* pInstance );

        /**
         * @brief 비트필드의 바이트와 마스크를 **이 빌드 구성의 실제 레이아웃에서** 찾아 이 프로퍼티를 비트필드로 만듭니다.
         * @details 0 으로 채운 메모리에서 그 비트만 세우는 함수를 부르고, 바뀐 바이트와 비트를 읽는다. 언리얼의 `FBoolProperty` 가 UHT 가 만든 SetBit 함수로 하는 것과 같은 방식이다.
         *          파서가 libclang 으로 측정한 바이트는 쓰면 안 된다. 파서는 Debug 정의(`SW_DEBUG`, `_DEBUG`)를 모른 채 크기를 재기 때문이다.
         *          Debug 에서 커지는 멤버(`sw::string` 의 경쟁 검사 필드나 반복자 디버그) 뒤에 비트필드가 있으면 엉뚱한 바이트를 가리키고,
         *          씬을 읽을 때마다 그 위치의 다른 필드를 덮어쓴다. 비트필드가 아닌 프로퍼티는 `offsetof` 를 쓴다.
         * @param ownerSize 이 프로퍼티를 가진 타입의 크기(`sizeof`)
         * @param pSetBit   그 비트만 세우는 함수
         * @return 바이트 하나의 비트 하나를 찾았으면 true 입니다. 못 찾으면 오류를 남기고, 읽으면 false 이고 쓰면 무시되는 비트필드로 둡니다. 엉뚱한 바이트를 건드리지 않기 위해서입니다.
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

        /** @brief 인스턴스의 프로퍼티 값을 쓰고 옵저버와 바인딩에 알립니다(비트필드 지원). */
        template <typename T, typename ObjectType>
        void setValue( ObjectType* pInstance, const T& newValue ) const
        {
            if ( _bIsBitField == SW_TRUE )
            {
                uint8* pByte = reinterpret_cast<uint8*>( pInstance ) + _offset;
                bool   bVal  = false;
                // 비트필드는 1비트 불리언 플래그뿐이다. 구조체 타입(float3 등)으로 부르면 이 분기에 닿지 않지만 컴파일은 되어야 한다.
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
                    _onPropertyBoundChanged( *this, ObjectAddress{ pInstance } );
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
                _onPropertyBoundChanged( *this, ObjectAddress{ pInstance } );
        }

        /** @brief 프로퍼티 값의 포인터입니다. 비트필드는 nullptr 입니다. 위치는 `getRawPtr` 가 정합니다. */
        template <typename T>
        T* getValuePtr( void* pInstance ) const
        {
            return static_cast<T*>( getRawPtr( pInstance ) );
        }

        /** @brief 프로퍼티 값의 포인터입니다. 비트필드는 nullptr 입니다. 위치는 `getRawPtr` 가 정합니다. */
        template <typename T>
        const T* getValuePtr( const void* pInstance ) const
        {
            return static_cast<const T*>( getRawPtr( pInstance ) );
        }

        /** @brief 값이 객체 밖에 있으면(`_pValueAccessor`) true 입니다. 이런 프로퍼티의 오프셋은 0 이고 의미가 없습니다. */
        bool hasValueAccessor() const noexcept { return _pValueAccessor != nullptr; }

        /**
         * @brief 인스턴스 기준 프로퍼티 값의 원시 메모리 시작 포인터를 반환합니다. **비트필드는 nullptr 입니다.**
         * @details 보통은 `인스턴스 + _offset` 이다. 값이 객체 밖에 있는 프로퍼티는 `_pValueAccessor` 가 찾은 주소다.
         *          `getValue`, `setValue`, `getValuePtr`, 직렬화기는 모두 이 함수를 거쳐 값에 접근한다.
         *          비트필드는 값 주소가 없다. 예전에는 그 비트가 든 바이트 주소를 줘서, 그 주소를 값 주소로 받은 쓰기(스키마 이관의 점 경로,
         *          컴포넌트 기본값 패치)가 bool 을 통째로 써 같은 바이트의 다른 비트필드를 지웠다. 비트필드는 `getValue` · `setValue` 로만 읽고 쓴다.
         */
        [[nodiscard]] void* getRawPtr( void* pInstance ) const noexcept
        {
            if ( _bIsBitField == SW_TRUE )
                return nullptr;
            if ( _pValueAccessor != nullptr )
                return _pValueAccessor( pInstance );
            return reinterpret_cast<utf8*>( pInstance ) + _offset;
        }

        /** @brief 인스턴스 기준 프로퍼티 값의 원시 메모리 const 시작 포인터를 반환합니다. 비트필드는 nullptr 입니다. */
        [[nodiscard]] const void* getRawPtr( const void* pInstance ) const noexcept
        {
            if ( _bIsBitField == SW_TRUE )
                return nullptr;
            if ( _pValueAccessor != nullptr )
                return _pValueAccessor( const_cast<void*>( pInstance ) );
            return reinterpret_cast<const utf8*>( pInstance ) + _offset;
        }

        /** @brief 커스텀 메타데이터 태그를 조회합니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept;

        /**
         * @brief 객체 안 오프셋입니다. 비트필드는 그 비트가 든 바이트 번호입니다. 레이아웃 확인 · 덤프에만 씁니다.
         * @details 값 주소는 `getRawPtr` 로 얻는다. 오프셋을 직접 더하면 접근자(`_pValueAccessor`)를 건너뛰고, 비트필드는 같은 바이트의 다른 플래그를 덮는다.
         */
        [[nodiscard]] size_t getOffset() const noexcept { return _offset; }
        /** @brief 비트필드의 그 비트 마스크입니다. 비트필드가 아니면 `0xFF` 입니다(레이아웃 확인 · 덤프용). */
        [[nodiscard]] uint8 getBitMask() const noexcept { return _bitMask; }

        /**
         * @brief 비트필드의 그 비트만 씁니다. 옵저버와 바인딩에 알리지 않습니다(직렬화 · 값 복사용, 알리려면 `setValue`).
         * @return 비트필드가 아니면 아무것도 쓰지 않고 false 입니다.
         */
        [[nodiscard]] bool writeBitFieldSilently( void* pInstance, const bool bValue ) const noexcept
        {
            if ( _bIsBitField == SW_FALSE || pInstance == nullptr )
                return false;
            uint8& byte = *( static_cast<uint8*>( pInstance ) + _offset );
            byte        = bValue ? static_cast<uint8>( byte | _bitMask ) : static_cast<uint8>( byte & ~_bitMask );
            return true;
        }

    private:
        /**
         * @brief 오프셋과 비트 마스크는 이 형식 안에서만 씁니다(`getRawPtr` · `getValue` · `setValue` · `writeBitFieldSilently`).
         * @details 밖에서 `인스턴스 + _offset` 을 만들면 비트필드는 그 바이트 주소가 되어 같은 바이트의 다른 플래그를 덮고, 접근자 프로퍼티는 엉뚱한 곳을 가리킨다.
         */
        size_t _offset;
        uint8  _bitMask;
    };
} // namespace sw

namespace sw
{
    /// @brief 등록된 enum 의 정보입니다. 이름과 값의 대응, Flags 여부, Invalid 와 Count 센티널을 담습니다.
    struct SW_API EnumInfo
    {
        unordered_map<hashed_string, int64> _mapNameToValue; ///< 지금 열거자 이름만 담습니다. 옛 이름은 `_mapRedirectNameToValue` 에 있습니다
        unordered_map<int64, hashed_string> _mapValueToName;
        /**
         * @brief 옛 열거자 이름에서 지금 값으로 가는 리다이렉트 표입니다. 이름 변경(`ENUM( ValueAlias = "Old:New" )`)과 지운 열거자의 대체
         *        (`ENUM( Removed = "Old:Replacement" )`)를 생성기가 채웁니다.
         * @details 읽기(바이너리 이름 해시, 텍스트 이름)는 지금 이름에서 찾지 못했을 때만 이 표를 봅니다. 쓰기는 이 이름을 쓰지 않습니다.
         *          선언이 없는 옛 이름은 여기 없으므로 읽기가 실패합니다(조용히 기본값으로 읽지 않습니다).
         */
        unordered_map<hashed_string, int64> _mapRedirectNameToValue;
#if !defined( SW_SHIPPING )
        unordered_map<hashed_string, string> _mapCustomMeta;
#endif
        hashed_string _name;
        hashed_string _fullyQualifiedName;
        hashed_string _moduleName;
        /**
         * @brief 값이 곧 인덱스인 이름 목록입니다. 열거자 값이 `0..N-1` 로 빈틈없는 열거형에만 생성기가 채우고, 그 밖에는 비어 있습니다.
         * @details `valueToCString` 이 해시 맵 대신 이 목록을 봅니다. 원소는 `_mapValueToName` 과 같은 intern 된 이름이라 돌려주는 포인터도 같습니다.
         *          `EnumInfo` 가 값으로 가진다. 모듈의 정적 배열을 가리키면 핫 리로드 해제와 겹친 렌더 · 오디오 · 로거 스레드의 `getEnumName` 이
         *          해제된 저장소를 읽는다. intern 된 이름은 모듈보다 오래 살므로 묘비에도 그대로 둔다(`_mapValueToName` 과 같은 규칙).
         */
        vector<hashed_string>  _listDenseName;
        int64                  _invalidValue{ 0 };
        int64                  _countValue{ 0 };
        uint8                  _size{ sizeof( int32 ) };
        uint8                  _bIsBitFlag    : 1;
        uint8                  _bHasInvalid   : 1;
        uint8                  _bHasCount     : 1;
        uint8                  _bIsSigned     : 1; ///< 기반 타입이 부호 있는 정수인지 나타낸다. 좁은 값을 메모리에서 읽을 때의 확장 방식이 이것으로 정해진다
        [[maybe_unused]] uint8 _reservedFlags : 4;

        /** @brief 비어 있는 이름-값 맵으로 만듭니다. */
        EnumInfo() noexcept;

        /** @brief 커스텀 메타데이터를 조회합니다. Shipping 빌드에서는 nullptr 를 반환합니다. */
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
         * @brief 메모리 포인터에서 실제 enum 크기만큼 안전하게 읽어 int64 로 반환합니다.
         * @details 기반 타입의 부호에 따라 확장한다. 이름 맵(`_mapValueToName`)의 값도 같은 규칙으로 만든다(생성 코드의 `static_cast<int64>( 열거자 )`).
         *          둘이 어긋나면 `uint8` 의 0x80 이나 `int8` 의 -1 같은 값이 이름을 잃는다.
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

        /** @brief ENUM(Invalid, Count) 센티널을 반영해 유효한 값인지 판단합니다. 메타가 없으면 true 입니다. */
        bool isValidValue( int64 value ) const noexcept
        {
            if ( _bHasInvalid != SW_FALSE && value == _invalidValue )
                return false;
            if ( _bHasCount != SW_FALSE && value >= _countValue )
                return false;
            return true;
        }

        /** @brief 값을 이름으로 바꿉니다. */
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
            (void)collectFlagNames( val, listName ); // 이름이 덮지 못한 비트는 텍스트에 적지 않는다
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
         * @brief 비트플래그 값에서 켜진 열거자의 이름을 모으고, 이름이 덮지 못한 비트를 반환합니다.
         * @details 0 이 아닌 열거자 중 비트가 모두 켜진 것을 이름으로 모은다(`_mapValueToName` 만 쓰고, ValueAlias 는 겹쳐 적지 않는다).
         *          텍스트(`toStringFlags`)와 바이너리(`SerializerUtil` 의 열거자 식별 인코딩)가 **같은 이름**을 적도록 이 규칙을 한 곳에 둔다.
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
         * @brief 이름 해시로 열거자 값을 찾습니다. 지금 이름에 없으면 리다이렉트 표(`_mapRedirectNameToValue`)를 한 번 봅니다.
         * @details 바이너리는 열거자를 이 해시로 저장한다. 값이 아니라 이름이 열거자의 정체성이기 때문이다.
         *          표식 값(`Invalid`, `Count`)도 받으며, 이 규칙은 `tryParseText` 와 같다. 필드에 들어 있는 값을 적힌 그대로 읽는다.
         *          이름 해시는 대소문자를 구분하지 않는다. `hashed_string` 이 그렇고, 텍스트 읽기도 마찬가지다.
         *          옛 이름(`ValueAlias` · `Removed`)은 실제 게임 데이터가 생긴 뒤 이름을 바꾸거나 지울 때 쓰는 수단이고, 그 전에는 데이터를 새 이름으로 다시 쓴다.
         */
        [[nodiscard]] bool findValueByNameHash( uint32 nameHash, int64& outValue ) const
        {
            if ( findInMapByNameHash( _mapNameToValue, nameHash, outValue ) )
                return true;
            return findInMapByNameHash( _mapRedirectNameToValue, nameHash, outValue );
        }

        /** @brief 값에 해당하는 intern 된 열거자 이름입니다. Invalid 나 Count 센티널이면 nullptr 입니다. */
        const utf8* valueToCString( int64 value ) const
        {
            if ( isValidValue( value ) == false )
                return nullptr;
            if ( _listDenseName.empty() == false )
            {
                const bool bInRange = 0 <= value && value < static_cast<int64>( _listDenseName.size() );
                return bInRange ? _listDenseName[static_cast<size_t>( value )].c_str() : nullptr;
            }
            const hashed_string name = ( _bIsBitFlag != SW_FALSE ) ? toStringFlags( value ) : toString( value );
            return name.empty() ? nullptr : name.c_str();
        }

        /** @brief 이름을 값으로 바꿉니다. 대소문자는 무시하고, 비트플래그는 `A|B` 도 허용합니다. */
        [[nodiscard]] bool tryParse( string_view name, int64& outValue ) const
        {
            if ( name.empty() )
                return false;

            {
                // 찾기만 한다. 열거자 이름은 등록할 때 intern 됐으므로 결과가 같고, 에셋의 모르는 문자열이 전역 이름 테이블에 들어가지 않는다.
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

            int64 namedValue{ 0 };
            if ( findValueByText( name, namedValue ) )
            {
                if ( isValidValue( namedValue ) == false )
                    return false;
                outValue = namedValue;
                return true;
            }

            int64      flagsValue{ 0 };
            const bool bInvalidFlagsText = _bIsBitFlag == SW_FALSE || tryParseFlags( name, flagsValue ) == false || isValidValue( flagsValue ) == false;
            if ( bInvalidFlagsText )
                return false;
            outValue = flagsValue;
            return true;
        }

        /**
         * @brief 에셋이나 설정 텍스트를 값으로 읽습니다. 이름(대소문자 무시), 비트플래그의 `A | B`, 알려진 값의 숫자를 받습니다.
         * @details 모르는 이름이나 모르는 플래그 토큰이 하나라도 있으면 false 다. 아는 토큰만 남기거나 0 으로 읽지 않는다.
         *          에셋과 설정 텍스트를 값으로 읽는 경로는 이 함수 하나다(직렬화기, 머티리얼).
         *
         *          `tryParse` 와 달리 표식 값(`Invalid`, `Count`)도 받는다. 직렬화는 필드에 들어 있는 값을 이름으로 적으므로(`Key::Unknown` 은 "바인딩 없음"이다)
         *          읽기도 그 이름을 받아야 왕복이 맞는다. 이름을 해시로 바꾸지 않는다. 에셋 텍스트를 전역 이름 테이블에 넣지 않기 위해서다.
         */
        [[nodiscard]] bool tryParseText( string_view text, int64& outValue ) const
        {
            const string_view trimmed = StringUtil::trim( text );
            // 비트플래그의 빈 텍스트는 "아무 비트도 없음"이다(손으로 쓴 에셋). 쓰는 쪽은 0 을 `None` 으로 적는다(`toStringFlags`).
            if ( trimmed.empty() && _bIsBitFlag != SW_FALSE )
            {
                outValue = 0;
                return true;
            }
            if ( findValueByText( trimmed, outValue ) )
                return true;
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
        /** @brief 이름 → 값 표 하나에서 이름 해시로 찾습니다. */
        [[nodiscard]] static bool findInMapByNameHash( const unordered_map<hashed_string, int64>& mapNameToValue, uint32 nameHash, int64& outValue )
        {
            for ( const auto& [nameKey, enumValue] : mapNameToValue )
            {
                if ( nameKey.getHash() == nameHash )
                {
                    outValue = enumValue;
                    return true;
                }
            }
            return false;
        }

        /** @brief 이름 → 값 표 하나에서 글(대소문자 무시)로 찾습니다. 글을 intern 하지 않습니다. */
        [[nodiscard]] static bool findInMapByText( const unordered_map<hashed_string, int64>& mapNameToValue, string_view text, int64& outValue )
        {
            for ( const auto& [nameKey, enumValue] : mapNameToValue )
            {
                if ( StringUtil::equals( text, nameKey.view(), true ) )
                {
                    outValue = enumValue;
                    return true;
                }
            }
            return false;
        }

        /**
         * @brief 열거자 이름 글(대소문자 무시)로 값을 찾습니다. 지금 이름에 없을 때만 리다이렉트 표를 봅니다.
         * @details 텍스트 읽기(`tryParse` · `tryParseText` · `tryParseFlags`)가 모두 이 함수를 써서 바이너리(`findValueByNameHash`)와 같은 옛 이름을 받습니다.
         */
        [[nodiscard]] bool findValueByText( string_view text, int64& outValue ) const
        {
            if ( findInMapByText( _mapNameToValue, text, outValue ) )
                return true;
            return findInMapByText( _mapRedirectNameToValue, text, outValue );
        }

        /** @brief `A | B` 를 읽습니다. 토큰마다 알려진 이름(대소문자 무시)이어야 하며, 하나라도 모르면 false 입니다. 표식 값 검사는 부르는 쪽이 합니다. */
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
                    int64      tokenValue{ 0 };
                    const bool bKnown = findValueByText( token, tokenValue );
                    if ( bKnown )
                        result |= tokenValue;
                    // `None` 은 0 열거자가 없는 플래그에 `toStringFlags` 가 0 을 적는 이름이다 . 읽을 때도 0 이어야 왕복이 맞는다.
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
    /** @brief 함수나 이벤트의 인자 하나입니다. 인자 순서는 선언 순서입니다. */
    struct SW_API FunctionParameterInfo
    {
        string _name;     ///< 선언에 적힌 이름. 이름 없이 적었으면 비어 있다
        string _typeName; ///< 정규 타입 이름이다(`int32`, `string`, `DamageEvent`). const 와 참조는 뗀다
        /** @brief 기본 인자의 C++ 식 그대로입니다(`1.0f`, `"idle"`, `Mode::Fast`). 없으면 비어 있습니다. 호출에서 인자를 생략하면 `ReflectionInvoke` 가 이 값을 읽어 넣습니다. */
        string _defaultValue;
        /** @brief 이 인자 타입의 이름과 값 변환(`ReflectTypeOpsOf<T>::kOps`)입니다. 이벤트는 `EventInfo::setOps` 가 채웁니다. */
        const ReflectTypeOps* _pType;

        FunctionParameterInfo() noexcept;
        FunctionParameterInfo( string name, string typeName, string defaultValue, const ReflectTypeOps* pType );

        /** @brief 기본 인자가 있으면 true 입니다. */
        bool hasDefaultValue() const noexcept { return _defaultValue.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /// @brief 리플렉션 메서드의 정보입니다. 이름, 시그니처, invoker 를 담습니다.
    struct SW_API FunctionInfo
    {
        string                               _name;
        hashed_string                        _hashName;
        string                               _returnTypeName; ///< 정규 타입 이름(예: void, int32)
        vector<FunctionParameterInfo>        _listParameter;  ///< 선언 순서대로 나열한 인자다. 이름, 타입, 기본 인자를 담는다
        ReflectMetadataBox<FunctionMetadata> _metadata;       ///< `FUNCTION()` 에 적은 메타데이터(`ReflectionMetadata.h`)
        /**
         * @brief 인자 묶음을 받아 메서드를 부르는 함수입니다.
         * @details 인자는 **그 C++ 타입 그대로**(`args.get<T>`) 넣어야 한다. 타입이 다르면 Debug 는 단언하고 Release 는 잘못 읽는다.
         *          타입을 모르는 쪽(콘솔, 비주얼 스크립팅)은 `ReflectionInvoke::call` 로 부른다. 이 함수는 인자를 바꿔 넣어 준다.
         */
        Delegate<TaskValue( void*, const TaskArgs& )> _invoker;
        /** @brief 반환 타입의 이름과 값 변환입니다. void 이거나 생성자면 nullptr 입니다. */
        const ReflectTypeOps* _pReturnType;

        /** @brief 빈 메서드 정보를 만듭니다(메타데이터 상자의 기본 생성이 `ReflectionMetadata.h` 를 봐야 해서 `ReflectionCore.cpp` 에 둔다). */
        FunctionInfo() noexcept;

        /** @brief 인자 개수입니다. */
        uint32 getParameterCount() const noexcept { return static_cast<uint32>( _listParameter.size() ); }

        /** @brief 커스텀 메타데이터 태그를 조회합니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 리플렉션 이벤트입니다. `PROPERTY()` 를 붙인 멀티캐스트 델리게이트 필드(`MulticastDelegate<void( Args... )>`)가 이벤트가 됩니다.
     * @details 이벤트는 값이 아니라 구독 목록이라서 직렬화와 인스펙터의 값 편집 대상이 아니다. 이름으로 찾은 뒤(`TypeInfo::findEventInHierarchy`)
     *          타입을 모른 채로 바인딩, 해제, 호출한다(`ReflectionInvoke::bindEvent`, `broadcastEvent`). 에디터, 비주얼 스크립팅, 기믹 배선이 이 방식을 쓴다.
     */
    struct SW_API EventInfo
    {
        hashed_string                        _name;
        vector<FunctionParameterInfo>        _listParameter; ///< 이름은 선언에서 가져오고, 변환 테이블은 `setOps` 가 템플릿 인자에서 채운다
        ReflectMetadataBox<PropertyMetadata> _metadata;      ///< 표시용 메타(Category, DisplayName, Tooltip, Meta, HideInInspector)만 쓴다
        /** @brief 바인딩, 해제, 호출, 인자 변환을 담은 테이블입니다(`ReflectEventOpsOf<필드 타입>::kOps`). */
        const ReflectEventOps* _pOps;
        size_t                 _offset; ///< 인스턴스 안에서 델리게이트 필드가 있는 오프셋이다

        EventInfo() noexcept;

        /** @brief 테이블을 등록하고, 인자 변환 테이블을 템플릿 인자에서 채웁니다. 선언에서 이름을 읽지 못한 인자도 슬롯은 채웁니다. */
        void setOps( const ReflectEventOps* pOps );

        /** @brief 인스턴스 안 델리게이트 필드의 주소입니다. */
        void* getEventPtr( void* pInstance ) const noexcept { return reinterpret_cast<utf8*>( pInstance ) + _offset; }
        /** @brief 인스턴스 안 델리게이트 필드의 주소입니다. */
        const void* getEventPtr( const void* pInstance ) const noexcept { return reinterpret_cast<const utf8*>( pInstance ) + _offset; }
        /** @brief 인자 개수입니다. */
        uint32 getParameterCount() const noexcept { return static_cast<uint32>( _listParameter.size() ); }
    };
} // namespace sw

namespace sw
{
    /// @brief 등록된 타입의 정보입니다. FQN, 프로퍼티, 메서드, 생성 가능 여부를 담습니다.
    struct SW_API TypeInfo
    {
        size_t _size;
        /** @brief `$ctor` 로 placement new 한 인스턴스를 소멸시킵니다. 없으면 nullptr 입니다. */
        void ( *_destroyInstance )( void* ) = nullptr;
        /**
         * @brief 이 타입의 컴포넌트를 @p pOwner 에 붙여 만드는 함수입니다(`GameObject::addComponentTo<T>`). 만들 수 있는 컴포넌트 타입이 아니면 nullptr 입니다.
         * @details 언리얼의 `UClass` 가 리플렉션 정보와 생성 함수(`ClassConstructor`)를 함께 가지는 것과 같다. 이름으로 컴포넌트를 만드는 경로
         *          (`GameObjectManager::addComponentByName` 이고, 씬 로드, 프리팹 로드, 에디터가 쓴다)는 이 함수 하나를 본다.
         *          코드젠이 추상이 아닌 컴포넌트 타입마다 채운다. 모듈 코드를 가리키므로 모듈을 해제할 때(`clearContent`) 비우고,
         *          같은 FQN 을 다시 등록하면 새 이미지의 주소로 덮어쓴다.
         */
        Component* ( *_addComponent )( GameObject* pOwner );
        /** @brief `REFLECT( Validate = fn )` 의 타입 검증 함수입니다. 없으면 nullptr 입니다. 모듈 코드를 가리키므로 `clearContent` 가 비웁니다. */
        ReflectValidateFunction _pValidate;
        /**
         * @brief 고정 배치형(B) 코덱 테이블입니다(`Engine/Serialization/Fixed/FixedLayout.h`). 코드젠이 모든 `REFLECT` 타입에 채우고, 적격이 아니면
         *        이유만 든 테이블입니다. 코드젠을 거치지 않은 타입(내장 스칼라)은 nullptr 입니다. 모듈 코드를 가리키므로 `clearContent` 가 비웁니다.
         */
        const FixedCodec*                                         _pFixedCodec;
        hashed_string                                             _name;
        hashed_string                                             _fullyQualifiedName;
        hashed_string                                             _parentFQN;
        hashed_string                                             _moduleName;
        vector<PropertyInfo>                                      _listProperty;
        vector<FunctionInfo>                                      _listMethod;
        vector<EventInfo>                                         _listEvent; ///< 이 타입이 선언한 이벤트다. 기반 타입의 이벤트는 `findEventInHierarchy` 가 따라 올라가며 찾는다
        ReflectMetadataBox<TypeMetadata>                          _metadata;  ///< `REFLECT()` 에 적은 메타데이터(`ReflectionMetadata.h`)
        mutable vector<PropertyInfo>                              _listPropertyWithBase;
        mutable unordered_map<hashed_string, const PropertyInfo*> _mapNameToPropertyWithBase; ///< 계층 병합 목록에서 이름과 별칭으로 항목을 찾는 맵이다. 목록과 함께 만든다
        mutable unordered_map<hashed_string, const PropertyInfo*> _mapNameToProperty;
        mutable unordered_map<hashed_string, const FunctionInfo*> _mapNameToMethod;
        /**
         * @brief `_parentFQN` 을 한 번 찾아 둔 부모 `TypeInfo` 입니다. 없거나 아직 찾지 못했으면 nullptr 입니다.
         * @details `isDerivedFrom` 이 조상마다 `findType(_parentFQN)`(shared_mutex 잠금과 해시맵 조회)을 부르지 않도록,
         *          등록 배치가 끝날 때(`TypeRegistry::populateLookupCaches`) 한 번 찾아 둔다. 그러면 조상을 따라가는 비용은 포인터 역참조 몇 번이다.
         *
         *          **타입을 해제하면 이 값을 비운다.** `TypeInfo` 의 주소는 고정이라 옮겨지지는 않지만,
         *          모듈 해제 뒤에는 묘비가 된 부모를 가리킬 수 있다. 그래서 해제할 때 남은 타입 전부의 이 값을 비우고,
         *          비어 있으면 `getParentType()` 이 이름으로 다시 찾는다. 원자값으로 둔 이유는, 배치 밖에서 등록한 타입(테스트)은
         *          첫 조회가 여러 스레드에서 동시에 올 수 있기 때문이다. 같은 값을 쓰는 경쟁이라 relaxed 로 충분하다.
         */
        mutable atomic<const TypeInfo*> _pParentType;
        /**
         * @brief `_parentFQN` 을 이름으로 찾아 봤지만 찾지 못했던 때의 타입 테이블 세대입니다. 0 이면 아직 찾아 보지 않았습니다.
         * @details 등록되지 않은 기반 타입은 이름만 적혀 있다. 부모를 물을 때마다(계층 프로퍼티 조회, 이름 따라가기, 기본값 체인 수집)
         *          레지스트리를 잠그고 다시 찾지 않도록, 세대가 같으면 결과도 같으므로 세대를 적어 두고 등록이나 해제로 세대가 바뀔 때만 다시 찾는다.
         */
        mutable atomic<uint32> _parentMissGeneration;
        uint32                 _typeID;
        /**
         * @brief 루트부터 자기까지의 **이름(FQN 의 intern 인덱스)** 을 깊이 순서로 적은 조상 테이블입니다. 자기 위치는 `_ancestorDepth` 가 가리킵니다.
         * @details 캐스트의 핫패스는 이 테이블만 본다. `테이블[pTarget 의 깊이] == pTarget 의 이름` 이면 파생 관계다.
         *          포인터가 아니라 이름을 비교하므로, 레지스트리 밖 사본(테스트 목이 손으로 만든 `StaticType()`)도 이름이 같으면 같은 타입으로 본다.
         *          이 규칙은 부모를 따라가는 `isSameTypeName` 과 같다. `_typeID` 를 쓰지 않는 이유도 그 사본이다. 사본은 자기 id 를 따로 가진다.
         *          등록이나 해제로 부모 체인이 바뀔 수 있으면 `TypeRegistry` 가 깊이를 `kAncestorDepthUnknown` 으로 비우고,
         *          등록 배치가 끝날 때(`populateLookupCaches`)나 첫 상속 검사가 테이블을 다시 만든다.
         *          원자값으로 둔 이유는 `_pParentType` 과 같다. 첫 조회는 여러 스레드에서 올 수 있고, 모두 같은 값을 쓴다.
         *          등록과 캐스트가 동시에 실행되는 경우는 `_pParentType` 과 마찬가지로 가정하지 않는다(모듈 로드는 단일 스레드다).
         */
        mutable atomic<uint32> _arrAncestorNameIndex[constant::reflection::kAncestorDisplayDepth];
        /** @brief 조상 테이블에서 자기 위치의 깊이입니다. Unknown 이면 아직 만들지 않은 것이고, None 이면 만들 수 없어 부모 포인터를 따라갑니다. */
        mutable atomic<uint8> _ancestorDepth;
        /**
         * @brief 레지스트리에 살아 있는 타입인지 나타냅니다. 모듈을 해제하면 항목을 지우지 않고 이 값을 내립니다(묘비).
         * @details `TypeInfo` 의 주소는 고정이다. 레지스트리가 `unique_ptr` 로 보관하고, 테이블이 커져도 옮기지 않으며, 해제해도
         *          지우지 않는다. 그래서 `_pParentType` 은 세대 검사 없이 포인터를 그대로 쓰고, 이 플래그 하나로 해제 여부를 확인한다.
         *          레지스트리보다 오래 사는 정적 캐시(`TypeLookupCache`)는 레지스트리 파괴도 봐야 하므로 세대를 담은 칸에 둔다.
         *          같은 FQN 을 다시 등록하면 같은 객체에 덮어써서 복원한다. 레지스트리 밖에서 만든 사본은 항상 살아 있다.
         */
        mutable atomic<uint8> _bAlive;
        /** @brief `REFLECT( Abstract )` 이거나 C++ 추상 클래스입니다. 생성할 수 없습니다(`UCLASS( Abstract )` 에 해당). */
        uint8 _bAbstract : 1;
        /** @brief `REFLECT( Static )` 타입(함수 라이브러리)입니다. `FunctionMetadata::_bStatic` 과는 다릅니다. */
        uint8 _bStatic : 1;
        /** @brief 내장 스칼라나 문자열 타입입니다(int32, bool, sw::string 등). REFLECT 코드젠이 없습니다. */
        uint8         _bPrimitive                 : 1;
        mutable uint8 _bIsCacheBuilt              : 1;
        mutable uint8 _bIsPODFastPath             : 1;
        mutable uint8 _bIsPODCalculated           : 1;
        mutable uint8 _bListPropertyWithBaseBuilt : 1;
        /**
         * @brief `getPropertiesWithBase()` 가 지금 이 타입에서 재귀 중인지 나타냅니다.
         * @details 부모 체인이 순환하면(`_parentFQN` 이 자기 자신이나 자손을 가리키면) 재귀가 끝나지 않아 스택이 넘친다.
         *          `_parentFQN` 은 코드젠이 적는 값이지만 `registerClass` 는 공개 API 이고 이 값을 검사하지 않는다.
         *          모듈을 따로 등록하는 핫 리로드에서는 A→B→A 순환이 만들어질 수 있다.
         */
        mutable uint8 _bBuildingPropertyWithBase : 1;
        /** @brief `hasSaveGameProperty` 의 결과와 계산 여부입니다. 상속 목록 캐시와 함께 비웁니다. */
        mutable uint8 _bHasSaveGameProperty : 1;
        mutable uint8 _bSaveGameCalculated  : 1;
        /** @brief `ReflectionValidation::hasValidator` 의 결과와 계산 여부입니다. 상속 목록 캐시와 함께 비웁니다. */
        mutable uint8          _bHasValidator        : 1;
        mutable uint8          _bValidatorCalculated : 1;
        [[maybe_unused]] uint8 _reservedCacheFlags   : 4;
        [[maybe_unused]] uint8 _reservedPadding[2];

        /** @brief 비어 있는 TypeInfo 를 만듭니다. */
        TypeInfo() noexcept;
        ~TypeInfo() = default;
        TypeInfo( const TypeInfo& other );
        TypeInfo( TypeInfo&& other ) noexcept;
        TypeInfo& operator=( const TypeInfo& other );
        TypeInfo& operator=( TypeInfo&& other ) noexcept;

        /** @brief 커스텀 메타데이터 태그를 조회합니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept;

        /** @brief 카테고리를 반환합니다. */
        string_view getCategory() const noexcept;
        /** @brief 표시 이름을 반환합니다. DisplayName 메타가 없으면 C++ 타입 이름을 반환합니다. */
        const utf8* getDisplayName() const noexcept;
        /** @brief 툴팁 문자열을 반환합니다. */
        string_view getTooltip() const noexcept;
        /** @brief "Add Component" 메뉴에서 숨겨야 하는지 여부를 반환합니다. */
        bool isHiddenInMenu() const noexcept;

        /** @brief 팩토리나 `$ctor` 로 인스턴스를 만들 수 있으면 true 입니다. */
        bool canConstruct() const noexcept { return _bAbstract == SW_FALSE && _bStatic == SW_FALSE && _bPrimitive == SW_FALSE; }

        /** @brief REFLECT 없이 등록된 내장 타입(int32, float32, …)이면 true 입니다. */
        bool isPrimitive() const noexcept { return _bPrimitive != SW_FALSE; }

        /** @brief 직렬화나 복사에서 memcpy POD 경로를 쓸 수 있으면 true 입니다. */
        bool usesPodCopyFastPath() const;
        /**
         * @brief 상속한 기반까지 포함해 `SaveGame` 프로퍼티가 하나라도 있으면 true 입니다. 세이브 직렬화가 그 프로퍼티만 쓰는 타입(옵트인)인지 나타냅니다.
         * @details 상속 목록 캐시와 함께 비우고 다시 구한다(`clearInheritedProperties`).
         */
        bool hasSaveGameProperty() const;
        /** @brief 이 타입이 targetFqn 이거나 그 파생이면 true 입니다. 부모가 등록되지 않았어도 `_parentFQN` 이 같으면 true 입니다. */
        bool isDerivedFrom( const hashed_string& targetFqn ) const;
        /**
         * @brief 이 타입이 pTarget 이거나 그 파생이면 true 입니다. 인라인 함수이며, 조상 테이블이 있으면 로드 두 번과 비교 한 번으로 끝납니다.
         * @details 캐스트의 핫패스라 DLL 경계를 넘지 않는다. 양쪽 모두 테이블이 있으면 `테이블[pTarget 의 깊이] == pTarget 의 이름` 으로 끝난다.
         *          테이블이 아직 없거나 만들 수 없는 쪽은 `isDerivedFromSlow` 가 테이블을 만들거나 부모 포인터를 따라간다.
         *          레지스트리 밖 사본(테스트 목의 `StaticType()`)도 이름이 같으면 같은 타입으로 본다.
         *          pTarget 이 nullptr 이면 false 다.
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
        /** @brief 조상 테이블이 없을 때의 경로입니다. 만들 수 있으면 만들어서 답하고, 아니면 부모 포인터를 따라갑니다. */
        bool isDerivedFromSlow( const TypeInfo* pTarget ) const;
        /** @brief 부모 TypeInfo 입니다. 찾아 둔 것이 없으면 이름으로 찾아 기록합니다. 부모가 없거나 등록되지 않았으면 nullptr 입니다. */
        const TypeInfo* getParentType() const;
        /** @brief 부모 포인터를 이름으로 다시 찾습니다. 등록 배치가 끝날 때 `TypeRegistry` 가 부릅니다. */
        void resolveParentType() const;
        /** @brief 부모 포인터와 "찾지 못했다"는 기록을 비웁니다. 해제 뒤에는 부모가 사라졌을 수 있으므로 `TypeRegistry` 가 부릅니다. */
        void clearParentType() const
        {
            _pParentType.store( nullptr, std::memory_order_relaxed );
            _parentMissGeneration.store( 0, std::memory_order_relaxed );
        }

        /** @brief 조상 테이블이 만들어져 있으면 true 입니다. 이때 상속 검사는 부모를 따라가지 않고 O(1) 로 답합니다. */
        bool hasAncestorDisplay() const
        {
            return _ancestorDepth.load( std::memory_order_acquire ) < constant::reflection::kAncestorDisplayDepth;
        }
        /**
         * @brief 부모 포인터를 따라 조상 테이블을 만듭니다. 체인을 모두 찾았고 테이블 깊이 안에 들어오면 true 입니다.
         * @details 이름이 없거나, 순환이거나, 테이블보다 깊으면 `kAncestorDepthNone` 을 적고 false 를 반환한다. 그 타입은 다음
         *          등록이나 해제까지 부모 포인터를 따라가서 답한다. 찾을 수 없는 부모(REFLECT 가 아닌 기반이나 아직 로드되지 않은
         *          모듈)는 체인의 끝으로 본다. 그 부모가 등록되는 순간 `registerClass` 가 테이블을 비운다.
         *          등록 배치가 끝날 때 `TypeRegistry` 가 부르고, 배치 밖의 타입은 첫 상속 검사가 부른다.
         */
        bool computeAncestorDisplay() const;
        /** @brief 조상 테이블을 비웁니다. 체인이 바뀔 수 있는 등록과 해제 뒤에 `TypeRegistry` 가 부릅니다. */
        void clearAncestorDisplay() const
        {
            _ancestorDepth.store( constant::reflection::kAncestorDepthUnknown, std::memory_order_relaxed );
        }
        /**
         * @brief 부모에게서 **복사해 온** 캐시(상속 포함 목록, 그 이름 맵, POD 판정)를 비웁니다. 다음 조회는 현재 부모로 다시 만듭니다.
         * @details 등록이나 해제 뒤에 `TypeRegistry` 가 모든 타입에 대해 부른다. 이 함수가 없으면 기반 타입을 다시 등록하거나 해제해도
         *          **다른 모듈의 파생 타입**이 이전 기반의 프로퍼티(이전 오프셋, 해제된 모듈 코드를 가리키는 접근자)를 계속 노출한다.
         */
        void clearInheritedProperties() const;
        /**
         * @brief 선언에서 **파생된** 캐시(이름 맵, 상속 포함 목록, 부모 포인터, 조상 테이블, POD 판정)를 모두 비웁니다.
         * @details 복사 대입과 이동 대입이 함께 쓴다. 캐시를 추가하면 여기서도 비워야 한다. 빠뜨리면 대입된 타입이 **이전 타입의 캐시**로 답한다
         *          (이름으로 조회하면 다른 오프셋을 준다).
         */
        void invalidateDerivedCaches();
        /** @brief 레지스트리에 살아 있으면 true 입니다. 모듈을 해제하면 내려가고 다시 등록하면 올라갑니다. 사본은 항상 true 입니다. */
        bool isAlive() const { return _bAlive.load( std::memory_order_acquire ) != SW_FALSE; }
        /**
         * @brief 묘비로 남길 때 모듈이 가지고 있던 내용을 비웁니다. 이름, id, 모듈만 남습니다.
         * @details 프로퍼티와 메서드 목록은 그 모듈의 코드(델리게이트, `$ctor`, 소멸 함수)를 가리킨다. 모듈이 언로드된 뒤에
         *          이 목록을 파괴하면(재등록 대입이나 레지스트리 소멸) 사라진 코드를 부른다. 그래서 해제 시점에,
         *          **모듈이 아직 살아 있을 때** 여기서 비운다.
         */
        void clearContent();

        /**
         * @brief 자신과 상속한 기반의 프로퍼티입니다(단일 부모 체인).
         * @details 자식의 이름이 부모를 덮어쓴다. 단일 상속에서만 안전하다.
         */
        const vector<PropertyInfo>& getPropertiesWithBase() const;

        /**
         * @brief 이름으로 프로퍼티와 메서드를 찾는 조회 캐시를 만듭니다.
         * @warning **여러 스레드가 동시에 부르면 안 됩니다.** `mutable` 맵 두 개를 잠금 없이 채웁니다.
         *          두 스레드가 같은 `TypeInfo` 를 처음 조회하면 같은 맵에 동시에 삽입합니다.
         *          그래서 `TypeRegistry::populateLookupCaches()` 가 **등록 배치가 끝난 직후 단일
         *          스레드에서** 한 번 만듭니다. 등록된 타입을 병렬로 조회하는 것은 그 뒤이므로 안전합니다.
         *          등록하지 않은 임시 사본은 만든 스레드만 씁니다.
         */
        void populateLookupCache() const
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

        /** @brief 조회 캐시가 이미 만들어져 있으면 true 입니다(`populateLookupCaches` 뒤에는 true 입니다). */
        bool isLookupCacheBuilt() const { return _bIsCacheBuilt != SW_FALSE; }

        /** @brief 이름이나 별칭으로 프로퍼티를 찾습니다. */
        const PropertyInfo* findProperty( const hashed_string& propertyNameOrAlias ) const
        {
            if ( _listProperty.size() <= constant::reflection::kLinearSearchThreshold )
            {
                return VectorUtil::findIf( _listProperty, [&]( const PropertyInfo& propertyInfo )
                { return propertyInfo.matchesName( propertyNameOrAlias ); } );
            }

            populateLookupCache();
            auto it = _mapNameToProperty.find( propertyNameOrAlias );
            return it != _mapNameToProperty.end() ? it->second : nullptr;
        }

        /** @brief 이름으로 메서드를 찾습니다. */
        const FunctionInfo* findMethod( const hashed_string& methodName ) const
        {
            if ( _listMethod.size() <= constant::reflection::kLinearSearchThreshold )
                return VectorUtil::findBy<&FunctionInfo::_hashName>( _listMethod, methodName );

            populateLookupCache();
            auto it = _mapNameToMethod.find( methodName );
            return it != _mapNameToMethod.end() ? it->second : nullptr;
        }

        /**
         * @brief 현재 클래스와 부모 상속 체인에서 프로퍼티를 찾습니다. 계층 병합 목록과 함께 만든 맵 하나로 찾습니다.
         * @details `getPropertiesWithBase()` 가 만들어 둔 맵을 한 번 조회한다. 파생 타입이 기반과 같은 이름을 다시 적으면 파생 타입이 이긴다(병합 규칙과 같다).
         */
        const PropertyInfo* findPropertyInHierarchy( const hashed_string& propertyNameOrAlias ) const;

        /**
         * @brief 프로퍼티를 순회합니다. bIncludeBase 가 true 이면 상속 체인을 포함합니다.
         * @details **직렬화는 true 를 써야 합니다.** 기본값 false 로 부르면 **상속된 PROPERTY 를 저장도 로드도 하지
         *          않는다.** 컴포넌트에서는 그것이 곧 `SceneComponent` 의 트랜스폼이라, 씬, 프리팹, Undo 스냅샷에서
         *          메시, 스프라이트, 카메라의 위치가 사라진다.
         *          `getPropertiesWithBase()` 는 기반 타입 먼저, 파생 타입이 같은 이름을 **덮어쓰는** 순서로
         *          평탄화하고 캐시하므로 중복으로 방출하지 않는다.
         * @note 상속 체인을 **부르는 쪽이 직접 순회하는** 코드(`ComponentDefaults` 가 레벨마다 자기 XML
         *       노드를 찾아 적용한다)는 false 가 맞다. true 를 주면 같은 값을 여러 노드에서
         *       중복 적용한다.
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
            return VectorUtil::findBy<&EventInfo::_name>( _listEvent, eventName );
        }

        /** @brief 자기부터 기반으로 올라가며 이벤트를 이름으로 찾습니다. 파생 타입이 우선합니다. */
        const EventInfo* findEventInHierarchy( const hashed_string& eventName ) const;
        /** @brief 자기부터 기반으로 올라가며 메서드를 이름으로 찾습니다. 파생 타입이 우선합니다. */
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

        /** @brief 자기부터 기반까지의 체인을 @p outArrType 에 담고 길이를 반환합니다. 찾을 수 없는 부모에서 끝나고, 순환이면 상한에서 멈춥니다. */
        uint32 collectTypeChain( const TypeInfo* ( &outArrType )[constant::reflection::kMaxParentChainDepth] ) const;
    };

} // namespace sw
