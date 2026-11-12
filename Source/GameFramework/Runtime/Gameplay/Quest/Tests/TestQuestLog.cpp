// QuestLogTest — 퀘스트 일지: 받기 조건, 목표 세기(선택 목표 · 개수 정하기), 단계 사슬 · 선택지 · 시간 제한, 저장과 데이터가 바뀐 뒤의 복원, 카탈로그 검사(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Object/GameObject/Tests/SceneIterationTestAccess.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"

using namespace sw;

namespace
{
    constexpr const utf8* kQuestXML = R"(<QuestCatalog>
  <Quest id="intro"><Stage id="talk" next="end"><Objective kind="Talk" target="elder"/></Stage><Stage id="end" complete="true"><Reward xp="10"/></Stage></Quest>
  <Quest id="wolves" level="2" requires="intro">
    <Stage id="hunt" next="report">
      <Objective kind="Kill" target="wolf" count="2"/>
      <Objective kind="Collect" target="pelt" count="3" optional="true"/>
    </Stage>
    <Stage id="report"><Objective kind="Talk" target="elder"/><Branch choice="spare" next="mercy"/><Branch choice="kill" next="blood"/></Stage>
    <Stage id="mercy" complete="true"><Reward xp="100"><Item item="ring" count="1"/></Reward></Stage>
    <Stage id="blood" fail="true"/>
  </Quest>
  <Quest id="daily" repeatable="true"><Stage id="work" next="end"><Objective kind="Work" target="field"/></Stage><Stage id="end" complete="true"/></Quest>
  <Quest id="timed"><Stage id="run" time="5" next="end"><Objective kind="Reach" target="gate"/></Stage><Stage id="end" complete="true"/></Quest>
  <Quest id="chain"><Stage id="a" next="b"/><Stage id="b" next="c"/><Stage id="c" complete="true"/></Quest>
  <Quest id="loop"><Stage id="a" next="b"/><Stage id="b" next="a"/></Quest>
  <Quest id="gather"><Stage id="get" next="end"><Objective kind="Have" target="herb" count="3"/></Stage><Stage id="end" complete="true"/></Quest>
</QuestCatalog>)";

    /** @brief 같은 id 의 단계 구성을 바꾼 데이터입니다. 저장 뒤 콘텐츠가 바뀐 경우를 봅니다. */
    constexpr const utf8* kChangedQuestXML = R"(<QuestCatalog>
  <Quest id="wolves">
    <Stage id="hunt" next="done">
      <Objective kind="Kill" target="wolf" count="1"/>
      <Objective kind="Kill" target="bear" count="1"/>
      <Objective kind="Kill" target="boar" count="1"/>
      <Objective kind="Kill" target="fox" count="1"/>
    </Stage>
    <Stage id="done" complete="true"/>
  </Quest>
  <Quest id="gather"><Stage id="other" complete="true"/></Quest>
</QuestCatalog>)";

    struct QuestLogTest
    {
        QuestCatalog _catalog;
        QuestLog     _log;

        QuestLogTest()
            : _catalog{}
            , _log{}
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kQuestXML, "QuestLogTest" ) );
            _log.initialize( &_catalog );
        }

        int32 countEvents( QuestEvent::Kind kind )
        {
            vector<QuestEvent> listEvent;
            _log.pollEvents( listEvent );
            int32 count = 0;
            for ( const QuestEvent& event : listEvent )
            {
                count += event._kind == kind ? 1 : 0;
            }
            return count;
        }

        void finishIntro()
        {
            SW_REQUIRE_TRUE( _log.start( "intro", 1 ) == QuestStartResult::Ok );
            (void)_log.notify( "Talk", "elder" );
            SW_REQUIRE_TRUE( _log.getStatus( "intro" ) == QuestStatus::Completed );
        }
    };
} // namespace

