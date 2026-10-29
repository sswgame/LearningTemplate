#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/DedicatedServer/ServerFrameHistory.h"

// 서버 프레임 이력 — 용량 안에서는 순서대로, 넘치면 가장 오래된 것부터 덮고, 용량 0 은 아무것도 남기지 않으며, 덤프 본문에 머리와 칸이 든다.

using namespace sw;

namespace
{
    ServerFrameSample makeSample( uint64 tick, int64 bodyMicroseconds )
    {
        ServerFrameSample sample;
        sample._tick             = tick;
        sample._bodyMicroseconds = bodyMicroseconds;
        sample._connectionCount  = 3;
        return sample;
    }
} // namespace

SW_TEST_CASE( ServerFrameHistoryTest, KeepsTheNewestSamplesInOrder )
{
    ServerFrameHistory history;
    history.initialize( 3 );
    SW_EXPECT_EQUAL( history.getCount(), 0u );
    history.record( makeSample( 1, 10 ) );
    history.record( makeSample( 2, 20 ) );
    SW_EXPECT_EQUAL( history.getCount(), 2u );
    history.record( makeSample( 3, 30 ) );
    history.record( makeSample( 4, 40 ) ); // 1 번 틱을 덮는다
    SW_REQUIRE_EQUAL( history.getCount(), 3u );
    ServerFrameSample sample;
    SW_REQUIRE_TRUE( history.tryGetSample( 0, sample ) );
    SW_EXPECT_EQUAL( sample._tick, uint64( 2 ) );
    SW_REQUIRE_TRUE( history.tryGetSample( 2, sample ) );
    SW_EXPECT_EQUAL( sample._tick, uint64( 4 ) );
    SW_EXPECT_FALSE( history.tryGetSample( 3, sample ) );
    SW_REQUIRE_TRUE( history.tryGetLatest( sample ) );
    SW_EXPECT_EQUAL( sample._bodyMicroseconds, int64( 40 ) );
}

SW_TEST_CASE( ServerFrameHistoryTest, ZeroCapacityKeepsNothing )
{
    ServerFrameHistory history;
    history.initialize( 0 );
    history.record( makeSample( 1, 10 ) );
    SW_EXPECT_EQUAL( history.getCount(), 0u );
    ServerFrameSample sample;
    SW_EXPECT_FALSE( history.tryGetLatest( sample ) );
}

SW_TEST_CASE( ServerFrameHistoryTest, ReportHasTheHeaderAndOneLinePerSample )
{
    ServerFrameHistory history;
    history.initialize( 4 );
    history.record( makeSample( 1, 100 ) );
    history.record( makeSample( 2, 900 ) );
    string text;
    history.writeReport( text, "slow_tick", "SESSION", "Net traffic stats connections=3" );
    SW_EXPECT_TRUE_MSG( text.find( "reason slow_tick\n" ) != string::npos, text.c_str() );
    SW_EXPECT_TRUE_MSG( text.find( "session SESSION\n" ) != string::npos, text.c_str() );
    SW_EXPECT_TRUE_MSG( text.find( "body_us avg 500 max 900 at tick 2\n" ) != string::npos, text.c_str() );
    SW_EXPECT_TRUE_MSG( text.find( "tick=1 body_us=100 connections=3 dropped=0\n" ) != string::npos, text.c_str() );
    SW_EXPECT_TRUE_MSG( text.find( "tick=2 body_us=900 connections=3 dropped=0\n" ) != string::npos, text.c_str() );
    SW_EXPECT_TRUE_MSG( text.find( "Net traffic stats connections=3\n" ) != string::npos, text.c_str() );
}
