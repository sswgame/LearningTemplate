#include "pch.h"

#include "Core/Diagnostics/ProfilerTimeline.h"
#include "Core/Network/Connection/NetFlowLog.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

#include <thread>

// ProfilerTimeline — 스레드별 링 · 프레임 구간 · 넘침 · 꺼짐.

/**
 * @brief [ProfilerTimelineTest] 두 스레드의 구간이 스레드별로 · 시작 시각 순으로 · 깊이를 지켜 모인다
 * @details 사건은 끝날 때(소멸자) 기록되므로 안쪽 구간이 바깥보다 먼저 쓰인다. 모은 목록은 시작 시각 순이어야 바깥이 먼저 온다.
 */
SW_TEST_CASE( ProfilerTimelineTest, CollectsEventsPerThreadInBeginOrder )
{
    sw::ProfilerTimeline timeline;
    timeline.setRecording( true );
    timeline.recordFrameBegin( 1000 );

    std::thread worker(
        [&timeline]()
    {
        timeline.recordEvent( 7, 1200, 1300, 1 ); // 안쪽이 먼저 끝난다
        timeline.recordEvent( 6, 1100, 1400, 0 );
    } );
    worker.join();
    timeline.recordEvent( 5, 1500, 1800, 0 );
    timeline.recordFrameBegin( 2000 );

    sw::vector<sw::ProfilerTimelineThread> listThread;
    uint64                                 beginNanos{ 0 };
    uint64                                 endNanos{ 0 };
    SW_REQUIRE_TRUE( timeline.collectRecentFrames( 1, listThread, beginNanos, endNanos ) );
    SW_EXPECT_EQUAL( 1000ull, beginNanos );
    SW_EXPECT_EQUAL( 2000ull, endNanos );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 2 ), listThread.size() );
    SW_EXPECT_EQUAL( 2u, timeline.countActiveThreads( 1 ) );

    const sw::ProfilerTimelineThread* pWorker = nullptr;
    const sw::ProfilerTimelineThread* pMain   = nullptr;
    for ( const sw::ProfilerTimelineThread& thread : listThread )
    {
        if ( thread._listEvent.size() == 2 )
            pWorker = &thread;
        else
            pMain = &thread;
    }
    SW_REQUIRE_TRUE( pWorker != nullptr && pMain != nullptr );
    SW_EXPECT_EQUAL( 6u, pWorker->_listEvent[0]._slot );
    SW_EXPECT_EQUAL( static_cast<uint16>( 0 ), pWorker->_listEvent[0]._depth );
    SW_EXPECT_EQUAL( 7u, pWorker->_listEvent[1]._slot );
    SW_EXPECT_EQUAL( static_cast<uint16>( 1 ), pWorker->_listEvent[1]._depth );
    SW_EXPECT_EQUAL( 5u, pMain->_listEvent[0]._slot );
    SW_EXPECT_TRUE( pMain->_name.empty() == false );
}

/**
 * @brief [ProfilerTimelineTest] 구간 밖 사건은 빠지고, 프레임 수가 모자라면 남은 가장 오래된 프레임부터다
 */
SW_TEST_CASE( ProfilerTimelineTest, WindowFollowsRecentFrames )
{
    sw::ProfilerTimeline timeline;
    timeline.setRecording( true );
    timeline.recordFrameBegin( 100 );
    timeline.recordEvent( 1, 110, 120, 0 ); // 첫 프레임
    timeline.recordFrameBegin( 200 );
    timeline.recordEvent( 2, 210, 220, 0 ); // 둘째 프레임
    timeline.recordFrameBegin( 300 );
    timeline.recordEvent( 3, 310, 320, 0 ); // 지금 도는 프레임 — 끝나지 않아 구간 밖

    sw::vector<sw::ProfilerTimelineThread> listThread;
    uint64                                 beginNanos{ 0 };
    uint64                                 endNanos{ 0 };
    SW_REQUIRE_TRUE( timeline.collectRecentFrames( 1, listThread, beginNanos, endNanos ) );
    SW_EXPECT_EQUAL( 200ull, beginNanos );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), listThread.size() );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), listThread[0]._listEvent.size() );
    SW_EXPECT_EQUAL( 2u, listThread[0]._listEvent[0]._slot );

    // 16 프레임을 달라고 해도 끝난 프레임은 둘뿐이다.
    SW_REQUIRE_TRUE( timeline.collectRecentFrames( 16, listThread, beginNanos, endNanos ) );
    SW_EXPECT_EQUAL( 100ull, beginNanos );
    SW_EXPECT_EQUAL( 300ull, endNanos );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 2 ), listThread[0]._listEvent.size() );
}

/**
 * @brief [ProfilerTimelineTest] 링이 넘치면 오래된 사건부터 버린다
 */
