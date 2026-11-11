// EditorSettleState — 정지 · 기다림 · 진동 판정: 진행 중인 일에는 시한이 없고, 할 일 없이 이어서 움직인 프레임만 진동으로 센다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/SelfTest/EditorSettleState.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( EditorSettleJudgeTest, ReadyWhenScreenAndJobsSettled )
{
    EditorSettleState judge;
    SW_EXPECT_TRUE( judge.advance( true, true ) == EditorSettleResult::Ready );
}

/** @brief 일이 남은 동안은 몇 프레임이든 기다린다 — 시한이 없다(Debug 의 느린 PNG 쓰기). */
SW_TEST_CASE( EditorSettleJudgeTest, PendingJobsWaitWithoutLimit )
{
    EditorSettleState judge;
    for ( uint32 frame = 0; frame < EditorSettleState::kMaxIdleMovingFrames * 10; ++frame )
        SW_REQUIRE_TRUE( judge.advance( false, false ) == EditorSettleResult::Waiting );
    SW_EXPECT_TRUE( judge.advance( true, true ) == EditorSettleResult::Ready );
}

/** @brief 할 일이 없는데 화면이 이어서 움직이면 한도를 넘는 프레임에서 진동으로 놓는다. */
SW_TEST_CASE( EditorSettleJudgeTest, IdleMovementBeyondLimitOscillates )
{
    EditorSettleState judge;
    for ( uint32 frame = 0; frame < EditorSettleState::kMaxIdleMovingFrames; ++frame )
        SW_REQUIRE_TRUE( judge.advance( false, true ) == EditorSettleResult::Waiting );
    SW_EXPECT_TRUE( judge.advance( false, true ) == EditorSettleResult::Oscillating );
}

/** @brief 일이 끼어든 프레임은 셈을 0 으로 되돌린다 — 그 움직임은 그 일 때문일 수 있다. */
SW_TEST_CASE( EditorSettleJudgeTest, PendingJobsResetIdleMovement )
{
    EditorSettleState judge;
    for ( uint32 frame = 0; frame < EditorSettleState::kMaxIdleMovingFrames; ++frame )
        SW_REQUIRE_TRUE( judge.advance( false, true ) == EditorSettleResult::Waiting );
    SW_REQUIRE_TRUE( judge.advance( false, false ) == EditorSettleResult::Waiting );
    for ( uint32 frame = 0; frame < EditorSettleState::kMaxIdleMovingFrames; ++frame )
        SW_REQUIRE_TRUE( judge.advance( false, true ) == EditorSettleResult::Waiting );
    SW_EXPECT_TRUE( judge.advance( false, true ) == EditorSettleResult::Oscillating );
}
