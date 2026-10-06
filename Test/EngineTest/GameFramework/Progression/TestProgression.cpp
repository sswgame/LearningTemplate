#include "pch.h"

#include "GameFramework/Base/Progression/LevelProgress.h"
#include "GameFramework/Base/Progression/Reputation.h"
#include "GameFramework/Base/Progression/RunMap.h"
#include "GameFramework/Base/Progression/SkillTree.h"

#include "TestFramework/TestFramework.h"

// 장르 공통 성장 — 경험치 곡선(공식 · 표)과 레벨 진행, 스킬 트리(선행 · 층 · 배타 그룹 · 되돌리기 · 능력치 · 어빌리티), 평판(단계 · 연결 세력 · 식기).

using namespace sw;

namespace
{
    constexpr const utf8* kSkillTestXml = R"(
<SkillTreeCatalog>
  <Tree id="combat">
    <Skill id="basic" maxRank="2" cost="1"><Stats attack="2"/></Skill>
    <Skill id="power" maxRank="3" cost="1" level="3" requires="basic:2" ability="PowerStrike"><Stats attack="5"/></Skill>
    <Skill id="berserk" cost="2" spent="4" group="stance" ability="Berserk"/>
    <Skill id="guard" cost="1" group="stance"><Stats armor="4"/></Skill>
  </Tree>
</SkillTreeCatalog>
)";

    constexpr const utf8* kReputationTestXml = R"(
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
    SW_ASSERT_TRUE( table.loadFromXmlText( R"(<ExperienceCurve><Level xp="10"/><Level xp="30"/></ExperienceCurve>)", "ProgressionTest" ) );
    SW_EXPECT_EQUAL( 3, table.getMaxLevel() );
    progress.setLevel( table, 2 );
    SW_EXPECT_EQUAL( 10, static_cast<int32>( progress.getTotalXp() ) );
}

SW_TEST_CASE( ProgressionTest, SkillTreesCheckRequirementsTiersGroupsAndRefunds )
{
    SkillTreeCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kSkillTestXml, "ProgressionTest" ) );
    SkillTreeState state;
    state.initialize( catalog.findTree( hashed_string( "combat" ) ) );
    state.addPoints( 5 );
    const hashed_string basic( "basic" );
    const hashed_string power( "power" );

    SW_EXPECT_TRUE( state.rankUp( power, 5 ) == SkillResult::RequirementMissing );
    SW_EXPECT_TRUE( state.rankUp( basic, 1 ) == SkillResult::Ok );
    SW_EXPECT_TRUE( state.rankUp( basic, 1 ) == SkillResult::Ok );
    SW_EXPECT_TRUE( state.rankUp( basic, 1 ) == SkillResult::MaxRank );
    SW_EXPECT_TRUE( state.rankUp( power, 2 ) == SkillResult::LevelTooLow );
    SW_EXPECT_TRUE( state.rankUp( power, 3 ) == SkillResult::Ok );
    SW_EXPECT_TRUE( state.rankUp( hashed_string( "berserk" ), 9 ) == SkillResult::TierLocked ); // 3 점만 썼다
    SW_EXPECT_TRUE( state.rankUp( hashed_string( "guard" ), 9 ) == SkillResult::Ok );
    SW_EXPECT_TRUE( state.rankUp( power, 3 ) == SkillResult::Ok );
    SW_EXPECT_TRUE( state.rankUp( power, 3 ) == SkillResult::NotEnoughPoints );
    state.addPoints( 2 );
    SW_EXPECT_TRUE( state.rankUp( hashed_string( "berserk" ), 9 ) == SkillResult::GroupTaken ); // 층은 열렸지만 같은 그룹의 guard 를 배웠다
    SW_EXPECT_EQUAL( 5, state.getSpentPoints() );

    StatBlock stats;
    state.computeStats( stats );
    SW_EXPECT_NEAR_EQUAL( 14.0f, stats.getValue( hashed_string( "attack" ) ), 1.0e-4f ); // 2 × 2 + 5 × 2
    SW_EXPECT_NEAR_EQUAL( 4.0f, stats.getValue( hashed_string( "armor" ) ), 1.0e-4f );
    vector<hashed_string> listAbility;
    state.collectAbilities( listAbility );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( listAbility.size() ) );

    SW_EXPECT_TRUE( state.rankDown( basic ) == SkillResult::RequiredByOther ); // power 가 basic 2 를 요구한다
    SW_EXPECT_TRUE( state.rankDown( power ) == SkillResult::Ok );
    SW_EXPECT_TRUE( state.rankDown( basic ) == SkillResult::RequiredByOther ); // 랭크 1 도 basic 2 를 요구한다
    SW_EXPECT_TRUE( state.rankDown( power ) == SkillResult::Ok );
    SW_EXPECT_TRUE( state.rankDown( basic ) == SkillResult::Ok );
    SW_EXPECT_EQUAL( 2, state.refundAll() );
    SW_EXPECT_EQUAL( 7, state.getPoints() );
    SW_EXPECT_EQUAL( 0, state.getRank( hashed_string( "guard" ) ) );
}

