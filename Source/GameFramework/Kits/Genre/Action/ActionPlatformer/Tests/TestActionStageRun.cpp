#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Catalog/ActionPlatformerCatalog.h"
#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Rule/ActionStageRun.h"

// ActionStageRunTest — 스테이지 한 판: 시간 · 피격 · 수집 점수와 등급 경계, 체크포인트 순서, 죽으면 다시 놓이는 비밀, 게임 오버 뒤 멈춤, 깨진 상태 바이트 거절(스테이지 없는 진행 판 · 겹친 비밀).
// ActionPlatformerCatalogTest — 카탈로그 거절: 등급표 없음 · 가중치 0 · 겹친 이름 · 모르는 상태. 키트 GF_ActionPlatformer(nogpu).

namespace
{
    constexpr const utf8* kStageRunXML = R"(<ActionPlatformer>
  <Grading time="1" hits="1" collect="1"><Grade id="C" min="50"/><Grade id="S" min="90"/></Grading>
  <Stage id="s1" name="Roof" parTime="10" lives="2" hitTolerance="4" checkpoints="cp1,cp2" secrets="g1,g2"/>
  <Stage id="bare" name="Bare" parTime="10" lives="1" hitTolerance="1"/>
</ActionPlatformer>)";

    const sw::hashed_string kStage1{ "s1" };
    const sw::hashed_string kStageBare{ "bare" };
    const sw::hashed_string kCheckpoint1{ "cp1" };
    const sw::hashed_string kCheckpoint2{ "cp2" };
    const sw::hashed_string kSecret1{ "g1" };
    const sw::hashed_string kSecret2{ "g2" };
    const sw::hashed_string kGradeS{ "S" };
    const sw::hashed_string kGradeC{ "C" };

    constexpr float32 kStageTolerance = 1.0e-3f;

    struct ActionStageRunTestUtil
    {
        /** @brief `ActionStageRun::writeState` 와 같은 모양의 바이트를 씁니다. 정상 경로로 만들 수 없는 판을 읽기에 넣을 때 씁니다. */
        static void writeRunBytes( sw::Archive& outArchive, const sw::hashed_string& stageID, sw::ActionStageState state, int32 checkpointIndex, const sw::vector<sw::hashed_string>& listCommitted,
                                   const sw::vector<sw::hashed_string>& listPending )
        {
            sw::StateIO::writeName( outArchive, stageID );
            outArchive << static_cast<uint8>( state );
            outArchive << 1.0f;                    // 경과 시간
            outArchive << static_cast<int32>( 1 ); // 목숨
            outArchive << checkpointIndex;
            outArchive << static_cast<int32>( 0 ); // 피격
            outArchive << static_cast<int32>( 0 ); // 죽은 수
            outArchive << static_cast<uint32>( listCommitted.size() );
            for ( const sw::hashed_string& secretID : listCommitted )
            {
                sw::StateIO::writeName( outArchive, secretID );
            }
            outArchive << static_cast<uint32>( listPending.size() );
            for ( const sw::hashed_string& secretID : listPending )
            {
                sw::StateIO::writeName( outArchive, secretID );
            }
        }
    };
} // namespace

/**
 * @brief [ActionStageRunTest] 세 점수의 평균이 등급 경계(50)에 정확히 닿으면 그 등급이고, 못 미치면 맨 아래 등급으로 올려 주지 않고 등급이 없다
 */
SW_TEST_CASE( ActionStageRunTest, ScoreAveragesThreePartsAndGradeHasNoFallback )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kStageRunXML, "ActionStageRunTest" ) );
    sw::ActionStageRun run;
    SW_REQUIRE_TRUE( run.start( &catalog, kStage1 ) );

    run.update( 20.0f ); // 기준 시간의 두 배 → 시간 점수 0.5
    SW_REQUIRE_TRUE( run.collectSecret( kSecret1 ) );
    SW_REQUIRE_TRUE( run.reachCheckpoint( kCheckpoint1 ) ); // 비밀 하나 확정 → 수집 점수 0.5
    run.registerHit();
    SW_EXPECT_EQUAL( kCheckpoint1, run.die() ); // 피격 1 + 죽음 1 = 2, 허용 4 → 피격 점수 0.5

    sw::ActionStageResult result = run.computeResult();
    SW_EXPECT_NEAR( 0.5f, result._timeScore, kStageTolerance );
    SW_EXPECT_NEAR( 0.5f, result._hitScore, kStageTolerance );
    SW_EXPECT_NEAR( 0.5f, result._collectScore, kStageTolerance );
    SW_EXPECT_NEAR( 50.0f, result._score, kStageTolerance );
    SW_EXPECT_EQUAL( kGradeC, result._grade );
    SW_EXPECT_EQUAL( 1, result._hitCount );
    SW_EXPECT_EQUAL( 1, result._deathCount );

    run.registerHit(); // 피격 점수 0.25 → 41.67 점은 어떤 등급의 최소 점수에도 못 미친다
    result = run.computeResult();
    SW_EXPECT_NEAR( 125.0f / 3.0f, result._score, kStageTolerance );
    SW_EXPECT_TRUE( result._grade.empty() );
}

