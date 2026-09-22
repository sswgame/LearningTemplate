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

    /// @brief Function Net Role — 목록은 PredefinedFunctionNetRole.xxx
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

    /// @brief PROPERTY() 저작 메타 (카테고리, 기본값, 범위, XML 속성)
    struct SW_API PropertyMetadata
    {
#if !defined( SW_SHIPPING )
        string                               _category;
        string                               _displayName;
        string                               _tooltip;
        unordered_map<hashed_string, string> _mapCustomMeta;
#endif
        /**
         * @brief 에셋이 이 프로퍼티를 생략할 때 쓰는 저작 기본값 (PROPERTY(Default="...")).
         * @details Xml/Json/Binary deserialize가 적용. C++ 멤버 초기화자와는 별개이므로 맞춰 두세요.
         */
        string _defaultValue;
        /** @brief 소프트 에셋 힌트 (PROPERTY(AssetPath) / AssetType="Texture"). */
        string  _assetType;
        float32 _minRange;
        float32 _maxRange;
        uint8   _bHasRange : 1;
        uint8   _bReadOnly : 1;
        /** @brief 부모 엘리먼트의 XML attribute로 직렬화 (PROPERTY(XmlAttribute)). */
        uint8 _bXmlAttribute : 1;
        uint8 _bAssetPath    : 1;
        /** @brief 값이 ReflectAny (또는 type+blob 다형 페이로드). */
        uint8 _bPolymorphic : 1;
        /** @brief 직렬화(Json/Xml/Binary/Diff) 저장/로드 대상에서 제외 (Transient / NonSerialized). */
        uint8 _bTransient : 1;
        /**
         * @brief 값이 비어 있으면 **쓸 때 생략**한다 (PROPERTY(SkipIfEmpty)).
         * @details 리플렉션 직렬화는 기본적으로 모든 PROPERTY 를 쓴다. 그래야 "파일에 없음" 과
         *          "명시적으로 비어 있음" 이 구분되기 때문이다. 다만 선택적 필드(안 쓰는 셰이더
         *          스테이지 진입점 같은 것)까지 전부 쓰면 파일이 읽기 어려워진다. 이 플래그는
         *          그 판단을 **스키마가 명시**하게 한다 — 생략해도 좋다고 선언한 필드만 생략되므로
         *          모호함이 생기지 않는다.
         * @note 읽기에는 영향이 없다. 없으면 멤버 초기값이 그대로 남는다.
         */
        uint8 _bSkipIfEmpty : 1;
#if !defined( SW_SHIPPING )
        /** @brief 에디터 인스펙터 패널 UI에서 숨김 (HideInInspector). */
        uint8 _bHideInInspector : 1;
#else
        [[maybe_unused]] uint8 _reservedFlags : 1;
#endif

        /** @brief 범위/플래그 끈 기본값. */
        PropertyMetadata() noexcept;

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

    /// @brief FUNCTION() 저작 메타 (카테고리, NetRole, static/const)
    struct SW_API FunctionMetadata
    {
#if !defined( SW_SHIPPING )
        string                               _category;
        string                               _displayName;
        string                               _tooltip;
        unordered_map<hashed_string, string> _mapCustomMeta;
#endif
        FunctionNetRole _netRole;
        uint8           _bReliable    : 1;
        uint8           _bValidate    : 1;
        uint8           _bConstructor : 1; ///< REFLECT 타입 ctor invoker (objPtr에 placement-new)
        uint8           _bStatic      : 1; ///< C++ static member / FUNCTION on static
        uint8           _bConst       : 1; ///< const member function
#if !defined( SW_SHIPPING )
        uint8                  _bCallInEditor : 1; ///< 에디터 인스펙터 패널에서 원클릭 실행 버튼 노출
        [[maybe_unused]] uint8 _reserved      : 2;
#else
        [[maybe_unused]] uint8 _reserved : 3;
#endif

        /** @brief 플래그 끈 기본값. */
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
        shared_ptr<NestedContainerInfo> _elementNested; ///< element/value is itself a container
    };

    /// @brief 리플렉션 프로퍼티: 오프셋, 타입, 별칭, 컨테이너 래퍼
    struct SW_API PropertyInfo
    {
        using PropertyBindingDelegate = Delegate<void( const PropertyInfo& prop, const void* pInstance )>;

        shared_ptr<IContainerWrapper>   _containerWrapper;
        shared_ptr<NestedContainerInfo> _nestedContainer; ///< full nesting chain when container
        mutable PropertyBindingDelegate _onPropertyBoundChanged;

        size_t _offset;

        hashed_string         _name;
        hashed_string         _typeName;
        hashed_string         _elementTypeName;
        hashed_string         _keyTypeName;
        vector<hashed_string> _listAlias; ///< PROPERTY(Alias=…) 복수 옛 키
        PropertyMetadata      _metadata;

        mutable uint32 _cachedNameHash;

        uint32                 _bitOffset;
        ContainerKind          _containerKind;
        uint8                  _bitMask;
        uint8                  _bIsContainer  : 1;
        uint8                  _bIsBitField   : 1;
        [[maybe_unused]] uint8 _reservedFlags : 6;

        /** @brief 오프셋 0, 컨테이너 아님. */
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

        /** @brief 이름 또는 임의의 Alias 해시와 일치하면 true. */
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

        /** @brief 이름 또는 Alias와 일치하면 true. */
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

        /** @brief 단일 컨테이너든 중첩 컨테이너든 동일하게 처리하기 위한 shape 정보를 반환합니다. */
        NestedContainerInfo getContainerShape() const;

        /** @brief 값 변경 콜백을 바인딩합니다. */
        void bindOnChanged( PropertyBindingDelegate delegate ) const { _onPropertyBoundChanged = std::move( delegate ); }

        /** @brief 인스턴스 프로퍼티 값을 읽습니다. (비트필드 지원) */
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

            const T* pPtr = reinterpret_cast<const T*>( reinterpret_cast<const utf8*>( pInstance ) + _offset );
            return *pPtr;
        }

        /** @brief 인스턴스 프로퍼티 값을 쓰고 옵저버/바인딩을 알립니다. (비트필드 지원) */
        template <typename T, typename ObjectType>
        void setValue( ObjectType* pInstance, const T& newValue ) const
        {
            if ( _bIsBitField == SW_TRUE )
            {
                uint8* pByte = reinterpret_cast<uint8*>( pInstance ) + _offset;
                bool   bVal  = false;
                if constexpr ( std::is_same_v<T, bool> )
                    bVal = newValue;
                else if constexpr ( std::is_same_v<T, float32> )
                    bVal = ( MathUtil::nearEqual( newValue, 0.0f ) == false );
                else if constexpr ( std::is_same_v<T, float64> )
                    bVal = ( MathUtil::nearEqual( newValue, 0.0 ) == false );
                else
                    bVal = ( newValue != static_cast<T>( 0 ) );

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

            T* pPtr = reinterpret_cast<T*>( reinterpret_cast<utf8*>( pInstance ) + _offset );
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

        /** @brief 인스턴스 + 오프셋의 값 포인터. (비트필드는 nullptr 반환) */
        template <typename T>
        T* getValuePtr( void* pInstance ) const
        {
            if ( _bIsBitField == SW_TRUE )
                return nullptr;
            return reinterpret_cast<T*>( reinterpret_cast<utf8*>( pInstance ) + _offset );
        }

        /** @brief 인스턴스 + 오프셋의 값 포인터. (비트필드는 nullptr 반환) */
        template <typename T>
        const T* getValuePtr( const void* pInstance ) const
        {
            if ( _bIsBitField == SW_TRUE )
                return nullptr;
            return reinterpret_cast<const T*>( reinterpret_cast<const utf8*>( pInstance ) + _offset );
        }

        /** @brief 인스턴스 기준 프로퍼티의 원시 메모리 시작 포인터를 반환합니다. */
        void* getRawPtr( void* pInstance ) const noexcept
        {
            return reinterpret_cast<utf8*>( pInstance ) + _offset;
        }

        /** @brief 인스턴스 기준 프로퍼티의 원시 메모리 const 시작 포인터를 반환합니다. */
        const void* getRawPtr( const void* pInstance ) const noexcept
        {
            return reinterpret_cast<const utf8*>( pInstance ) + _offset;
        }

        /** @brief 커스텀 메타데이터 태그를 조회합니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept
        {
            return _metadata.findCustomMeta( key );
        }
    };

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
        [[maybe_unused]] uint8 _reservedFlags : 5;

        /** @brief 빈 이름↔값 맵. */
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

        /** @brief 메모리 포인터에서 실제 enum 크기만큼 안전하게 읽어 int64로 반환합니다. */
        int64 readValueFromMemory( const void* pPtr ) const noexcept
        {
            if ( pPtr == nullptr )
                return 0;
            switch ( _size )
            {
                case 1:
                    return static_cast<int64>( *static_cast<const uint8*>( pPtr ) );
                case 2:
                    return static_cast<int64>( *static_cast<const uint16*>( pPtr ) );
                case 4:
                    return static_cast<int64>( *static_cast<const int32*>( pPtr ) );
                case 8:
                    return *static_cast<const int64*>( pPtr );
                default:
                    return static_cast<int64>( *static_cast<const int32*>( pPtr ) );
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

        /** @brief ENUM(Invalid/Count) 센티널을 반영한 유효성. 메타가 없으면 true. */
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
                return iter != _mapValueToName.end() ? iter->second : hashed_string( constants::reflection::kNone );
            }

            // _mapValueToName 만 사용 — ValueAlias 가 _mapNameToValue 에 있어도 출력에 중복되지 않음.
            string result;
            result.reserve( 64 );
            for ( const auto& [bitVal, name] : _mapValueToName )
            {
                if ( bitVal != 0 && ( val & bitVal ) == bitVal )
                {
                    if ( result.empty() == false )
                        result += constants::reflection::kFlagSeparator;
                    result += name.c_str();
                }
            }

            return hashed_string( result.c_str() );
        }

        /** @brief `"A | B"` 플래그 문자열을 값으로 파싱합니다. */
        int64 stringFlagsToValue( string_view flagsStr ) const
        {
            if ( _bIsBitFlag == SW_FALSE )
            {
                hashed_string nameKey{ flagsStr };
                auto          iter = _mapNameToValue.find( nameKey );
                return iter != _mapNameToValue.end() ? iter->second : 0;
            }

            int64  intResult{ 0 };
            size_t startPos{ 0 };
            while ( startPos < flagsStr.size() )
            {
                const size_t      delimiterPos = flagsStr.find( '|', startPos );
                const size_t      endPos       = ( delimiterPos != string_view::npos ) ? delimiterPos : flagsStr.size();
                const string_view token        = StringUtil::trim( flagsStr.substr( startPos, endPos - startPos ) );
                if ( token.empty() == false )
                {
                    hashed_string tokenKey{ token };
                    auto          iter = _mapNameToValue.find( tokenKey );
                    if ( iter != _mapNameToValue.end() )
                        intResult |= iter->second;
                }
                if ( delimiterPos == string_view::npos )
                    break;
                startPos = delimiterPos + 1;
            }

            return intResult;
        }

        /** @brief 값 → interned enumerator 이름. Invalid/Count 센티널이면 nullptr. */
        const utf8* valueToCString( int64 value ) const
        {
            if ( isValidValue( value ) == false )
                return nullptr;
            const hashed_string name = ( _bIsBitFlag != SW_FALSE ) ? toStringFlags( value ) : toString( value );
            return name.empty() ? nullptr : name.c_str();
        }

        /** @brief 이름 → 값. 대소문자 무시. 비트플래그는 `A|B` 도 허용. */
        bool tryParse( string_view name, int64& outValue ) const
        {
            if ( name.empty() )
                return false;

            {
                hashed_string key{ name };
                const auto    it = _mapNameToValue.find( key );
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

            if ( _bIsBitFlag != SW_FALSE )
            {
                outValue = stringFlagsToValue( name );
                return isValidValue( outValue );
            }
            return false;
        }
    };

    /// @brief 리플렉션 메서드: 이름, 시그니처, invoker
    struct SW_API FunctionInfo
    {
        string                                        _name;
        hashed_string                                 _hashName;
        string                                        _returnTypeName;        ///< clang spelling (e.g. void, int32)
        vector<string>                                _listParameterTypeName; ///< clang spellings in declaration order
        FunctionMetadata                              _metadata;
        Delegate<TaskValue( void*, const TaskArgs& )> _invoker; ///< instance + args → TaskValue

        /** @brief 커스텀 메타데이터 태그를 조회합니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept
        {
            return _metadata.findCustomMeta( key );
        }
    };

    /// @brief 등록된 타입: FQN, 프로퍼티/메서드, 생성 가능 여부
    struct SW_API TypeInfo
    {
        size_t _size;
        /** @brief `$ctor`로 placement-new 된 인스턴스를 파괴합니다. 없으면 nullptr. */
        void ( *_destroyInstance )( void* ) = nullptr;
        hashed_string                                             _name;
        hashed_string                                             _fullyQualifiedName;
        hashed_string                                             _parentFQN;
        hashed_string                                             _moduleName;
        vector<PropertyInfo>                                      _listProperty;
        vector<FunctionInfo>                                      _listMethod;
        TypeMetadata                                              _metadata;
        mutable vector<PropertyInfo>                              _listPropertyWithBase;
        mutable unordered_map<hashed_string, const PropertyInfo*> _mapNameToPropertyWithBase; ///< 계층 병합 목록의 이름·별칭 → 항목. 목록과 함께 짓는다
        mutable unordered_map<hashed_string, const PropertyInfo*> _mapNameToProperty;
        mutable unordered_map<hashed_string, const FunctionInfo*> _mapNameToMethod;
        /**
         * @brief `_parentFQN` 을 한 번 풀어 둔 부모 `TypeInfo`. 없거나 아직 못 풀었으면 nullptr.
         * @details `isDerivedFrom` 이 조상마다 `findType(_parentFQN)` 을 불렀다 — 조상 하나당
         *          shared_mutex 잠금 + 해시맵 조회. 캐스트가 실패하는 흔한 경우엔 사슬 끝까지 그것을
         *          두 번 걸었다. 등록 배치 끝(`TypeRegistry::buildLookupCaches`)에서 한 번 풀어 두면
         *          걷는 일은 포인터 역참조 몇 번이다.
         *
         *          **포인터는 등록·해제 때 무효가 된다.** 타입 표는 밀집 배열이라 커질 때 원소를 옮기고
         *          (이동 생성자가 이 칸을 비운다), 모듈 해제는 마지막 원소를 빈 자리로 옮긴다(해제가
         *          남은 타입 전부의 이 칸을 비운다). 비어 있으면 `getParentType()` 이 이름으로 다시
         *          푼다. 원자값인 이유: 배치 밖에서 등록된 타입(테스트)은 첫 조회가 여러 스레드에서
         *          동시에 올 수 있고, 같은 값을 쓰는 경쟁이라 relaxed 로 충분하다.
         */
        mutable atomic<const TypeInfo*> _pParentType;
        /**
         * @brief `_parentFQN` 을 이름으로 풀어 봤지만 못 푼 타입 표 세대. 0 이면 아직 안 해 봤다.
         * @details `Component` 처럼 REFLECT 가 아닌 기반은 이름만 적혀 있고 등록되지 않는다 — 예전엔 부모를 묻는
         *          자리마다(계층 프로퍼티 조회 · 이름 걷기 · 기본값의 사슬 수집) 레지스트리를 잠금 잡고 다시 찾았다
         *          (호출당 24 ns). 같은 세대면 답이 같으므로 세대를 적어 두고, 등록·해제로 세대가 바뀔 때만 다시 찾는다.
         */
        mutable atomic<uint32> _parentMissGeneration;
        uint32                 _typeId;
        /**
         * @brief 루트부터 자기까지의 **이름(FQN 의 intern 인덱스)** 을 깊이 순서로 적은 조상 표. `_ancestorDepth` 가 자기 칸이다.
         * @details 캐스트의 핫패스가 이것만 본다: `표[pTarget 의 깊이] == pTarget 의 이름`. 포인터가 아니라 이름이라 타입
         *          표가 원소를 옮겨도 그대로 맞고, 레지스트리 밖 사본(테스트 목의 손으로 만든 `StaticType()`)도 같은
         *          이름이면 같은 타입으로 본다 — 걷기의 `isSameTypeName` 과 같은 규칙이다. `_typeId` 를 쓰지 않는 이유가
         *          그 사본이다: 사본은 자기 id 를 따로 가진다. 등록·해제로 사슬이 바뀔 수 있으면 `TypeRegistry` 가 깊이를
         *          `kAncestorDepthUnknown` 으로 비우고, 배치 끝(`buildLookupCaches`)이나 첫 상속 검사가 다시 세운다.
         *          원자값인 이유는 `_pParentType` 과 같다 — 첫 조회는 여러 스레드에서 올 수 있고 같은 값을 쓴다. 등록과
         *          캐스트가 겹치는 것은 `_pParentType` 과 마찬가지로 전제하지 않는다(모듈 로드는 단일 스레드).
         */
        mutable atomic<uint32> _arrAncestorNameIndex[constants::reflection::kAncestorDisplayDepth];
        /** @brief 조상 표에서 자기 칸의 깊이. Unknown 이면 아직 안 세웠고, None 이면 세울 수 없어 부모 포인터를 걷는다. */
        mutable atomic<uint8> _ancestorDepth;
        /**
         * @brief 레지스트리에 살아 있는 타입인가. 모듈 해제는 항목을 지우지 않고 이것을 내린다(묘비).
         * @details `TypeInfo` 의 주소는 고정이다 — 레지스트리가 `unique_ptr` 로 들고, 표가 커져도 옮기지 않으며, 해제해도
         *          지우지 않는다. 그래서 `TypeLookupCache` 와 `_pParentType` 은 세대 검사 없이 포인터를 그대로 쓰고, 이
         *          플래그 하나로 "해제됐나" 를 본다. 같은 FQN 이 다시 등록되면 같은 객체에 덮어써 되살린다. 레지스트리
         *          밖에서 만든 사본은 늘 살아 있다.
         */
        mutable atomic<uint8> _bAlive;
        /** @brief REFLECT(Abstract) / C++ abstract — not constructible (UCLASS(Abstract)). */
        uint8 _bAbstract : 1;
        /** @brief REFLECT(Static) type (function-library). Not the same as FunctionMetadata::_bStatic. */
        uint8 _bStatic : 1;
        /** @brief 내장 스칼라/문자열 (int32, bool, sw::string, …). REFLECT codegen 없음. */
        uint8         _bPrimitive                 : 1;
        mutable uint8 _bIsCacheBuilt              : 1;
        mutable uint8 _bIsPODFastPath             : 1;
        mutable uint8 _bIsPODCalculated           : 1;
        mutable uint8 _bListPropertyWithBaseBuilt : 1;
        /**
         * @brief `getPropertiesWithBase()` 가 지금 이 타입에서 재귀 중인지.
         * @details 부모 체인이 순환하면(`_parentFQN` 이 자기 자신이나 자손을 가리키면) 그 재귀가
         *          돌아오지 않는다 — 스택이 넘친다. `_parentFQN` 은 코드젠이 적는 값이지만
         *          `registerClass` 는 공개 API 이고 그 값을 검사하지 않으며, 모듈이 따로따로
         *          등록되는 핫리로드에서는 A→B→A 가 만들어질 수 있다.
         */
        mutable uint8          _bBuildingPropertyWithBase : 1;
        [[maybe_unused]] uint8 _reservedPadding[3];

        /** @brief 빈 TypeInfo. */
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

        /** @brief REFLECT 없이 등록된 내장 타입 (int32, float32, …). */
        bool isPrimitive() const noexcept { return _bPrimitive != SW_FALSE; }

        /** @brief 직렬화/복사 시 memcpy POD 경로를 쓸 수 있으면 true. */
        bool usesPodCopyFastPath() const;
        /** @brief 이 타입이 targetFqn이거나 그 파생이면 true. 부모가 미등록이어도 `_parentFQN` 이 같으면 true. */
        bool isDerivedFrom( const hashed_string& targetFqn ) const;
        /**
         * @brief 이 타입이 pTarget 이거나 그 파생이면 true. 조상 표가 있으면 로드 둘과 비교 하나, 여기 인라인.
         * @details 캐스트의 핫패스라 DLL 경계를 넘지 않는다. 둘 다 표가 서 있으면 `표[pTarget 의 깊이] == pTarget 의
         *          이름` 으로 끝난다. 표가 아직 없거나 세울 수 없는 쪽은 `isDerivedFromSlow` 가 세우거나 부모 포인터를
         *          걷는다 — 레지스트리 밖의 사본(테스트 목의 `StaticType()`)도 이름이 같으면 같은 타입으로 본다.
         *          pTarget 이 nullptr 이면 false.
         */
        bool isDerivedFrom( const TypeInfo* pTarget ) const
        {
            if ( pTarget == nullptr )
                return false;
            if ( pTarget == this )
                return true;
            const uint8 selfDepth   = _ancestorDepth.load( std::memory_order_acquire );
            const uint8 targetDepth = pTarget->_ancestorDepth.load( std::memory_order_acquire );
            if ( selfDepth < constants::reflection::kAncestorDisplayDepth && targetDepth < constants::reflection::kAncestorDisplayDepth )
            {
                return targetDepth <= selfDepth && _arrAncestorNameIndex[targetDepth].load( std::memory_order_relaxed ) ==
                                                       pTarget->_arrAncestorNameIndex[targetDepth].load( std::memory_order_relaxed );
            }
            return isDerivedFromSlow( pTarget );
        }
        /** @brief 표가 없을 때의 길 — 세울 수 있으면 세워서 답하고, 아니면 부모 포인터를 걷는다. */
        bool isDerivedFromSlow( const TypeInfo* pTarget ) const;
        /** @brief 부모 TypeInfo. 풀어 둔 것이 없으면 이름으로 찾아 적어 둔다. 부모가 없거나 미등록이면 nullptr. */
        const TypeInfo* getParentType() const;
        /** @brief 부모 포인터를 이름으로 다시 푼다. 등록 배치 끝에서 `TypeRegistry` 가 부른다. */
        void resolveParentType() const;
        /** @brief 부모 포인터와 "못 풀었다" 기억을 비운다. 해제 뒤(부모가 죽었을 수 있다) `TypeRegistry` 가 부른다. */
        void clearParentType() const
        {
            _pParentType.store( nullptr, std::memory_order_relaxed );
            _parentMissGeneration.store( 0, std::memory_order_relaxed );
        }

        /** @brief 조상 표가 세워져 있으면 true — 상속 검사가 걷지 않고 O(1) 로 답한다. */
        bool hasAncestorDisplay() const
        {
            return _ancestorDepth.load( std::memory_order_acquire ) < constants::reflection::kAncestorDisplayDepth;
        }
        /**
         * @brief 부모 포인터를 따라 조상 표를 세운다. 사슬이 다 풀려 있고 표 깊이 안이면 true.
         * @details 이름이 없거나, 순환이거나, 표보다 깊으면 `kAncestorDepthNone` 을 적고 false — 그 타입은 다음
         *          등록·해제까지 부모 포인터 걷기로 답한다. 안 풀리는 부모(REFLECT 가 아닌 기반 ·
         *          아직 안 올라온 모듈)는 사슬의 끝으로 본다 — 그 부모가 등록되는 순간 `registerClass` 가 표를 비운다.
         *          등록 배치 끝에서 `TypeRegistry` 가 부르고, 배치 밖 타입은 첫 상속 검사가 부른다.
         */
        bool buildAncestorDisplay() const;
        /** @brief 조상 표를 비운다. 사슬이 바뀔 수 있는 등록·해제 뒤에 `TypeRegistry` 가 부른다. */
        void clearAncestorDisplay() const
        {
            _ancestorDepth.store( constants::reflection::kAncestorDepthUnknown, std::memory_order_relaxed );
        }
        /** @brief 레지스트리에 살아 있으면 true. 모듈 해제가 내리고 재등록이 올린다. 사본은 늘 true. */
        bool isAlive() const { return _bAlive.load( std::memory_order_acquire ) != SW_FALSE; }
        /**
         * @brief 묘비로 남길 때 모듈이 든 내용을 비운다 — 이름·id·모듈만 남는다.
         * @details 프로퍼티·메서드 목록은 그 모듈의 코드(델리게이트 · `$ctor` · 소멸 함수)를 가리킨다. 모듈이 내려간 뒤에
         *          이것을 파괴하면(재등록 대입 · 레지스트리 소멸) 사라진 코드를 부른다 — SmokeTest 의 핫리로드가 그렇게
         *          죽었다. 그래서 해제 시점, **모듈이 아직 살아 있을 때** 여기서 비운다.
         */
        void clearContent();

        /**
         * @brief 자신 + 상속 베이스 프로퍼티 (단일 부모 체인).
         * @details 자식 이름이 부모를 덮어씁니다. 단일 상속에 안전.
         */
        const vector<PropertyInfo>& getPropertiesWithBase() const;

        /**
         * @brief 이름→프로퍼티/메서드 조회 캐시를 만듭니다.
         * @warning **여러 스레드가 동시에 부르면 안 된다.** `mutable` 맵 둘을 잠금 없이 채운다 —
         *          두 스레드가 같은 `TypeInfo` 를 처음 조회하면 같은 맵에 동시에 삽입한다.
         *          그래서 `TypeRegistry::buildLookupCaches()` 가 **등록 배치가 끝난 직후 단일
         *          스레드에서** 한 번 만든다. 등록된 타입을 병렬로 조회하는 것은 그 뒤이므로 안전하다.
         *          (등록하지 않은 임시 사본은 만든 스레드가 알아서 쓴다.)
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
            if ( _listProperty.size() <= constants::reflection::kLinearSearchThreshold )
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

        /** @brief 이름 메서드를 찾습니다. */
        const FunctionInfo* findMethod( const hashed_string& methodName ) const
        {
            if ( _listMethod.size() <= constants::reflection::kLinearSearchThreshold )
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
         * @brief 현재 클래스 및 부모 상속 체인에서 프로퍼티를 검색합니다 — 계층 병합 목록과 함께 지은 맵 하나로.
         * @details 예전엔 단계마다 `findProperty` 를 따로 불렀고, 사슬 끝의 못 푸는 부모 이름을 잠금 잡고 찾았다
         *          (적중 17 · 실패 34 ns). 이제 `getPropertiesWithBase()` 가 지어 둔 맵을 한 번 본다. 파생이 기반과 같은
         *          이름을 다시 적으면 파생이 이긴다(병합 규칙과 같다).
         */
        const PropertyInfo* findPropertyInHierarchy( const hashed_string& propertyNameOrAlias ) const;

        /**
         * @brief 프로퍼티를 순회합니다. bIncludeBase가 true이면 상속 체인 포함.
         * @details **직렬화는 true 를 써야 한다.** 기본값이 false 라서 오래도록 `XmlSerializer`·
         *          `JsonSerializer`·`ObjectDiffSerializer` 가 **상속된 PROPERTY 를 저장도 로드도
         *          하지 않았다** — 컴포넌트에서는 그것이 곧 `SceneComponent` 의 트랜스폼이라,
         *          씬·프리팹·Undo 스냅샷에서 메시·스프라이트·카메라의 위치가 사라졌다
         *          (`BinarySerializer` 만 처음부터 `getPropertiesWithBase()` 를 써서 옳았다).
         *          `getPropertiesWithBase()` 는 기반 먼저, 파생이 같은 이름을 **덮어쓰는** 순서로
         *          평탄화하고 캐시하므로 중복 방출은 없다.
         * @note 상속 체인을 **호출부가 직접 도는** 코드(`ComponentDefaults` 가 레벨마다 자기 XML
         *       노드를 찾아 적용한다)는 false 가 맞다 — true 로 주면 같은 값을 여러 노드에서
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
    };

} // namespace sw
