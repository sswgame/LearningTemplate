/**
 * @file Core/Subsystem/ServiceSlot.h
 * @brief 타입 하나가 가진 프로세스 범위의 서비스 칸입니다 — 바인딩이 찾은 객체 주소를 두고, 창구(`engine::getSubsystem<T>()`)가 이것만 읽습니다.
 *
 * @details 언리얼 `GEngine->GetEngineSubsystem<T>()` 처럼 타입으로 찾되, 부를 때마다 표를 찾지 않도록 타입마다 칸 하나를 둡니다.
 *          - **선언**: 타입의 헤더에서 클래스 정의 바로 뒤(namespace `sw` 안)에 `SW_SERVICE_SLOT( Type, Required );` 한 줄. 필수 여부와
 *            const 여부는 부르는 쪽 TU 가 컴파일 때 알아야 하므로(비어 있을 수 있는 칸에 `getSubsystem` 을 쓰면 컴파일 오류) 헤더에 둔다.
 *          - **정의와 등록**: 그 객체를 범위에 넣는 .cpp 의 등록 줄(`SW_ENGINE_SERVICE*`, `Engine/Foundation/EngineSubsystemRegistration.h`)이
 *            칸을 정의하고 엔진의 등록 목록에 단다. 중앙 목록은 없다 — 줄이 둘이면 링크 오류(칸 정의가 둘), 줄이 없으면 그 칸을 읽는 쪽이 링크 오류다.
 *          칸은 특수화의 정적 데이터 멤버라 Engine 이미지에 하나뿐이다(`SW_API`). 헤더 템플릿의 정적 데이터(인라인 변수)는 Windows 에서 DLL 마다
 *          따로 생겨 칸으로 쓰지 않는다. Core 는 칸의 모양만 안다 — 무엇을 묶고 누구에게 보이는지는 엔진의 등록이 정한다.
 *          칸을 선언하지 않은 타입은 기본 템플릿(`kRegistered == false`)이고, 창구는 범위를 올라가며 찾는다(`engine::findSubsystem<T>()`).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

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

    /** @brief 타입 @p T 의 서비스 칸입니다. 칸을 선언한 타입만 특수화가 있습니다(`SW_SERVICE_SLOT`). */
    template <class T>
    struct ServiceSlotOf
    {
        static constexpr bool kRegistered = false;
        using Access                      = T;
    };
} // namespace sw

// 인자가 **타입 이름**이라 괄호를 씌울 수 없다(`struct ServiceSlotOf<(Type)>` 는 올바른 문법이 아니다).
// NOLINTBEGIN(bugprone-macro-parentheses)
#define SW_SERVICE_SLOT_IMPL( Type, AccessType, requirement )                               \
    template <>                                                                             \
    struct ServiceSlotOf<Type>                                                              \
    {                                                                                       \
        static constexpr bool               kRegistered  = true;                            \
        static constexpr ServiceRequirement kRequirement = ServiceRequirement::requirement; \
        static constexpr const utf8*        kTypeName    = #Type;                           \
        using Access                                     = AccessType;                      \
        SW_API static ServiceSlot s_slot; /* 정의는 등록 줄(`SW_ENGINE_SERVICE*`) */        \
    }
// NOLINTEND(bugprone-macro-parentheses)

/** @brief 타입 @p Type 의 서비스 칸을 선언합니다. @p requirement 는 `Required` · `Optional` 입니다. namespace `sw` 안, 클래스 정의 뒤에 둡니다. */
#define SW_SERVICE_SLOT( Type, requirement ) SW_SERVICE_SLOT_IMPL( Type, Type, requirement )
/** @brief 위와 같고, 창구가 const 로 돌려줍니다(읽기 전용 값 묶음 — 설정 · 기본 에셋 표). */
#define SW_SERVICE_SLOT_CONST( Type, requirement ) SW_SERVICE_SLOT_IMPL( Type, const Type, requirement )
