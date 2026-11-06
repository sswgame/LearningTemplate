#include "pch.h"

#include "Core/Container/DependencyGraph.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    struct DependencyGraphTestUtil
    {
        /** @brief 0:A → 1:B → 2:C → 0:A 순환, 3:D → 0:A(매달림), 4:E 독립입니다. */
        static DependencyGraph makeCycleGraph()
        {
            DependencyGraph graph{ 5 };
            graph.addDependency( 0, 1 );
            graph.addDependency( 1, 2 );
            graph.addDependency( 2, 0 );
            graph.addDependency( 3, 0 );
            return graph;
        }
    };
} // namespace

/** @brief [DependencyGraphTest] 이름 정렬: 의존이 먼저 오고, 동시에 준비된 것은 이름 순이다(입력 순서와 무관) */
SW_TEST_CASE( DependencyGraphTest, SortByNameDependenciesFirstThenByName )
{
    // 0:Game ← 1:Kit, 2:Framework / 1:Kit ← 2:Framework / 3:Editor 독립
    const vector<string_view> listName{ "Game", "Kit", "Framework", "Editor" };
    DependencyGraph           graph{ 4 };
    graph.addDependency( 0, 1 );
    graph.addDependency( 0, 2 );
    graph.addDependency( 1, 2 );
    const DependencyOrder order = graph.sortByName( listName );
    SW_REQUIRE_TRUE( order.isSorted() );
    const vector<uint32> listExpected{ 3, 2, 1, 0 }; // Editor · Framework 가 함께 준비됐을 때 이름이 앞인 Editor 가 먼저다
    SW_EXPECT_TRUE( order._listOrder == listExpected );
}

/** @brief [DependencyGraphTest] 데이터 순서 정렬: 준비된 것 가운데 번호가 작은 것이 먼저다 */
SW_TEST_CASE( DependencyGraphTest, SortInDataOrderPicksSmallestReadyIndex )
{
    // 0 ← 1, 1 · 2 는 처음부터 준비됐다. 1 을 꺼내면 0 이 준비되고 0 이 2 보다 번호가 작다.
    DependencyGraph graph{ 3 };
    graph.addDependency( 0, 1 );
    const DependencyOrder order = graph.sortInDataOrder();
    SW_REQUIRE_TRUE( order.isSorted() );
    const vector<uint32> listExpected{ 1, 0, 2 };
    SW_EXPECT_TRUE( order._listOrder == listExpected );
}

/** @brief [DependencyGraphTest] Kahn 이 실패하면 정렬하지 못한 노드와 순환 경로를 값으로 돌려준다(매달린 노드는 경로에 들지 않는다) */
SW_TEST_CASE( DependencyGraphTest, KahnCycleIsReportedWithItsPath )
{
    const DependencyGraph graph = DependencyGraphTestUtil::makeCycleGraph();
    const DependencyOrder order = graph.sortInDataOrder();
    SW_EXPECT_FALSE( order.isSorted() );
    SW_EXPECT_TRUE( order._listOrder.empty() );
    SW_EXPECT_EQUAL( size_t{ 4 }, order._listUnsorted.size() ); // E 만 정렬됐다
    const vector<uint32> listCycle{ 0, 1, 2, 0 };
    SW_EXPECT_TRUE( order._listCycle == listCycle );
}

/** @brief [DependencyGraphTest] 깊이 우선: 뿌리에서 닿는 노드만 의존 먼저 담고, 간선을 더한 차례로 내려간다 */
SW_TEST_CASE( DependencyGraphTest, SortDepthFirstVisitsReachableInEdgeOrder )
{
    // 뿌리 0 ← 2, 1 / 2 ← 3 / 4 는 뿌리에서 닿지 않는다.
    DependencyGraph graph{ 5 };
    graph.addDependency( 0, 2 );
    graph.addDependency( 0, 1 );
    graph.addDependency( 2, 3 );
    graph.addDependency( 1, 3 ); // 이미 담은 노드는 다시 담지 않는다
    const DependencyOrder order = graph.sortDepthFirst( { 0 } );
    SW_REQUIRE_TRUE( order.isSorted() );
    const vector<uint32> listExpected{ 3, 2, 1, 0 };
    SW_EXPECT_TRUE( order._listOrder == listExpected );
}

/** @brief [DependencyGraphTest] 깊이 우선이 순환을 만나면 멈추고, 다시 만난 노드부터의 경로를 돌려준다. 자기 간선도 순환이다 */
SW_TEST_CASE( DependencyGraphTest, SortDepthFirstReportsCycle )
{
    const DependencyGraph graph = DependencyGraphTestUtil::makeCycleGraph();
    const DependencyOrder order = graph.sortDepthFirst( { 3 } );
    SW_EXPECT_FALSE( order.isSorted() );
    SW_EXPECT_TRUE( order._listOrder.empty() );
    const vector<uint32> listCycle{ 0, 1, 2, 0 };
    SW_EXPECT_TRUE( order._listCycle == listCycle );

    DependencyGraph self{ 1 };
    self.addDependency( 0, 0 );
    const vector<uint32> listSelf{ 0, 0 };
    SW_EXPECT_TRUE( self.sortDepthFirst( { 0 } )._listCycle == listSelf );
    SW_EXPECT_TRUE( self.findCycle( { 0 } ) == listSelf );
}

/** @brief [DependencyGraphTest] 순환 찾기는 후보 안의 간선만 따른다 */
SW_TEST_CASE( DependencyGraphTest, FindCycleStaysInsideCandidates )
{
    const DependencyGraph graph = DependencyGraphTestUtil::makeCycleGraph();
    SW_EXPECT_TRUE( graph.findCycle( { 0, 1, 3 } ).empty() ); // 2 가 빠지면 고리가 끊긴다
    SW_EXPECT_EQUAL( size_t{ 4 }, graph.findCycle( { 3, 0, 1, 2 } ).size() );
}
