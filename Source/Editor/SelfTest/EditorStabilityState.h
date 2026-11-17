/**
 * @file EditorStabilityState.h
 * @brief `EditorWaitStable` 의 판정입니다 — 정지했나(`AutomationReadiness::ready`), 아직 일이 남았나(진척이 는다), 할 일 없이 화면이 계속 움직이나(진척 없음 — 진동).
 * @details 단계(`EditorScenarioSteps.cpp`)가 프레임마다 재료(화면이 자리 잡았나 · 할 일이 끝났나)를 넘기고 이 판정을 따른다. 테스트가 갈래를 직접 본다.
 *          시나리오 대기와 같은 형식이다(`AutomationReadiness` 의 진척 수 + `AutomationStallClock`) — 예전에는 따로 센 bool 판정(`_idleMovingFrames`)이라 진척을 보고하지 못했다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Automation/Scenario/AutomationReadiness.h"

namespace sw::editor
{
    /**
     * @class EditorStabilityState
     * @brief 정지 대기 하나의 판정 상태입니다(진척 수 = 일이 남은 채 지난 프레임 수, 정지 시계 = 그 수가 그대로인 이어진 프레임 수).
     * @details 진행 중인 일에는 시한이 없다 — 그 일(파일 쓰기 · 에셋 · 씬 전환)의 시간은 Debug · 부하 · 크기에 달려 어떤 숫자도 임의다. 그래서 일이 남은 프레임마다 진척이 하나 늘어
     *          정지 시계가 0 으로 돌아간다. 끝나지 않는 일은 바깥 그물(시나리오 벽시계 상한 · 멈춤 감시)이 잡는다. 이 판정이 가르는 결함은 하나다: 바른 화면은 할 일이 끝나면
     *          몇 프레임 안에 멈추고 진동은 끝나지 않으므로, 할 일 없이 움직인 프레임(진척 없음)을 센다(기계 빠르기와 무관하다).
     *          정지 한도는 `ScenarioRun.json` 의 `_stallFrames` 가 아니라 `kMaxIdleMovingFrames` 다 — 자체 테스트(`EditorSelfTestStep::WaitStable`) · UI 캡처도 같은 판정을 쓰고 실행기 정책을 읽지 않는다.
     */
    class EditorStabilityState
    {
    public:
        /** @brief 할 일 없이 이만큼 넘게 이어서 움직이면 진동으로 본다(바른 도킹 · 팝업 · 자동 크기는 몇 프레임이면 멈춘다). */
        static constexpr uint32 kMaxIdleMovingFrames = 120;
        /** @brief 정지 시계의 조건 이름이자 반복 실패 차단 열쇠의 꼬리입니다. */
        static constexpr const utf8* kConditionName = "EditorScreenOscillation";

        EditorStabilityState()
        {
            (void)_stallClock.advance( kConditionName, 0 ); // 처음부터 할 일 없이 움직인 프레임도 1 부터 센다
        }

        /**
         * @brief 이번 프레임의 재료로 판정합니다. @p bScreenStable 화면이 자리 잡았다, @p bJobDone 씬 · 에셋 · 뒤에 끝날 일이 모두 끝났다.
         * @return 둘 다면 준비됨, 아니면 진행 중(진척 = 일이 남은 채 지난 프레임 수). 진행 중이어도 `isStalled` 면 포기한다
         */
        [[nodiscard]] AutomationReadiness advance( bool bScreenStable, bool bJobDone )
        {
            if ( bScreenStable && bJobDone )
                return AutomationReadiness::ready();
            if ( bJobDone == false )
                ++_jobFrameCount;
            (void)_stallClock.advance( kConditionName, _jobFrameCount );
            return AutomationReadiness::waiting( _jobFrameCount, bJobDone ? "the screen to stop moving (no pending work)" : "pending work to finish" );
        }

        /** @brief 할 일 없이 `kMaxIdleMovingFrames` 넘게 이어서 움직였으면 true 입니다 — 진동하는 배치, 그 대기는 `WaitStalled` 로 포기한다. */
        [[nodiscard]] bool isStalled() const { return _stallClock.getUnchangedCount() > kMaxIdleMovingFrames; }
        /** @brief 할 일 없이 이어서 움직인 프레임 수입니다(진척이 그대로인 수). */
        [[nodiscard]] uint32 getIdleMovingFrames() const { return _stallClock.getUnchangedCount(); }

    private:
        uint64               _jobFrameCount{ 0 };
        AutomationStallClock _stallClock;
    };
} // namespace sw::editor
