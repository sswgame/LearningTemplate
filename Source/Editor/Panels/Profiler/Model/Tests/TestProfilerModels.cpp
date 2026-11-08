#include "pch.h"

#include "Core/Concurrency/thread.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Profiler/Model/ProfilerCallNode.h"
#include "Editor/Panels/Profiler/Model/ProfilerCaptureModel.h"
#include "Editor/Panels/Profiler/Model/ProfilerPerformanceModel.h"
#include "Editor/Panels/Profiler/Model/ProfilerScopeHistory.h"
#include "Editor/Panels/Profiler/Model/ProfilerTimelineLayout.h"
#include "Editor/Panels/Profiler/Model/ProfilerTimelineModel.h"

#include "Engine/Profiling/FrameProfiler.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct ProfilerModelTestUtil
    {
        static ProfilerTimelineEvent makeEvent( uint64 begin, uint64 end, uint32 slot, uint16 depth )
        {
            ProfilerTimelineEvent event;
            event._beginNanos = begin;
            event._endNanos   = end;
            event._slot       = slot;
            event._depth      = depth;
            return event;
        }

        static ProfilerTimelineThread makeThread( const utf8* pName, const vector<ProfilerTimelineEvent>& listEvent )
        {
            ProfilerTimelineThread thread;
            thread._name      = pName;
            thread._listEvent = listEvent;
            return thread;
        }

        /** @brief 켜진 프로파일러입니다. 칸이 많아 힙에 둡니다. */
        static unique_ptr<FrameProfiler> makeProfiler()
        {
            unique_ptr<FrameProfiler> pProfiler = make_unique<FrameProfiler>();
            pProfiler->setEnabled( true );
            return pProfiler;
        }

        /** @brief 구간 @p slot 이 @p micro 마이크로초인 프레임 하나를 접고 담습니다. */
        static void addFrame( FrameProfiler& profiler, ProfilerScopeHistory& history, uint32 slot, uint64 micro )
        {
            profiler.beginFrame();
            profiler.addSample( slot, micro * 1000u );
            profiler.endFrame();
            SW_EXPECT_TRUE( history.capture( profiler ) );
        }

        static string makeTempPath( const utf8* pName ) { return test::makeScratchPath( pName ); }
    };
    using Util = ProfilerModelTestUtil;
} // namespace

// ---------------------------------------------------------------------- 호출 트리

// 같은 부모 아래 같은 구간은 접고, 자기 시간은 자식을 뺀 값이다.
SW_TEST_CASE( ProfilerCallTreeTest, FoldsCallsAndComputesSelfTime )
{
    vector<ProfilerCallNode> listNode;
    ProfilerCallNode::compute( { Util::makeThread( "Main", {
                                                               Util::makeEvent( 0, 100, 1, 0 ),
                                                               Util::makeEvent( 10, 30, 2, 1 ),
                                                               Util::makeEvent( 40, 60, 2, 1 ),
                                                               Util::makeEvent( 60, 80, 3, 1 ),
                                                               Util::makeEvent( 100, 150, 1, 0 ),
                                                           } ) },
                               listNode );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listNode.size() );
    SW_EXPECT_EQUAL( uint64{ 150 }, listNode[0]._totalNanos );
    SW_EXPECT_EQUAL( uint64{ 90 }, listNode[0]._selfNanos );
    SW_EXPECT_EQUAL( 2u, listNode[0]._callCount );
    SW_EXPECT_EQUAL( ProfilerCallNode::kNoParent, listNode[0]._parent );
    SW_EXPECT_EQUAL( uint64{ 40 }, listNode[1]._totalNanos );
    SW_EXPECT_EQUAL( 2u, listNode[1]._callCount );
    SW_EXPECT_EQUAL( 0u, listNode[1]._parent );
    SW_EXPECT_EQUAL( 1u, listNode[0]._firstChild );
    SW_EXPECT_EQUAL( 2u, listNode[1]._nextSibling );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( listNode[2]._depth ) );
}

