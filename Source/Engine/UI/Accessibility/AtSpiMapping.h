/**
 * @file AtSpiMapping.h
 * @brief 접근성 역할 · 상태를 Linux AT-SPI(`org.a11y.atspi`)의 번호로 바꾸는 순수 규칙입니다.
 * @details D-Bus 에 닿지 않는 부분이라 모든 플랫폼에서 지어 단위 테스트가 돈다(리눅스 브리지 `AtSpiAccessibilityBridge` 가 쓴다). 번호의 정본은 at-spi2-core 의
 *          `atspi-constants.h` 의 `AtspiRole` · `AtspiStateType` 이다 — 값을 바꾸지 않는다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/UI/Accessibility/UIAccessibilityTypes.h"

namespace sw
{
    /** @brief AT-SPI 상태 집합(`GetState` 의 `au` — 32 비트 낱말 둘, 상태 번호 n 은 낱말 n/32 의 비트 n%32)입니다. */
    struct AtSpiStateSet
    {
        uint32 _word[2]{ 0, 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief AT-SPI 가 노드에 붙는 인터페이스 묶음입니다(`GetInterfaces`). 비트로 고른다. */
    namespace AtSpiInterface
    {
        inline constexpr uint32 kComponent = 1u << 0; ///< org.a11y.atspi.Component — 화면 사각형
        inline constexpr uint32 kAction    = 1u << 1; ///< org.a11y.atspi.Action — 누르기
        inline constexpr uint32 kValue     = 1u << 2; ///< org.a11y.atspi.Value — 범위 값
    } // namespace AtSpiInterface

    /**
     * @struct AtSpiMapping
     * @brief 역할 · 상태 → AT-SPI 번호입니다.
     */
    struct SW_API AtSpiMapping
    {
        /** @brief `AtspiRole` 번호입니다(`None` 은 `ATSPI_ROLE_INVALID` 0). */
        [[nodiscard]] static uint32 getRole( AccessibleRole role );
        /** @brief 사람이 읽는 영문 역할 이름입니다(`GetRoleName`, AT-SPI 의 소문자 이름). */
        [[nodiscard]] static const utf8* getRoleName( AccessibleRole role );
        /** @brief `AccessibleState` 비트와 역할로 AT-SPI 상태 집합을 만듭니다. 접근성 트리에는 보이는 위젯만 들어오므로 VISIBLE · SHOWING 은 늘 켠다. */
        [[nodiscard]] static AtSpiStateSet makeStateSet( AccessibleRole role, uint32 states );
        /** @brief 역할이 노출하는 인터페이스(`AtSpiInterface` 비트)입니다. */
        [[nodiscard]] static uint32 getInterfaces( AccessibleRole role );
    };
} // namespace sw