/** @brief [QuestLogTest] 받기 조건: 모르는 퀘스트 · 선행 · 레벨 · 진행 중 · 이미 끝냄. 반복 퀘스트는 다시 받는다 */
SW_TEST_CASE_F( QuestLogTest, StartRules )
{
    SW_EXPECT_TRUE( _log.start( "nothing", 9 ) == QuestStartResult::UnknownQuest );
    SW_EXPECT_TRUE( _log.start( "wolves", 9 ) == QuestStartResult::RequirementMissing );
    finishIntro();
    SW_EXPECT_TRUE( _log.start( "wolves", 1 ) == QuestStartResult::LevelTooLow );
    SW_EXPECT_TRUE( _log.start( "wolves", 2 ) == QuestStartResult::Ok );
    SW_EXPECT_TRUE( _log.start( "wolves", 2 ) == QuestStartResult::AlreadyActive );
    SW_EXPECT_TRUE( _log.start( "intro", 1 ) == QuestStartResult::AlreadyDone );

    SW_REQUIRE_TRUE( _log.start( "daily", 1 ) == QuestStartResult::Ok );
    (void)_log.notify( "Work", "field" );
    SW_REQUIRE_TRUE( _log.getStatus( "daily" ) == QuestStatus::Completed );
    SW_REQUIRE_TRUE( _log.start( "daily", 1 ) == QuestStartResult::Ok );
    (void)_log.notify( "Work", "field" );
    SW_EXPECT_EQUAL( 2, _log.findProgress( "daily" )->_completedCount );

    vector<hashed_string> listActive;
    _log.collectActive( listActive );
    SW_REQUIRE_EQUAL( size_t( 1 ), listActive.size() );
    SW_EXPECT_TRUE( listActive[0] == hashed_string( "wolves" ) );
}

/** @brief [QuestLogTest] 런타임 디버거 줄: 진행 중 단계 · 목표 달성도(필수 · 선택 · 끝남), 받지 않은 퀘스트는 잠금 사유(빠진 선행 퀘스트 이름), 끝난 퀘스트, 줄 상한 */
SW_TEST_CASE_F( QuestLogTest, DebugLinesExplainProgressAndLocks )
{
    const auto findLine = []( const DebugHUDSectionWriter& writer, string_view label ) -> string
    {
        for ( uint32 index = 0; index < writer.getLineCount(); ++index )
        {
            if ( writer.getLineAt( index )._label == label )
                return writer.getLineAt( index )._value;
        }
        return "(missing)";
    };
    DebugHUDSectionWriter before{ SceneIterationTestAccess::getKey() };
    _log.writeDebugLines( before, 99, 64 );
    SW_EXPECT_EQUAL( string( "locked: RequirementMissing (intro)" ), findLine( before, "wolves" ) );
    SW_EXPECT_EQUAL( string( "available" ), findLine( before, "intro" ) );

    finishIntro();
    SW_REQUIRE_TRUE( _log.start( "wolves", 2 ) == QuestStartResult::Ok );
    (void)_log.notify( "Kill", "wolf" );
    DebugHUDSectionWriter during{ SceneIterationTestAccess::getKey() };
    _log.writeDebugLines( during, 99, 64 );
    SW_EXPECT_EQUAL( string( "Completed x1" ), findLine( during, "intro" ) );
    SW_EXPECT_EQUAL( string( "Active stage hunt t=0s" ), findLine( during, "wolves" ) );
    SW_EXPECT_EQUAL( string( "1/2" ), findLine( during, "  Kill wolf" ) );
    SW_EXPECT_EQUAL( string( "0/3 optional" ), findLine( during, "  Collect pelt" ) );

    DebugHUDSectionWriter capped{ SceneIterationTestAccess::getKey() };
    _log.writeDebugLines( capped, 99, 2 );
    SW_EXPECT_EQUAL( uint32( 3 ), capped.getLineCount() ); // 두 줄 + "… N more"
    SW_EXPECT_EQUAL( string( "..." ), capped.getLineAt( 2 )._label );
}