/**
 * @brief [ActionStageRunTest] 기준 시간에 정확히 깨면 시간 만점이고, 클리어가 지닌 비밀을 확정하며, 깬 뒤에는 시간도 행동도 멈춘다
 */
SW_TEST_CASE( ActionStageRunTest, ClearCommitsPendingSecretsAndFreezesTheRun )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kStageRunXML, "ActionStageRunTest" ) );
    sw::ActionStageRun run;
    SW_REQUIRE_TRUE( run.start( &catalog, kStage1 ) );
    run.update( 10.0f );
    SW_REQUIRE_TRUE( run.collectSecret( kSecret1 ) );
    SW_REQUIRE_TRUE( run.collectSecret( kSecret2 ) );
    SW_EXPECT_EQUAL( 0, run.computeResult()._secretFound ); // 지닌 것은 아직 세지 않는다

    SW_REQUIRE_TRUE( run.clearStage() );
    SW_EXPECT_TRUE( run.getState() == sw::ActionStageState::Cleared );
    SW_EXPECT_EQUAL( 0, run.getSecretPendingCount() );
    const sw::ActionStageResult result = run.computeResult();
    SW_EXPECT_EQUAL( 2, result._secretFound );
    SW_EXPECT_NEAR( 100.0f, result._score, kStageTolerance );
    SW_EXPECT_EQUAL( kGradeS, result._grade );

    run.update( 5.0f );
    run.registerHit();
    SW_EXPECT_NEAR( 10.0f, run.getElapsed(), kStageTolerance );
    SW_EXPECT_EQUAL( 0, run.computeResult()._hitCount );
    SW_EXPECT_FALSE( run.clearStage() );
    SW_EXPECT_FALSE( run.reachCheckpoint( kCheckpoint1 ) );
}

/**
 * @brief [ActionStageRunTest] 허용 수를 넘긴 피격은 점수를 음수로 만들지 않고, 비밀이 없는 스테이지는 수집 만점이며, 음수 시간은 흐르지 않는다
 */
SW_TEST_CASE( ActionStageRunTest, ScoresStayInsideTheirRange )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kStageRunXML, "ActionStageRunTest" ) );
    sw::ActionStageRun run;
    SW_REQUIRE_TRUE( run.start( &catalog, kStageBare ) );
    run.update( -5.0f );
    run.update( 0.0f );
    SW_EXPECT_NEAR( 0.0f, run.getElapsed(), kStageTolerance );
    run.registerHit();
    run.registerHit();
    run.registerHit();
    const sw::ActionStageResult result = run.computeResult();
    SW_EXPECT_NEAR( 0.0f, result._hitScore, kStageTolerance );
    SW_EXPECT_NEAR( 1.0f, result._collectScore, kStageTolerance );
    SW_EXPECT_EQUAL( 0, result._secretTotal );
    SW_EXPECT_NEAR( 200.0f / 3.0f, result._score, kStageTolerance );
}

/**
 * @brief [ActionStageRunTest] 체크포인트는 앞으로만 간다: 건너뛰어도 인정하고, 지난 것 · 같은 것 · 모르는 것은 거절하며 시작점이 그대로다
 */
