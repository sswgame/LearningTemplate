#include "pch.h"

#include "Editor/Common/Workspace/EditorStateHashCompare.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorStateHashCompareTest] 두 이력이 겹치는 틱만 비교하고, 처음 어긋난 틱과 겹친 틱 수를 낸다 — 한쪽에만 있는 틱은 어긋남이 아니다
 */
SW_TEST_CASE( EditorStateHashCompareTest, FindsTheFirstDivergentTickOverCommonTicks )
{
    EditorStateHashCompare compare;
    SW_ASSERT_TRUE( compare.loadFromReport( 0, "A", "10 00000000000000aa\n11 00000000000000bb\n12 00000000000000cc\n13 00000000000000dd\n" ) );
    SW_ASSERT_TRUE( compare.loadFromReport( 1, "B", "13 00000000000000ff\n11 00000000000000bb\n12 00000000000000c0\n14 0000000000000011\n" ) );
    SW_EXPECT_FALSE( compare.isComparisonCurrent() );
    SW_ASSERT_TRUE( compare.compare() );
    SW_EXPECT_TRUE( compare.isComparisonCurrent() );
    SW_EXPECT_TRUE( compare.getComparison()._bDiverged );
    SW_EXPECT_EQUAL( uint64{ 12 }, compare.getComparison()._firstDivergentTick );
    SW_EXPECT_EQUAL( uint32{ 3 }, compare.getComparison()._commonTickCount );
    SW_EXPECT_EQUAL( uint64{ 11 }, compare.getSource( 1 )._listSample.front()._tick ); // 담을 때 틱 순으로 정렬한다
}

/**
 * @brief [EditorStateHashCompareTest] 이력이 아닌 글(서버 오류 답)과 빈 이력은 칸에 담지 않는다 — 빈 칸을 "어긋남 없음" 으로 비교하지 않는다
 */
SW_TEST_CASE( EditorStateHashCompareTest, RejectsTextThatIsNotAHistory )
{
    EditorStateHashCompare compare;
    SW_EXPECT_FALSE( compare.loadFromReport( 0, "A", "state hash history is off - start with -gv_stateHashHistory=<ticks>" ) );
    SW_EXPECT_FALSE( compare.loadFromReport( 0, "A", "\n" ) );
    SW_EXPECT_TRUE( compare.getSource( 0 )._listSample.empty() );
    SW_EXPECT_FALSE( compare.compare() );
    SW_EXPECT_FALSE( compare.getLastError().empty() );
}

/**
 * @brief [EditorStateHashCompareTest] 두 오브젝트 해시 답을 견줘 다른 오브젝트 이름만 낸다 — 한쪽에만 있는 오브젝트도 다른 것이다
 */
SW_TEST_CASE( EditorStateHashCompareTest, NamesTheObjectsWhoseHashesDiffer )
{
    EditorStateHashCompare compare;
    SW_EXPECT_FALSE( compare.hasObjectComparison() );
    SW_ASSERT_TRUE( compare.compareObjectReports( "tick 40\n00000000000000aa Door\n00000000000000bb Lamp\n00000000000000cc Gone\n",
                                                  "tick 40\n00000000000000aa Door\n00000000000000b0 Lamp\n" ) );
    SW_EXPECT_TRUE( compare.hasObjectComparison() );
    SW_EXPECT_EQUAL( uint64{ 40 }, compare.getObjectTick() );
    SW_ASSERT_EQUAL( size_t{ 2 }, compare.getDifferingObjects().size() );
    SW_EXPECT_TRUE( compare.getDifferingObjects()[0] == "Gone" );
    SW_EXPECT_TRUE( compare.getDifferingObjects()[1] == "Lamp" );
}

/**
 * @brief [EditorStateHashCompareTest] 오브젝트 목록이 아닌 답(서버 오류 글)은 견주지 않고 거절한다 — 앞 결과를 지우지 않는다
 */
SW_TEST_CASE( EditorStateHashCompareTest, RejectsAnObjectAnswerThatIsNotAList )
{
    EditorStateHashCompare compare;
    SW_ASSERT_TRUE( compare.compareObjectReports( "tick 7\n00000000000000aa Door\n", "tick 7\n00000000000000ab Door\n" ) );
    SW_EXPECT_FALSE( compare.compareObjectReports( "no active scene", "tick 7\n00000000000000aa Door\n" ) );
    SW_EXPECT_FALSE( compare.compareObjectReports( "00000000000000aa Door\n", "00000000000000aa Door\n" ) ); // 틱 머리가 없는 답은 어느 시각인지 모른다
    SW_EXPECT_FALSE( compare.getLastError().empty() );
    SW_EXPECT_EQUAL( size_t{ 1 }, compare.getDifferingObjects().size() );
}

/**
 * @brief [EditorStateHashCompareTest] 두 서버의 답이 다른 틱이면 견주지 않고 거절한다 — 다른 시각의 해시를 같은 시각으로 읽지 않는다
 */
SW_TEST_CASE( EditorStateHashCompareTest, RejectsObjectAnswersFromDifferentTicks )
{
    EditorStateHashCompare compare;
    SW_EXPECT_FALSE( compare.compareObjectReports( "tick 7\n00000000000000aa Door\n", "tick 8\n00000000000000aa Door\n" ) );
    SW_EXPECT_FALSE( compare.hasObjectComparison() );
    SW_EXPECT_FALSE( compare.getLastError().empty() );
}