SW_TEST_CASE( ProgressionTest, ReputationTiersSpreadToLinkedFactionsAndDecay )
{
    ReputationCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kReputationTestXml, "ProgressionTest" ) );
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

SW_TEST_CASE( ProgressionTest, RunMapsConnectFloorsWithoutCrossingAndEndAtTheBoss )
{
    RunMapSettings settings;
    SW_ASSERT_TRUE( RunMap::loadSettings( R"(<RunMap floors="12" columns="7" paths="6">
        <Node kind="Battle" weight="5"/><Node kind="Elite" weight="1" minFloor="4"/><Node kind="Rest" weight="1" minFloor="3" noRepeat="true"/>
        <Node kind="Shop" weight="1" noRepeat="true"/><Floor index="0" kind="Battle"/><Floor index="6" kind="Treasure"/><Floor index="-1" kind="Boss"/>
      </RunMap>)",
                                          "ProgressionTest", settings ) );
    RunMap mapA;
    RunMap mapB;
    mapA.generate( settings, 42u );
    mapB.generate( settings, 42u );
    SW_ASSERT_TRUE( mapA.getNodes().size() == mapB.getNodes().size() ); // 씨앗이 같으면 같은 지도
    int32 bossCount = 0;
    for ( size_t index = 0; index < mapA.getNodes().size(); ++index )
    {
        const RunNode& node = mapA.getNodes()[index];
        SW_EXPECT_TRUE( node._kind == mapB.getNodes()[index]._kind );
        if ( node._floor == 0 )
            SW_EXPECT_TRUE( node._kind == hashed_string( "Battle" ) );
        if ( node._floor == 6 )
            SW_EXPECT_TRUE( node._kind == hashed_string( "Treasure" ) );
        if ( node._floor < 4 )
            SW_EXPECT_TRUE( node._kind != hashed_string( "Elite" ) );
        bossCount += node._kind == hashed_string( "Boss" ) ? 1 : 0;
        // 위층의 같은 · 옆 열로만, 끝 층 아니면 막다른 칸이 없다.
        if ( node._floor < 11 )
            SW_EXPECT_FALSE( node._listNext.empty() );
        for ( const int32 nextIndex : node._listNext )
        {
            const RunNode& next = mapA.getNodes()[static_cast<size_t>( nextIndex )];
            SW_EXPECT_EQUAL( node._floor + 1, next._floor );
            if ( next._floor < 11 )
                SW_EXPECT_TRUE( next._column >= node._column - 1 && next._column <= node._column + 1 );
            // 휴식 · 상점은 연달아 나오지 않는다.
            if ( node._kind == hashed_string( "Rest" ) || node._kind == hashed_string( "Shop" ) )
                SW_EXPECT_TRUE( next._kind != node._kind );
        }
    }
    SW_EXPECT_EQUAL( 1, bossCount );
    // 엇갈리는 선이 없다 — (f, c) → (f+1, c+1) 과 (f, c+1) → (f+1, c) 가 함께 있지 않다.
    for ( const RunNode& node : mapA.getNodes() )
    {
        for ( const int32 nextIndex : node._listNext )
        {
            const RunNode& next = mapA.getNodes()[static_cast<size_t>( nextIndex )];
            if ( next._column != node._column + 1 || next._floor == 11 )
                continue;
            for ( const RunNode& other : mapA.getNodes() )
            {
                if ( other._floor != node._floor || other._column != node._column + 1 )
                    continue;
                for ( const int32 otherNext : other._listNext )
                    SW_EXPECT_FALSE( mapA.getNodes()[static_cast<size_t>( otherNext )]._column == node._column &&
                                     mapA.getNodes()[static_cast<size_t>( otherNext )]._floor == node._floor + 1 );
            }
        }
    }

    // 걷기 — 갈 수 있는 칸만, 끝까지.
    vector<int32> listChoice;
    mapA.collectChoices( listChoice );
    SW_ASSERT_TRUE( listChoice.empty() == false );
    SW_EXPECT_FALSE( mapA.moveTo( static_cast<int32>( mapA.getNodes().size() ) + 5 ) );
    while ( mapA.isFinished() == false )
    {
        mapA.collectChoices( listChoice );
        SW_ASSERT_TRUE( listChoice.empty() == false );
        SW_ASSERT_TRUE( mapA.moveTo( listChoice.back() ) );
    }
    SW_EXPECT_TRUE( mapA.findNode( mapA.getCurrent() )->_kind == hashed_string( "Boss" ) );
}