SW_TEST_CASE( ActionStageRunTest, CheckpointsOnlyMoveForward )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kStageRunXML, "ActionStageRunTest" ) );
    sw::ActionStageRun run;
    SW_EXPECT_FALSE( run.reachCheckpoint( kCheckpoint1 ) ); // 시작 전
    SW_REQUIRE_TRUE( run.start( &catalog, kStage1 ) );
    SW_EXPECT_EQUAL( -1, run.getCheckpointIndex() );

    SW_EXPECT_TRUE( run.reachCheckpoint( kCheckpoint2 ) );
    SW_EXPECT_EQUAL( 1, run.getCheckpointIndex() );
    SW_EXPECT_FALSE( run.reachCheckpoint( kCheckpoint1 ) );
    SW_EXPECT_FALSE( run.reachCheckpoint( kCheckpoint2 ) );
    SW_EXPECT_FALSE( run.reachCheckpoint( sw::hashed_string( "nowhere" ) ) );
    SW_EXPECT_EQUAL( 1, run.getCheckpointIndex() );
}

/**
 * @brief [ActionStageRunTest] 확정하지 못한 비밀은 죽으면 다시 놓여 또 주울 수 있고, 확정한 것 · 모르는 것 · 이미 지닌 것은 줍지 못한다
 */
SW_TEST_CASE( ActionStageRunTest, DeathReturnsOnlyUncommittedSecrets )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kStageRunXML, "ActionStageRunTest" ) );
    sw::ActionStageRun run;
    SW_REQUIRE_TRUE( run.start( &catalog, kStage1 ) );
    SW_EXPECT_FALSE( run.collectSecret( sw::hashed_string( "unknown" ) ) );
    SW_REQUIRE_TRUE( run.collectSecret( kSecret1 ) );
    SW_EXPECT_FALSE( run.collectSecret( kSecret1 ) );
    SW_REQUIRE_TRUE( run.reachCheckpoint( kCheckpoint1 ) );
    SW_EXPECT_FALSE( run.collectSecret( kSecret1 ) ); // 확정한 것
    SW_REQUIRE_TRUE( run.collectSecret( kSecret2 ) );

    SW_EXPECT_EQUAL( kCheckpoint1, run.die() );
    SW_EXPECT_TRUE( run.getState() == sw::ActionStageState::Playing );
    SW_EXPECT_TRUE( run.isSecretFound( kSecret1 ) );
    SW_EXPECT_FALSE( run.isSecretFound( kSecret2 ) );
    SW_EXPECT_EQUAL( 1, run.getSecretCommittedCount() );
    SW_EXPECT_TRUE( run.collectSecret( kSecret2 ) );
}

/**
 * @brief [ActionStageRunTest] 마지막 목숨이 지면 게임 오버로 모든 행동이 멈추고, 다시 시작하면 모두 처음으로 돌아간다
 */
SW_TEST_CASE( ActionStageRunTest, GameOverStopsEverythingUntilRestart )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kStageRunXML, "ActionStageRunTest" ) );
    sw::ActionStageRun run;
    SW_REQUIRE_TRUE( run.start( &catalog, kStage1 ) );
    run.update( 3.0f );
    SW_REQUIRE_TRUE( run.collectSecret( kSecret1 ) );
    SW_REQUIRE_TRUE( run.reachCheckpoint( kCheckpoint1 ) );

    SW_EXPECT_EQUAL( kCheckpoint1, run.die() );
    SW_EXPECT_EQUAL( 1, run.getLives() );
    SW_EXPECT_TRUE( run.die().empty() ); // 체크포인트가 있어도 게임 오버에는 돌아갈 곳이 없다
    SW_EXPECT_EQUAL( 0, run.getLives() );
    SW_EXPECT_TRUE( run.getState() == sw::ActionStageState::GameOver );

    SW_EXPECT_TRUE( run.die().empty() );
    SW_EXPECT_EQUAL( 0, run.getLives() );
    SW_EXPECT_EQUAL( 2, run.computeResult()._deathCount );
    run.update( 1.0f );
    SW_EXPECT_NEAR( 3.0f, run.getElapsed(), kStageTolerance );
    SW_EXPECT_FALSE( run.collectSecret( kSecret2 ) );
    SW_EXPECT_FALSE( run.clearStage() );

    run.restartStage();
    SW_EXPECT_TRUE( run.getState() == sw::ActionStageState::Playing );
    SW_EXPECT_EQUAL( 2, run.getLives() );
    SW_EXPECT_EQUAL( -1, run.getCheckpointIndex() );
    SW_EXPECT_EQUAL( 0, run.getSecretCommittedCount() );
    SW_EXPECT_NEAR( 0.0f, run.getElapsed(), kStageTolerance );
    SW_EXPECT_EQUAL( 0, run.computeResult()._deathCount );
}

