#include "pch.h"

#include "Engine/Profiling/ProfilerTimeline.h"

#include "TestFramework/TestFramework.h"

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
    SW_ASSERT_TRUE( timeline.collectRecentFrames( 1, listThread, beginNanos, endNanos ) );
    SW_EXPECT_EQUAL( 1000ull, beginNanos );
    SW_EXPECT_EQUAL( 2000ull, endNanos );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), listThread.size() );
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
    SW_ASSERT_TRUE( pWorker != nullptr && pMain != nullptr );
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
    SW_ASSERT_TRUE( timeline.collectRecentFrames( 1, listThread, beginNanos, endNanos ) );
    SW_EXPECT_EQUAL( 200ull, beginNanos );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listThread.size() );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listThread[0]._listEvent.size() );
    SW_EXPECT_EQUAL( 2u, listThread[0]._listEvent[0]._slot );

    // 16 프레임을 달라고 해도 끝난 프레임은 둘뿐이다.
    SW_ASSERT_TRUE( timeline.collectRecentFrames( 16, listThread, beginNanos, endNanos ) );
    SW_EXPECT_EQUAL( 100ull, beginNanos );
    SW_EXPECT_EQUAL( 300ull, endNanos );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), listThread[0]._listEvent.size() );
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
    SW_ASSERT_TRUE( timeline.collectRecentFrames( 1, listThread, beginNanos, endNanos ) );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listThread.size() );
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
