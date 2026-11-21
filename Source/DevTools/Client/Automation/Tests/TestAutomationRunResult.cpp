// AutomationRunResultTest — 판정 값 하나(결과 · 실패 종류 · 까닭 한 줄)가 끝 줄 · 체인 줄 · 부모 요약 · 끝 줄(`SCENARIO_RESULT=`)을 모두 짓는다(nogpu).
#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Client/Automation/Runner/AutomationExitCode.h"
#include "DevTools/Client/Automation/Runner/AutomationPerBackend.h"
#include "DevTools/Client/Automation/Runner/AutomationScenarioChain.h"

#include "Engine/Automation/AutomationResult.h"

using namespace sw;

/**
 * @brief [AutomationRunResultTest] 통과로 끝맺었는데 실패 줄이 있으면 첫 실패 줄이 까닭이다 — 끝 단계 이름(`<Pass/>`)이 까닭으로 남지 않는다
 * @details 예전 끝 줄: `Failed X at frame 5: <Pass/>` — 결과와 까닭이 따로 든 두 변수였다.
 */
SW_TEST_CASE( AutomationRunResultTest, PassWithRecordedFailuresTakesTheFirstFailureAsReason )
{
    const AutomationRunResult runResult = AutomationRunResult::passed().concludeWithFailures( "frame 5 <Expect> #3: Game.Score == 1, got 2", AutomationFailureKind::Assertion );
    SW_EXPECT_TRUE( runResult.getResult() == AutomationResult::Failed );
    SW_EXPECT_TRUE( runResult.isFailure() );
    SW_EXPECT_EQUAL( string( "frame 5 <Expect> #3: Game.Score == 1, got 2" ), runResult.getReason() );
    SW_EXPECT_EQUAL( string( "Assertion - frame 5 <Expect> #3: Game.Score == 1, got 2" ), runResult.describe() );
    SW_EXPECT_FALSE( StringUtil::contains( runResult.describe(), "<Pass/>" ) );

    // 실패 줄이 없는 통과는 그대로, 이미 진 판정은 첫 까닭을 지킨다.
    SW_EXPECT_TRUE( AutomationRunResult::passed().concludeWithFailures( {}, AutomationFailureKind::Assertion ).getResult() == AutomationResult::Passed );
    const AutomationRunResult loadError = AutomationRunResult::failed( AutomationFailureKind::LoadError, "unknown step", "<Bogus> #1" ).concludeWithFailures( "later", AutomationFailureKind::Assertion );
    SW_EXPECT_EQUAL( string( "unknown step" ), loadError.getReason() );
    SW_EXPECT_EQUAL( string( "<Bogus> #1" ), loadError.getStepLocation() );
}

/** @brief [AutomationRunResultTest] 종류가 결과(종료 코드의 단위)를 정한다 — 읽기 오류 · 처리하지 않은 로그 · 시한은 제 결과, 골든 · 기준 없음 · 단언은 Failed */
SW_TEST_CASE( AutomationRunResultTest, KindDecidesTheResult )
{
    SW_EXPECT_TRUE( AutomationRunResult::failed( AutomationFailureKind::LoadError, "x", {} ).getResult() == AutomationResult::LoadError );
    SW_EXPECT_TRUE( AutomationRunResult::failed( AutomationFailureKind::UnhandledLog, "x", {} ).getResult() == AutomationResult::UnhandledLog );
    SW_EXPECT_TRUE( AutomationRunResult::failed( AutomationFailureKind::WaitTimeout, "x", {} ).getResult() == AutomationResult::TimedOut );
    SW_EXPECT_TRUE( AutomationRunResult::failed( AutomationFailureKind::Hang, "x", {} ).getResult() == AutomationResult::TimedOut );
    SW_EXPECT_TRUE( AutomationRunResult::failed( AutomationFailureKind::GoldenMismatch, "x", {} ).getResult() == AutomationResult::Failed );
    SW_EXPECT_TRUE( AutomationRunResult::failed( AutomationFailureKind::MissingBaseline, "x", {} ).getResult() == AutomationResult::Failed );
    SW_EXPECT_TRUE( AutomationRunResult::skipped( "no window manager", true ).isSkippedByBuild() );
    SW_EXPECT_FALSE( AutomationRunResult::skipped( "no window manager", false ).isFailure() );
    SW_EXPECT_EQUAL( string( "two lines one line" ), AutomationRunResult::skipped( "two lines\none line", false ).describe() ); // 끝 줄은 한 줄이다
}