/**
 * @brief [ActionStageRunTest] 모르는 스테이지 · 빈 카탈로그로는 시작하지 않고, 앞서 하던 판도 건드리지 않는다. 시작 전의 호출은 아무 일도 하지 않는다
 */
SW_TEST_CASE( ActionStageRunTest, StartRejectsUnknownStage )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kStageRunXML, "ActionStageRunTest" ) );
    sw::ActionStageRun run;
    run.restartStage();
    SW_EXPECT_TRUE( run.getState() == sw::ActionStageState::NotStarted );
    SW_EXPECT_TRUE( run.die().empty() );
    SW_EXPECT_FALSE( run.collectSecret( kSecret1 ) );
    run.registerHit();
    SW_EXPECT_NEAR( 0.0f, run.computeResult()._score, kStageTolerance );

    SW_EXPECT_FALSE( run.start( nullptr, kStage1 ) );
    SW_EXPECT_FALSE( run.start( &catalog, sw::hashed_string( "nowhere" ) ) );
    SW_EXPECT_TRUE( run.getStage() == nullptr );

    SW_REQUIRE_TRUE( run.start( &catalog, kStage1 ) );
    run.update( 2.0f );
    SW_EXPECT_FALSE( run.start( &catalog, sw::hashed_string( "nowhere" ) ) );
    SW_EXPECT_NEAR( 2.0f, run.getElapsed(), kStageTolerance );
    SW_EXPECT_EQUAL( kStage1, run.getStage()->_id );
}

/**
 * @brief [ActionStageRunTest] 쓰고 읽은 판은 같은 점수를 내고, 확정한 비밀과 지닌 비밀을 구분해 이어 간다
 */
SW_TEST_CASE( ActionStageRunTest, StateRoundTripKeepsTheRun )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kStageRunXML, "ActionStageRunTest" ) );
    sw::ActionStageRun original;
    SW_REQUIRE_TRUE( original.start( &catalog, kStage1 ) );
    original.update( 12.0f );
    original.registerHit();
    SW_REQUIRE_TRUE( original.collectSecret( kSecret1 ) );
    SW_REQUIRE_TRUE( original.reachCheckpoint( kCheckpoint2 ) );
    SW_REQUIRE_TRUE( original.collectSecret( kSecret2 ) );

    sw::Archive written;
    original.writeState( written );

    sw::ActionStageRun noCatalog;
    sw::Archive        withoutCatalog( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( noCatalog.readState( withoutCatalog ) );

    sw::ActionStageRun restored;
    restored.bindCatalog( &catalog );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( restored.getState() == sw::ActionStageState::Playing );
    SW_EXPECT_EQUAL( 1, restored.getCheckpointIndex() );
    SW_EXPECT_EQUAL( 1, restored.getSecretCommittedCount() );
    SW_EXPECT_EQUAL( 1, restored.getSecretPendingCount() );
    SW_EXPECT_NEAR( original.computeResult()._score, restored.computeResult()._score, kStageTolerance );

    (void)restored.die();
    SW_EXPECT_TRUE( restored.isSecretFound( kSecret1 ) );
    SW_EXPECT_FALSE( restored.isSecretFound( kSecret2 ) );

    sw::ActionStageRun truncatedTarget;
    truncatedTarget.bindCatalog( &catalog );
    sw::Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( truncatedTarget.readState( truncated ) );
    SW_EXPECT_TRUE( truncatedTarget.getState() == sw::ActionStageState::NotStarted );
}

/**
 * @brief [ActionStageRunTest] 결함: 스테이지 이름이 빈 채 진행 중인 판을 받아들였다 — 다음 행동이 빈 정의를 읽어 죽는다. 시작 전 판만 스테이지 없이 읽힌다
 */
SW_TEST_CASE( ActionStageRunTest, ReadRejectsARunningStateWithoutStage )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kStageRunXML, "ActionStageRunTest" ) );
    const sw::ActionStageState arrState[] = { sw::ActionStageState::Playing, sw::ActionStageState::Cleared, sw::ActionStageState::GameOver };
    for ( const sw::ActionStageState state : arrState )
    {
        sw::Archive written;
        ActionStageRunTestUtil::writeRunBytes( written, sw::hashed_string{}, state, -1, {}, {} );
        sw::ActionStageRun target;
        target.bindCatalog( &catalog );
        sw::Archive reading( written.getData(), written.getSize() );
        SW_EXPECT_FALSE( target.readState( reading ) );
        SW_EXPECT_TRUE( target.getState() == sw::ActionStageState::NotStarted );
    }

    sw::Archive written;
    ActionStageRunTestUtil::writeRunBytes( written, sw::hashed_string{}, sw::ActionStageState::NotStarted, -1, {}, {} );
    sw::ActionStageRun target;
    target.bindCatalog( &catalog );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_EXPECT_TRUE( target.readState( reading ) );
}

