/**
 * @file Core/Subsystem/ServiceSlot.h
 * @brief 타입 하나가 가진 프로세스 범위의 서비스 칸입니다 — 바인딩이 찾은 객체 주소를 두고, 창구(`engine::getSubsystem<T>()`)가 이것만 읽습니다.
 *
 * @details 언리얼 `GEngine->GetEngineSubsystem<T>()` 처럼 타입으로 찾되, 부를 때마다 표를 찾지 않도록 타입마다 칸 하나를 둡니다.
 *          - **선언**: 타입의 **클래스 본문 안**(접근 구역은 상관없다 — 창구는 friend 다)에 `SW_SERVICE_SLOT( Type, Required );` 한 줄. 필수 여부와
 *            const 여부는 부르는 쪽 TU 가 컴파일 때 알아야 하므로(비어 있을 수 있는 칸에 `getSubsystem` 을 쓰면 컴파일 오류) 형식에 둔다.
 *          - **정의와 등록**: 그 객체를 범위에 넣는 .cpp 의 등록 줄(`SW_ENGINE_SERVICE*`, `Engine/Foundation/EngineSubsystemRegistration.h`)이
 *            칸을 정의하고 엔진의 등록 목록에 단다. 중앙 목록은 없다 — 줄이 둘이면 링크 오류(칸 정의가 둘), 줄이 없으면 그 칸을 읽는 쪽이 링크 오류다.
 *          칸은 그 타입의 정적 데이터 멤버라 Engine 이미지에 하나뿐이다 — 타입을 `SW_API` 로 내보낸다(내보낸 클래스의 멤버에 `SW_API` 를 또 붙이면 clang 이 멈춘다).
 *          헤더 템플릿의 정적 데이터(인라인 변수)는 Windows 에서 DLL 마다 따로 생겨 칸으로 쓰지 않는다. Core 는 칸의 모양만 안다 — 무엇을 묶고 누구에게 보이는지는 엔진의 등록이 정한다.
 *          **왜 클래스 안인가.** 예전 칸은 클래스 뒤에 둔 `ServiceSlotOf<T>` 명시적 특수화라, 사용이 먼저 오면 기본 템플릿이 인스턴스화되고 뒤의 특수화가
 *          ill-formed 였다 — clang · gcc 만 잡고 MSVC 는 지나갔다(옛 린트 service-slot-order 는 같은 파일만 봤다). 선언이 형식의 일부면 형식이 완전한 곳 어디서나 칸이 보이고,
 *          불완전할 때 먼저 쓴 TU 는 매크로의 완전 클래스 문맥 검사(`static_assert`)가 모든 컴파일러에서 멈춘다(언리얼 `GENERATED_BODY` 처럼 선언을 형식 안에 둔다).
 *          칸을 선언하지 않은 타입은 기본 템플릿(`kRegistered == false`)이고, 창구는 범위를 올라가며 찾는다(`engine::findSubsystem<T>()`).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include <type_traits>

namespace sw
{
    /** @brief 서비스 칸의 필수 여부입니다. `Required` 는 바인딩 검사에 들고 `getSubsystem` 이 되며, `Optional` 은 `findSubsystem` 만 된다. */
    enum class ServiceRequirement : uint8
    {
        Required,
        Optional,
    };

    /** @brief 서비스 칸입니다. 바인딩 전 · 묶을 객체가 없으면 nullptr 입니다. 아무것도 소유하지 않는다(소유는 범위). */
    struct ServiceSlot
    {
        void* _pBound; ///< 묶은 객체(그 타입으로 바꾼 주소)
    };

    /** @brief 칸 선언이 형식에 남기는 값입니다(`SW_SERVICE_SLOT` 의 숨은 friend 가 돌려주는 형식 — 평가하지 않는 문맥에서만 쓴다). */
    template <class TAccess, ServiceRequirement requirement>
    struct ServiceSlotTraits
    {
        static constexpr ServiceRequirement kRequirement = requirement;
        using Access                                     = TAccess;
    };

    /** @brief 타입 @p T 의 서비스 칸입니다. 칸을 선언하지 않은 타입은 이 기본 템플릿입니다. 둘째 인자는 아래 부분 특수화가 고르는 자리다(C++17 — concept 대신 void_t). */
    template <class T, class = void>
    struct ServiceSlotOf
    {
        static constexpr bool kRegistered = false;
        using Access                      = T;
    };

    /** @brief 클래스 안에 칸을 선언한 타입(`SW_SERVICE_SLOT`)의 칸입니다 — 숨은 friend 라 ADL 로만 보이고 접근 구역과 무관하다. 칸은 그 타입의 정적 멤버이고 이 템플릿이 friend 로 읽는다. */
    template <class T>
    struct ServiceSlotOf<T, std::void_t<decltype( getServiceSlotTraits( static_cast<T*>( nullptr ) ) )>>
    {
        using Traits                                     = decltype( getServiceSlotTraits( static_cast<T*>( nullptr ) ) );
        static constexpr bool               kRegistered  = true;
        static constexpr ServiceRequirement kRequirement = Traits::kRequirement;
        static constexpr const utf8*        kTypeName    = T::kServiceSlotTypeName;
        using Access                                     = typename Traits::Access;

        /** @brief 칸입니다(Engine 이미지의 정적 멤버 — 정의는 등록 줄). 창구의 빠른 길이라 강제 인라인이다. */
        SW_INLINE static ServiceSlot& getSlot() noexcept { return T::s_serviceSlot; }
    };
} // namespace sw

