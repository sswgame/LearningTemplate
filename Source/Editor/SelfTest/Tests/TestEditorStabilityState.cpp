// EditorStabilityState — 정지 · 기다림 · 진동 판정: 진행 중인 일에는 시한이 없고, 할 일 없이 이어서 움직인 프레임만 진동으로 센다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/SelfTest/EditorStabilityState.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( EditorStabilityStateTest, ReadyWhenScreenAndJobsStable )
{
    EditorStabilityState stability;
    SW_EXPECT_TRUE( stability.advance( true, true ) == EditorStabilityResult::Ready );
}

/** @brief 일이 남은 동안은 몇 프레임이든 기다린다 — 시한이 없다(Debug 의 느린 PNG 쓰기). */
SW_TEST_CASE( EditorStabilityStateTest, PendingJobsWaitWithoutLimit )
{
    EditorStabilityState stability;
    for ( uint32 frame = 0; frame < EditorStabilityState::kMaxIdleMovingFrames * 10; ++frame )
        SW_REQUIRE_TRUE( stability.advance( false, false ) == EditorStabilityResult::Waiting );
    SW_EXPECT_TRUE( stability.advance( true, true ) == EditorStabilityResult::Ready );
}

/** @brief 할 일이 없는데 화면이 이어서 움직이면 한도를 넘는 프레임에서 진동으로 놓는다. */
SW_TEST_CASE( EditorStabilityStateTest, IdleMovementBeyondLimitOscillates )
{
    EditorStabilityState stability;
    for ( uint32 frame = 0; frame < EditorStabilityState::kMaxIdleMovingFrames; ++frame )
        SW_REQUIRE_TRUE( stability.advance( false, true ) == EditorStabilityResult::Waiting );
    SW_EXPECT_TRUE( stability.advance( false, true ) == EditorStabilityResult::Oscillating );
}

/** @brief 일이 끼어든 프레임은 셈을 0 으로 되돌린다 — 그 움직임은 그 일 때문일 수 있다. */
SW_TEST_CASE( EditorStabilityStateTest, PendingJobsResetIdleMovement )
{
    EditorStabilityState stability;
    for ( uint32 frame = 0; frame < EditorStabilityState::kMaxIdleMovingFrames; ++frame )
        SW_REQUIRE_TRUE( stability.advance( false, true ) == EditorStabilityResult::Waiting );
    SW_REQUIRE_TRUE( stability.advance( false, false ) == EditorStabilityResult::Waiting );
    for ( uint32 frame = 0; frame < EditorStabilityState::kMaxIdleMovingFrames; ++frame )
        SW_REQUIRE_TRUE( stability.advance( false, true ) == EditorStabilityResult::Waiting );
    SW_EXPECT_TRUE( stability.advance( false, true ) == EditorStabilityResult::Oscillating );
}
