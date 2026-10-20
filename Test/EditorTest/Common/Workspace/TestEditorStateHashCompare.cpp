#include "pch.h"

#include "Editor/Common/Workspace/EditorStateHashCompare.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorStateHashCompareTest] 두 이력이 겹치는 틱만 견주고, 처음 어긋난 틱과 겹친 틱 수를 낸다 — 한쪽에만 있는 틱은 어긋남이 아니다
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
 * @brief [EditorStateHashCompareTest] 이력이 아닌 글(서버 오류 답)과 빈 이력은 칸에 담지 않는다 — 빈 칸을 "어긋남 없음" 으로 견주지 않는다
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