/**
 * @brief [ActionStageRunTest] 결함: 같은 비밀이 두 번 든 바이트를 받아들여 수집 점수가 1 을 넘었다. 겹침 · 남의 비밀 · 범위 밖 체크포인트 · 모르는 스테이지 · 모르는 판 상태를 거절하고 판은 그대로다
 */
SW_TEST_CASE( ActionStageRunTest, ReadRejectsInconsistentBytes )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kStageRunXML, "ActionStageRunTest" ) );
    sw::ActionStageRun target;
    SW_REQUIRE_TRUE( target.start( &catalog, kStage1 ) );
    target.update( 4.0f );

    const auto expectRejected = [&target]( const sw::Archive& written )
    {
        sw::Archive reading( written.getData(), written.getSize() );
        SW_EXPECT_FALSE( target.readState( reading ) );
        SW_EXPECT_NEAR( 4.0f, target.getElapsed(), kStageTolerance );
        SW_EXPECT_EQUAL( 0, target.getSecretCommittedCount() );
    };

    sw::Archive twiceCommitted;
    ActionStageRunTestUtil::writeRunBytes( twiceCommitted, kStage1, sw::ActionStageState::Playing, -1, { kSecret1, kSecret1 }, {} );
    expectRejected( twiceCommitted );

    sw::Archive inBothLists;
    ActionStageRunTestUtil::writeRunBytes( inBothLists, kStage1, sw::ActionStageState::Playing, -1, { kSecret1 }, { kSecret1 } );
    expectRejected( inBothLists );

    sw::Archive foreignSecret;
    ActionStageRunTestUtil::writeRunBytes( foreignSecret, kStage1, sw::ActionStageState::Playing, -1, { sw::hashed_string( "other" ) }, {} );
    expectRejected( foreignSecret );

    sw::Archive checkpointOutOfRange;
    ActionStageRunTestUtil::writeRunBytes( checkpointOutOfRange, kStage1, sw::ActionStageState::Playing, 2, {}, {} );
    expectRejected( checkpointOutOfRange );

    sw::Archive checkpointBelowStart;
    ActionStageRunTestUtil::writeRunBytes( checkpointBelowStart, kStage1, sw::ActionStageState::Playing, -2, {}, {} );
    expectRejected( checkpointBelowStart );

    sw::Archive unknownStage;
    ActionStageRunTestUtil::writeRunBytes( unknownStage, sw::hashed_string( "nowhere" ), sw::ActionStageState::Playing, -1, {}, {} );
    expectRejected( unknownStage );

    sw::Archive unknownState;
    ActionStageRunTestUtil::writeRunBytes( unknownState, kStage1, static_cast<sw::ActionStageState>( 200 ), -1, {}, {} );
    expectRejected( unknownState );
}

/**
 * @brief [ActionPlatformerCatalogTest] 등급표가 없거나 비었거나 가중치가 모두 0 이면 읽기가 실패하고, 등급은 높은 것부터 놓인다
 */
SW_TEST_CASE( ActionPlatformerCatalogTest, GradingIsRequired )
{
    SW_EXPECT_LOG( "ActionPlatformerCatalog.NeedsGradingLeastOne" );
    SW_EXPECT_LOG( "ActionPlatformerCatalog.GradingWeightsTimeHits" );
    SW_EXPECT_LOG( "ActionPlatformerCatalog.GradingNeedsLeastOne" );

    sw::ActionPlatformerCatalog noGrading;
    SW_EXPECT_FALSE( noGrading.loadFromXMLText( R"(<ActionPlatformer><Stage id="a" name="a"/></ActionPlatformer>)", "noGrading" ) );
    sw::ActionPlatformerCatalog zeroWeights;
    SW_EXPECT_FALSE( zeroWeights.loadFromXMLText( R"(<ActionPlatformer><Grading time="0" hits="0" collect="0"><Grade id="S" min="1"/></Grading></ActionPlatformer>)", "zeroWeights" ) );
    sw::ActionPlatformerCatalog emptyGrading;
    SW_EXPECT_FALSE( emptyGrading.loadFromXMLText( R"(<ActionPlatformer><Grading/></ActionPlatformer>)", "emptyGrading" ) );

    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kStageRunXML, "ActionStageRunTest" ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), catalog.getGrading()._listGrade.size() );
    SW_EXPECT_EQUAL( kGradeS, catalog.getGrading()._listGrade[0]._grade );
    SW_EXPECT_EQUAL( kGradeC, catalog.getGrading()._listGrade[1]._grade );
}