// 스레드가 다르거나 부모가 다르면 같은 슬롯도 다른 노드다.
SW_TEST_CASE( ProfilerCallTreeTest, ThreadsAndParentsKeepNodesApart )
{
    vector<ProfilerCallNode> listNode;
    ProfilerCallNode::compute( { Util::makeThread( "A", { Util::makeEvent( 0, 10, 1, 0 ), Util::makeEvent( 1, 2, 5, 1 ), Util::makeEvent( 10, 20, 2, 0 ), Util::makeEvent( 11, 12, 5, 1 ) } ),
                                 Util::makeThread( "B", { Util::makeEvent( 0, 10, 1, 0 ) } ) },
                               listNode );
    SW_REQUIRE_EQUAL( size_t{ 5 }, listNode.size() );
    SW_EXPECT_EQUAL( 0u, listNode[1]._parent );
    SW_EXPECT_EQUAL( 2u, listNode[3]._parent );
    SW_EXPECT_EQUAL( 1u, listNode[4]._threadIndex );
}

// 녹화 링이 바깥 구간을 잃어도 있는 데까지만 붙이고, 틀린 시각에 감기지 않는다.
SW_TEST_CASE( ProfilerCallTreeTest, ToleratesLostParentsAndBadTimes )
{
    vector<ProfilerCallNode> listNode;
    ProfilerCallNode::compute( { Util::makeThread( "A", { Util::makeEvent( 0, 10, 1, 2 ), Util::makeEvent( 2, 500, 2, 3 ), Util::makeEvent( 30, 20, 3, 4 ) } ) }, listNode );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listNode.size() );
    SW_EXPECT_EQUAL( ProfilerCallNode::kNoParent, listNode[0]._parent ); // 앞이 덮였다 — 루트로 붙인다
    SW_EXPECT_EQUAL( 0, static_cast<int32>( listNode[0]._depth ) );
    SW_EXPECT_EQUAL( 0u, listNode[1]._parent );
    SW_EXPECT_EQUAL( uint64{ 0 }, listNode[0]._selfNanos );  // 자식이 더 길어도 감기지 않는다
    SW_EXPECT_EQUAL( uint64{ 0 }, listNode[2]._totalNanos ); // 끝이 시작보다 빠르면 0
}

SW_TEST_CASE( ProfilerCallTreeTest, SortingPutsTheBiggestFirst )
{
    vector<ProfilerCallNode> listNode;
    ProfilerCallNode::compute( { Util::makeThread( "A", { Util::makeEvent( 0, 100, 1, 0 ), Util::makeEvent( 0, 10, 2, 1 ), Util::makeEvent( 10, 50, 3, 1 ), Util::makeEvent( 200, 500, 4, 0 ) } ),
                                 Util::makeThread( "B", { Util::makeEvent( 0, 900, 5, 0 ) } ) },
                               listNode );
    ProfilerCallNode::sortChildrenByTotal( listNode );
    SW_EXPECT_EQUAL( 3u, listNode[listNode[0]._firstChild]._slot );
    SW_EXPECT_EQUAL( 2u, listNode[listNode[listNode[0]._firstChild]._nextSibling]._slot );

    vector<uint32> listRoot;
    ProfilerCallNode::collectRoots( listNode, listRoot );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listRoot.size() );
    SW_EXPECT_EQUAL( 4u, listNode[listRoot[0]]._slot ); // 스레드 순, 그 안에서 긴 것
    SW_EXPECT_EQUAL( 1u, listNode[listRoot[1]]._slot );
    SW_EXPECT_EQUAL( 5u, listNode[listRoot[2]]._slot );
}

SW_TEST_CASE( ProfilerCallTreeWindowTest, OldFramesLeaveTheTotals )
{
    ProfilerCallTreeWindow window;
    window.reset( 2 );
    window.addFrame( { Util::makeThread( "Main", { Util::makeEvent( 0, 10, 1, 0 ) } ) } );
    window.addFrame( { Util::makeThread( "Main", { Util::makeEvent( 0, 20, 1, 0 ) } ) } );
    SW_EXPECT_EQUAL( uint64{ 30 }, window.getNodes()[0]._totalNanos );

    window.addFrame( { Util::makeThread( "Main", { Util::makeEvent( 0, 40, 2, 0 ) } ) } );
    SW_EXPECT_EQUAL( 2u, window.getFrameCount() );
    SW_EXPECT_EQUAL( uint64{ 20 }, window.getNodes()[0]._totalNanos );
    SW_EXPECT_EQUAL( 2u, window.countLiveNodes() );

    window.addFrame( { Util::makeThread( "Main", { Util::makeEvent( 0, 40, 2, 0 ) } ) } );
    SW_EXPECT_EQUAL( 1u, window.countLiveNodes() ); // 창에서 나간 경로는 살아 있지 않다
    window.refreshOrder();
    SW_REQUIRE_EQUAL( size_t{ 1 }, window.getRoots().size() );
    SW_EXPECT_EQUAL( 2u, window.getNodes()[window.getRoots()[0]]._slot );
}

