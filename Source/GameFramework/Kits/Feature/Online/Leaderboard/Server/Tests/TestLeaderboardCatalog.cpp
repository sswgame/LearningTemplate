// LeaderboardCatalogTest — 순위 보드 정의 읽기: 보드 · 시즌 · 보상 구간을 스키마로 읽고, 틀린 보드(구간 끝이 시작보다 앞 · 빠진 필수 칸)는 넣지 않는다.
// 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardCatalog.h"

using namespace sw;

/** @brief [LeaderboardCatalogTest] 시즌 보드의 시즌 · 보상 구간과 순서 · 갱신 · 초기화 규칙을 그대로 읽는다 */
SW_TEST_CASE( LeaderboardCatalogTest, ReadsSeasonBoardWithRewardTiers )
{
    LeaderboardCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<LeaderboardCatalog>
  <Board id="arena_kills" order="Descending" update="Best" reset="Season" clientSubmit="false">
    <Season id="1" startMs="1000" endMs="2000">
      <Reward asset="cur.gem" mailTitle="lb.reward.top" amount="100" from="1" to="1"/>
      <Reward asset="cur.gem" mailTitle="lb.reward.top10" amount="20" from="2" to="10"/>
    </Season>
  </Board>
  <Board id="daily_score" order="Ascending" update="Latest" reset="Daily" resetMinute="90"/>
</LeaderboardCatalog>)",
                                              "test" ) );
    const vector<LeaderboardDefinition>& listBoard = catalog.getBoards();
    SW_REQUIRE_EQUAL( size_t( 2 ), listBoard.size() );
    const LeaderboardDefinition& arena = listBoard[0];
    SW_EXPECT_EQUAL( string( "arena_kills" ), arena._boardID );
    SW_EXPECT_TRUE( arena._reset == LeaderboardReset::Season );
    SW_REQUIRE_EQUAL( size_t( 1 ), arena._listSeason.size() );
    SW_EXPECT_EQUAL( 1u, arena._listSeason[0]._seasonID );
    SW_REQUIRE_EQUAL( size_t( 2 ), arena._listSeason[0]._listRewardTier.size() );
    SW_EXPECT_EQUAL( 10, arena._listSeason[0]._listRewardTier[1]._rankTo );
    const LeaderboardDefinition& daily = listBoard[1];
    SW_EXPECT_TRUE( daily._order == LeaderboardOrder::Ascending );
    SW_EXPECT_TRUE( daily._reset == LeaderboardReset::Daily );
    SW_EXPECT_EQUAL( 90, daily._resetMinuteOfDay );
}

/** @brief [LeaderboardCatalogTest] 보상 구간의 끝이 시작보다 앞이거나 필수 칸(asset)이 빠진 보드는 넣지 않는다 — 다른 보드는 그대로 읽는다 */
SW_TEST_CASE( LeaderboardCatalogTest, RejectsBoardsWithBadRewardTiers )
{
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "XMLFields.RequiredFieldMissing" );
    LeaderboardCatalog catalog;
    (void)catalog.loadFromXMLText( R"(<LeaderboardCatalog>
  <Board id="reversed" reset="Season">
    <Season id="1"><Reward asset="cur.gem" mailTitle="t" amount="1" from="5" to="2"/></Season>
  </Board>
  <Board id="no_asset" reset="Season">
    <Season id="1"><Reward mailTitle="t" amount="1" from="1" to="1"/></Season>
  </Board>
  <Board id="good"/>
</LeaderboardCatalog>)",
                                   "test" );
    const vector<LeaderboardDefinition>& listBoard = catalog.getBoards();
    SW_REQUIRE_EQUAL( size_t( 1 ), listBoard.size() );
    SW_EXPECT_EQUAL( string( "good" ), listBoard[0]._boardID );
}
