#include "pch.h"

#include "GameFramework/Quest/QuestCatalog.h"
#include "GameFramework/Quest/QuestLog.h"

#include "TestFramework/TestFramework.h"

// 장르 공통 퀘스트 — 받기 조건(선행 · 레벨 · 반복), 목표 알림과 선택 목표, 대사만 있는 단계 건너뛰기, 선택지 분기, 보상 알림, 시간 제한 실패, 포기.

using namespace sw;

namespace
{
    constexpr const utf8* kQuestTestXml = R"(
<QuestCatalog>
  <Quest id="intro"><Stage id="talk" next="done"><Objective kind="Talk" target="elder"/></Stage><Stage id="done" complete="true"><Reward xp="10"/></Stage></Quest>
  <Quest id="wolves" level="2" requires="intro">
    <Stage id="hunt" next="report">
      <Objective kind="Kill" target="wolf" count="3"/>
      <Objective kind="Collect" target="pelt" count="2" optional="true"/>
    </Stage>
    <Stage id="report" next="decide"/>
    <Stage id="decide"><Objective kind="Talk" target="elder"/><Branch choice="spare" next="mercy"/><Branch choice="kill" next="blood"/></Stage>
    <Stage id="mercy" complete="true"><Reward xp="100" gold="5"><Item item="ring" count="1"/></Reward></Stage>
    <Stage id="blood" fail="true"/>
  </Quest>
  <Quest id="race" repeatable="true"><Stage id="run" next="win" time="10"><Objective kind="Reach" target="flag"/></Stage><Stage id="win" complete="true"/></Quest>
</QuestCatalog>
)";
} // namespace

SW_TEST_CASE( QuestTest, QuestsAdvanceThroughObjectivesBranchesAndRewards )
{
    QuestCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kQuestTestXml, "QuestTest" ) );
    QuestLog log;
    log.initialize( &catalog );
    const hashed_string wolves( "wolves" );

    SW_EXPECT_TRUE( log.start( wolves, 5 ) == QuestStartResult::RequirementMissing );
    SW_EXPECT_TRUE( log.start( hashed_string( "intro" ), 1 ) == QuestStartResult::Ok );
    SW_EXPECT_TRUE( log.start( hashed_string( "intro" ), 1 ) == QuestStartResult::AlreadyActive );
    SW_EXPECT_EQUAL( 1, log.notify( hashed_string( "Talk" ), hashed_string( "elder" ) ) );
    SW_EXPECT_TRUE( log.getStatus( hashed_string( "intro" ) ) == QuestStatus::Completed );
    SW_EXPECT_TRUE( log.start( hashed_string( "intro" ), 1 ) == QuestStartResult::AlreadyDone );
    SW_EXPECT_TRUE( log.start( wolves, 1 ) == QuestStartResult::LevelTooLow );
    SW_EXPECT_TRUE( log.start( wolves, 2 ) == QuestStartResult::Ok );

    SW_EXPECT_EQUAL( 0, log.notify( hashed_string( "Kill" ), hashed_string( "bear" ) ) );
    SW_EXPECT_EQUAL( 1, log.notify( hashed_string( "Kill" ), hashed_string( "wolf" ), 2 ) );
    SW_EXPECT_EQUAL( 1, log.notifyCount( hashed_string( "Collect" ), hashed_string( "pelt" ), 1 ) );
    SW_EXPECT_TRUE( log.findCurrentStage( wolves )->_id == hashed_string( "hunt" ) );
    SW_EXPECT_EQUAL( 1, log.notify( hashed_string( "Kill" ), hashed_string( "wolf" ), 5 ) ); // 넘쳐도 3 에서 멈춘다 — 선택 목표 없이 넘어간다
    // "report" 는 목표가 없어 바로 "decide" 로.
    SW_EXPECT_TRUE( log.findCurrentStage( wolves )->_id == hashed_string( "decide" ) );
    SW_EXPECT_FALSE( log.choose( wolves, hashed_string( "spare" ) ) ); // 아직 이야기하지 않았다
    (void)log.notify( hashed_string( "Talk" ), hashed_string( "elder" ) );
    SW_EXPECT_TRUE( log.findProgress( wolves )->_bAwaitingChoice != SW_FALSE );
    SW_EXPECT_FALSE( log.choose( wolves, hashed_string( "flee" ) ) );
    SW_EXPECT_TRUE( log.choose( wolves, hashed_string( "spare" ) ) );
    SW_EXPECT_TRUE( log.getStatus( wolves ) == QuestStatus::Completed );

    vector<QuestEvent> listEvent;
    log.drainEvents( listEvent );
    const QuestReward* pReward   = nullptr;
    int32              doneCount = 0;
    for ( const QuestEvent& event : listEvent )
    {
        if ( event._questId == wolves && event._kind == QuestEvent::Kind::StageEntered && event._pReward != nullptr )
            pReward = event._pReward;
        doneCount += event._kind == QuestEvent::Kind::ObjectiveDone ? 1 : 0;
    }
    SW_ASSERT_NOT_NULL( pReward );
    SW_EXPECT_NEAR_EQUAL( 100.0f, pReward->_values.getValue( hashed_string( "xp" ) ), 1.0e-4f );
    SW_EXPECT_EQUAL( 1, pReward->_items.getItemCount( hashed_string( "ring" ) ) );
    SW_EXPECT_EQUAL( 3, doneCount ); // elder(intro) · wolf · elder(decide)
}

SW_TEST_CASE( QuestTest, TimeLimitsFailAndAbandonedOrRepeatableQuestsRestart )
{
    QuestCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kQuestTestXml, "QuestTest" ) );
    QuestLog log;
    log.initialize( &catalog );
    const hashed_string race( "race" );
    SW_EXPECT_TRUE( log.start( race, 1 ) == QuestStartResult::Ok );
    log.update( 9.0f );
    SW_EXPECT_TRUE( log.getStatus( race ) == QuestStatus::Active );
    log.update( 2.0f );
    SW_EXPECT_TRUE( log.getStatus( race ) == QuestStatus::Failed );
    SW_EXPECT_TRUE( log.start( race, 1 ) == QuestStartResult::Ok ); // 실패한 퀘스트는 다시 받는다
    (void)log.notify( hashed_string( "Reach" ), hashed_string( "flag" ) );
    SW_EXPECT_TRUE( log.getStatus( race ) == QuestStatus::Completed );
    SW_EXPECT_TRUE( log.start( race, 1 ) == QuestStartResult::Ok ); // 반복
    SW_EXPECT_EQUAL( 1, log.findProgress( race )->_completedCount );

    vector<hashed_string> listActive;
    log.collectActive( listActive );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( listActive.size() ) );
    SW_EXPECT_TRUE( log.abandon( race ) );
    SW_EXPECT_TRUE( log.getStatus( race ) == QuestStatus::NotStarted );
    SW_EXPECT_FALSE( log.fail( race ) );

    // 실패 단계로 가는 분기.
    (void)log.start( hashed_string( "intro" ), 1 );
    (void)log.notify( hashed_string( "Talk" ), hashed_string( "elder" ) );
    (void)log.start( hashed_string( "wolves" ), 3 );
    (void)log.notify( hashed_string( "Kill" ), hashed_string( "wolf" ), 3 );
    (void)log.notify( hashed_string( "Talk" ), hashed_string( "elder" ) );
    SW_EXPECT_TRUE( log.choose( hashed_string( "wolves" ), hashed_string( "kill" ) ) );
    SW_EXPECT_TRUE( log.getStatus( hashed_string( "wolves" ) ) == QuestStatus::Failed );
}