// 스레드 번호는 프레임마다 다를 수 있어도 이름이 같으면 같은 줄이다.
SW_TEST_CASE( ProfilerCallTreeWindowTest, ThreadsMergeByNameNotIndex )
{
    ProfilerCallTreeWindow window;
    window.reset( 8 );
    window.addFrame( { Util::makeThread( "Main", { Util::makeEvent( 0, 10, 1, 0 ) } ), Util::makeThread( "Render", { Util::makeEvent( 0, 5, 2, 0 ) } ) } );
    window.addFrame( { Util::makeThread( "Render", { Util::makeEvent( 0, 5, 2, 0 ) } ), Util::makeThread( "Main", { Util::makeEvent( 0, 10, 1, 0 ) } ) } );
    SW_EXPECT_EQUAL( size_t{ 2 }, window.getThreadNames().size() );
    SW_EXPECT_EQUAL( size_t{ 2 }, window.getNodes().size() );
    SW_EXPECT_EQUAL( uint64{ 20 }, window.getNodes()[0]._totalNanos );
    SW_EXPECT_EQUAL( uint64{ 10 }, window.getNodes()[1]._totalNanos );
}

SW_TEST_CASE( ProfilerCallTreeWindowTest, ResetEmptiesAndCapacityIsAtLeastOne )
{
    ProfilerCallTreeWindow window;
    window.reset( 0 );
    SW_EXPECT_EQUAL( 1u, window.getFrameCapacity() );
    window.addFrame( { Util::makeThread( "Main", { Util::makeEvent( 0, 10, 1, 0 ) } ) } );
    window.addFrame( { Util::makeThread( "Main", { Util::makeEvent( 0, 10, 1, 0 ) } ) } );
    SW_EXPECT_EQUAL( 1u, window.getFrameCount() );
    window.reset( 4 );
    SW_EXPECT_EQUAL( 0u, window.getFrameCount() );
    SW_EXPECT_TRUE( window.getNodes().empty() );
    SW_EXPECT_TRUE( window.getThreadNames().empty() );
}

// ---------------------------------------------------------------------- 타임라인 배치

SW_TEST_CASE( ProfilerTimelineLayoutTest, EventsBecomeClippedRects )
{
    vector<ProfilerTimelineRect>         listRect;
    vector<uint16>                       listLane;
    const vector<ProfilerTimelineThread> listThread = { Util::makeThread( "A", {
                                                                                   Util::makeEvent( 50, 150, 1, 0 ),   // 왼쪽이 잘린다
                                                                                   Util::makeEvent( 150, 151, 2, 1 ),  // 아주 좁다
                                                                                   Util::makeEvent( 900, 1000, 3, 2 ), // 범위 밖 — 줄 수에는 든다
                                                                               } ) };
    ProfilerTimelineLayout::layoutRects( listThread, 100, 200, 1000.0f, listRect, listLane );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listRect.size() );
    SW_EXPECT_NEAR( 0.0f, listRect[0]._x0, 1e-3f );
    SW_EXPECT_NEAR( 500.0f, listRect[0]._x1, 1e-3f );
    SW_EXPECT_TRUE( listRect[0]._bShowsLabel == SW_TRUE );
    SW_EXPECT_TRUE( listRect[1]._x1 - listRect[1]._x0 >= ProfilerTimelineLayout::kMinRectWidthPixels );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( listRect[1]._depth ) );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( listLane[0] ) );
}

SW_TEST_CASE( ProfilerTimelineLayoutTest, EmptyRangeOrWidthMakesNoRects )
{
    vector<ProfilerTimelineRect>         listRect;
    vector<uint16>                       listLane;
    const vector<ProfilerTimelineThread> listThread = { Util::makeThread( "A", { Util::makeEvent( 0, 10, 1, 0 ) } ) };
    ProfilerTimelineLayout::layoutRects( listThread, 100, 100, 1000.0f, listRect, listLane );
    SW_EXPECT_TRUE( listRect.empty() );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( listLane[0] ) );
    ProfilerTimelineLayout::layoutRects( listThread, 0, 100, 0.0f, listRect, listLane );
    SW_EXPECT_TRUE( listRect.empty() );
    SW_EXPECT_EQUAL( 0.0f, ProfilerTimelineLayout::computeX( 5, 10, 10, 100.0f ) );
    SW_EXPECT_NEAR( 50.0f, ProfilerTimelineLayout::computeX( 150, 100, 200, 100.0f ), 1e-3f );
}