SW_TEST_CASE( ProfilerTimelineTest, RingOverflowDropsOldestEvents )
{
    sw::ProfilerTimeline timeline;
    timeline.setRecording( true );
    timeline.recordFrameBegin( 0 );
    constexpr uint64 kExtra = 100;
    for ( uint64 eventIndex = 0; eventIndex < sw::ProfilerTimeline::kEventCapacity + kExtra; ++eventIndex )
    {
        timeline.recordEvent( static_cast<uint32>( eventIndex ), 10 + eventIndex, 11 + eventIndex, 0 );
    }
    timeline.recordFrameBegin( 1000000 );

    sw::vector<sw::ProfilerTimelineThread> listThread;
    uint64                                 beginNanos{ 0 };
    uint64                                 endNanos{ 0 };
    SW_REQUIRE_TRUE( timeline.collectRecentFrames( 1, listThread, beginNanos, endNanos ) );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), listThread.size() );
    // 마지막으로 쓴 칸(덮이는 중일 수 있는 다음 칸 자리)까지 빼므로 링 크기 - 1 개가 남고, 가장 오래된 것은 kExtra + 1 번이다.
    SW_EXPECT_EQUAL( static_cast<size_t>( sw::ProfilerTimeline::kEventCapacity - 1 ), listThread[0]._listEvent.size() );
    SW_EXPECT_EQUAL( static_cast<uint32>( kExtra + 1 ), listThread[0]._listEvent.front()._slot );
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::ProfilerTimeline::kEventCapacity + kExtra - 1 ), listThread[0]._listEvent.back()._slot );
}

/**
 * @brief [ProfilerTimelineTest] 기록이 꺼져 있으면 아무것도 쌓이지 않는다
 */
SW_TEST_CASE( ProfilerTimelineTest, NothingIsRecordedWhileOff )
{
    sw::ProfilerTimeline timeline;
    timeline.recordFrameBegin( 100 );
    timeline.recordEvent( 1, 110, 120, 0 );
    timeline.recordFrameBegin( 200 );

    sw::vector<sw::ProfilerTimelineThread> listThread;
    uint64                                 beginNanos{ 0 };
    uint64                                 endNanos{ 0 };
    SW_EXPECT_TRUE( timeline.collectRecentFrames( 1, listThread, beginNanos, endNanos ) == false );
    SW_EXPECT_TRUE( listThread.empty() );
    SW_EXPECT_EQUAL( 0u, timeline.countActiveThreads( 4 ) );
}

#if SW_PROFILER_TRACE_ENABLED

/**
 * @brief [ProfilerTimelineTest] 긴 캡처는 링이 넘치는 양도 처음부터 담고, 예산을 넘으면 거기서 멈춰 잘림 표지를 남긴다
 */
SW_TEST_CASE( ProfilerTimelineTest, LongCaptureKeepsEverythingFromTheStartUntilTheBudget )
{
    sw::ProfilerTimeline timeline;
    SW_EXPECT_FALSE( timeline.isRecording() );
    constexpr uint32 kBudget = sw::ProfilerTimeline::kEventCapacity + 100; // 링 크기보다 큰 예산
    timeline.beginLongCapture( kBudget );
    SW_EXPECT_TRUE( timeline.isLongCaptureActive() );
    SW_EXPECT_TRUE( timeline.isRecording() ); // 꺼져 있어도 캡처가 켠다
    timeline.recordFrameBegin( 1 );
    for ( uint32 index = 0; index < kBudget + 5; ++index )
    {
        timeline.recordEvent( 3, 10 + index * 2, 11 + index * 2, 0 );
    }
    timeline.endLongCapture();
    SW_EXPECT_FALSE( timeline.isLongCaptureActive() );
    SW_EXPECT_FALSE( timeline.isRecording() ); // 시작 전 상태로

    sw::vector<sw::ProfilerTimelineThread> listThread;
    sw::vector<uint64>                     listFrameBegin;
    bool                                   bTruncated = false;
    SW_REQUIRE_TRUE( timeline.collectLongCapture( listThread, listFrameBegin, bTruncated ) );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), listThread.size() );
    SW_EXPECT_EQUAL( static_cast<size_t>( kBudget ), listThread[0]._listEvent.size() );
    SW_EXPECT_EQUAL( 10ull, listThread[0]._listEvent.front()._beginNanos ); // 링이었다면 앞이 버려졌을 것
    SW_EXPECT_TRUE( bTruncated );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), listFrameBegin.size() );

    // 멈춘 뒤의 사건은 담지 않는다.
    timeline.recordEvent( 3, 99999, 100000, 0 );
    SW_REQUIRE_TRUE( timeline.collectLongCapture( listThread, listFrameBegin, bTruncated ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( kBudget ), listThread[0]._listEvent.size() );
}

/**
 * @brief [ProfilerTimelineTest] 긴 캡처를 다시 시작하면 지난 캡처의 사건은 비워진다
 */
