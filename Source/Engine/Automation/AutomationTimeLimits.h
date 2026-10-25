/**
 * @file AutomationTimeLimits.h
 * @brief 시나리오 실행의 벽시계 시한들과 그 대소 관계입니다 — 서로 겹치지 않게 한 곳에서 못 박습니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct AutomationTimeLimits
     * @brief 감시 < 감시 + 유예 < 시계 보류 < 프로세스 시한 순서의 시한(초)입니다.
     * @details 시나리오가 끝나지 않을 때 보고를 남기는 쪽이 항상 먼저 닿아야 한다. 프로세스 시한이 먼저 죽이면 보고가 없다.
     *          - 감시(`kDefaultWallTimeoutSeconds`): 실행기가 프레임 앞뒤에서 보는 시나리오 한 편의 벽시계 상한(시나리오가 `wallTimeoutSeconds` 로 바꿀 수 있다).
     *          - 유예(`kHangGraceSeconds`): 감시 시한이 지났는데도 게임 스레드가 끝맺지 못하면(프레임이 멈춘 행) 감시 스레드가 이만큼 더 기다린 뒤 보고를 대신 쓴다.
     *          - 시계 보류(`kClockHoldTimeoutSeconds`): 시계 잡기 조건 하나가 이어서 시계를 붙잡을 수 있는 시간.
     *          - 프로세스 시한(`kProcessKillSeconds`): 테스트 하네스가 시나리오 프로세스를 죽이는 시한(`AppTestUtil::kScenarioTimeoutSeconds` 가 이 값이다).
     */
    struct AutomationTimeLimits
    {
        static constexpr float32 kDefaultWallTimeoutSeconds = 150.0f;
        static constexpr float32 kHangGraceSeconds          = 5.0f;
        static constexpr float64 kClockHoldTimeoutSeconds   = 170.0;
        static constexpr uint32  kProcessKillSeconds        = 180;

        static_assert( static_cast<float64>( kDefaultWallTimeoutSeconds ) + static_cast<float64>( kHangGraceSeconds ) < kClockHoldTimeoutSeconds,
                       "wall watch + hang grace must come before the clock-hold timeout" );
        static_assert( kClockHoldTimeoutSeconds < static_cast<float64>( kProcessKillSeconds ), "the clock-hold timeout must come before the process kill" );
    };
} // namespace sw
