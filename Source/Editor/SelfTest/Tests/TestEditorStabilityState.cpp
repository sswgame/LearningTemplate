// EditorStabilityState — 정지 · 기다림 · 진동 판정: 진행 중인 일은 진척이 늘어 시한이 없고, 할 일 없이 이어서 움직인 프레임(진척 없음)만 진동으로 센다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/SelfTest/EditorStabilityState.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( EditorStabilityStateTest, ReadyWhenScreenAndJobsStable )
{
    EditorStabilityState stability;
    SW_EXPECT_FALSE( stability.advance( true, true ).isWaiting() );
    SW_EXPECT_FALSE( stability.isStalled() );
}

/** @brief 일이 남은 동안은 몇 프레임이든 기다린다 — 프레임마다 진척이 늘어 정지하지 않는다(Debug 의 느린 PNG 쓰기). */
SW_TEST_CASE( EditorStabilityStateTest, PendingJobsWaitWithoutLimit )
{
    EditorStabilityState stability;
    for ( uint32 frame = 0; frame < EditorStabilityState::kMaxIdleMovingFrames * 10; ++frame )
    {
        const AutomationReadiness readiness = stability.advance( false, false );
        SW_REQUIRE_TRUE( readiness.isWaiting() );
        SW_REQUIRE_EQUAL( static_cast<uint64>( frame + 1 ), readiness.getProgress() );
        SW_REQUIRE_FALSE( stability.isStalled() );
    }
    SW_EXPECT_FALSE( stability.advance( true, true ).isWaiting() );
}

/** @brief 할 일이 없는데 화면이 이어서 움직이면 진척이 그대로라 한도를 넘는 프레임에서 정지(진동)로 포기한다 — 옛 판정과 같은 프레임(121 번째). */
SW_TEST_CASE( EditorStabilityStateTest, IdleMovementBeyondLimitStalls )
{
    EditorStabilityState stability;
    for ( uint32 frame = 0; frame < EditorStabilityState::kMaxIdleMovingFrames; ++frame )
    {
        const AutomationReadiness readiness = stability.advance( false, true );
        SW_REQUIRE_TRUE( readiness.isWaiting() );
        SW_REQUIRE_EQUAL( static_cast<uint64>( 0 ), readiness.getProgress() );
        SW_REQUIRE_FALSE( stability.isStalled() );
    }
    SW_EXPECT_TRUE( stability.advance( false, true ).isWaiting() );
    SW_EXPECT_TRUE( stability.isStalled() );
    SW_EXPECT_EQUAL( EditorStabilityState::kMaxIdleMovingFrames + 1, stability.getIdleMovingFrames() );
}

/** @brief 일이 끼어든 프레임은 진척을 늘려 셈을 0 으로 되돌린다 — 그 움직임은 그 일 때문일 수 있다. */
SW_TEST_CASE( EditorStabilityStateTest, PendingJobsResetIdleMovement )
{
    EditorStabilityState stability;
    for ( uint32 frame = 0; frame < EditorStabilityState::kMaxIdleMovingFrames; ++frame )
        (void)stability.advance( false, true );
    SW_REQUIRE_FALSE( stability.isStalled() );
    (void)stability.advance( false, false );
    SW_REQUIRE_EQUAL( 0u, stability.getIdleMovingFrames() );
    for ( uint32 frame = 0; frame < EditorStabilityState::kMaxIdleMovingFrames; ++frame )
        (void)stability.advance( false, true );
    SW_REQUIRE_FALSE( stability.isStalled() );
    (void)stability.advance( false, true );
    SW_EXPECT_TRUE( stability.isStalled() );
}
