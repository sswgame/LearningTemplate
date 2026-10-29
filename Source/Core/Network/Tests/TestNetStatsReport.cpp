#include "pch.h"

#include "Core/Network/DebugReplyLine.h"
#include "Core/Network/Message/NetStatsReport.h"
#include "Core/Testing/TestFramework.h"

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
    SW_REQUIRE_TRUE( NetStatsReport::tryParseLine( line, parsed ) );
    SW_EXPECT_EQUAL( parsed._connectionCount, 2 );
    SW_EXPECT_EQUAL( parsed._sentPacketCount, uint64{ 1200 } );
    SW_EXPECT_EQUAL( parsed._receivedPacketCount, uint64{ 900 } );
    SW_EXPECT_EQUAL( parsed._resentMessageCount, uint64{ 41 } );
    SW_EXPECT_NEAR( parsed._sentBytesPerSecond, 123456.75f, 0.01f );
    SW_EXPECT_NEAR( parsed._receivedBytesPerSecond, 512.5f, 1.0e-3f );
    SW_EXPECT_NEAR( parsed._rttMilliseconds, 12.25f, 1.0e-4f );
    SW_EXPECT_NEAR( parsed._lossPercent, 29.5f, 1.0e-4f );
    SW_REQUIRE_EQUAL( 2, static_cast<int32>( parsed._listKindEntry.size() ) );
    SW_EXPECT_EQUAL( 0x0F, static_cast<int32>( parsed._listKindEntry[0]._kind ) );
    SW_EXPECT_EQUAL( parsed._listKindEntry[0]._messageCount, uint64{ 100 } );
    SW_EXPECT_EQUAL( parsed._listKindEntry[0]._byteCount, uint64{ 102400 } );
    SW_EXPECT_EQUAL( 0x81, static_cast<int32>( parsed._listKindEntry[1]._kind ) );

    // 종류가 없는 보고도 읽힌다(빈 kinds= 칸).
    NetStatsReport emptyKinds;
    SW_REQUIRE_TRUE( NetStatsReport::tryParseLine( NetStatsReport{}.makeLine(), emptyKinds ) );
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
    SW_EXPECT_FALSE( NetStatsReport::tryParseLine( string( NetStatsReport::kLinePrefix ) + " replicas=Bad Name:1:2", untouched ) );
    SW_EXPECT_FALSE( NetStatsReport::tryParseLine( string( NetStatsReport::kLinePrefix ) + " replicas=entities:1", untouched ) );
    SW_EXPECT_EQUAL( untouched._resentMessageCount, uint64{ 7 } );
}

/**
 * @brief [NetStatsReportTest] 복제 키트가 올린 셈(`replicas=`)은 줄에 실려 되읽히고, 형식에 맞지 않는 이름은 싣지 않으며, 이름이 없는 보고도 읽힌다
 */
SW_TEST_CASE( NetStatsReportTest, ReplicaStatsRoundTripAndBadNamesAreNotWritten )
{
    SW_EXPECT_TRUE( NetStatsReport::isValidReplicaName( "net-replication.entities_01" ) );
    SW_EXPECT_FALSE( NetStatsReport::isValidReplicaName( "" ) );
    SW_EXPECT_FALSE( NetStatsReport::isValidReplicaName( "Upper" ) );
    SW_EXPECT_FALSE( NetStatsReport::isValidReplicaName( "a,b" ) );
    SW_EXPECT_FALSE( NetStatsReport::isValidReplicaName( "a:b" ) );

    NetStatsReport report;
    report._listReplicaEntry.push_back( NetStatsReplicaEntry{ "entities", 600, 480000 } );
    report._listReplicaEntry.push_back( NetStatsReplicaEntry{ "bad name", 1, 1 } ); // 형식이 맞지 않아 싣지 않는다
    report._listReplicaEntry.push_back( NetStatsReplicaEntry{ "voxel.chunks", 12, 9000 } );

    NetStatsReport parsed;
    SW_REQUIRE_TRUE( NetStatsReport::tryParseLine( report.makeLine(), parsed ) );
    SW_REQUIRE_EQUAL( 2, static_cast<int32>( parsed._listReplicaEntry.size() ) );
    SW_EXPECT_TRUE( parsed._listReplicaEntry[0]._name == "entities" );
    SW_EXPECT_EQUAL( parsed._listReplicaEntry[0]._updateCount, uint64{ 600 } );
    SW_EXPECT_EQUAL( parsed._listReplicaEntry[1]._byteCount, uint64{ 9000 } );

    NetStatsReport emptyReplicas;
    SW_REQUIRE_TRUE( NetStatsReport::tryParseLine( NetStatsReport{}.makeLine(), emptyReplicas ) );
    SW_EXPECT_TRUE( emptyReplicas._listReplicaEntry.empty() );
}

/**
 * @brief [NetStatsReportTest] 줄이 상한을 넘으면 복제 셈을 빼고 잘린 사실과 원래 길이를 줄에 싣는다(말없이 자르지 않는다)
 */
SW_TEST_CASE( NetStatsReportTest, OverlongLineIsCappedAndMarkedTruncated )
{
    NetStatsReport report;
    const string   longName( 400, 'a' );
    for ( int32 index = 0; index < 20; ++index )
    {
        report._listReplicaEntry.push_back( NetStatsReplicaEntry{ longName + to_string( index ), 1, 1 } );
    }
    const string line = report.makeLine();
    SW_EXPECT_TRUE( line.size() <= DebugReplyLine::kMaxBytes );

    NetStatsReport parsed;
    SW_REQUIRE_TRUE( NetStatsReport::tryParseLine( line, parsed ) );
    SW_EXPECT_TRUE( parsed._truncatedLineBytes > DebugReplyLine::kMaxBytes );
    SW_EXPECT_TRUE( parsed._listReplicaEntry.size() < report._listReplicaEntry.size() );

    NetStatsReport small;
    small._listReplicaEntry.push_back( NetStatsReplicaEntry{ "entities", 1, 1 } );
    NetStatsReport smallParsed;
    SW_REQUIRE_TRUE( NetStatsReport::tryParseLine( small.makeLine(), smallParsed ) );
    SW_EXPECT_EQUAL( smallParsed._truncatedLineBytes, uint64{ 0 } );
}
