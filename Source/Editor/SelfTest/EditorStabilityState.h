/**
 * @file EditorStabilityState.h
 * @brief `EditorWaitStable` 의 판정입니다 — 정지했나, 아직 일이 남았나, 할 일 없이 화면이 계속 움직이나(진동).
 * @details 단계(`EditorScenarioSteps.cpp`)가 프레임마다 재료(화면이 자리 잡았나 · 할 일이 끝났나)를 넘기고 이 판정을 따른다. 테스트가 갈래를 직접 본다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 정지 대기 한 프레임의 판정입니다. */
    enum class EditorStabilityResult : uint8
    {
        Ready,       ///< 화면도 일도 멈췄다 — 놓는다
        Waiting,     ///< 아직 — 붙잡는다
        Oscillating, ///< 할 일이 없는데 화면이 `kMaxIdleMovingFrames` 넘게 이어서 움직였다 — 진동하는 배치, 실패로 놓는다
    };

    /**
     * @struct EditorStabilityState
     * @brief 정지 대기 하나의 판정 상태입니다(할 일 없이 화면만 움직인 연속 프레임 수).
     * @details 진행 중인 일에는 시한이 없다 — 그 일(파일 쓰기 · 에셋 · 씬 전환)의 시간은 Debug · 부하 · 크기에 달려 어떤 숫자도 임의다. 끝나지 않는 일은 바깥 그물(시나리오
     *          벽시계 상한 · 멈춤 감시)이 잡는다. 이 판정이 가르는 결함은 하나다: 바른 화면은 할 일이 끝나면 몇 프레임 안에 멈추고 진동은 끝나지 않으므로, 할 일 없이 움직인
     *          프레임을 센다(기계 빠르기와 무관하다). 일이 남은 프레임은 세지 않는다 — 그 움직임은 그 일 때문일 수 있다.
     */
    struct EditorStabilityState
    {
        /** @brief 할 일 없이 이만큼 이어서 움직이면 진동으로 본다(바른 도킹 · 팝업 · 자동 크기는 몇 프레임이면 멈춘다). */
        static constexpr uint32 kMaxIdleMovingFrames = 120;

        uint32 _idleMovingFrames{ 0 };

        /** @brief 이번 프레임의 재료로 판정합니다. @p bScreenStable 화면이 자리 잡았다, @p bJobDone 씬 · 에셋 · 뒤에 끝날 일이 모두 끝났다. */
        [[nodiscard]] EditorStabilityResult advance( bool bScreenStable, bool bJobDone )
        {
            if ( bScreenStable && bJobDone )
                return EditorStabilityResult::Ready;
            _idleMovingFrames = bJobDone ? _idleMovingFrames + 1 : 0;
            return _idleMovingFrames > kMaxIdleMovingFrames ? EditorStabilityResult::Oscillating : EditorStabilityResult::Waiting;
        }
    };
} // namespace sw::editor