// 인자가 **타입 이름**이라 괄호를 씌울 수 없다(`const (Type)` 는 올바른 문법이 아니다).
// 마지막 멤버 함수는 완전 클래스 문맥이라 형식이 완전할 때 칸을 다시 찾는다 — 그 전에 형식이 불완전한 채로 `ServiceSlotOf<Type>` 을 쓴 TU 는 기본 템플릿이 굳어 여기서 멈춘다.
// NOLINTBEGIN(bugprone-macro-parentheses)
#define SW_SERVICE_SLOT_IMPL( Type, AccessType, requirement )                                                                                                  \
    template <class, class>                                                                                                                                    \
    friend struct ::sw::ServiceSlotOf;                                                                                                                         \
    friend ::sw::ServiceSlotTraits<AccessType, ::sw::ServiceRequirement::requirement> getServiceSlotTraits( Type* ) noexcept;                                  \
    static constexpr const ::utf8*                                                    kServiceSlotTypeName = #Type;                                            \
    static ::sw::ServiceSlot                                                          s_serviceSlot; /* 정의는 등록 줄(`SW_ENGINE_SERVICE*`) */                \
    [[maybe_unused]] static void                                                      requireServiceSlotDeclaredBeforeUse() noexcept                           \
    {                                                                                                                                                          \
        static_assert( ::sw::ServiceSlotOf<Type>::kRegistered, #Type " was used (getSubsystem / findSubsystem) while incomplete - include its header first" ); \
    }                                                                                                                                                          \
    static_assert( true, "" )
// NOLINTEND(bugprone-macro-parentheses)

/** @brief 타입 @p Type 의 서비스 칸을 선언합니다. @p requirement 는 `Required` · `Optional` 입니다. **클래스 본문 안**에 두고, 클래스는 `SW_API` 로 내보낸다(.cpp 안의 형식은 빼도 된다). */
#define SW_SERVICE_SLOT( Type, requirement ) SW_SERVICE_SLOT_IMPL( Type, Type, requirement )
/** @brief 위와 같고, 창구가 const 로 돌려줍니다(읽기 전용 값 묶음 — 설정 · 기본 에셋 표). */
#define SW_SERVICE_SLOT_CONST( Type, requirement ) SW_SERVICE_SLOT_IMPL( Type, const Type, requirement )