/** @brief [QuestLogTest] 목표: 맞는 종류 · 대상만 세고 목표 수를 넘지 않는다. 필수를 다 하면 선택 목표가 남아도 넘어간다. 0 이하를 알리면 아무 일도 없다 */
SW_TEST_CASE_F( QuestLogTest, ObjectivesCountAndAdvance )
{
    finishIntro();
    SW_REQUIRE_TRUE( _log.start( "wolves", 2 ) == QuestStartResult::Ok );
    (void)countEvents( QuestEvent::Kind::Started );

    SW_EXPECT_EQUAL( 0, _log.notify( "Kill", "bear" ) );
    SW_EXPECT_EQUAL( 0, _log.notify( "Kill", "wolf", 0 ) );
    SW_EXPECT_EQUAL( 0, _log.notify( "Kill", "wolf", -5 ) );
    SW_EXPECT_TRUE( _log.isObjectiveOpen( "wolves", "Kill", "wolf" ) );
    SW_EXPECT_FALSE( _log.isObjectiveOpen( "wolves", "Kill", "bear" ) );

    SW_EXPECT_EQUAL( 1, _log.notify( "Collect", "pelt" ) ); // 선택 목표는 세되 넘기지 않는다
    SW_EXPECT_EQUAL( 1, _log.notify( "Kill", "wolf" ) );
    SW_EXPECT_TRUE( _log.findCurrentStage( "wolves" )->_id == hashed_string( "hunt" ) );
    SW_EXPECT_EQUAL( 1, _log.notify( "Kill", "wolf", 99 ) );
    SW_EXPECT_TRUE( _log.findCurrentStage( "wolves" )->_id == hashed_string( "report" ) );
    SW_EXPECT_FALSE( _log.isObjectiveOpen( "wolves", "Kill", "wolf" ) ); // 지나간 단계이다
}

/** @brief [QuestLogTest] 개수 정하기: 줄어들 수 있고 범위로 잘린다. 같은 값이면 바뀐 목표가 없다 */
SW_TEST_CASE_F( QuestLogTest, NotifyCountSetsAbsolute )
{
    SW_REQUIRE_TRUE( _log.start( "gather", 1 ) == QuestStartResult::Ok );
    SW_EXPECT_EQUAL( 1, _log.notifyCount( "Have", "herb", 2 ) );
    SW_EXPECT_EQUAL( 2, _log.findProgress( "gather" )->_listCount[0] );
    SW_EXPECT_EQUAL( 0, _log.notifyCount( "Have", "herb", 2 ) );
    SW_EXPECT_EQUAL( 1, _log.notifyCount( "Have", "herb", -3 ) );
    SW_EXPECT_EQUAL( 0, _log.findProgress( "gather" )->_listCount[0] );
    SW_EXPECT_EQUAL( 1, _log.notifyCount( "Have", "herb", 50 ) );
    SW_EXPECT_TRUE( _log.getStatus( "gather" ) == QuestStatus::Completed );
}

/** @brief [QuestLogTest] 선택지: 목표를 다 하면 기다리고 그동안 세지 않는다. 없는 선택은 거절하고, 고른 갈래의 보상과 결말을 따른다 */
SW_TEST_CASE_F( QuestLogTest, BranchesWaitForChoice )
{
    finishIntro();
    SW_REQUIRE_TRUE( _log.start( "wolves", 2 ) == QuestStartResult::Ok );
    SW_EXPECT_FALSE( _log.choose( "wolves", "spare" ) ); // 아직 기다리지 않는다
    (void)_log.notify( "Kill", "wolf", 2 );
    (void)_log.notify( "Talk", "elder" );
    SW_EXPECT_EQUAL( SW_TRUE, _log.findProgress( "wolves" )->_bAwaitingChoice );
    SW_EXPECT_FALSE( _log.isObjectiveOpen( "wolves", "Talk", "elder" ) );
    SW_EXPECT_FALSE( _log.choose( "wolves", "nothing" ) );

    vector<QuestEvent> listEvent;
    _log.pollEvents( listEvent );
    SW_REQUIRE_TRUE( _log.choose( "wolves", "spare" ) );
    SW_EXPECT_TRUE( _log.getStatus( "wolves" ) == QuestStatus::Completed );

    listEvent.clear();
    _log.pollEvents( listEvent );
    bool bSawReward = false;
    for ( const QuestEvent& event : listEvent )
    {
        bSawReward = bSawReward || ( event._kind == QuestEvent::Kind::StageEntered && event._pReward != nullptr && event._pReward->_items.hasItem( "ring" ) );
    }
    SW_EXPECT_TRUE( bSawReward );
}