/** @brief [AutomationRunResultTest] 처리하지 않은 로그만 남은 통과는 `UnhandledLog` 이고 까닭은 첫 줄과 남은 수다 */
SW_TEST_CASE( AutomationRunResultTest, UnhandledLogsBecomeTheReason )
{
    const vector<string>      listLine{ "[Warning] (id=Shadow.AtlasFull) atlas full", "[Warning] (id=Shadow.AtlasFull) atlas full", "[Error] (id=X.Y) z" };
    const AutomationRunResult runResult = AutomationRunResult::passed().concludeWithUnhandledLogs( listLine );
    SW_EXPECT_TRUE( runResult.getResult() == AutomationResult::UnhandledLog );
    SW_EXPECT_EQUAL( string( "UnhandledLog - [Warning] (id=Shadow.AtlasFull) atlas full (+2)" ), runResult.describe() );
    // 이미 진 판정은 바꾸지 않는다 — 원인은 단언이다.
    SW_EXPECT_TRUE( AutomationRunResult::failed( AutomationFailureKind::Assertion, "x", {} ).concludeWithUnhandledLogs( listLine ).getFailureKind() == AutomationFailureKind::Assertion );
}

/**
 * @brief [AutomationRunResultTest] 체인 줄은 앞머리(`chain stage i/n <결과> (<이름>)`)를 지키고 `: <종류> - <까닭>` 을 붙인다. 진 단계는 체인 끝 표에 남는다
 * @details 앞머리는 `linux_scenarios.py` 의 정규식이 읽는다. `: <Pass/>` 로 끝나는 실패 줄은 없다(합격 문턱).
 */
SW_TEST_CASE( AutomationRunResultTest, ChainStageLineCarriesTheResult )
{
    AutomationScenarioChain chain;
    chain.begin( { "game/a.scenario.xml", "game/b.scenario.xml", "game/c.scenario.xml" }, false, 3 );
    SW_EXPECT_TRUE( chain.onStageEnded( AutomationRunResult::passed(), "game.a" ) );
    SW_EXPECT_EQUAL( string( "chain stage 1/3 Passed (game.a)" ), chain.describeStageEnd( 0, AutomationRunResult::passed(), "game.a" ) );
    const AutomationRunResult failed = AutomationRunResult::passed().concludeWithFailures( "frame 9 <ExpectImage> #4: differs from its golden: 2.1%", AutomationFailureKind::GoldenMismatch );
    SW_EXPECT_FALSE( chain.onStageEnded( failed, "game.b" ) ); // 첫 실패에서 멈춘다
    const string line = chain.describeStageEnd( 1, failed, "game.b" );
    SW_EXPECT_EQUAL( string( "chain stage 2/3 Failed (game.b): GoldenMismatch - frame 9 <ExpectImage> #4: differs from its golden: 2.1% - 1 later stage(s) skipped" ), line );
    SW_EXPECT_FALSE( StringUtil::endsWith( line, ": <Pass/>" ) );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( chain.describeFailedStages().size() ) );
    SW_EXPECT_EQUAL( string( "2/3 game.b: GoldenMismatch - frame 9 <ExpectImage> #4: differs from its golden: 2.1%" ), chain.describeFailedStages()[0] );
    SW_EXPECT_TRUE( chain.getFinalResult() == AutomationResult::Failed );
}

/**
 * @brief [AutomationRunResultTest] `keepGoing` 체인도 같은 대기 실패(종류 + 반복 열쇠)가 한도만큼 나오면 멈추고 끝 표에 `RepeatedFailure` 줄을 남긴다 — 다른 열쇠 · 열쇠 없는 실패는 세지 않는다
 * @details 변이: 열쇠를 세지 않으면 넷째 단계로 넘어가 셋째 단언이 진다. 종류를 열쇠에 넣지 않으면 시한 · 정지가 한데 세여 둘째 단언이 진다.
 */