// 확대해도 커서 아래 시각은 그대로다.
SW_TEST_CASE( ProfilerTimelineLayoutTest, ZoomKeepsThePivotStill )
{
    uint64 begin = 10000;
    uint64 end   = 20000;
    ProfilerTimelineLayout::zoom( begin, end, 25.0f, 100.0f, 0.5f, 0, 100000 );
    SW_EXPECT_EQUAL( uint64{ 11250 }, begin ); // 커서는 앞에서 4 분의 1 자리, 시각 12500
    SW_EXPECT_EQUAL( uint64{ 16250 }, end );
}

SW_TEST_CASE( ProfilerTimelineLayoutTest, ZoomStopsAtTheLimits )
{
    uint64 begin = 1000;
    uint64 end   = 2000;
    ProfilerTimelineLayout::zoom( begin, end, 50.0f, 100.0f, 100.0f, 500, 3000 ); // 한도보다 넓다
    SW_EXPECT_EQUAL( uint64{ 500 }, begin );
    SW_EXPECT_EQUAL( uint64{ 3000 }, end );

    begin = 1000;
    end   = 1100;
    ProfilerTimelineLayout::zoom( begin, end, 50.0f, 100.0f, 0.001f, 0, 100000 ); // 최소 폭 아래로 가지 않는다
    SW_EXPECT_EQUAL( ProfilerTimelineLayout::kMinSpanNanos, end - begin );

    begin = 1000;
    end   = 2000;
    ProfilerTimelineLayout::zoom( begin, end, 50.0f, 0.0f, 0.5f, 0, 10000 ); // 틀린 입력은 그대로 둔다
    SW_EXPECT_EQUAL( uint64{ 1000 }, begin );
    SW_EXPECT_EQUAL( uint64{ 2000 }, end );
}

SW_TEST_CASE( ProfilerTimelineLayoutTest, PanMovesAndStopsAtTheEdge )
{
    uint64 begin = 1000;
    uint64 end   = 2000;
    ProfilerTimelineLayout::pan( begin, end, 10.0f, 100.0f, 0, 5000 );
    SW_EXPECT_EQUAL( uint64{ 900 }, begin );
    SW_EXPECT_EQUAL( uint64{ 1900 }, end );

    ProfilerTimelineLayout::pan( begin, end, 10.0f, 100.0f, 850, 5000 );
    SW_EXPECT_EQUAL( uint64{ 850 }, begin );
    SW_EXPECT_EQUAL( uint64{ 1850 }, end ); // 폭은 그대로다

    ProfilerTimelineLayout::pan( begin, end, -1000.0f, 100.0f, 0, 2000 );
    SW_EXPECT_EQUAL( uint64{ 2000 }, end );
    SW_EXPECT_EQUAL( uint64{ 1000 }, begin );
}

// ---------------------------------------------------------------------- 구간 기록

SW_TEST_CASE( ProfilerScopeHistoryTest, CapturesEachProfilerFrameOnce )
{
    unique_ptr<FrameProfiler> pProfiler = Util::makeProfiler();
    const uint32              slot      = pProfiler->registerScope( "A" );
    ProfilerScopeHistory      history( 4 );
    Util::addFrame( *pProfiler, history, slot, 5 );
    SW_EXPECT_FALSE( history.capture( *pProfiler ) ); // 새 프레임이 없다
    SW_EXPECT_EQUAL( uint64{ 1 }, history.getCapturedFrameCount() );

    float32 value = 0.0f;
    SW_REQUIRE_TRUE( history.readValue( "A", 0, value ) );
    SW_EXPECT_NEAR( 5.0f, value, 1e-3f );
    SW_EXPECT_FALSE( history.readValue( "A", 4, value ) ); // 창 밖
    SW_EXPECT_FALSE( history.readValue( "B", 0, value ) );

    pProfiler->setEnabled( false );
    SW_EXPECT_FALSE( history.capture( *pProfiler ) );
}