SW_TEST_CASE( ProfilerTimelineTest, RestartedLongCaptureDropsThePreviousCapture )
{
    sw::ProfilerTimeline timeline;
    timeline.beginLongCapture( 100 );
    timeline.recordEvent( 1, 10, 20, 0 );
    timeline.endLongCapture();
    timeline.beginLongCapture( 100 );
    timeline.recordEvent( 2, 30, 40, 0 );
    timeline.endLongCapture();

    sw::vector<sw::ProfilerTimelineThread> listThread;
    sw::vector<uint64>                     listFrameBegin;
    bool                                   bTruncated = true;
    SW_REQUIRE_TRUE( timeline.collectLongCapture( listThread, listFrameBegin, bTruncated ) );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), listThread[0]._listEvent.size() );
    SW_EXPECT_EQUAL( 2u, listThread[0]._listEvent[0]._slot );
    SW_EXPECT_FALSE( bTruncated );
}

/**
 * @brief [ProfilerTimelineTest] 흐름 id 는 연결 · 방향 · 순서 번호마다 다르고, 같은 셋이면 같다(양쪽이 따로 계산한다). 53 비트 안이다
 */
SW_TEST_CASE( ProfilerTimelineTest, NetFlowIDMatchesOnBothEndsAndSeparatesDirections )
{
    constexpr uint64 kToken = 0x1234567890ABCDEFull;
    const uint64     sent   = sw::NetFlowLog::makeFlowID( kToken, false, sw::NetChannelType::ReliableOrdered, 7 );
    SW_EXPECT_EQUAL( sent, sw::NetFlowLog::makeFlowID( kToken, false, sw::NetChannelType::ReliableOrdered, 7 ) );
    SW_EXPECT_TRUE( sent != sw::NetFlowLog::makeFlowID( kToken, true, sw::NetChannelType::ReliableOrdered, 7 ) );      // 반대 방향의 7 번째
    SW_EXPECT_TRUE( sent != sw::NetFlowLog::makeFlowID( kToken, false, sw::NetChannelType::ReliableOrdered, 8 ) );     // 다음 메시지
    SW_EXPECT_TRUE( sent != sw::NetFlowLog::makeFlowID( kToken ^ 1, false, sw::NetChannelType::ReliableOrdered, 7 ) ); // 다른 연결
    SW_EXPECT_TRUE( sent != sw::NetFlowLog::makeFlowID( kToken, false, sw::NetChannelType::Unreliable, 7 ) );          // 다른 채널의 같은 번호
    SW_EXPECT_EQUAL( 0ull, sent >> 53 );
}

/**
 * @brief [ProfilerTimelineTest] 흐름 사건은 기록 중인 타임라인이 있을 때만 남고, 구간 안의 것만 시각 순으로 모인다
 */
SW_TEST_CASE( ProfilerTimelineTest, NetFlowsAreKeptOnlyWhileATimelineRecords )
{
    sw::vector<sw::NetFlowEvent> listFlow;
    {
        sw::ProfilerTimeline timeline; // 꺼진 채로 — 흐름도 남지 않는다
        sw::NetFlowLog::record( 0xAAAAull, 0x81, false );
        sw::NetFlowLog::collect( 0, ~0ull, listFlow );
        for ( const sw::NetFlowEvent& flow : listFlow )
        {
            SW_EXPECT_TRUE( flow._id != 0xAAAAull );
        }
    }

    // 링은 프로세스에 하나라 앞 회차(`--test-repeat`)의 흐름도 남아 있다 — 이 회차가 쓴 구간만 모은다
    const uint64         beginNanos = static_cast<uint64>( sw::MonotonicClock::nowNanoseconds() );
    sw::ProfilerTimeline timeline;
    timeline.setRecording( true );
    sw::NetFlowLog::record( 0xBBBBull, 0x81, false );
    sw::NetFlowLog::record( 0xBBBBull, 0x81, true );
    timeline.setRecording( false );
    sw::NetFlowLog::record( 0xCCCCull, 0x82, false ); // 꺼진 뒤 — 남지 않는다

    sw::NetFlowLog::collect( beginNanos, ~0ull, listFlow );
    uint32 sentCount     = 0;
    uint32 receivedCount = 0;
    for ( size_t index = 0; index < listFlow.size(); ++index )
    {
        const sw::NetFlowEvent& flow = listFlow[index];
        SW_EXPECT_TRUE( flow._id != 0xCCCCull );
        if ( index > 0 )
            SW_EXPECT_TRUE( listFlow[index - 1]._nanos <= flow._nanos );
        if ( flow._id != 0xBBBBull )
            continue;
        SW_EXPECT_EQUAL( static_cast<uint8>( 0x81 ), flow._kind );
        ++( flow._bReceive ? receivedCount : sentCount );
    }
    SW_EXPECT_EQUAL( 1u, sentCount );
    SW_EXPECT_EQUAL( 1u, receivedCount );

    // 구간 밖이면 빠진다.
    sw::NetFlowLog::collect( 0, 0, listFlow );
    SW_EXPECT_TRUE( listFlow.empty() );
}

#endif // SW_PROFILER_TRACE_ENABLED
