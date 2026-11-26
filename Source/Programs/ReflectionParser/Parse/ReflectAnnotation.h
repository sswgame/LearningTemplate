/**
 * @file ReflectAnnotation.h
 * @brief ReflectionParser 가 소스에서 읽는 애노테이션 계약입니다 — `ReflectionMacros.h` · 생성 코드와 맞춰야 하는 매크로 철자 · 표식 · 정체 FQN 표.
 * @details clang 인자 · SDK 경로 · 컴포넌트 기반 타입은 Config/Environment/parser_config.defaults.json 에서, 설정 키 · CLI 플래그는 `ParserConfigKeys.h` 에 있습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // parse — clang annotate 매크로 (PredefinedReflectAnnotation.xxx)
    // ------------------------------------------------------------------------------
    /** @brief clang annotate 매크로 (PredefinedReflectAnnotation.xxx) */
    struct ReflectAnnotationDesc
    {
        const utf8* _pMacroName;
        const utf8* _pPrefix;
        const utf8* _pMacroOpen;
        const utf8* _pScope;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // parse — REFLECT/PROPERTY/FUNCTION 접두사·마커 (매크로 계약)
    // ------------------------------------------------------------------------------
    namespace annotation
    {
        /** @brief `k<ID>` 는 그 애노테이션의 설명자 한 벌입니다. 접두사와 매크로 철자를 따로 들고 다니지 않게 합니다. */
#define REGISTER_REFLECT_ANNOTATION( ID, MacroName, AnnotatePrefix, ScopeName )                          \
    inline constexpr ReflectAnnotationDesc k##ID{ MacroName, AnnotatePrefix, MacroName "(", ScopeName }; \
    inline constexpr const utf8*           k##ID##Macro     = MacroName;                                 \
    inline constexpr const utf8*           k##ID##Prefix    = AnnotatePrefix;                            \
    inline constexpr const utf8*           k##ID##MacroOpen = MacroName "(";                             \
    inline constexpr const utf8*           k##ID##Scope     = ScopeName;
#include "PredefinedReflectAnnotation.xxx"
#undef REGISTER_REFLECT_ANNOTATION

        /**
         * @brief 넷 역할 필드의 정규 이름입니다.
         * @details `FUNCTION( Server )` 는 키가 아니라 역할 이름을 적습니다(토큰 자체가 값).
         *          그래서 적용할 때는 이 이름의 필드에 역할 이름을 값으로 넘깁니다.
         */
        inline constexpr const utf8* kNetRoleField = "NetRole";

        inline constexpr const utf8* kReflectBodyPrefix   = "REFLECT_BODY";
        inline constexpr const utf8* kReflectBodyMarkerFn = "__sw_reflect_body";
        /** @brief `REFLECT_INTERFACE( I, _port )` 의 애노테이션 머리와 표식 함수 이름 머리입니다(`ReflectionMacros.h`). */
        inline constexpr const utf8* kReflectInterfacePrefix   = "REFLECT_INTERFACE;";
        inline constexpr const utf8* kReflectInterfaceMarkerFn = "__sw_reflect_interface_";
        inline constexpr const utf8* kCtorLookupName           = "$ctor";
        inline constexpr const utf8* kVoidTypeName             = "void";
        inline constexpr const utf8* kDefaultMethodCategory    = "General";
        inline constexpr const utf8* kConstructorCategory      = "Constructor";
        /** @brief 끝에 오면 개수 표식으로 보는 열거자 이름입니다(`ENUM( Count = … )` 을 적지 않아도 된다). */
        inline constexpr const utf8* kCountEnumerator = "Count";

        /**
         * @brief 서버 버스 본문 타입의 표식 멤버(`static constexpr uint32 kBusVersion`)입니다(`ServerBusTopic.h` 의 `HasServerBusVersion`).
         * @details 이 멤버가 있는 `REFLECT` 타입은 다른 모듈의 `ServerBusTopic<T>` 가 `T::StaticType()` 을 부르므로 모듈 내보내기 매크로가 있어야 합니다.
         */
        inline constexpr const utf8* kBusVersionMember = "kBusVersion";

        /**
         * @brief 리플렉션 이벤트가 되는 필드 타입입니다 — `PROPERTY()` 가 붙은 멀티캐스트 델리게이트는 값이 아니라 이벤트로 수집합니다.
         * @details 판정은 형식의 정체(`typeidentity::kMulticastDelegate`)로, 인자 이름은 소스 토큰의 템플릿 이름(`kEventTemplateLeaf`)부터 읽습니다.
         */
        inline constexpr const utf8* kEventTemplateLeaf = "MulticastDelegate";

        /** @brief 검증 함수(`Validate = fn`)의 인자 타입 — 정규 이름(`sw::` 와 참조를 벗긴 것)입니다. */
        inline constexpr const utf8* kValidationContextTypeName = "ValidationContext";

        /** @brief C 고정 배열(`T _arr[N]`)이 타는 `ReflectBuiltins.xxx` CONTAINER 줄의 이름입니다 — 래퍼 줄기 · 종류 · 역할은 그 줄이 정한다(`std::array` 와 같다). */
        inline constexpr const utf8* kFixedArrayTypeName = "array";

        /** @brief 멤버의 상태 선언 `SW_TRANSIENT` · `SW_CODEC_STATE` 의 애노테이션 글입니다(`ReflectionMacros.h`). */
        inline constexpr const utf8* kTransientAnnotation  = "SW_TRANSIENT";
        inline constexpr const utf8* kCodecStateAnnotation = "SW_CODEC_STATE";
        /** @brief `SW_CODEC_STATE` 멤버를 싣는 메서드 이름입니다(상태 코덱 · 디렉터의 본문). */
        inline constexpr const utf8* kSerializeStateMethod     = "serializeState";
        inline constexpr const utf8* kSerializeGameStateMethod = "serializeGameState";
        /** @brief 비트필드의 남는 비트 이름 머리입니다 — 값이 아니라 상태 선언을 묻지 않는다. */
        inline constexpr const utf8* kReservedMemberPrefix = "_reserved";
    } // namespace annotation

    /**
     * @brief 생성기가 정체로 가르는 형식의 FQN 표입니다 — 형식 철자(`find( "AssetRef<" )`)가 아니라 clang 이 준 선언의 FQN(템플릿이면 기본 템플릿)과 견준다(`CursorReader::getTypeIdentity`).
     * @details 철자로 가르면 별칭 · 이름공간 생략 · 템플릿 인자 안의 같은 낱말에 속는다. 인라인 이름공간(`std::__1` · `std::__cxx11`)은 FQN 에 넣지 않는다.
     */
    namespace typeidentity
    {
        inline constexpr const utf8* kAssetRef             = "sw::AssetRef";
        inline constexpr const utf8* kDataPath             = "sw::DataPath";
        inline constexpr const utf8* kCatalogKey           = "sw::CatalogKey";
        inline constexpr const utf8* kTypedComponentHandle = "sw::TComponentHandle";
        inline constexpr const utf8* kComponentHandle      = "sw::ComponentHandle";
        inline constexpr const utf8* kDynamicBitset        = "sw::DynamicBitset";
        inline constexpr const utf8* kEnumArray            = "sw::EnumArray";
        inline constexpr const utf8* kMulticastDelegate    = "sw::MulticastDelegate";
        /** @brief 글(문자 원소가 `char`)로 싣는 문자열 템플릿입니다(`sw::string` 은 자체 클래스). */
        inline constexpr const utf8* kArrStringTemplate[] = { "sw::basic_string", "std::basic_string" };
        /** @brief 가변 길이 시퀀스로 싣는 템플릿입니다. */
        inline constexpr const utf8* kArrVectorTemplate[] = { "sw::vector", "std::vector" };
        /** @brief 고정 길이 시퀀스로 싣는 템플릿입니다. */
        inline constexpr const utf8* kArrArrayTemplate[] = { "sw::array", "std::array" };
        /**
         * @brief 이름 · 경로 칸 형식입니다(글 형식 `kArrStringTemplate` 과 함께) — 다른 데이터를 가리키는 칸이라 C++ 문자열 기본값이 엔진이 정한 기본값인지 묻는다
         *        (`PROPERTY( DefaultName )` · `Required`). 이것들의 `vector` 도 같다.
         */
        inline constexpr const utf8* kArrNameFieldType[] = { "sw::basic_hashed_string", "sw::DataPath", "sw::AssetID", "sw::TagID", "sw::AssetRef" };
        /**
         * @brief 값이 아니라 구독 · 연결 · 공유 에셋 · 잠금인 형식입니다 — 싣고 되살릴 값이 없어 상태 선언(`SW_TRANSIENT` · `SW_CODEC_STATE`)을 묻지 않는다
         *        (포인터 · 참조 · const 값 · `shared_ptr<const T>` 도 같다, `MemberCollectorValidationInternal::hasStateValue`).
         */
        inline constexpr const utf8* kArrNonStateType[] = { "sw::Subscription", "sw::SubscriptionList", "sw::ItemSubscriptionList", "sw::Delegate", "sw::MulticastDelegate",
                                                            "sw::DelegateSlot", "sw::SharedAssetRef", "sw::AssetHandle", "sw::BoundAssetRef", "sw::AssetReloadWatch",
                                                            "sw::ConsumerLease", "sw::DevCounter", "sw::IdentityToken", "sw::ThreadRole", "sw::mutex", "std::weak_ptr" };
        /** @brief 나눠 쥐는 포인터 템플릿입니다(인자가 const 면 바꿀 수 없는 공유 데이터라 상태가 아니다). */
        inline constexpr const utf8* kArrSharedPointerTemplate[] = { "std::shared_ptr" };
    } // namespace typeidentity

    // ------------------------------------------------------------------------------
    // parse — 소스 키워드 스캔
    // ------------------------------------------------------------------------------
    inline static constexpr const utf8* kSourceKeywordScan[] = {
        annotation::kReflectMacro,
        annotation::kPropertyMacro,
        annotation::kFunctionMacro,
        annotation::kEnumMacro,
    };

} // namespace sw
