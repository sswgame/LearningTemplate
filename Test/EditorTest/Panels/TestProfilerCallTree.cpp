#include "pch.h"

#include "Editor/Panels/ProfilerCallTree.h"

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
    addCallEventInternal( listThread[1], 5, 0, 400, 0 ); // 다른 스레드는 자기 뿌리

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
