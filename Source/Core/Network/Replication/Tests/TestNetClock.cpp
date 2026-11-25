#include "pch.h"

#include "Core/Network/Replication/NetClock.h"
#include "Core/Testing/TestFramework.h"

// 서버 시계: 서버는 흐른 시간이 시각이고 클라이언트는 받은 틱을 하한으로 흐르되 정한 만큼까지만 앞선다. 화면 시계: 지연 규칙(최소값 · 표본 간격 × 2), 받은 표본을 지나치지 않음, 되돌아가지 않음, 비우기.

using namespace sw;

namespace
{
    constexpr float32 kClockFrame = NetClock::kTickSeconds;

    NetRenderClock makeRenderClock( uint32 stride, float32 sampleInterval )
    {
        NetRenderClockConfig settings;
        settings._stride             = stride;
        settings._interpolationDelay = 0.1f;
        settings._sampleInterval     = sampleInterval;
        NetRenderClock render;
        render.initialize( settings );
        return render;
    }
} // namespace

/** @brief [NetClockTest] 결함: 지연이 늘어난 뒤에도 짧던 때의 추정을 이어 가, 왕복의 절반을 더하면 지연을 두 번 셌다 — 받은 시각은 지금 지연만큼 뒤다 */
SW_TEST_CASE( NetClockTest, ReceivedTickFollowsTheCurrentLatency )
{
    NetClock clock;
    clock.reset( NetClockSide::Replica );
    uint32 serverTick = 100;
    for ( uint32 frame = 0; frame < 60; ++frame ) // 지연 없음
    {
        ++serverTick;
        clock.observe( serverTick );
        clock.advance( kClockFrame );
    }
    SW_EXPECT_NEAR( static_cast<float64>( serverTick ) + 1.0, clock.getReceivedTick(), 1.0e-6 );
    constexpr uint32 kDelayTicks = 12;
    for ( uint32 frame = 0; frame < 60; ++frame ) // 한쪽 지연 12 틱
    {
        ++serverTick;
        if ( frame >= kDelayTicks )
            clock.observe( serverTick - kDelayTicks );
        clock.advance( kClockFrame );
    }
    SW_EXPECT_NEAR( static_cast<float64>( serverTick - kDelayTicks ) + 1.0, clock.getReceivedTick(), 1.0e-6 );
    SW_EXPECT_TRUE( clock.getServerTick() > clock.getReceivedTick() + 10.0 ); // 되돌아가지 않는 추정은 앞선 채다
}

/** @brief [NetClockTest] 서버의 시각은 흐른 시간이다 — 프레임 길이가 달라도 같은 수열이고, 간격이 긴 기능은 그 배수에서 돈다. 멈춘 긴 프레임은 상한까지만 흐른다 */
SW_TEST_CASE( NetClockTest, AuthorityCountsElapsedTimeOnOneSequence )
{
    NetClock clock;
    clock.reset( NetClockSide::Authority );
    SW_EXPECT_TRUE( clock.hasServerTick() );
    SW_EXPECT_EQUAL( 0u, clock.getWholeTick() );
    for ( int32 frame = 0; frame < 30; ++frame )
    {
        clock.advance( 1.0f / 30.0f );
    }
    SW_EXPECT_EQUAL( 60u, clock.getWholeTick() );
    SW_EXPECT_EQUAL( 30u, clock.getWholeTick( NetClock::computeStride( 1.0f / 30.0f ).value_or( 1u ) ) );
    clock.observe( 5000 ); // 서버의 시각은 메시지가 정하지 않는다
    SW_EXPECT_EQUAL( 60u, clock.getWholeTick() );
    clock.advance( 10.0f );
    SW_EXPECT_EQUAL( 60u + NetClock::kMaxTicksPerAdvance, clock.getWholeTick() );
}

/** @brief [NetClockTest] 서버 틱의 정수 배수가 아닌 간격은 이 수열 위에서 돌 수 없다 */
SW_TEST_CASE( NetClockTest, OnlyMultiplesOfTheTickHaveAStride )
{
    SW_EXPECT_EQUAL( 1u, NetClock::computeStride( 1.0f / 60.0f ).value_or( 0u ) );
    SW_EXPECT_EQUAL( 2u, NetClock::computeStride( 1.0f / 30.0f ).value_or( 0u ) );
    SW_EXPECT_EQUAL( 6u, NetClock::computeStride( 0.1f ).value_or( 0u ) );
    SW_EXPECT_FALSE( NetClock::computeStride( 1.0f / 25.0f ).has_value() );
    SW_EXPECT_FALSE( NetClock::computeStride( 1.0f / 120.0f ).has_value() );
}