SW_TEST_CASE( ProfilerScopeHistoryTest, RowsSummariseTheWindow )
{
    unique_ptr<FrameProfiler> pProfiler = Util::makeProfiler();
    const uint32              slot      = pProfiler->registerScope( "A" );
    ProfilerScopeHistory      history( 8 );
    for ( uint64 micro = 1; micro <= 4; ++micro )
    {
        Util::addFrame( *pProfiler, history, slot, micro );
    }
    ProfilerRowQuery         query;
    vector<ProfilerScopeRow> listRow;
    history.makeRows( query, listRow );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listRow.size() );
    SW_EXPECT_EQUAL( 4u, listRow[0]._sampleCount );
    SW_EXPECT_NEAR( 4.0, listRow[0]._last, 1e-3 );
    SW_EXPECT_NEAR( 2.5, listRow[0]._average, 1e-3 );
    SW_EXPECT_NEAR( 2.0, listRow[0]._p50, 1e-3 );
    SW_EXPECT_NEAR( 4.0, listRow[0]._p99, 1e-3 );
    SW_EXPECT_NEAR( 4.0, listRow[0]._max, 1e-3 );

    query._frameOffset = 1; // 그래프에서 고른 프레임
    history.makeRows( query, listRow );
    SW_EXPECT_NEAR( 3.0, listRow[0]._last, 1e-3 );
}

SW_TEST_CASE( ProfilerScopeHistoryTest, QueryFiltersKindNameAndSorts )
{
    unique_ptr<FrameProfiler> pProfiler = Util::makeProfiler();
    const uint32              slotAlpha = pProfiler->registerScope( "Alpha" );
    const uint32              slotBeta  = pProfiler->registerScope( "Beta" );
    const uint32              slotGPU   = pProfiler->registerScope( "GPU.Pass" );
    const uint32              slotCount = pProfiler->registerScope( "Draws" );
    pProfiler->addSample( slotAlpha, 1000 );
    pProfiler->addSample( slotBeta, 3000 );
    pProfiler->addSample( slotGPU, 2000 );
    pProfiler->addCount( slotCount, 7 );
    pProfiler->endFrame();
    ProfilerScopeHistory history( 4 );
    SW_REQUIRE_TRUE( history.capture( *pProfiler ) );

    ProfilerRowQuery         query;
    vector<ProfilerScopeRow> listRow;
    query._sortColumn = ProfilerSortColumn::Last;
    history.makeRows( query, listRow );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listRow.size() );
    SW_EXPECT_TRUE( listRow[0]._name == "Beta" );

    query._bDescending = false;
    history.makeRows( query, listRow );
    SW_EXPECT_TRUE( listRow[0]._name == "Alpha" );

    query._sortColumn = ProfilerSortColumn::Name;
    history.makeRows( query, listRow );
    SW_EXPECT_TRUE( listRow[0]._name == "Alpha" );
    query._bDescending = true;
    history.makeRows( query, listRow );
    SW_EXPECT_TRUE( listRow[0]._name == "Beta" );

    query._filterText = "alp"; // 대소문자를 보지 않는다
    history.makeRows( query, listRow );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listRow.size() );
    SW_EXPECT_TRUE( listRow[0]._name == "Alpha" );

    query._filterText.clear();
    query._kind = ProfilerScopeKind::GPU;
    history.makeRows( query, listRow );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listRow.size() );
    SW_EXPECT_TRUE( listRow[0]._name == "GPU.Pass" );

    query._kind = ProfilerScopeKind::Counter;
    history.makeRows( query, listRow );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listRow.size() );
    SW_EXPECT_TRUE( listRow[0]._name == "Draws" );
    SW_EXPECT_NEAR( 7.0, listRow[0]._last, 1e-3 );
}

