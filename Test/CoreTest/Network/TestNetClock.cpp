#include "pch.h"

#include "Core/Network/Replication/NetClock.h"

#include "TestFramework/TestFramework.h"

// 서버 시계 — 지연 규칙(최소값 · 표본 간격 × 2), 처음 받은 틱에서 지연만큼 뒤에 선다, 추정은 흐르되 받은 가장 새 틱 + 지연을 넘지 않는다(렌더 틱은 받은 틱을
// 지나치지 않는다), 앞선 틱은 추정을 끌어올리고 옛 틱은 아무 일 없다, 렌더 틱은 뒤로 가지 않는다, 비우기.

using namespace sw;

namespace
{
    NetClockSettings makeClockSettings( float32 tickInterval, float32 sampleInterval )
    {
        NetClockSettings settings;
        settings._tickInterval       = tickInterval;
        settings._interpolationDelay = 0.1f;
        settings._sampleInterval     = sampleInterval;
        return settings;
    }
} // namespace

/**
 * @brief [NetClockTest] 파괴 덩어리 — 자세 10 Hz · 서버 60 Hz 면 지연 12 틱(자세 간격 × 2), 추정은 흐르는 시간으로 가고 렌더 틱은 뒤로 가지 않는다
 */
SW_TEST_CASE( NetClockTest, DrawsTwoSampleIntervalsBehindAndNeverGoesBack )
{
    NetClock clock;
    clock.initialize( makeClockSettings( 1.0f / 60.0f, 0.1f ) );
    clock.advance( 1.0f ); // 서버 틱을 받기 전 — 서 있다
    SW_EXPECT_FALSE( clock.hasServerTick() );
    SW_EXPECT_NEAR_EQUAL( NetClock::kNoRenderTick, clock.getRenderTick(), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.2f, clock.computeInterpolationDelay(), 1.0e-6f );

    clock.observeServerTick( 100 );
    SW_EXPECT_NEAR_EQUAL( 88.0f, clock.getRenderTick(), 1.0e-3f ); // 처음 받은 틱 − 12
    clock.advance( 1.0f / 60.0f );
    SW_EXPECT_NEAR_EQUAL( 101.0f, clock.getServerTick(), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 89.0f, clock.getRenderTick(), 1.0e-3f );
    clock.observeServerTick( 90 ); // 늦게 온 옛 틱 — 추정은 내려가지 않는다
    clock.advance( 1.0f / 60.0f );
    SW_EXPECT_NEAR_EQUAL( 90.0f, clock.getRenderTick(), 1.0e-3f );
    SW_EXPECT_EQUAL( 100u, clock.getNewestServerTick() );

    clock.setSampleInterval( 0.5f ); // 지연 1 초(60 틱) — 목표가 뒤로 가도 렌더 틱은 서서 기다린다
    clock.advance( 1.0f / 60.0f );
    SW_EXPECT_NEAR_EQUAL( 90.0f, clock.getRenderTick(), 1.0e-3f );
    clock.setSampleInterval( 0.0f ); // 지연 0.1 초(6 틱) — 앞으로는 바로 간다
    clock.advance( 1.0f / 60.0f );
    SW_EXPECT_NEAR_EQUAL( 98.0f, clock.getRenderTick(), 1.0e-3f ); // 104 − 6
}

/**
 * @brief [NetClockTest] 받은 틱이 끊기면 추정은 받은 가장 새 틱 + 지연에서, 렌더 틱은 받은 가장 새 틱에서 멈춘다(내다보지 않는다 — 로컬 시계가 빨라도 끝없이 앞서지 않는다)
 */
SW_TEST_CASE( NetClockTest, RenderTickStopsAtTheNewestReceivedTick )
{
    NetClock clock;
    clock.initialize( makeClockSettings( 1.0f / 30.0f, 0.0f ) ); // 지연 0.1 초 = 3 틱
    clock.observeServerTick( 100 );
    SW_EXPECT_NEAR_EQUAL( 97.0f, clock.getRenderTick(), 1.0e-3f );
    for ( int32 frame = 0; frame < 30; ++frame )
        clock.advance( 1.0f / 30.0f );
    SW_EXPECT_NEAR_EQUAL( 103.0f, clock.getServerTick(), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, clock.getRenderTick(), 1.0e-3f );
}

/**
 * @brief [NetClockTest] 추정보다 앞선 틱은 추정을 끌어올리고(렌더 틱은 다음 흐름에 따라간다), 옛 틱은 아무 일도 하지 않는다
 */
SW_TEST_CASE( NetClockTest, LaterTicksPullTheEstimateUpAndOlderTicksDoNothing )
{
    NetClock clock;
    clock.initialize( makeClockSettings( 1.0f / 30.0f, 0.0f ) );
    clock.observeServerTick( 100 );
    clock.advance( 1.0f / 30.0f );
    clock.observeServerTick( 110 );
    SW_EXPECT_NEAR_EQUAL( 110.0f, clock.getServerTick(), 1.0e-3f );
    clock.advance( 1.0f / 30.0f );
    SW_EXPECT_NEAR_EQUAL( 111.0f, clock.getServerTick(), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 108.0f, clock.getRenderTick(), 1.0e-3f );
    clock.observeServerTick( 90 );
    SW_EXPECT_NEAR_EQUAL( 111.0f, clock.getServerTick(), 1.0e-3f );
    SW_EXPECT_EQUAL( 110u, clock.getNewestServerTick() );
}

/**
 * @brief [NetClockTest] 지연은 최소값과 표본 간격 × 2 중 큰 것 — 10 Hz 서버면 스냅숏 둘(0.2 초) 뒤를 그리고, 비우면 다음 첫 틱에서 다시 선다
 */
SW_TEST_CASE( NetClockTest, DelayCoversTwoSampleIntervalsAndResetStartsOver )
{
    NetClock clock;
    clock.initialize( makeClockSettings( 0.1f, 0.1f ) );
    SW_EXPECT_NEAR_EQUAL( 0.2f, clock.computeInterpolationDelay(), 1.0e-6f );
    clock.observeServerTick( 50 );
    SW_EXPECT_NEAR_EQUAL( 48.0f, clock.getRenderTick(), 1.0e-3f );
    clock.reset();
    SW_EXPECT_FALSE( clock.hasServerTick() );
    SW_EXPECT_NEAR_EQUAL( NetClock::kNoRenderTick, clock.getRenderTick(), 1.0e-6f );
    clock.observeServerTick( 5 ); // 새 서버 — 옛 것보다 작은 틱에서 다시
    SW_EXPECT_NEAR_EQUAL( 3.0f, clock.getRenderTick(), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, clock.getServerTick(), 1.0e-6f );
}
