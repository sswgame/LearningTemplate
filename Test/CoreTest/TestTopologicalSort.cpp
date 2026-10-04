/**
 * @file TestTopologicalSort.cpp
 * @brief 위상 정렬(Kahn, 동점은 이름 순)과 순환 경로 찾기 — 엔진 기동 단계와 모듈 적재 순서가 같은 함수를 쓴다.
 */
#include "pch.h"

#include "Core/Common/TopologicalSortUtil.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [TopologicalSortTest] 의존이 먼저 오고, 동시에 준비된 것은 이름 순이다(입력 순서와 무관)
 */
SW_TEST_CASE( TopologicalSortTest, DependenciesFirstThenByName )
{
    // 0:Game ← 1:Kit, 2:Framework / 1:Kit ← 2:Framework / 3:Editor 독립
    const sw::vector<sw::string_view>    listName{ "Game", "Kit", "Framework", "Editor" };
    const sw::vector<sw::vector<uint32>> listDependency{
        { 1, 2 },
        { 2 },
        {},
        {}
    };
    sw::vector<uint32> listOrder;
    sw::vector<uint32> listUnsorted;
    SW_ASSERT_TRUE( sw::TopologicalSortUtil::sortByDependency( listName, listDependency, listOrder, listUnsorted ) );
    SW_ASSERT_EQUAL( size_t{ 4 }, listOrder.size() );
    // Editor · Framework 가 함께 준비됐을 때 이름이 앞인 Editor 가 먼저다.
    SW_EXPECT_EQUAL( uint32{ 3 }, listOrder[0] );
    SW_EXPECT_EQUAL( uint32{ 2 }, listOrder[1] );
    SW_EXPECT_EQUAL( uint32{ 1 }, listOrder[2] );
    SW_EXPECT_EQUAL( uint32{ 0 }, listOrder[3] );
    SW_EXPECT_TRUE( listUnsorted.empty() );
}

/**
 * @brief [TopologicalSortTest] 순환이면 실패하고, 정렬하지 못한 노드와 순환 경로를 돌려준다(순환에 매달린 노드는 경로에 들지 않는다)
 */
SW_TEST_CASE( TopologicalSortTest, CycleIsReportedWithItsPath )
{
    // 0:A → 1:B → 2:C → 0:A, 3:D → 0:A(매달림), 4:E 독립
    const sw::vector<sw::string_view>    listName{ "A", "B", "C", "D", "E" };
    const sw::vector<sw::vector<uint32>> listDependency{ { 1 }, { 2 }, { 0 }, { 0 }, {} };
    sw::vector<uint32>                   listOrder;
    sw::vector<uint32>                   listUnsorted;
    SW_EXPECT_FALSE( sw::TopologicalSortUtil::sortByDependency( listName, listDependency, listOrder, listUnsorted ) );
    SW_EXPECT_TRUE( listOrder.empty() );
    SW_EXPECT_EQUAL( size_t{ 4 }, listUnsorted.size() ); // E 만 정렬됐다

    sw::vector<uint32> listCycle;
    SW_ASSERT_TRUE( sw::TopologicalSortUtil::findCycle( listDependency, listUnsorted, listCycle ) );
    SW_ASSERT_EQUAL( size_t{ 4 }, listCycle.size() );
    SW_EXPECT_EQUAL( uint32{ 0 }, listCycle[0] );
    SW_EXPECT_EQUAL( uint32{ 1 }, listCycle[1] );
    SW_EXPECT_EQUAL( uint32{ 2 }, listCycle[2] );
    SW_EXPECT_EQUAL( uint32{ 0 }, listCycle[3] );
}
