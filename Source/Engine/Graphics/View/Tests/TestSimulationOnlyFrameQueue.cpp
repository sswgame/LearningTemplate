#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/View/SimulationOnlyFrameQueue.h"

// 그리지 않고 진행하는 고정 걸음 — 고정 프레임 시간이 있어야 받고, 요청한 수만큼만 건너뛰며, 건너뛴 뒤 그린 프레임이 따라잡을 때까지 캡처를 막는다.

using namespace sw;

namespace
{
    constexpr float32 kFixedDelta = 1.0f / 60.0f;
} // namespace

/** @brief [SimulationOnlyFrameQueueTest] 벽시계 실행(고정 프레임 시간 0)은 거절한다 — 그리기를 건너뛰면 프레임 시간이 바뀌어 결과가 달라진다 */
SW_TEST_CASE( SimulationOnlyFrameQueueTest, RefusesWallClockRuns )
{
    SimulationOnlyFrameQueue frames;
    SW_EXPECT_FALSE( frames.request( 10, 0.0f ) );
    SW_EXPECT_EQUAL( 0u, frames.getRemainingCount() );
    SW_EXPECT_TRUE( frames.beginFrame( 0.0f ) == FrameDrawMode::Draw );
}

/** @brief [SimulationOnlyFrameQueueTest] 요청한 N 프레임만 그리지 않고, 그 뒤는 그린다 — 시간 기록이 프레임 종류를 가른다 */
SW_TEST_CASE( SimulationOnlyFrameQueueTest, SkipsExactlyTheRequestedFrames )
{
    SimulationOnlyFrameQueue frames;
    SW_REQUIRE_TRUE( frames.request( 3, kFixedDelta ) );
    for ( uint32 frame = 0; frame < 3u; ++frame )
    {
        SW_EXPECT_TRUE( frames.beginFrame( kFixedDelta ) == FrameDrawMode::SimulationOnly );
        SW_EXPECT_TRUE( frames.wasLastFrameSimulationOnly() );
    }
    SW_EXPECT_TRUE( frames.beginFrame( kFixedDelta ) == FrameDrawMode::Draw );
    SW_EXPECT_FALSE( frames.wasLastFrameSimulationOnly() );
    SW_EXPECT_EQUAL( uint64( 3 ), frames.getLastSkippedSerial() );
}

/** @brief [SimulationOnlyFrameQueueTest] 재생 중 고정 프레임 시간이 풀리면 남은 요청을 버리고 그린다(벽시계로 건너뛰지 않는다) */
SW_TEST_CASE( SimulationOnlyFrameQueueTest, DropsTheRestWhenTheFixedDeltaIsReleased )
{
    SimulationOnlyFrameQueue frames;
    SW_REQUIRE_TRUE( frames.request( 5, kFixedDelta ) );
    SW_EXPECT_TRUE( frames.beginFrame( kFixedDelta ) == FrameDrawMode::SimulationOnly );
    SW_EXPECT_TRUE( frames.beginFrame( 0.0f ) == FrameDrawMode::Draw );
    SW_EXPECT_EQUAL( 0u, frames.getRemainingCount() );
}

/** @brief [SimulationOnlyFrameQueueTest] 건너뛴 뒤 그린 프레임이 GPU 입자를 다 따라잡아야 캡처가 풀리고, 건너뛰기 전에 지은 패킷은 뒤의 건너뛰기를 풀지 못한다 */
SW_TEST_CASE( SimulationOnlyFrameQueueTest, CatchUpGatesCaptureUntilADrawnFrameCompletes )
{
    SimulationOnlyFrameQueue frames;
    SW_EXPECT_FALSE( frames.isCatchUpPending() );
    SW_REQUIRE_TRUE( frames.request( 2, kFixedDelta ) );
    (void)frames.beginFrame( kFixedDelta );
    (void)frames.beginFrame( kFixedDelta );
    SW_EXPECT_TRUE( frames.isCatchUpPending() );

    frames.reportDrawnFrame( 0, 0 ); // 건너뛰기 전에 지은 패킷 — 풀지 못한다
    SW_EXPECT_TRUE( frames.isCatchUpPending() );
    frames.reportDrawnFrame( frames.getLastSkippedSerial(), 40 ); // 그렸지만 입자가 40 단계 밀렸다
    SW_EXPECT_TRUE( frames.isCatchUpPending() );
    frames.reportDrawnFrame( frames.getLastSkippedSerial(), 0 );
    SW_EXPECT_FALSE( frames.isCatchUpPending() );
}