// 고리가 돌면 오래된 값이 나가고, 안 불린 프레임은 그래프에서 0 이다.
SW_TEST_CASE( ProfilerScopeHistoryTest, SeriesRunsOldestFirstThroughTheRing )
{
    unique_ptr<FrameProfiler> pProfiler = Util::makeProfiler();
    const uint32              slot      = pProfiler->registerScope( "A" );
    ProfilerScopeHistory      history( 3 );
    Util::addFrame( *pProfiler, history, slot, 1 );
    pProfiler->endFrame(); // 이 프레임에는 불리지 않았다
    SW_REQUIRE_TRUE( history.capture( *pProfiler ) );
    Util::addFrame( *pProfiler, history, slot, 3 );
    Util::addFrame( *pProfiler, history, slot, 4 );

    vector<float32> listValue;
    SW_REQUIRE_TRUE( history.copySeries( "A", listValue ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listValue.size() );
    SW_EXPECT_NEAR( 0.0f, listValue[0], 1e-3f );
    SW_EXPECT_NEAR( 3.0f, listValue[1], 1e-3f );
    SW_EXPECT_NEAR( 4.0f, listValue[2], 1e-3f );
    float32 value = 0.0f;
    SW_EXPECT_FALSE( history.readValue( "A", 2, value ) ); // 안 불린 프레임
    SW_EXPECT_FALSE( history.copySeries( "NoSuchScope", listValue ) );
}

SW_TEST_CASE( ProfilerScopeHistoryTest, ResetForgetsEverything )
{
    unique_ptr<FrameProfiler> pProfiler = Util::makeProfiler();
    const uint32              slot      = pProfiler->registerScope( "A" );
    ProfilerScopeHistory      history( 4 );
    Util::addFrame( *pProfiler, history, slot, 1 );
    history.reset();
    SW_EXPECT_EQUAL( uint64{ 0 }, history.getCapturedFrameCount() );
    vector<ProfilerScopeRow> listRow;
    history.makeRows( ProfilerRowQuery{}, listRow );
    SW_EXPECT_TRUE( listRow.empty() );
}

SW_TEST_CASE( ProfilerScopeHistoryTest, FileRoundTripKeepsTheSeries )
{
    unique_ptr<FrameProfiler> pProfiler = Util::makeProfiler();
    const uint32              slot      = pProfiler->registerScope( "A" );
    const uint32              slotGPU   = pProfiler->registerScope( "GPU.Pass" );
    ProfilerScopeHistory      history( 4 );
    pProfiler->addSample( slotGPU, 2000 );
    Util::addFrame( *pProfiler, history, slot, 1 );
    Util::addFrame( *pProfiler, history, slot, 2 );

    const string path = Util::makeTempPath( "SwProfilerRoundTrip.txt" );
    SW_REQUIRE_TRUE( history.saveToFile( path ) );
    ProfilerScopeHistory loaded;
    SW_REQUIRE_TRUE( loaded.loadFromFile( path ) );
    (void)FileUtil::removeFile( path );

    SW_EXPECT_EQUAL( 4u, loaded.getWindowFrame() );
    SW_EXPECT_EQUAL( uint64{ 2 }, loaded.getCapturedFrameCount() );
    vector<float32> listExpected;
    vector<float32> listLoaded;
    SW_REQUIRE_TRUE( history.copySeries( "A", listExpected ) );
    SW_REQUIRE_TRUE( loaded.copySeries( "A", listLoaded ) );
    SW_REQUIRE_EQUAL( listExpected.size(), listLoaded.size() );
    for ( size_t index = 0; index < listExpected.size(); ++index )
    {
        SW_EXPECT_NEAR( listExpected[index], listLoaded[index], 1e-3f );
    }
    ProfilerRowQuery         query;
    vector<ProfilerScopeRow> listRow;
    query._kind = ProfilerScopeKind::GPU;
    loaded.makeRows( query, listRow );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listRow.size() ); // 종류도 파일에 있다
    SW_EXPECT_TRUE( listRow[0]._name == "GPU.Pass" );
}