/** @brief [QuestLogTest] 실패 결말 · 시간 제한 · 실패시키기 · 포기: 실패한 퀘스트와 포기한 퀘스트는 다시 받을 수 있다 */
SW_TEST_CASE_F( QuestLogTest, FailAndAbandon )
{
    SW_REQUIRE_TRUE( _log.start( "timed", 1 ) == QuestStartResult::Ok );
    _log.update( 4.0f );
    SW_EXPECT_TRUE( _log.getStatus( "timed" ) == QuestStatus::Active );
    _log.update( 1.0f );
    SW_EXPECT_TRUE( _log.getStatus( "timed" ) == QuestStatus::Failed );
    SW_EXPECT_TRUE( _log.evaluateStart( "timed", 1 ) == QuestStartResult::Ok );

    SW_REQUIRE_TRUE( _log.start( "gather", 1 ) == QuestStartResult::Ok );
    SW_EXPECT_TRUE( _log.fail( "gather" ) );
    SW_EXPECT_FALSE( _log.fail( "gather" ) ); // 이미 진행 중이 아니다
    SW_REQUIRE_TRUE( _log.start( "gather", 1 ) == QuestStartResult::Ok );
    SW_EXPECT_TRUE( _log.abandon( "gather" ) );
    SW_EXPECT_TRUE( _log.getStatus( "gather" ) == QuestStatus::NotStarted );
    SW_EXPECT_FALSE( _log.abandon( "gather" ) );
    SW_EXPECT_FALSE( _log.abandon( "nothing" ) );
}

/** @brief [QuestLogTest] 목표 없는 단계는 바로 넘어간다. 서로를 가리키는 단계는 멈추지 않고 실패가 된다 */
SW_TEST_CASE_F( QuestLogTest, StageChains )
{
    SW_REQUIRE_TRUE( _log.start( "chain", 1 ) == QuestStartResult::Ok );
    SW_EXPECT_TRUE( _log.getStatus( "chain" ) == QuestStatus::Completed );
    SW_REQUIRE_TRUE( _log.start( "loop", 1 ) == QuestStartResult::Ok );
    SW_EXPECT_TRUE( _log.getStatus( "loop" ) == QuestStatus::Failed );
}

/** @brief [QuestLogTest] 저장: 단계 · 개수 · 시간 · 완료 횟수 · 선택 대기가 되돌아오고 알림은 없다 */
SW_TEST_CASE_F( QuestLogTest, StateRoundTrips )
{
    finishIntro();
    SW_REQUIRE_TRUE( _log.start( "wolves", 2 ) == QuestStartResult::Ok );
    (void)_log.notify( "Kill", "wolf" );
    SW_REQUIRE_TRUE( _log.start( "timed", 1 ) == QuestStartResult::Ok );
    _log.update( 2.0f );

    Archive archive;
    _log.writeState( archive );

    QuestLog restored;
    restored.initialize( &_catalog );
    Archive reading( archive.getData(), archive.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( restored.getStatus( "intro" ) == QuestStatus::Completed );
    SW_EXPECT_EQUAL( 1, restored.findProgress( "intro" )->_completedCount );
    SW_EXPECT_EQUAL( 1, restored.findProgress( "wolves" )->_listCount[0] );
    SW_EXPECT_NEAR( 2.0f, restored.findProgress( "timed" )->_stageTime, 1.0e-5f );
    vector<QuestEvent> listEvent;
    restored.pollEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );
}