/** @brief [NetClockTest] 클라이언트의 추정은 받은 틱을 하한으로 흐르고, 앞선 틱은 끌어올리고 옛 틱은 아무 일도 없으며, 받은 틱이 끊기면 정한 만큼까지만 앞선다 */
SW_TEST_CASE( NetClockTest, ReplicaFollowsTheNewestReceivedTick )
{
    NetClock clock;
    clock.reset( NetClockSide::Replica );
    clock.advance( 1.0f ); // 서버 틱을 받기 전 — 서 있다
    SW_EXPECT_FALSE( clock.hasServerTick() );
    clock.observe( 100 );
    SW_EXPECT_NEAR( 100.0, clock.getServerTick(), 1.0e-6 );
    clock.advance( kClockFrame );
    SW_EXPECT_NEAR( 101.0, clock.getServerTick(), 1.0e-3 );
    clock.observe( 90 );
    SW_EXPECT_NEAR( 101.0, clock.getServerTick(), 1.0e-3 );
    clock.observe( 110 );
    SW_EXPECT_NEAR( 110.0, clock.getServerTick(), 1.0e-6 );
    for ( int32 frame = 0; frame < 120; ++frame )
    {
        clock.advance( kClockFrame );
    }
    SW_EXPECT_NEAR( 110.0 + static_cast<float64>( NetClock::kMaxLeadSeconds / NetClock::kTickSeconds ), clock.getServerTick(), 1.0e-3 );
    clock.reset( NetClockSide::Replica );
    SW_EXPECT_FALSE( clock.hasServerTick() );
    clock.observe( 5 ); // 새 서버 — 옛 것보다 작은 틱에서 다시
    SW_EXPECT_NEAR( 5.0, clock.getServerTick(), 1.0e-6 );
}

/** @brief [NetClockTest] 파괴 덩어리 — 자세 10 Hz 면 지연 12 틱(자세 간격 × 2), 화면 틱은 뒤로 가지 않는다 */
SW_TEST_CASE( NetClockTest, RenderTickDrawsTwoSampleIntervalsBehindAndNeverGoesBack )
{
    NetClock clock;
    clock.reset( NetClockSide::Replica );
    NetRenderClock render = makeRenderClock( 1, 0.1f );
    render.follow( clock );
    SW_EXPECT_NEAR( NetRenderClock::kNoRenderTick, render.getRenderTick(), 1.0e-6 );
    SW_EXPECT_NEAR( 0.2f, render.computeInterpolationDelay(), 1.0e-6f );

    clock.observe( 100 );
    render.observeSample( 100 );
    render.follow( clock );
    SW_EXPECT_NEAR( 88.0, render.getRenderTick(), 1.0e-3 );
    clock.advance( kClockFrame );
    render.follow( clock );
    SW_EXPECT_NEAR( 89.0, render.getRenderTick(), 1.0e-3 );

    render.setSampleInterval( 0.5f ); // 지연 1 초(60 틱) — 목표가 뒤로 가도 화면 틱은 서서 기다린다
    clock.advance( kClockFrame );
    render.follow( clock );
    SW_EXPECT_NEAR( 89.0, render.getRenderTick(), 1.0e-3 );
    render.setSampleInterval( 0.0f ); // 지연 0.1 초(6 틱) — 앞으로는 바로 간다
    render.follow( clock );
    SW_EXPECT_NEAR( 96.0, render.getRenderTick(), 1.0e-3 );
    render.reset();
    SW_EXPECT_FALSE( render.hasSample() );
    SW_EXPECT_NEAR( NetRenderClock::kNoRenderTick, render.getRenderTick(), 1.0e-6 );
}

/** @brief [NetClockTest] 두 기능이 한 시계를 본다 — 간격이 달라도 같은 순간을 그리고, 한쪽의 표본이 끊겨도 그 기능만 받은 표본에서 멈춘다 */
SW_TEST_CASE( NetClockTest, FeaturesOfOneWorldShareTheClock )
{
    NetClock clock;
    clock.reset( NetClockSide::Replica );
    NetRenderClock movement = makeRenderClock( 2, 1.0f / 30.0f ); // 30 Hz · 지연 0.1 초
    NetRenderClock chunks   = makeRenderClock( 1, 0.0f );         // 60 Hz · 지연 0.1 초
    clock.observe( 100 );
    movement.observeSample( 50 );
    chunks.observeSample( 100 );
    movement.follow( clock );
    chunks.follow( clock );
    SW_EXPECT_NEAR( chunks.getRenderTick(), movement.getRenderTick() * 2.0, 1.0e-3 );

    // 이동 스냅샷만 온다 — 덩어리는 새 표본이 없어 받은 자리(100)에서 멈추고, 이동은 계속 간다.
    for ( uint32 tick = 51; tick <= 80; ++tick )
    {
        clock.observe( tick * 2u );
        movement.observeSample( tick );
        clock.advance( kClockFrame * 2.0f );
        movement.follow( clock );
        chunks.follow( clock );
    }
    SW_EXPECT_NEAR( 100.0, chunks.getRenderTick(), 1.0e-3 );
    SW_EXPECT_TRUE( movement.getRenderTick() > 70.0 );
    SW_EXPECT_TRUE( movement.getRenderTick() <= 80.0 );
}

/** @brief [NetClockTest] 여러 날 돈 서버에서도 화면 틱이 프레임마다 나아간다 — float32 틱은 한 프레임(0.5 틱)을 잃어 보간이 멈췄다 */
SW_TEST_CASE( NetClockTest, RenderTickAdvancesAfterDaysOfTicks )
{
    NetClock clock;
    clock.reset( NetClockSide::Replica );
    NetRenderClock   render       = makeRenderClock( 2, 0.0f );
    constexpr uint32 kDaysOfTicks = 60u * 3600u * 24u * 3u; // 사흘
    clock.observe( kDaysOfTicks );
    render.observeSample( kDaysOfTicks / 2u );
    clock.advance( kClockFrame );
    render.follow( clock );
    const float64 before = render.getRenderTick();
    clock.advance( kClockFrame ); // 새 틱 없이 한 프레임 — 30 Hz 틱의 절반만큼 나아간다
    render.follow( clock );
    SW_EXPECT_NEAR( 0.5, render.getRenderTick() - before, 1.0e-3 );
}