/**
 * @brief [ActionPlatformerCatalogTest] 결함: 겹친 비밀(다 모아도 만점이 못 된다) · 겹친 체크포인트 · 겹친 상태 id 를 말없이 받았다. 그 원소만 거절하고 나머지는 읽는다
 */
SW_TEST_CASE( ActionPlatformerCatalogTest, DuplicateNamesAreRejected )
{
    SW_EXPECT_LOG( "ActionPlatformerCatalog.StageDuplicateName" );
    SW_EXPECT_LOG( "ActionPlatformerCatalog.PatternDuplicateState" );

    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<ActionPlatformer>
  <Grading><Grade id="S" min="90"/></Grading>
  <Stage id="twoSecrets" name="a" secrets="g,g"/>
  <Stage id="twoCheckpoints" name="b" checkpoints="c,d,c"/>
  <Stage id="fine" name="c" checkpoints="c,d" secrets="c,d"/>
  <Pattern id="twoStates" start="a"><State id="a"/><State id="a"/></Pattern>
  <Pattern id="finePattern" start="a"><State id="a"/></Pattern>
</ActionPlatformer>)",
                                              "duplicates" ) );
    SW_EXPECT_TRUE( catalog.findStage( sw::hashed_string( "twoSecrets" ) ) == nullptr );
    SW_EXPECT_TRUE( catalog.findStage( sw::hashed_string( "twoCheckpoints" ) ) == nullptr );
    SW_EXPECT_TRUE( catalog.findStage( sw::hashed_string( "fine" ) ) != nullptr ); // 비밀과 체크포인트는 이름 공간이 다르다
    SW_EXPECT_TRUE( catalog.findPattern( sw::hashed_string( "twoStates" ) ) == nullptr );
    SW_EXPECT_TRUE( catalog.findPattern( sw::hashed_string( "finePattern" ) ) != nullptr );
}

/**
 * @brief [ActionPlatformerCatalogTest] 빈 콤보, 상태 없는 패턴, 모르는 시작 상태 · 이어지는 상태, 모르는 원소는 오류를 남기고 넣지 않는다
 */
SW_TEST_CASE( ActionPlatformerCatalogTest, BrokenElementsAreSkipped )
{
    SW_EXPECT_LOG( "ActionPlatformerCatalog.ComboIdNeedsMoves" );
    SW_EXPECT_LOG( "ActionPlatformerCatalog.PatternIdNoState" );
    SW_EXPECT_LOG( "ActionPlatformerCatalog.PatternIdStartsUnknown" );
    SW_EXPECT_LOG( "ActionPlatformerCatalog.PatternStateGoesUnknown" );
    SW_EXPECT_LOG( "ActionPlatformerCatalog.UnknownElementSkipped" );

    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<ActionPlatformer>
  <Grading><Grade id="S" min="90"/></Grading>
  <Combo id="empty"/>
  <Pattern id="noState" start="a"/>
  <Pattern id="badStart" start="ghost"><State id="a"/></Pattern>
  <Pattern id="badNext" start="a"><State id="a" next="ghost"/></Pattern>
  <Pattern id="badNear" start="a"><State id="a" onNear="ghost"/></Pattern>
  <Pattern id="badHit" start="a"><State id="a" onHit="ghost"/></Pattern>
  <Mystery/>
</ActionPlatformer>)",
                                              "broken" ) );
    SW_EXPECT_TRUE( catalog.findCombo( sw::hashed_string( "empty" ) ) == nullptr );
    const utf8* const arrPattern[] = { "noState", "badStart", "badNext", "badNear", "badHit" };
    for ( const utf8* const pPattern : arrPattern )
    {
        SW_EXPECT_TRUE( catalog.findPattern( sw::hashed_string( pPattern ) ) == nullptr );
    }
}
