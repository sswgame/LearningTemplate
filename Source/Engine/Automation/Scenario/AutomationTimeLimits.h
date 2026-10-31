/**
 * @file AutomationTimeLimits.h
 * @brief 시나리오 실행의 벽시계 시한들과 그 대소 관계입니다 — 서로 겹치지 않게 한 곳에서 못 박습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct AutomationTimeLimits
     * @brief 감시 < 감시 + 유예 < 시계 보류 < 프로세스 시한 순서의 시한(초)입니다.
     * @details 시나리오가 끝나지 않을 때 보고를 남기는 쪽이 항상 먼저 닿아야 한다. 프로세스 시한이 먼저 죽이면 보고가 없다.
     *          - 감시(`kDefaultWallTimeoutSeconds`): 실행기가 프레임 앞뒤에서 보는 시나리오 한 편의 벽시계 상한(시나리오가 `wallTimeoutSeconds` 로 바꿀 수 있다).
     *          - 유예(`kHangGraceSeconds`): 감시 시한이 지났는데도 게임 스레드가 끝맺지 못하면(프레임이 멈춘 행) 감시 스레드가 이만큼 더 기다린 뒤 보고를 대신 쓴다.
     *          - 시계 보류(`kClockHoldTimeoutSeconds`): 시계 잡기 조건 하나가 이어서 시계를 붙잡을 수 있는 시간.
     *          - 프로세스 시한(`kProcessKillSeconds`): 시나리오 행렬(`AutomationBackendMatrix`)이 끝나지 않은 자식 App 을 죽이는 시한. CTest 시한(600)은 그보다 길다.
     */
    struct AutomationTimeLimits
    {
        static constexpr float32 kDefaultWallTimeoutSeconds = 150.0f;
        static constexpr float32 kHangGraceSeconds          = 5.0f;
        static constexpr float64 kClockHoldTimeoutSeconds   = 170.0;
        static constexpr uint32  kProcessKillSeconds        = 180;

        /** @brief `CloseWindow` 의 기본 종료 시한(초)입니다. */
        static constexpr float32 kDefaultExitWithinSeconds = 10.0f;
        /** @brief 부하 시나리오(`<Load>`)의 멈춤 그물 여유(초)입니다 — 그물은 측정 창 길이의 두 배에 이것을 더한다. 워밍업(60 프레임)과 느린 기계를 넉넉히 덮는다. */
        static constexpr float64 kLoadDeadlineSlackSeconds = 120.0;
        /** @brief `WaitUntil` 의 기본 시한(프레임)입니다. 부하가 걸린 기계에서 에디터가 에셋을 여는 데 드는 시간보다 넉넉하게 잡습니다. */
        static constexpr uint32 kDefaultWaitTimeoutFrames = 1200;
        /**
         * @brief 스크린샷이 에셋 일(읽는 중 · 나타나는 중인 메시, 올리기를 기다리는 텍스처)을 기다리는 최대 프레임 수입니다. 넘기면 실패로 적고 그대로 찍는다.
         * @details 고정 프레임 시간(60 Hz)에서 10 초다 — 첫 프레임에 큰 씬의 텍스처 수백 장이 워커에 줄을 서도 넉넉하다.
         */
        static constexpr uint32 kMaxAssetSettleFrames = 600;
        /** @brief `<Pass/>` 가 뒤의 일(`SW_AUTOMATION_FINISH_BLOCKER`)을 기다리는 최대 프레임 수입니다. 60 Hz 에서 10 초 - 캡처 수십 장의 파일 쓰기가 워커에 줄을 서도 넉넉하다. */
        static constexpr uint32 kMaxFinishWaitFrames = 600;
        /** @brief 체인(`-scenario=a,b,c`)의 단계 하나가 프로세스 시한에 더하는 몫(초)입니다. 에디터 체인(125 단계, Debug)은 혼자 돌아도 2 분 남짓이다. */
        static constexpr uint32 kChainStageSeconds = 5;

        /**
         * @brief 단계가 @p stageCount 개인 체인의 프로세스 시한(초)입니다 — 단일 시나리오 시한에 단계 몫을 **더한다**.
         * @details 앞 단계들이 쓴 시간 뒤에 마지막 단계가 행에 걸려도 그 단계의 감시 + 유예가 프로세스 시한보다 먼저 닿아야 보고가 남는다(옛 `max(단계 몫 합, 단일 시한)` 은 짧은 체인에서 못 미쳤다).
         */
        static constexpr uint32 getChainKillSeconds( uint32 stageCount ) { return stageCount * kChainStageSeconds + kProcessKillSeconds; }

        static_assert( static_cast<float64>( kDefaultExitWithinSeconds ) < static_cast<float64>( kDefaultWallTimeoutSeconds ), "the default exit deadline must come before the wall watch" );
        static_assert( static_cast<float64>( kDefaultWallTimeoutSeconds ) + static_cast<float64>( kHangGraceSeconds ) < kClockHoldTimeoutSeconds,
                       "wall watch + hang grace must come before the clock-hold timeout" );
        static_assert( kClockHoldTimeoutSeconds < static_cast<float64>( kProcessKillSeconds ), "the clock-hold timeout must come before the process kill" );
    };

    // 멤버 함수는 클래스 안에서 상수식에 쓸 수 없어 아래에서 묶는다.
    static_assert( AutomationTimeLimits::getChainKillSeconds( 1 ) > AutomationTimeLimits::kProcessKillSeconds, "a chain gets the single-scenario deadline plus its stage shares" );
    static_assert( static_cast<float64>( AutomationTimeLimits::getChainKillSeconds( 1 ) ) >
                       static_cast<float64>( AutomationTimeLimits::kDefaultWallTimeoutSeconds ) + static_cast<float64>( AutomationTimeLimits::kHangGraceSeconds ),
                   "even a one-stage chain must outlast the wall watch + hang grace" );
} // namespace sw