/** @brief [QuestLogTest] 저장 뒤 데이터가 바뀌어도 열린다: 목표 수가 늘면 세기가 범위를 넘지 않고, 지워진 단계는 실패로 두어 다시 받을 수 있으며, 지워진 퀘스트는 버린다 */
SW_TEST_CASE_F( QuestLogTest, SaveSurvivesChangedData )
{
    SW_EXPECT_LOG( "QuestLog.QuestLogSavedStageNot" );
    SW_EXPECT_LOG( "QuestLog.QuestLogSavedQuestNot" );
    finishIntro();
    SW_REQUIRE_TRUE( _log.start( "wolves", 2 ) == QuestStartResult::Ok );
    (void)_log.notify( "Kill", "wolf", 1 );
    SW_REQUIRE_TRUE( _log.start( "gather", 1 ) == QuestStartResult::Ok );

    Archive archive;
    _log.writeState( archive );

    QuestCatalog changed;
    SW_REQUIRE_TRUE( changed.loadFromXMLText( kChangedQuestXML, "QuestLogTest" ) );
    QuestLog restored;
    restored.initialize( &changed );
    Archive reading( archive.getData(), archive.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );

    SW_EXPECT_TRUE( restored.getStatus( "intro" ) == QuestStatus::NotStarted ); // 지워졌다
    const QuestProgress* pWolves = restored.findProgress( "wolves" );
    SW_REQUIRE_TRUE( pWolves != nullptr );
    SW_REQUIRE_EQUAL( size_t( 4 ), pWolves->_listCount.size() );
    SW_EXPECT_EQUAL( 1, pWolves->_listCount[0] );
    SW_EXPECT_EQUAL( 1, restored.notify( "Kill", "fox" ) ); // 저장본에 없던 네 번째 칸이다
    SW_EXPECT_TRUE( restored.getStatus( "gather" ) == QuestStatus::Failed );
    SW_EXPECT_TRUE( restored.evaluateStart( "gather", 1 ) == QuestStartResult::Ok );
}

/** @brief [QuestLogTest] 망가진 저장은 거절하고 있던 일지를 그대로 둔다 */
SW_TEST_CASE_F( QuestLogTest, BrokenSaveIsRefused )
{
    finishIntro();
    Archive archive;
    _log.writeState( archive );
    Archive truncated( archive.getData(), archive.getSize() - 1 );
    SW_EXPECT_FALSE( _log.readState( truncated ) );
    SW_EXPECT_TRUE( _log.getStatus( "intro" ) == QuestStatus::Completed );
}

/** @brief [QuestLogTest] 카탈로그 검사: 없는 단계로 가면 로드가 실패하고(참조 표), 끝나지 못하는 단계는 알리고, 단계 없는 퀘스트와 같은 id 의 단계는 넣지 않는다 */
SW_TEST_CASE( QuestCatalogTest, ReportsBrokenQuests )
{
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" ); // 다음 단계 · 갈래 둘
    SW_EXPECT_LOG( "QuestCatalog.QuestStageDeadEnd" );
    SW_EXPECT_LOG( "QuestCatalog.QuestNoStages" );
    SW_EXPECT_LOG( "QuestCatalog.QuestDuplicateStage" );
    SW_EXPECT_LOG( "QuestCatalog.QuestInvalidFieldsSkipped" );

    QuestCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<QuestCatalog>
  <Quest id="next"><Stage id="a" next="gone"/></Quest>
  <Quest id="branch"><Stage id="a"><Branch choice="x" next="gone"/></Stage></Quest>
  <Quest id="deadEnd"><Stage id="a"/></Quest>
  <Quest id="empty"/>
  <Quest id="duplicate"><Stage id="a" complete="true"/><Stage id="a" fail="true"/></Quest>
</QuestCatalog>)",
                                              "QuestLogTest" ) );
    SW_EXPECT_TRUE( catalog.find( "next" ) != nullptr );
    SW_EXPECT_TRUE( catalog.find( "deadEnd" ) != nullptr );
    SW_EXPECT_TRUE( catalog.find( "empty" ) == nullptr );
    SW_EXPECT_TRUE( catalog.find( "duplicate" ) == nullptr );
}
