#include "pch.h"

#include "GameFramework/Base/Gameplay/Progression/LevelProgress.h"
#include "GameFramework/Base/Gameplay/Progression/Reputation.h"

#include "TestFramework/TestFramework.h"

// 장르 공통 성장 — 경험치 곡선(공식 · 표)과 레벨 진행, 평판(단계 · 연결 세력 · 식기).

using namespace sw;

namespace
{
    constexpr const utf8* kReputationTestXML = R"(
<ReputationCatalog>
  <Faction id="town" min="-100" max="100" start="0">
    <Tier name="Hated" min="-100"/><Tier name="Neutral" min="-20"/><Tier name="Friendly" min="30"/><Tier name="Hero" min="80"/>
    <Link faction="bandits" ratio="-0.5"/>
  </Faction>
  <Faction id="bandits" min="-100" max="100" start="10"/>
  <Faction id="mira" min="0" max="1000" start="0" decay="5"><Tier name="Stranger" min="0"/><Tier name="Friend" min="200"/></Faction>
</ReputationCatalog>
)";
} // namespace

SW_TEST_CASE( ProgressionTest, ExperienceCurvesLevelUpAndStopAtMaxLevel )
{
    ExperienceCurve curve;
    curve.setFormula( 100.0f, 1.0f, 0.0f, 5 ); // 100 · 200 · 300 · 400
    SW_EXPECT_EQUAL( 100, static_cast<int32>( curve.getXpToNext( 1 ) ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( curve.getXpToNext( 5 ) ) );
    SW_EXPECT_EQUAL( 600, static_cast<int32>( curve.computeTotalXp( 4 ) ) );

    LevelProgress progress;
    SW_EXPECT_EQUAL( 0, progress.addXp( curve, 50 ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, progress.computeRatio( curve ), 1.0e-4f );
    SW_EXPECT_EQUAL( 2, progress.addXp( curve, 300 ) ); // 350 — 레벨 3, 50 남음
    SW_EXPECT_EQUAL( 3, progress.getLevel() );
    SW_EXPECT_EQUAL( 50, static_cast<int32>( progress.getXp() ) );
    SW_EXPECT_EQUAL( 2, progress.addXp( curve, 100000 ) );
    SW_EXPECT_EQUAL( 5, progress.getLevel() );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( progress.getXp() ) );
    SW_EXPECT_EQUAL( 1000, static_cast<int32>( progress.getTotalXp() ) ); // 넘친 것은 버린다
    SW_EXPECT_NEAR_EQUAL( 1.0f, progress.computeRatio( curve ), 1.0e-4f );
    SW_EXPECT_EQUAL( 0, progress.addXp( curve, 10 ) );

    ExperienceCurve table;
    SW_ASSERT_TRUE( table.loadFromXMLText( R"(<ExperienceCurve><Level xp="10"/><Level xp="30"/></ExperienceCurve>)", "ProgressionTest" ) );
    SW_EXPECT_EQUAL( 3, table.getMaxLevel() );
    progress.setLevel( table, 2 );
    SW_EXPECT_EQUAL( 10, static_cast<int32>( progress.getTotalXp() ) );
}

SW_TEST_CASE( ProgressionTest, ReputationTiersSpreadToLinkedFactionsAndDecay )
{
    ReputationCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXMLText( kReputationTestXML, "ProgressionTest" ) );
    ReputationState state;
    state.initialize( &catalog );
    const hashed_string town( "town" );
    SW_EXPECT_TRUE( state.getTierName( town ) == hashed_string( "Neutral" ) );
    SW_EXPECT_EQUAL( 10, state.getValue( hashed_string( "bandits" ) ) );

    SW_EXPECT_EQUAL( 40, state.changeValue( town, 40 ) );
    SW_EXPECT_TRUE( state.getTierName( town ) == hashed_string( "Friendly" ) );
    SW_EXPECT_EQUAL( -10, state.getValue( hashed_string( "bandits" ) ) ); // 마을을 도우면 도적은 싫어한다
    SW_EXPECT_EQUAL( 60, state.changeValue( town, 500 ) );                // 상한
    SW_EXPECT_EQUAL( 3, state.getTierIndex( town ) );
    vector<ReputationEvent> listEvent;
    state.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( listEvent.size() ) ); // Neutral → Friendly → Hero
    SW_EXPECT_TRUE( listEvent.back()._newTier == hashed_string( "Hero" ) );

    // 호감도는 하루마다 식는다.
    state.setValue( hashed_string( "mira" ), 203 );
    SW_EXPECT_TRUE( state.getTierName( hashed_string( "mira" ) ) == hashed_string( "Friend" ) );
    state.advanceDay();
    SW_EXPECT_EQUAL( 198, state.getValue( hashed_string( "mira" ) ) );
    SW_EXPECT_TRUE( state.getTierName( hashed_string( "mira" ) ) == hashed_string( "Stranger" ) );
    SW_EXPECT_EQUAL( 100, state.getValue( town ) ); // 식지 않는 세력

    // 카탈로그에 없는 NPC 도 받는다(0..1000).
    SW_EXPECT_EQUAL( 0, state.changeValue( hashed_string( "stranger" ), -5 ) );
    SW_EXPECT_EQUAL( 7, state.changeValue( hashed_string( "stranger" ), 7 ) );
    SW_EXPECT_EQUAL( -1, state.getTierIndex( hashed_string( "stranger" ) ) );
}
