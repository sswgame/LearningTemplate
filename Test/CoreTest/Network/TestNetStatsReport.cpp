#include "pch.h"

#include "Core/Network/Message/NetStatsReport.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

/**
 * @brief [NetStatsReportTest] `net.stats` 보고 줄은 로그 꼬리표 · 명령 메아리가 붙어도 모든 칸(종류별 셈 포함)이 그대로 읽힌다
 */
SW_TEST_CASE( NetStatsReportTest, ReportLineRoundTripsThroughALogLine )
{
    NetStatsReport report;
    report._connectionCount        = 2;
    report._sentPacketCount        = 1200;
    report._receivedPacketCount    = 900;
    report._resentMessageCount     = 41;
    report._sentBytesPerSecond     = 123456.75f; // 큰 값도 지수 표기 없이 나가야 한다
    report._receivedBytesPerSecond = 512.5f;
    report._rttMilliseconds        = 12.25f;
    report._lossPercent            = 29.5f;
    report._listKindEntry.push_back( NetStatsKindEntry{ 100, 102400, 0x0F } );
    report._listKindEntry.push_back( NetStatsKindEntry{ 3, 90, 0x81 } );
    const string line = "[12:00:01.234] [Info] [DevConsole] > net.stats : " + report.makeLine();

    NetStatsReport parsed;
    SW_ASSERT_TRUE( NetStatsReport::tryParseLine( line, parsed ) );
    SW_EXPECT_EQUAL( parsed._connectionCount, 2 );
    SW_EXPECT_EQUAL( parsed._sentPacketCount, uint64{ 1200 } );
    SW_EXPECT_EQUAL( parsed._receivedPacketCount, uint64{ 900 } );
    SW_EXPECT_EQUAL( parsed._resentMessageCount, uint64{ 41 } );
    SW_EXPECT_NEAR_EQUAL( parsed._sentBytesPerSecond, 123456.75f, 0.01f );
    SW_EXPECT_NEAR_EQUAL( parsed._receivedBytesPerSecond, 512.5f, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( parsed._rttMilliseconds, 12.25f, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( parsed._lossPercent, 29.5f, 1.0e-4f );
    SW_ASSERT_EQUAL( 2, static_cast<int32>( parsed._listKindEntry.size() ) );
    SW_EXPECT_EQUAL( 0x0F, static_cast<int32>( parsed._listKindEntry[0]._kind ) );
    SW_EXPECT_EQUAL( parsed._listKindEntry[0]._messageCount, uint64{ 100 } );
    SW_EXPECT_EQUAL( parsed._listKindEntry[0]._byteCount, uint64{ 102400 } );
    SW_EXPECT_EQUAL( 0x81, static_cast<int32>( parsed._listKindEntry[1]._kind ) );

    // 종류가 없는 보고도 읽힌다(빈 kinds= 칸).
    NetStatsReport emptyKinds;
    SW_ASSERT_TRUE( NetStatsReport::tryParseLine( NetStatsReport{}.makeLine(), emptyKinds ) );
    SW_EXPECT_TRUE( emptyKinds._listKindEntry.empty() );
}

/**
 * @brief [NetStatsReportTest] 머리가 없거나 아는 칸이 깨진 줄은 읽지 않고 받는 쪽을 바꾸지 않는다 — 회선 흉내 보고 줄과도 섞이지 않는다
 */
SW_TEST_CASE( NetStatsReportTest, RejectsOtherLinesAndBrokenFields )
{
    NetStatsReport untouched;
    untouched._resentMessageCount = 7;
    SW_EXPECT_FALSE( NetStatsReport::tryParseLine( "Net emulation stats connections=1 sent=3 dropped=0 resent=0 lossPercent=0 rttMs=0", untouched ) );
    SW_EXPECT_FALSE( NetStatsReport::tryParseLine( string( NetStatsReport::kLinePrefix ) + " resent=many", untouched ) );
    SW_EXPECT_FALSE( NetStatsReport::tryParseLine( string( NetStatsReport::kLinePrefix ) + " kinds=15:3", untouched ) );
    SW_EXPECT_FALSE( NetStatsReport::tryParseLine( string( NetStatsReport::kLinePrefix ) + " kinds=300:1:1", untouched ) );
    SW_EXPECT_EQUAL( untouched._resentMessageCount, uint64{ 7 } );
}
