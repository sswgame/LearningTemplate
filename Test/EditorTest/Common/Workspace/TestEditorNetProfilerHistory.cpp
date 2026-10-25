#include "pch.h"

#include "Core/Network/Message/NetStatsReport.h"

#include "Editor/Common/Workspace/EditorNetProfilerHistory.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct TestEditorNetProfilerHistoryInternal
    {
        static EditorNetProfilerSample makeSample( float32 sentBytesPerSecond, float64 timeSeconds )
        {
            EditorNetProfilerSample sample;
            sample._timeSeconds                                                                   = timeSeconds;
            sample._arrValue[static_cast<size_t>( EditorNetProfilerChannel::SentBytesPerSecond )] = sentBytesPerSecond;
            return sample;
        }

        static float32 readChannel( const EditorNetProfilerSample& sample, EditorNetProfilerChannel channel )
        {
            return sample._arrValue[static_cast<size_t>( channel )];
        }
    };
} // namespace

/**
 * @brief [EditorNetProfilerTest] 링 버퍼는 용량을 넘으면 가장 오래된 표본을 덮어쓰고, 순서는 언제나 오래된 것부터다
 */
SW_TEST_CASE( EditorNetProfilerTest, SeriesWrapsAndKeepsOldestFirst )
{
    using Internal = TestEditorNetProfilerHistoryInternal;
    EditorNetProfilerSeries series( 3 );
    SW_EXPECT_EQUAL( 0, series.getCount() );
    SW_EXPECT_TRUE( series.findLatest() == nullptr );
    SW_EXPECT_NEAR_EQUAL( series.computePeak( EditorNetProfilerChannel::SentBytesPerSecond ), 0.0f, 1.0e-6f );

    for ( int32 index = 1; index <= 5; ++index )
    {
        series.push( Internal::makeSample( static_cast<float32>( index * 10 ), static_cast<float64>( index ) ) );
    }
    SW_ASSERT_EQUAL( 3, series.getCount() );
    SW_EXPECT_EQUAL( 3, series.getCapacity() );
    // 1 · 2 는 밀려났다 — 남은 것은 3 · 4 · 5 이다.
    SW_EXPECT_NEAR_EQUAL( series.getSample( 0 )._timeSeconds, 3.0, 1.0e-9 );
    SW_EXPECT_NEAR_EQUAL( series.getSample( 2 )._timeSeconds, 5.0, 1.0e-9 );
    SW_ASSERT_TRUE( series.findLatest() != nullptr );
    SW_EXPECT_NEAR_EQUAL( series.findLatest()->_timeSeconds, 5.0, 1.0e-9 );

    vector<float32> listValue;
    series.copyChannel( EditorNetProfilerChannel::SentBytesPerSecond, listValue );
    SW_ASSERT_EQUAL( 3, static_cast<int32>( listValue.size() ) );
    SW_EXPECT_NEAR_EQUAL( listValue[0], 30.0f, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( listValue[2], 50.0f, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( series.computePeak( EditorNetProfilerChannel::SentBytesPerSecond ), 50.0f, 1.0e-6f );

    series.clear();
    SW_EXPECT_EQUAL( 0, series.getCount() );
    series.push( Internal::makeSample( 7.0f, 9.0 ) );
    SW_EXPECT_NEAR_EQUAL( series.getSample( 0 )._timeSeconds, 9.0, 1.0e-9 );
}

/**
 * @brief [EditorNetProfilerTest] 표본은 대역폭 · RTT · 손실을 보고 그대로, 누적 값은 앞 보고와의 차를 초당 값으로 쓴다 — 첫 보고와 줄어든 누적 값(다시 붙음)은 0
 */
SW_TEST_CASE( EditorNetProfilerTest, SampleTurnsCumulativeCountsIntoRates )
{
    using Internal = TestEditorNetProfilerHistoryInternal;
    NetStatsReport first;
    first._sentPacketCount        = 100;
    first._receivedPacketCount    = 50;
    first._resentMessageCount     = 4;
    first._sentBytesPerSecond     = 2048.0f;
    first._receivedBytesPerSecond = 1024.0f;
    first._rttMilliseconds        = 30.0f;
    first._lossPercent            = 5.0f;

    const EditorNetProfilerSample firstSample = EditorNetProfilerUtil::makeSample( nullptr, 0.0, first, 10.0 );
    SW_EXPECT_NEAR_EQUAL( Internal::readChannel( firstSample, EditorNetProfilerChannel::SentBytesPerSecond ), 2048.0f, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( Internal::readChannel( firstSample, EditorNetProfilerChannel::ReceivedBytesPerSecond ), 1024.0f, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( Internal::readChannel( firstSample, EditorNetProfilerChannel::RttMilliseconds ), 30.0f, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( Internal::readChannel( firstSample, EditorNetProfilerChannel::LossPercent ), 5.0f, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( Internal::readChannel( firstSample, EditorNetProfilerChannel::SentPacketsPerSecond ), 0.0f, 1.0e-6f );

    NetStatsReport second                      = first;
    second._sentPacketCount                    = 130; // 0.5 초에 30 개 → 60 /s
    second._receivedPacketCount                = 60;
    second._resentMessageCount                 = 5;
    const EditorNetProfilerSample secondSample = EditorNetProfilerUtil::makeSample( &first, 10.0, second, 10.5 );
    SW_EXPECT_NEAR_EQUAL( Internal::readChannel( secondSample, EditorNetProfilerChannel::SentPacketsPerSecond ), 60.0f, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( Internal::readChannel( secondSample, EditorNetProfilerChannel::ReceivedPacketsPerSecond ), 20.0f, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( Internal::readChannel( secondSample, EditorNetProfilerChannel::ResentMessagesPerSecond ), 2.0f, 1.0e-3f );

    // 다시 붙어 연결 통계가 새로 시작하면 누적 값이 줄어든다 — 음수 비율 대신 0.
    NetStatsReport reconnected;
    reconnected._sentPacketCount              = 3;
    const EditorNetProfilerSample resetSample = EditorNetProfilerUtil::makeSample( &second, 10.5, reconnected, 11.0 );
    SW_EXPECT_NEAR_EQUAL( Internal::readChannel( resetSample, EditorNetProfilerChannel::SentPacketsPerSecond ), 0.0f, 1.0e-6f );
    // 시간이 흐르지 않았으면 0(나누기 0 방지).
    const EditorNetProfilerSample sameTimeSample = EditorNetProfilerUtil::makeSample( &first, 10.0, second, 10.0 );
    SW_EXPECT_NEAR_EQUAL( Internal::readChannel( sameTimeSample, EditorNetProfilerChannel::SentPacketsPerSecond ), 0.0f, 1.0e-6f );
}

/**
 * @brief [EditorNetProfilerTest] 종류별 비중은 프로세스들의 셈을 종류마다 합치고 바이트 몫으로 나누며, 바이트가 큰 순서다(합은 1)
 */
SW_TEST_CASE( EditorNetProfilerTest, KindSharesMergeKindsAndSumToOne )
{
    vector<NetStatsKindEntry> listEntry;
    listEntry.push_back( NetStatsKindEntry{ 10, 300, 0x81 } ); // 서버가 받은 것
    listEntry.push_back( NetStatsKindEntry{ 4, 100, 0x0F } );
    listEntry.push_back( NetStatsKindEntry{ 5, 600, 0x0F } ); // 클라이언트가 받은 것 — 같은 종류는 합친다

    vector<EditorNetKindShare> listShare;
    EditorNetProfilerUtil::computeKindShares( listEntry, listShare );
    SW_ASSERT_EQUAL( 2, static_cast<int32>( listShare.size() ) );
    SW_EXPECT_EQUAL( 0x0F, static_cast<int32>( listShare[0]._kind ) );
    SW_EXPECT_EQUAL( listShare[0]._messageCount, uint64{ 9 } );
    SW_EXPECT_EQUAL( listShare[0]._byteCount, uint64{ 700 } );
    SW_EXPECT_NEAR_EQUAL( listShare[0]._share, 0.7f, 1.0e-5f );
    SW_EXPECT_EQUAL( 0x81, static_cast<int32>( listShare[1]._kind ) );
    SW_EXPECT_NEAR_EQUAL( listShare[1]._share, 0.3f, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( listShare[0]._share + listShare[1]._share, 1.0f, 1.0e-5f );

    // 빈 입력은 빈 결과 — 앞 결과를 지운다.
    EditorNetProfilerUtil::computeKindShares( vector<NetStatsKindEntry>{}, listShare );
    SW_EXPECT_TRUE( listShare.empty() );
}

/**
 * @brief [EditorNetProfilerTest] 기록은 프로세스마다 시계열을 따로 쌓고, 표본 수 · 최댓값 · 종류별 셈을 모든 프로세스에서 모은다
 */
SW_TEST_CASE( EditorNetProfilerTest, HistoryKeepsOneTrackPerProcess )
{
    EditorNetProfilerHistory history;
    SW_EXPECT_EQUAL( 0, history.computeSampleCount() );
    SW_EXPECT_TRUE( history.findTrack( 0 ) == nullptr );

    NetStatsReport server;
    server._sentBytesPerSecond = 4000.0f;
    server._sentPacketCount    = 10;
    server._listKindEntry.push_back( NetStatsKindEntry{ 2, 20, 0x0F } );
    server._listSentKindEntry.push_back( NetStatsKindEntry{ 5, 50, 0x10 } );
    NetStatsReport client;
    client._sentBytesPerSecond = 9000.0f;
    client._listKindEntry.push_back( NetStatsKindEntry{ 1, 30, 0x0F } );

    history.ingest( 0, server, 1.0 );
    history.ingest( 1, client, 1.0 );
    server._sentPacketCount = 20;
    history.ingest( 0, server, 1.5 );
    history.ingest( -1, server, 2.0 ); // 번호가 없으면 무시한다

    SW_EXPECT_EQUAL( 2, history.getTrackCount() );
    SW_EXPECT_EQUAL( 3, history.computeSampleCount() );
    SW_ASSERT_TRUE( history.findTrack( 0 ) != nullptr );
    SW_EXPECT_EQUAL( 2, history.findTrack( 0 )->_series.getCount() );
    SW_EXPECT_NEAR_EQUAL( history.findTrack( 0 )->_series.findLatest()->_arrValue[static_cast<size_t>( EditorNetProfilerChannel::SentPacketsPerSecond )], 20.0f, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( history.computePeak( EditorNetProfilerChannel::SentBytesPerSecond ), 9000.0f, 1.0e-3f );

    vector<NetStatsKindEntry> listEntry;
    history.collectKindEntries( NetStatsKindDirection::Received, listEntry );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( listEntry.size() ) );
    listEntry.clear();
    history.collectKindEntries( NetStatsKindDirection::Sent, listEntry ); // 보낸 쪽은 서버만 실었다
    SW_ASSERT_EQUAL( 1, static_cast<int32>( listEntry.size() ) );
    SW_EXPECT_EQUAL( 0x10, static_cast<int32>( listEntry[0]._kind ) );

    history.clear();
    SW_EXPECT_EQUAL( 0, history.computeSampleCount() );
    SW_EXPECT_EQUAL( 0, history.getTrackCount() );
}

/**
 * @brief [EditorNetProfilerTest] 복제 셈은 두 보고의 누적 차를 초당 값으로 바꾸고, 기준선이 없는 이름과 줄어든 값은 0 이다 — 바이트가 큰 순서와 비율을 낸다
 */
SW_TEST_CASE( EditorNetProfilerTest, ReplicaRatesUseTheDifferenceOfTwoReports )
{
    vector<NetStatsReplicaEntry> listPrevious;
    listPrevious.push_back( NetStatsReplicaEntry{ "door", 10, 1000 } );
    listPrevious.push_back( NetStatsReplicaEntry{ "lamp", 5, 500 } );
    listPrevious.push_back( NetStatsReplicaEntry{ "wheel", 9, 900 } );
    vector<NetStatsReplicaEntry> listCurrent;
    listCurrent.push_back( NetStatsReplicaEntry{ "door", 14, 1200 } );
    listCurrent.push_back( NetStatsReplicaEntry{ "lamp", 5, 500 } );
    listCurrent.push_back( NetStatsReplicaEntry{ "new", 7, 700 } );
    listCurrent.push_back( NetStatsReplicaEntry{ "wheel", 1, 100 } );
    vector<EditorNetReplicaRate> listRate;
    EditorNetProfilerUtil::computeReplicaRates( listPrevious, listCurrent, 2.0, listRate );
    SW_ASSERT_EQUAL( size_t{ 4 }, listRate.size() );
    SW_EXPECT_TRUE( listRate[0]._name == "door" );
    SW_EXPECT_NEAR_EQUAL( listRate[0]._updatesPerSecond, 2.0f, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( listRate[0]._bytesPerSecond, 100.0f, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( listRate[0]._share, 1.0f, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( listRate[1]._bytesPerSecond, 0.0f, 1.0e-6f ); // lamp(변화 없음) · new(기준선 없음) · wheel(줄어듦) 모두 0, 이름 순
    SW_EXPECT_TRUE( listRate[1]._name == "lamp" );
    SW_EXPECT_TRUE( listRate[2]._name == "new" );
    SW_EXPECT_TRUE( listRate[3]._name == "wheel" );

    EditorNetProfilerUtil::computeReplicaRates( listPrevious, listCurrent, 0.0, listRate );
    SW_EXPECT_TRUE( listRate.empty() );
}

/**
 * @brief [EditorNetProfilerTest] 첫 보고에는 복제 비율이 없고, 두 번째 보고부터 앞 보고와의 차로 채운다
 */
SW_TEST_CASE( EditorNetProfilerTest, HistoryFillsReplicaRatesFromTheSecondReport )
{
    EditorNetProfilerHistory history;
    NetStatsReport           first;
    first._listReplicaEntry.push_back( NetStatsReplicaEntry{ "door", 10, 1000 } );
    history.ingest( 0, first, 1.0 );
    SW_ASSERT_TRUE( history.findTrack( 0 ) != nullptr );
    SW_EXPECT_TRUE( history.findTrack( 0 )->_listReplicaRate.empty() );
    NetStatsReport second;
    second._listReplicaEntry.push_back( NetStatsReplicaEntry{ "door", 12, 1400 } );
    history.ingest( 0, second, 2.0 );
    SW_ASSERT_EQUAL( size_t{ 1 }, history.findTrack( 0 )->_listReplicaRate.size() );
    SW_EXPECT_NEAR_EQUAL( history.findTrack( 0 )->_listReplicaRate[0]._bytesPerSecond, 400.0f, 1.0e-6f );
}