// 틀린 파일은 거절하고 지금 내용을 건드리지 않는다.
SW_TEST_CASE( ProfilerScopeHistoryTest, BadFilesAreRefused )
{
    unique_ptr<FrameProfiler> pProfiler = Util::makeProfiler();
    const uint32              slot      = pProfiler->registerScope( "A" );
    ProfilerScopeHistory      history( 4 );
    Util::addFrame( *pProfiler, history, slot, 1 );

    const string path = Util::makeTempPath( "SwProfilerBad.txt" );
    SW_EXPECT_LOG( "File.NotFound" );
    SW_EXPECT_FALSE( history.loadFromFile( Util::makeTempPath( "SwProfilerNoSuchFile.txt" ) ) );
    const utf8* const arrBad[] = {
        "NotACapture\nwindow 2\ncaptured 1\n",                         // 머리
        "SWProfilerCapture 1\nwindow 0\ncaptured 1\n",                 // 창 0
        "SWProfilerCapture 1\nwindow 200000\ncaptured 1\n",            // 창이 너무 크다
        "SWProfilerCapture 1\nwindow 2\ncaptured 1\n0\tA\t1.0\n",      // 값 수가 창과 다르다
        "SWProfilerCapture 1\nwindow 2\ncaptured 1\n9\tA\t1.0,2.0\n",  // 없는 종류
        "SWProfilerCapture 1\nwindow 2\ncaptured 1\n0\tA\t1.0,oops\n", // 숫자가 아니다
    };
    for ( const utf8* pText : arrBad )
    {
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, pText ) );
        SW_EXPECT_FALSE( history.loadFromFile( path ) );
    }
    (void)FileUtil::removeFile( path );
    SW_EXPECT_EQUAL( 4u, history.getWindowFrame() );
    float32 value = 0.0f;
    SW_EXPECT_TRUE( history.readValue( "A", 0, value ) );
}

// 연 캡처의 트랙 순서는 파일 순서다. 그 뒤 담는 프레임이 다른 이름의 트랙에 섞이면 안 된다.
SW_TEST_CASE( ProfilerScopeHistoryTest, CapturingAfterALoadStartsANewRecording )
{
    unique_ptr<FrameProfiler> pProfiler = Util::makeProfiler();
    const uint32              slot      = pProfiler->registerScope( "Live" );
    const string              path      = Util::makeTempPath( "SwProfilerResume.txt" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "SWProfilerCapture 1\nwindow 3\ncaptured 3\n0\tOldRun\t7.0,7.0,7.0\n" ) );
    ProfilerScopeHistory history( 3 );
    SW_REQUIRE_TRUE( history.loadFromFile( path ) );
    (void)FileUtil::removeFile( path );

    Util::addFrame( *pProfiler, history, slot, 5 );
    float32 value = 0.0f;
    SW_EXPECT_TRUE( history.readValue( "Live", 0, value ) );
    SW_EXPECT_NEAR( 5.0f, value, 1e-3f );
    vector<float32> listOld;
    SW_EXPECT_FALSE( history.copySeries( "OldRun", listOld ) );
    SW_EXPECT_EQUAL( uint64{ 1 }, history.getCapturedFrameCount() );
}

// ---------------------------------------------------------------------- 수집 모델

SW_TEST_CASE( ProfilerCaptureModelTest, TickTurnsTheProfilerOnAndCaptures )
{
    unique_ptr<FrameProfiler> pProfiler = make_unique<FrameProfiler>();
    ProfilerCaptureModel      model;
    model.tick( *pProfiler );
    SW_EXPECT_TRUE( pProfiler->isEnabled() );
    SW_EXPECT_EQUAL( uint64{ 1 }, model.getHistory().getCapturedFrameCount() );
    model.tick( *pProfiler );
    SW_EXPECT_EQUAL( uint64{ 1 }, model.getHistory().getCapturedFrameCount() ); // 새 프레임이 없다

    model.setCollect( *pProfiler, false );
    SW_EXPECT_FALSE( pProfiler->isEnabled() );
    model.tick( *pProfiler );
    SW_EXPECT_FALSE( pProfiler->isEnabled() ); // 끈 수집은 다시 켜지 않는다
}

SW_TEST_CASE( ProfilerCaptureModelTest, PauseStopsCapturingResumeClearsTheSelection )
{
    unique_ptr<FrameProfiler> pProfiler = Util::makeProfiler();
    ProfilerCaptureModel      model;
    model.tick( *pProfiler );
    model.pause();
    model.selectFrameOffset( 3 );
    pProfiler->endFrame();
    model.tick( *pProfiler );
    SW_EXPECT_EQUAL( uint64{ 1 }, model.getHistory().getCapturedFrameCount() );

    model.resume();
    SW_EXPECT_FALSE( model.isPaused() );
    SW_EXPECT_EQUAL( 0u, model.getSelectedFrameOffset() );
    model.tick( *pProfiler );
    SW_EXPECT_EQUAL( uint64{ 2 }, model.getHistory().getCapturedFrameCount() );

    model.selectFrameOffset( 1 );
    model.clear();
    SW_EXPECT_EQUAL( uint64{ 0 }, model.getHistory().getCapturedFrameCount() );
    SW_EXPECT_EQUAL( 0u, model.getSelectedFrameOffset() );
}

