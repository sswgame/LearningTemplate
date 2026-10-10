#include "pch.h"

#include "Editor/Panels/ProfilerTimelineLayout.h"

#include "TestFramework/TestFramework.h"

// ProfilerTimelineLayout — 프로파일러 패널 Timeline 탭의 배치(ImGui 없음).

namespace
{
    /** @brief 스레드 하나에 사건을 더합니다. */
    void addEventInternal( sw::ProfilerTimelineThread& thread, uint64 beginNanos, uint64 endNanos, uint16 depth )
    {
        sw::ProfilerTimelineEvent event;
        event._beginNanos = beginNanos;
        event._endNanos   = endNanos;
        event._depth      = depth;
        thread._listEvent.push_back( event );
    }
} // namespace

/**
 * @brief [ProfilerTimelineLayoutTest] 사건이 x 범위 · 스레드 줄 · 겹으로 펼쳐지고, 보이는 범위 밖은 빠지고 걸친 것은 잘린다
 */
SW_TEST_CASE( ProfilerTimelineLayoutTest, EventsBecomeClippedRectsPerThreadAndLane )
{
    sw::vector<sw::ProfilerTimelineThread> listThread( 2 );
    addEventInternal( listThread[0], 0, 500, 0 );     // 앞 절반
    addEventInternal( listThread[0], 100, 200, 1 );   // 안쪽 겹
    addEventInternal( listThread[0], 2000, 2100, 0 ); // 범위 밖 — 빠진다
    addEventInternal( listThread[1], 900, 1500, 0 );  // 끝에 걸친다 — 잘린다

    sw::vector<sw::editor::ProfilerTimelineRect> listRect;
    sw::vector<uint16>                           listLaneCount;
    sw::editor::ProfilerTimelineLayout::layoutRects( listThread, 0, 1000, 100.0f, listRect, listLaneCount );

    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), listRect.size() );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), listLaneCount.size() );
    SW_EXPECT_EQUAL( static_cast<uint16>( 2 ), listLaneCount[0] );
    SW_EXPECT_EQUAL( static_cast<uint16>( 1 ), listLaneCount[1] );

    SW_EXPECT_NEAR_EQUAL( 0.0f, listRect[0]._x0, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 50.0f, listRect[0]._x1, 0.001f );
    SW_EXPECT_TRUE( listRect[0]._bShowsLabel == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( 10.0f, listRect[1]._x0, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, listRect[1]._x1, 0.001f );
    SW_EXPECT_EQUAL( static_cast<uint16>( 1 ), listRect[1]._depth );
    SW_EXPECT_TRUE( listRect[1]._bShowsLabel == SW_FALSE ); // 10 px — 이름을 쓰기에 좁다
    SW_EXPECT_EQUAL( 1u, listRect[2]._threadIndex );
    SW_EXPECT_NEAR_EQUAL( 90.0f, listRect[2]._x0, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, listRect[2]._x1, 0.001f ); // 폭 안으로 잘렸다
}

/**
 * @brief [ProfilerTimelineLayoutTest] 아주 짧은 사건도 한 픽셀은 보인다
 */
SW_TEST_CASE( ProfilerTimelineLayoutTest, TinyEventKeepsOnePixel )
{
    sw::vector<sw::ProfilerTimelineThread> listThread( 1 );
    addEventInternal( listThread[0], 500, 501, 0 );

    sw::vector<sw::editor::ProfilerTimelineRect> listRect;
    sw::vector<uint16>                           listLaneCount;
    sw::editor::ProfilerTimelineLayout::layoutRects( listThread, 0, 1000000, 100.0f, listRect, listLaneCount );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listRect.size() );
    SW_EXPECT_TRUE( listRect[0]._x1 - listRect[0]._x0 >= sw::editor::ProfilerTimelineLayout::kMinRectWidthPixels - 0.001f );
}

/**
 * @brief [ProfilerTimelineLayoutTest] 확대는 커서 자리의 시각을 지키고, 이동 · 확대는 한도 밖으로 나가지 않는다
 */
SW_TEST_CASE( ProfilerTimelineLayoutTest, ZoomKeepsPivotAndPanStaysInLimits )
{
    uint64 begin = 0;
    uint64 end   = 1000000;
    sw::editor::ProfilerTimelineLayout::zoom( begin, end, 25.0f, 100.0f, 0.5f, 0, 1000000 );
    SW_EXPECT_EQUAL( 125000ull, begin ); // 커서(250000)가 같은 자리(1/4)에 남는다
    SW_EXPECT_EQUAL( 625000ull, end );

    sw::editor::ProfilerTimelineLayout::pan( begin, end, 100.0f, 100.0f, 0, 1000000 ); // 한 화면 오른쪽으로 끌기 — 이른 쪽 끝에서 멈춘다
    SW_EXPECT_EQUAL( 0ull, begin );
    SW_EXPECT_EQUAL( 500000ull, end );

    sw::editor::ProfilerTimelineLayout::zoom( begin, end, 50.0f, 100.0f, 4.0f, 0, 1000000 ); // 한도보다 넓게 줄이면 한도 전체
    SW_EXPECT_EQUAL( 0ull, begin );
    SW_EXPECT_EQUAL( 1000000ull, end );
}
