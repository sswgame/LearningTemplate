/**
 * @file PropertyInfo.h
 * @brief 프로퍼티 하나의 리플렉션 정보입니다.
 */
#pragma once
#include "Engine/Reflection/ReflectionMetadataBox.h"

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
         *          파서가 값 하나와 시퀀스 원소만 허용하므로 필드당 하나면 충분합니다.
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

        /**
         * @brief 글 @p text 가 지금 이름이나 옛 이름(`PROPERTY( Alias = … )`)과 **철자 그대로** 같으면 true 입니다.
         * @details XML · JSON 의 태그 · 속성 이름을 프로퍼티에 맞출 때 쓴다. 이름 해시(`matchesNameHash`)는 `hashed_string` 규칙이라 대소문자를 무시해
         *          `_Width` 같은 철자가 조용히 같은 칸이 된다(별칭). 해시는 엔진이 쓴 바이너리 태그에만 쓴다.
         */
        bool matchesNameText( string_view text ) const
        {
            if ( _name.view() == text )
                return true;
            return VectorUtil::containsIf( _listAlias, [&]( const hashed_string& alias )
            { return alias.empty() == false && alias.view() == text; } );
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
        [[nodiscard]] bool resolveBitField( size_t ownerSize, SetBitFunction pSetBit );

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