SW_TEST_CASE( ProfilerCaptureModelTest, SpikeOverTheThresholdPausesOnlyWhenAsked )
{
    unique_ptr<FrameProfiler> pProfiler = Util::makeProfiler();
    const uint32              slot      = pProfiler->registerScope( "GT.Frame" );
    ProfilerCaptureModel      model;
    model.editSpikeThresholdMs() = 10.0f;

    pProfiler->addSample( slot, 20u * 1000u * 1000u );
    pProfiler->endFrame();
    model.tick( *pProfiler );
    SW_EXPECT_FALSE( model.isPaused() ); // 켜지 않았다

    model.setPauseOnSpike( true );
    pProfiler->addSample( slot, 5u * 1000u * 1000u );
    pProfiler->endFrame();
    model.tick( *pProfiler );
    SW_EXPECT_FALSE( model.isPaused() ); // 임계값 아래

    pProfiler->addSample( slot, 20u * 1000u * 1000u );
    pProfiler->endFrame();
    model.tick( *pProfiler );
    SW_EXPECT_TRUE( model.isPaused() );
}

// ---------------------------------------------------------------------- 성능 탭

// 창이 차기 전에도 평균은 담은 표본의 평균이다. 빈 칸을 0 으로 세면 처음 2 초 동안 FPS 가 몇십 배로 보인다.
SW_TEST_CASE( ProfilerPerformanceModelTest, AverageCountsOnlyTheSamplesTaken )
{
    unique_ptr<ProfilerPerformanceModel> pModel = make_unique<ProfilerPerformanceModel>();
    ThreadUtil::sleepForMilliseconds( 2 );
    const ProfilerFrameTimeStats first = pModel->sampleFrame();
    SW_EXPECT_TRUE( first._currentMs > 0.0f );
    SW_EXPECT_NEAR( first._currentMs, first._averageMs, 1e-3f );
    SW_EXPECT_NEAR( first._currentFPS, first._averageFPS, first._currentFPS * 0.01f );
    SW_EXPECT_NEAR( first._currentMs, first._minMs, 1e-3f );
    SW_EXPECT_NEAR( first._currentMs, first._maxMs, 1e-3f );

    ThreadUtil::sleepForMilliseconds( 2 );
    const ProfilerFrameTimeStats second = pModel->sampleFrame();
    SW_EXPECT_TRUE( second._minMs <= second._averageMs );
    SW_EXPECT_TRUE( second._averageMs <= second._maxMs );
    SW_EXPECT_EQUAL( 2u, pModel->getHistoryOffset() );
}

// 창이 차면 가장 오래된 표본이 나간다(오프셋은 처음으로 돈다).
SW_TEST_CASE( ProfilerPerformanceModelTest, HistoryWrapsAround )
{
    unique_ptr<ProfilerPerformanceModel> pModel = make_unique<ProfilerPerformanceModel>();
    for ( uint32 index = 0; index < ProfilerPerformanceModel::kHistoryCount + 3; ++index )
    {
        (void)pModel->sampleFrame();
    }
    SW_EXPECT_EQUAL( 3u, pModel->getHistoryOffset() );
}

// 녹화를 켜면 수집도 켜지고 지난 사건이 비워진다. 끄면 녹화만 멈추고 수집은 남는다(다른 탭이 쓴다).
SW_TEST_CASE( ProfilerTimelineModelTest, ToggleRecordingStartsCollecting )
{
    for ( const ProfilerRecordingView view : { ProfilerRecordingView::Timeline, ProfilerRecordingView::CallTree } )
    {
        unique_ptr<FrameProfiler> pProfiler = make_unique<FrameProfiler>();
        ProfilerCaptureModel      capture;
        ProfilerTimelineModel     timeline;
        SW_REQUIRE_FALSE( pProfiler->getTimeline().isRecording() );

        timeline.toggleRecording( *pProfiler, capture, view );
        SW_EXPECT_TRUE( pProfiler->getTimeline().isRecording() );
        SW_EXPECT_TRUE( capture.isCollecting() );
        SW_EXPECT_TRUE( pProfiler->isEnabled() );

        timeline.toggleRecording( *pProfiler, capture, view );
        SW_EXPECT_FALSE( pProfiler->getTimeline().isRecording() );
        SW_EXPECT_TRUE( capture.isCollecting() );
    }
}
