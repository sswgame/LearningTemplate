/**
 * @file ReflectionDesc.h
 * @brief 생성 코드(.gen.cpp)가 형식마다 내는 상수 서술자 표입니다. 등록(`TypeRegistry::registerType` · `registerEnum`)이 표를 한 번 돌아 리플렉션 정보를 짓습니다.
 * @details 언리얼 UHT 의 `FClassParams` · `FPropertyParams` · `FMetaDataPairParam`(정적 서술자 + 엔진의 공용 생성 함수 하나)과 같은 자리다. 형식마다 등록 **코드**를 펼치면
 *          생성 단위 수백 개가 같은 대입 · 소멸 사슬을 다시 최적화한다 — 생성 단위에는 상수와, 형식이 있어야 하는 호출(값 접근 · 호출 · 생성)만 남긴다.
 *          칸 차례가 곧 생성기(`CodeGenerator`)가 내는 차례다. 뒤쪽 칸은 기본값이 있어 적지 않은 만큼 글이 짧다 — 칸을 더하면 생성기와 함께 고친다.
 *          생성 코드와 리플렉션 구현만 include 한다(형식 정보 헤더 `TypeInfo.h` 를 읽는 단위는 표를 모른다).
 */
#pragma once
#include "Engine/Reflection/EnumInfo.h"
#include "Engine/Reflection/FunctionInfo.h"
#include "Engine/Reflection/PropertyInfo.h"
#include "Engine/Reflection/ReflectionMetadata.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 프로퍼티
    // ------------------------------------------------------------------------------
    /** @brief 런타임 프로퍼티 메타데이터의 켜진 플래그 비트입니다(`PropertyMetadata` 의 같은 이름 비트필드). 이름은 멤버 `_bX` 의 X 다 — 생성기가 표(`PredefinedAnnotationField.xxx`)의 멤버 이름으로 짓는다. */
    struct PropertyMetadataFlag
    {
        static constexpr uint32 kHasMinRange    = 1u << 0;
        static constexpr uint32 kHasMaxRange    = 1u << 1;
        static constexpr uint32 kReadOnly       = 1u << 2;
        static constexpr uint32 kXMLAttribute   = 1u << 3;
        static constexpr uint32 kAssetPath      = 1u << 4;
        static constexpr uint32 kTransient      = 1u << 5;
        static constexpr uint32 kSkipIfEmpty    = 1u << 6;
        static constexpr uint32 kReplicated     = 1u << 7;
        static constexpr uint32 kRequired       = 1u << 8;
        static constexpr uint32 kInheritKey     = 1u << 9;
        static constexpr uint32 kSaveGame       = 1u << 10;
        static constexpr uint32 kInterp         = 1u << 11;
        static constexpr uint32 kRuntimeState   = 1u << 12;
        static constexpr uint32 kRequiredAtRoot = 1u << 13;
        static constexpr uint32 kInheritParent  = 1u << 14;
    };

    /** @brief 런타임 프로퍼티 메타데이터의 글 칸입니다. 이름은 멤버 `_x` 의 X 다(`PropertyMetadataFlag` 와 같은 약속). */
    enum class PropertyMetadataTextKind : uint8
    {
        DefaultValue,
        AssetType,
        CatalogKeyTag,
        RepNotify,
        Validate,
    };

    /** @brief 글 칸 하나입니다. */
    struct PropertyMetadataText
    {
        PropertyMetadataTextKind _kind;
        const utf8*              _pText;
    };

    /** @brief `PROPERTY( … )` 에 적은 런타임 메타데이터입니다. 적은 것이 없는 프로퍼티는 표가 없고 공유 기본값을 쓴다(힙 없음, `ReflectMetadataBox`). */
    struct PropertyMetadataDesc
    {
        uint32                        _flags{ 0 }; ///< `PropertyMetadataFlag` 비트
        float32                       _minRange{ 0.0f };
        float32                       _maxRange{ 1.0f };
        ReflectUnitKind               _unit{};
        uint32                        _maxCount{ 0 };
        const PropertyMetadataText*   _pText{ nullptr };
        uint32                        _textCount{ 0 };
        PropertyDefaultApplyFunction  _pApplyDefault{ nullptr };
        PropertyRangeReadFunction     _pReadMinRange{ nullptr };
        PropertyRangeReadFunction     _pReadMaxRange{ nullptr };
        PropertyEditConditionFunction _pEditCondition{ nullptr };

        /** @brief 적은 값을 @p outMetadata 에 넣습니다. */
        SW_API void applyTo( PropertyMetadata& outMetadata ) const;
    };

    /** @brief 중첩 컨테이너의 한 단계입니다(`NestedContainerInfo` 로 짓는다). 안쪽 단계의 표를 가리킨다. */
    struct NestedContainerDesc
    {
        ContainerKind              _kind;
        const utf8*                _pTypeName;
        const utf8*                _pElementTypeName;
        const utf8*                _pKeyTypeName;
        const IContainerWrapper*   _pWrapper;
        const NestedContainerDesc* _pElementNested;
    };

    /** @brief 컨테이너 프로퍼티의 모양입니다. 컨테이너가 아니면 표가 없다. */
    struct PropertyContainerDesc
    {
        ContainerKind              _kind;
        const utf8*                _pElementTypeName; ///< 없으면 nullptr
        const utf8*                _pKeyTypeName;     ///< 없으면 nullptr
        const IContainerWrapper*   _pWrapper;         ///< `ContainerWrapperOf` 의 정적 객체다(상태가 없다 — 모듈의 수명)
        const NestedContainerDesc* _pNested;          ///< 원소 형식 이름을 가진 단계 표. 없으면 nullptr
    };

    /** @brief 값 주소 · 알림 · 검증 · 비트필드 · 옛 이름처럼 드문 칸입니다. 하나도 없으면 표가 없다. */
    struct PropertyBindingDesc
    {
        const utf8*                     _pHandleTargetTypeName{ nullptr };
        const utf8* const*              _ppAlias{ nullptr }; ///< `_aliasCount` 개의 옛 이름
        uint32                          _aliasCount{ 0 };
        PropertyInfo::ValueAccessor     _pValueAccessor{ nullptr };
        PropertyInfo::RepNotifyFunction _pRepNotify{ nullptr };
        ReflectValidateFunction         _pValidate{ nullptr };
        PropertyInfo::SetBitFunction    _pSetBit{ nullptr }; ///< 비트필드면 그 비트만 세우는 함수
        size_t                          _ownerSize{ 0 };     ///< 비트필드의 주인 타입 크기(`resolveBitField`)
    };

    /**
     * @brief 프로퍼티 하나의 서술자 줄(`static constexpr ::sw::PropertyDesc kProperties[]`)입니다.
     * @details 글은 리터럴, 함수는 캡처 없는 람다의 포인터, 래퍼는 정적 객체 주소라 표가 상수 초기화된다. 이름 해시는 따로 싣지 않는다 —
     *          intern 항목이 이미 갖고 있어(`PropertyInfo::getNameHash`) 둘째 사본은 낡을 수만 있다. 드문 칸은 곁 표의 주소라 흔한 줄은 세 칸이다.
     */
    struct PropertyDesc
    {
        const utf8*                  _pName;
        const utf8*                  _pTypeName;
        size_t                       _offset; ///< 비트필드 · 접근자 프로퍼티는 0
        const PropertyMetadataDesc*  _pMetadata{ nullptr };
        const PropertyContainerDesc* _pContainer{ nullptr };
        const PropertyBindingDesc*   _pBinding{ nullptr };
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) 메서드 · 이벤트
    // ------------------------------------------------------------------------------
    /** @brief 런타임 메서드 메타데이터의 켜진 플래그 비트입니다(`FunctionMetadata` 의 같은 이름 비트필드, 이름 약속은 `PropertyMetadataFlag`). */
    struct FunctionMetadataFlag
    {
        static constexpr uint8 kReliable    = 1u << 0;
        static constexpr uint8 kValidate    = 1u << 1;
        static constexpr uint8 kConstructor = 1u << 2;
        static constexpr uint8 kStatic      = 1u << 3;
        static constexpr uint8 kConst       = 1u << 4;
    };

    /** @brief 함수나 이벤트의 인자 하나입니다. */
    struct FunctionParameterDesc
    {
        const utf8*           _pName;                            ///< 선언에 적힌 이름(없으면 "")
        const utf8*           _pTypeName;                        ///< 정규 타입 이름
        const utf8*           _pDefaultValue;                    ///< 기본 인자 식 그대로(없으면 "")
        const ReflectTypeOps* _pType;                            ///< 이벤트 인자는 nullptr(`EventInfo::setOps` 가 채운다)
        const utf8*           _pHandleTargetTypeName{ nullptr }; ///< 타입 핸들 인자의 대상(아니면 nullptr)
    };

    /** @brief 메서드(또는 생성자) 하나의 서술자 줄입니다 — 프로퍼티의 `PropertyDesc` 와 같은 자리(언리얼 UHT `FClassFunctionLinkInfo`). */
    struct FunctionDesc
    {
        using InvokeFunction = TaskValue ( * )( void* pObject, const TaskArgs& args );
        /** @brief 생성자 줄의 생성 함수입니다 — @p pObject 에 placement new 한 줄만 한다. 반환값(`TaskValue`) 만들기는 `invokeConstructor` 가 한 곳에서 한다. */
        using ConstructFunction = void ( * )( void* pObject, const TaskArgs& args );

        const utf8*                  _pName;           ///< 표시 이름(생성자는 `$ctor`)
        const utf8*                  _pLookupName;     ///< 찾는 이름(`_hashName` — 생성자는 인자 타입까지 붙인 이름)
        const utf8*                  _pReturnTypeName; ///< 정규 반환 타입 이름
        const ReflectTypeOps*        _pReturnType;     ///< void · 생성자면 nullptr
        const FunctionParameterDesc* _pParameters;
        uint32                       _parameterCount;
        /** @brief 호출 함수입니다. 생성자 줄은 nullptr 이고 `_pConstruct` 를 쓴다. 둘 다 nullptr 인 줄은 등록에서 빠진다. */
        InvokeFunction _pInvoke;
        /**
         * @brief 생성자 줄의 생성 함수입니다(아니면 nullptr). 파서가 더한 기본 생성자는 그 타입이 기본 생성 가능하지 않으면 nullptr 이다(`defaultConstructor`).
         * @details 생성 TU 마다 있어야 하는 것은 placement new 한 줄뿐이다 — 예전 `$ctor` 람다는 반환값 만들기 · 인자 버리기까지 타입마다 펼쳤다.
         */
        ConstructFunction _pConstruct{ nullptr };
        uint8             _flags{ 0 }; ///< `FunctionMetadataFlag` 비트
        FunctionNetRole   _netRole{ FunctionNetRole::Local };
        uint32            _maxCallsPerSecond{ 0 };

        /** @brief 파서가 더한 기본 생성자 줄입니다. 기본 생성할 수 없는 타입이면 생성 함수가 없어 등록에서 빠진다. */
        template <typename T>
        static constexpr FunctionDesc defaultConstructor( const utf8* pCtorName, const utf8* pVoidName );

        /** @brief 생성자 줄을 부릅니다 — `_pConstruct` 로 짓고 빈 값을 돌려준다. `FunctionInfo::_invoker` 가 이 줄에 묶인다(비인라인, 한 곳). */
        SW_API TaskValue invokeConstructor( void* pObject, const TaskArgs& args ) const;
        /** @brief 선언 사실 · 런타임 메타데이터를 적은 줄이면 true 입니다(아니면 공유 기본값을 쓴다). */
        constexpr bool hasMetadata() const noexcept { return _flags != 0 || _netRole != FunctionNetRole::Local || _maxCallsPerSecond != 0; }
        /** @brief 플래그 · 넷 역할 · 초당 상한을 @p outMetadata 에 넣습니다. */
        SW_API void applyTo( FunctionMetadata& outMetadata ) const;
    };

    /** @brief 이벤트 하나의 서술자 줄입니다. 이벤트는 표시 메타(에디터 메타데이터)만 받아 런타임 메타데이터 칸이 없다. */
    struct EventDesc
    {
        const utf8*                  _pName;
        size_t                       _offset; ///< 인스턴스 안 델리게이트 필드의 오프셋
        const FunctionParameterDesc* _pParameters;
        uint32                       _parameterCount;
        const ReflectEventOps*       _pOps; ///< `ReflectEventOpsOf<필드 타입>::kOps`
    };

    /**
     * @brief 파서가 더한 기본 생성자(`$ctor`)의 생성 함수입니다. 기본 생성할 수 없는 타입(참조 멤버 · 기본 생성자 없는 기반)은 `kConstruct` 가 nullptr 라 등록에서 빠진다.
     * @details 생성 코드의 `Registrar<T>` 는 템플릿이 아닌 특수화라 `if constexpr` 로 본문을 버릴 수 없다 — 템플릿 안에서 가른다.
     */
    template <typename T>
    struct ReflectDefaultCtorOf
    {
        static void construct( void* pObject, const TaskArgs& )
        {
            if constexpr ( std::is_default_constructible_v<T> )
                sw_placement_new( pObject ) T();
        }
        static constexpr FunctionDesc::ConstructFunction kConstruct = std::is_default_constructible_v<T> ? &construct : nullptr;
    };

    template <typename T>
    constexpr FunctionDesc FunctionDesc::defaultConstructor( const utf8* pCtorName, const utf8* pVoidName )
    {
        return FunctionDesc{ pCtorName, pCtorName, pVoidName, nullptr, nullptr, 0u, nullptr, ReflectDefaultCtorOf<T>::kConstruct, FunctionMetadataFlag::kConstructor,
                             FunctionNetRole::Local, 0u };
    }
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) 타입
    // ------------------------------------------------------------------------------
    /** @brief 타입 서술자의 켜진 플래그 비트입니다. */
    struct TypeDescFlag
    {
        static constexpr uint32 kAbstract                = 1u << 0;
        static constexpr uint32 kStatic                  = 1u << 1;
        static constexpr uint32 kPrimitive               = 1u << 2;
        static constexpr uint32 kInterface               = 1u << 3;
        static constexpr uint32 kOverridesTick           = 1u << 4;
        static constexpr uint32 kOverridesSubTick        = 1u << 5;
        static constexpr uint32 kOverridesSerializeState = 1u << 6;
    };

    /** @brief 내보낸 인터페이스 한 줄(`REFLECT_INTERFACE( I, _port )`)입니다. */
    struct TypeInterfaceDesc
    {
        const utf8* _pInterfaceFQN;
        uint32      _portOffset;
    };

    /**
     * @brief 타입 하나의 서술자입니다(언리얼 UHT `FClassParams`). 생성 코드의 `Registrar<T>::getDesc` 가 상수로 짓고 `TypeRegistry::registerType` 이 읽습니다.
     * @details 적지 않은 칸은 기본값이다 — `withX` 가 적은 것만 채운 사본을 돌려준다(모두 상수 문맥).
     */
    struct TypeDesc
    {
        using DestroyFunction      = void ( * )( void* pInstance );
        using AddComponentFunction = void* (*)( void* pOwner );

        const utf8*              _pName;
        const utf8*              _pFullyQualifiedName;
        const utf8*              _pParentFQN; ///< 부모가 없으면 ""
        const utf8*              _pModuleName;
        size_t                   _size;
        DestroyFunction          _pDestroyInstance{ nullptr };
        AddComponentFunction     _pAddComponent{ nullptr };
        ReflectValidateFunction  _pValidate{ nullptr };
        const FixedCodec*        _pFixedCodec{ nullptr };
        const PropertyDesc*      _pProperty{ nullptr };
        const FunctionDesc*      _pMethod{ nullptr };
        const EventDesc*         _pEvent{ nullptr };
        const TypeInterfaceDesc* _pInterface{ nullptr };
        const utf8* const*       _ppRemovedField{ nullptr };
        const utf8* const*       _ppAlias{ nullptr };
        const utf8* const*       _ppRetiredTypeName{ nullptr };
        uint32                   _propertyCount{ 0 };
        uint32                   _methodCount{ 0 };
        uint32                   _eventCount{ 0 };
        uint32                   _interfaceCount{ 0 };
        uint32                   _removedFieldCount{ 0 };
        uint32                   _aliasCount{ 0 };
        uint32                   _retiredTypeNameCount{ 0 };
        uint32                   _version{ 0 }; ///< `REFLECT( Version = N )`. 적지 않았으면 0
        uint32                   _flags{ 0 };   ///< `TypeDescFlag` 비트

        /** @brief C++ 타입 @p T 의 서술자입니다 — 크기와 소멸 함수를 채운다. */
        template <typename T>
        static constexpr TypeDesc of( const utf8* pName, const utf8* pFullyQualifiedName, const utf8* pParentFQN, const utf8* pModuleName )
        {
            return TypeDesc{ pName, pFullyQualifiedName, pParentFQN, pModuleName, sizeof( T ), &destroyInstance<T> };
        }

        /** @brief 내장 타입(`ReflectBuiltins.xxx`)의 서술자입니다 — 이름이 곧 FQN 이고 소멸 함수가 없다. */
        static constexpr TypeDesc makePrimitive( const utf8* pName, const utf8* pModuleName, const size_t size )
        {
            return TypeDesc{ pName, pName, "", pModuleName, size }.withFlags( TypeDescFlag::kPrimitive );
        }

        constexpr TypeDesc withFlags( const uint32 flags ) const
        {
            TypeDesc copy = *this;
            copy._flags |= flags;
            return copy;
        }
        constexpr TypeDesc withVersion( const uint32 version ) const
        {
            TypeDesc copy = *this;
            copy._version = version;
            return copy;
        }
        constexpr TypeDesc withAddComponent( const AddComponentFunction pAddComponent ) const
        {
            TypeDesc copy       = *this;
            copy._pAddComponent = pAddComponent;
            return copy;
        }
        constexpr TypeDesc withValidate( const ReflectValidateFunction pValidate ) const
        {
            TypeDesc copy   = *this;
            copy._pValidate = pValidate;
            return copy;
        }
        constexpr TypeDesc withFixedCodec( const FixedCodec* pFixedCodec ) const
        {
            TypeDesc copy     = *this;
            copy._pFixedCodec = pFixedCodec;
            return copy;
        }
        template <size_t N>
        constexpr TypeDesc withProperties( const PropertyDesc ( &arrProperty )[N] ) const
        {
            TypeDesc copy       = *this;
            copy._pProperty     = arrProperty;
            copy._propertyCount = static_cast<uint32>( N );
            return copy;
        }
        template <size_t N>
        constexpr TypeDesc withMethods( const FunctionDesc ( &arrMethod )[N] ) const
        {
            TypeDesc copy     = *this;
            copy._pMethod     = arrMethod;
            copy._methodCount = static_cast<uint32>( N );
            return copy;
        }
        template <size_t N>
        constexpr TypeDesc withEvents( const EventDesc ( &arrEvent )[N] ) const
        {
            TypeDesc copy    = *this;
            copy._pEvent     = arrEvent;
            copy._eventCount = static_cast<uint32>( N );
            return copy;
        }
        template <size_t N>
        constexpr TypeDesc withInterfaces( const TypeInterfaceDesc ( &arrInterface )[N] ) const
        {
            TypeDesc copy        = *this;
            copy._pInterface     = arrInterface;
            copy._interfaceCount = static_cast<uint32>( N );
            return copy;
        }
        template <size_t N>
        constexpr TypeDesc withRemovedFieldNames( const utf8* const ( &arrName )[N] ) const
        {
            TypeDesc copy           = *this;
            copy._ppRemovedField    = arrName;
            copy._removedFieldCount = static_cast<uint32>( N );
            return copy;
        }
        template <size_t N>
        constexpr TypeDesc withAliases( const utf8* const ( &arrName )[N] ) const
        {
            TypeDesc copy    = *this;
            copy._ppAlias    = arrName;
            copy._aliasCount = static_cast<uint32>( N );
            return copy;
        }
        template <size_t N>
        constexpr TypeDesc withRetiredTypeNames( const utf8* const ( &arrName )[N] ) const
        {
            TypeDesc copy              = *this;
            copy._ppRetiredTypeName    = arrName;
            copy._retiredTypeNameCount = static_cast<uint32>( N );
            return copy;
        }

    private:
        template <typename T>
        static void destroyInstance( void* pInstance )
        {
            std::destroy_at( static_cast<T*>( pInstance ) );
        }
    };
} // namespace sw