SW_TEST_CASE( AutomationRunResultTest, RepeatedWaitFailureStopsTheChain )
{
    AutomationScenarioChain chain;
    chain.begin( { "a", "b", "c", "d", "e", "f" }, true, 2 );
    const AutomationRunResult stalled   = AutomationRunResult::failed( AutomationFailureKind::WaitStalled, "no progress", "<Screenshot> #2" ).withRepeatKey( "Screenshot RenderTemporalAccumulation" );
    const AutomationRunResult timedOut  = AutomationRunResult::failed( AutomationFailureKind::WaitTimeout, "too long", "<Screenshot> #2" ).withRepeatKey( "Screenshot RenderTemporalAccumulation" );
    const AutomationRunResult assertion = AutomationRunResult::failed( AutomationFailureKind::Assertion, "x", "<Expect> #1" );
    SW_EXPECT_TRUE( chain.onStageEnded( stalled, "a" ) );
    SW_EXPECT_TRUE( chain.onStageEnded( timedOut, "b" ) );  // 종류가 다르다
    SW_EXPECT_TRUE( chain.onStageEnded( assertion, "c" ) ); // 열쇠가 없다 — 대기의 포기가 아니다
    SW_EXPECT_FALSE( chain.onStageEnded( stalled, "d" ) );
    SW_EXPECT_EQUAL( string( "RepeatedFailure - WaitStalled Screenshot RenderTemporalAccumulation x2" ), chain.getRepeatedFailureLine() );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( chain.getSkippedStageCount() ) );
    SW_EXPECT_TRUE( StringUtil::endsWith( chain.describeFailedStages().back(), "- chain stopped" ) );
    SW_EXPECT_TRUE( StringUtil::contains( chain.describeStageEnd( 3, stalled, "d" ), "RepeatedFailure" ) );
}

/** @brief [AutomationRunResultTest] 부모 요약은 (단계 · 종류 · 까닭)이 같은 실패를 백엔드로 접는다 — 모든 백엔드가 진 것(전제)과 하나만 진 것(백엔드)이 갈린다 */
SW_TEST_CASE( AutomationRunResultTest, ParentFoldsTheSameFailureAcrossBackends )
{
    const vector<AutomationStageFailure> listStageFailure{
        {"game.vfxsort", "dx11", "GoldenMismatch",        "differs"},
        {"game.vfxsort", "dx12", "GoldenMismatch",        "differs"},
        {   "game.ssgi",   "vk",      "Assertion", "frame 3: got 2"},
        {"game.vfxsort",   "vk", "GoldenMismatch",        "differs"},
        {"game.vfxsort",   "gl", "GoldenMismatch",        "differs"},
    };
    const vector<string> listLine = AutomationPerBackend::foldFailures( listStageFailure );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( listLine.size() ) );
    SW_EXPECT_EQUAL( string( "game.vfxsort: GoldenMismatch dx11,dx12,vk,gl - differs" ), listLine[0] );
    SW_EXPECT_EQUAL( string( "game.ssgi: Assertion vk - frame 3: got 2" ), listLine[1] );
}

/** @brief [AutomationRunResultTest] 부모의 끝 줄은 종료 코드가 결과 이름을 정하고 단계 수를 싣는다(`WINDOWS_OK=` 와 같은 꼴) */
SW_TEST_CASE( AutomationRunResultTest, ParentResultLineNamesTheExitCode )
{
    SW_EXPECT_EQUAL( string( "SCENARIO_RESULT=PASSED stages=3 failed=0 notRun=0" ), AutomationPerBackend::makeResultLine( 0, 3, 0, 0 ) );
    SW_EXPECT_EQUAL( string( "SCENARIO_RESULT=FAILED stages=4 failed=1 notRun=2" ),
                     AutomationPerBackend::makeResultLine( getAutomationExitStatus( AutomationResult::Failed ), 4, 1, 2 ) );
    SW_EXPECT_TRUE( StringUtil::startsWith( AutomationPerBackend::makeResultLine( getAutomationExitStatus( AutomationResult::TimedOut ), 1, 1, 0 ), "SCENARIO_RESULT=TIMEOUT " ) );
    SW_EXPECT_TRUE( StringUtil::startsWith( AutomationPerBackend::makeResultLine( getAutomationExitStatus( AutomationResult::LoadError ), 1, 1, 0 ), "SCENARIO_RESULT=LOAD_ERROR " ) );
    SW_EXPECT_TRUE( StringUtil::startsWith( AutomationPerBackend::makeResultLine( getAutomationExitStatus( AutomationResult::Skipped ), 1, 0, 0 ), "SCENARIO_RESULT=SKIPPED " ) );
}
