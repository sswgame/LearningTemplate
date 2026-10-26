#include "pch.h"

#include "Editor/Panels/Profiler/Model/ProfilerCallTree.h"

#include "TestFramework/TestFramework.h"

// ProfilerCallTree — 프로파일러 패널 Call Tree 탭의 집계(ImGui 없음).

namespace
{
    /** @brief 스레드 하나에 사건을 더합니다. */
    void addCallEventInternal( sw::ProfilerTimelineThread& thread, uint32 slot, uint64 beginNanos, uint64 endNanos, uint16 depth )
    {
        sw::ProfilerTimelineEvent event;
        event._beginNanos = beginNanos;
        event._endNanos   = endNanos;
        event._slot       = slot;
        event._depth      = depth;
        thread._listEvent.push_back( event );
    }
} // namespace

/**
 * @brief [ProfilerCallTreeTest] 같은 경로의 같은 구간은 한 노드로 접히고(횟수 · 합), 자기 시간은 자식을 뺀 값이며, 형제는 합이 큰 순이다
 * @details 프레임(0) 두 번 안에 Update(1) · Render(2), Update 안에 Physics(3) 이 있다. 두 프레임이 한 트리로 접혀야 Unity Hierarchy 처럼 읽힌다.
 */
SW_TEST_CASE( ProfilerCallTreeTest, FoldsPathsAndComputesSelfTime )
{
    sw::vector<sw::ProfilerTimelineThread> listThread( 2 );
    sw::ProfilerTimelineThread&            game = listThread[0];
    for ( uint64 frame = 0; frame < 2; ++frame )
    {
        const uint64 base = frame * 1000;
        addCallEventInternal( game, 0, base, base + 1000, 0 );
        addCallEventInternal( game, 1, base + 0, base + 300, 1 );
        addCallEventInternal( game, 3, base + 50, base + 150, 2 );
        addCallEventInternal( game, 2, base + 300, base + 900, 1 );
    }
    addCallEventInternal( listThread[1], 5, 0, 400, 0 ); // 다른 스레드는 자기 루트

    sw::vector<sw::editor::ProfilerCallNode> listNode;
    sw::editor::ProfilerCallTree::compute( listThread, listNode );
    SW_ASSERT_EQUAL( size_t( 5 ), listNode.size() );
    const sw::editor::ProfilerCallNode& frameNode = listNode[0];
    SW_EXPECT_EQUAL( 2u, frameNode._callCount );
    SW_EXPECT_EQUAL( uint64( 2000 ), frameNode._totalNanos );
    SW_EXPECT_EQUAL( uint64( 200 ), frameNode._selfNanos );   // 2000 - (600 + 1200)
    SW_EXPECT_EQUAL( uint64( 400 ), listNode[1]._selfNanos ); // Update 600 - Physics 200

    sw::editor::ProfilerCallTree::sortChildrenByTotal( listNode );
    const sw::editor::ProfilerCallNode& first = listNode[frameNode._firstChild];
    SW_EXPECT_EQUAL( 2u, first._slot ); // Render(1200) 가 Update(600) 보다 위
    SW_EXPECT_EQUAL( 1u, listNode[first._nextSibling]._slot );

    sw::vector<uint32> listRoot;
    sw::editor::ProfilerCallTree::collectRoots( listNode, listRoot );
    SW_ASSERT_EQUAL( size_t( 2 ), listRoot.size() );
    SW_EXPECT_EQUAL( 0u, listNode[listRoot[0]]._threadIndex );
    SW_EXPECT_EQUAL( 1u, listNode[listRoot[1]]._threadIndex );
}

/**
 * @brief [ProfilerCallTreeTest] 슬라이딩 창은 새 프레임의 몫을 더하고 창을 벗어난 프레임의 몫을 빼서, 같은 프레임들을 한 번에 접은 것과 같은 합을 낸다
 * @details 프레임마다 다른 스레드 순서로 와도 이름으로 같은 스레드에 모인다. 창 밖으로 나간 경로는 호출 0 이 되어 루트 목록 · 살아 있는 노드 수에서 빠진다.
 */
SW_TEST_CASE( ProfilerCallTreeTest, SlidingWindowAddsNewFramesAndSubtractsExpiredOnes )
{
    sw::editor::ProfilerCallTreeWindow window;
    window.reset( 2 );

    // 프레임 1: Game 스레드 Frame(0) 안에 Update(1).
    sw::vector<sw::ProfilerTimelineThread> listFrame( 1 );
    listFrame[0]._name = "Game";
    addCallEventInternal( listFrame[0], 0, 0, 1000, 0 );
    addCallEventInternal( listFrame[0], 1, 0, 400, 1 );
    window.addFrame( listFrame );

    // 프레임 2: Worker 가 앞에 오고 Game 은 같은 경로.
    listFrame.clear();
    listFrame.resize( 2 );
    listFrame[0]._name = "Worker";
    addCallEventInternal( listFrame[0], 7, 1000, 1100, 0 );
    listFrame[1]._name = "Game";
    addCallEventInternal( listFrame[1], 0, 1000, 2000, 0 );
    addCallEventInternal( listFrame[1], 1, 1000, 1600, 1 );
    window.addFrame( listFrame );
    window.refreshOrder();

    const sw::vector<sw::editor::ProfilerCallNode>& listNode = window.getNodes();
    SW_EXPECT_EQUAL( 2u, window.getFrameCount() );
    SW_ASSERT_EQUAL( size_t( 3 ), listNode.size() );
    SW_EXPECT_EQUAL( 2u, listNode[0]._callCount );
    SW_EXPECT_EQUAL( uint64( 2000 ), listNode[0]._totalNanos );
    SW_EXPECT_EQUAL( uint64( 1000 ), listNode[0]._selfNanos ); // 2000 - (400 + 600)
    SW_EXPECT_EQUAL( uint64( 1000 ), listNode[1]._totalNanos );
    SW_ASSERT_EQUAL( size_t( 2 ), window.getRoots().size() );
    SW_EXPECT_STREQ( "Game", window.getThreadNames()[listNode[window.getRoots()[0]]._threadIndex].c_str() );

    // 프레임 3: Game 만. 창이 2 라 프레임 1 이 빠진다.
    listFrame.clear();
    listFrame.resize( 1 );
    listFrame[0]._name = "Game";
    addCallEventInternal( listFrame[0], 0, 2000, 2500, 0 );
    window.addFrame( listFrame );
    window.refreshOrder();
    SW_EXPECT_EQUAL( 2u, window.getFrameCount() );
    SW_EXPECT_EQUAL( 2u, listNode[0]._callCount );
    SW_EXPECT_EQUAL( uint64( 1500 ), listNode[0]._totalNanos );
    SW_EXPECT_EQUAL( 1u, listNode[1]._callCount ); // 프레임 2 의 Update 만 남는다

    // 프레임 4: Game 만. 프레임 2 가 빠지며 Worker 와 Update 는 호출 0 이 된다.
    listFrame[0]._listEvent.clear();
    addCallEventInternal( listFrame[0], 0, 3000, 3200, 0 );
    window.addFrame( listFrame );
    window.refreshOrder();
    SW_EXPECT_EQUAL( 1u, window.countLiveNodes() );
    SW_ASSERT_EQUAL( size_t( 1 ), window.getRoots().size() );
    SW_EXPECT_EQUAL( uint64( 700 ), listNode[window.getRoots()[0]]._totalNanos );
}
