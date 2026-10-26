#include "pch.h"

#include "Core/Diagnostics/CrashContext.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Automation/AutomationExitCode.h"
#include "Engine/Automation/AutomationRunTitle.h"
#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Probe/AutomationProbe.h"

#include "TestFramework/TestFramework.h"

// 실행기의 벽시계 감시 · 시작 차단 · 진 실행의 로그 게이트 · 보고의 실행 환경 칸 — 씬 · 창 없이 실행기를 손으로 돌린다.
SW_LOG_CALLER( "TestAutomationRunnerWatch" );

namespace
{
    struct TestAutomationRunnerWatchInternal
    {
        /** @brief 프레임마다 쉬는 시간(나노초)입니다 — 짧은 벽시계 상한이 몇 프레임 안에 닿게 한다. */
        static constexpr int64 kFrameSleepNanoseconds = 2000000;

        static inline bool s_bStartBlocked = false; ///< 시작 차단 조건이 참인지(등록 함수는 사용자 자료 없는 함수 포인터라 정적 칸에 둔다)

        [[nodiscard]] static bool isStartBlockedNow() { return s_bStartBlocked; }

        [[nodiscard]] static bool readNothing( const sw::GameObjectManager* /*pManager*/, float64& /*outValue*/ ) { return false; }

        /** @brief 실행기를 한 프레임 돌립니다(엔진 루프와 같은 순서). */
        static sw::AutomationOutcome stepFrame( sw::AutomationRunner& runner, sw::InputManager& input )
        {
            runner.onFrameBegin( input );
            input.beginFrame( 1.0f / 60.0f );
            const sw::AutomationOutcome result = runner.onFrameEnd( input );
            input.endFrame();
            return result;
        }

        static sw::string readReport( const sw::string& path )
        {
            sw::string text;
            SW_EXPECT_TRUE( sw::FileUtil::readTextFile( path, text ) );
            return text;
        }

        static sw::string makeReportPath( const utf8* pName ) { return test::makeTempDirectory( "RunnerWatch" ) + "/" + pName + ".json"; }
    };

    SW_AUTOMATION_PROBE( watchTestNothing, "WatchTest.Nothing", "never has a value", &TestAutomationRunnerWatchInternal::readNothing );
    SW_AUTOMATION_START_BLOCKER( watchTestBlocker, "WatchTestBlocker", &TestAutomationRunnerWatchInternal::isStartBlockedNow );
} // namespace

/**
 * @brief [AutomationRunnerWatchTest] 시계를 붙잡힌 채 프레임 시한이 서지 않는 시나리오도 벽시계 상한(`wallTimeoutSeconds`)에서 시간 초과(12)로 끝나고 보고를 쓴다
 * @details 프레임 시한(`timeoutFrames`)은 붙잡은 프레임을 세지 않아 이 시나리오(끝나지 않는 `WaitUntil`)는 상한 없이는 프로세스 시한(보고 없는 kill)까지 간다.
 *          변이: 벽시계 감시를 빼면 하네스 프레임 상한(400)까지 Running 이다.
 */
SW_TEST_CASE( AutomationRunnerWatchTest, WallClockLimitEndsAHeldScenarioAndWritesTheReport )
{
    using Internal = TestAutomationRunnerWatchInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    SW_TEST_DEFENSIVE_SCOPE( "a timed out scenario logs an error line" );
    const sw::string     reportPath = Internal::makeReportPath( "wall" );
    sw::AutomationRunner runner;
    SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" timeoutFrames="10" wallTimeoutSeconds="0.05">
        <At frame="1"><WaitUntil probe="WatchTest.Nothing" atLeast="0" timeoutFrames="100000"/></At>
        <At frame="2"><Pass/></At>
      </Scenario>)",
                                          reportPath ) );
    sw::AutomationOutcome result = sw::AutomationOutcome::Running;
    for ( uint32 frame = 0; frame < 400 && result == sw::AutomationOutcome::Running; ++frame )
    {
        result = Internal::stepFrame( runner, input );
        sw::MonotonicClock::sleepUntilNanoseconds( sw::MonotonicClock::nowNanoseconds() + Internal::kFrameSleepNanoseconds );
    }
    SW_EXPECT_TRUE_MSG( result == sw::AutomationOutcome::TimedOut, runner.getFinishReason().c_str() );
    SW_EXPECT_EQUAL( 12, sw::getAutomationExitCode( result ) );
    SW_EXPECT_TRUE_MSG( runner.getFinishReason().find( "wall-clock limit" ) != sw::string::npos, runner.getFinishReason().c_str() );
    SW_EXPECT_TRUE_MSG( runner.getFinishReason().find( "held the scenario clock" ) != sw::string::npos, runner.getFinishReason().c_str() ); // 어디서 붙잡혔는지 이유에 든다
    SW_EXPECT_TRUE( runner.getFrameIndex() <= 2u );                                                                                         // 시나리오 프레임은 서 있었다(프레임 시한은 닿지 못했다)
    const sw::string json = Internal::readReport( reportPath );
    SW_EXPECT_TRUE_MSG( json.find( "\"exitCode\": 12" ) != sw::string::npos, json.c_str() );
    SW_EXPECT_TRUE_MSG( json.find( "\"wallLimitMilliseconds\": 50," ) != sw::string::npos, json.c_str() );
    input.shutdown();
}

/**
 * @brief [AutomationRunnerWatchTest] `wallTimeoutSeconds` 는 양수만 받고, 적지 않은 시나리오는 기본 상한을 받는다
 */
SW_TEST_CASE( AutomationRunnerWatchTest, WallTimeoutAttributeMustBePositive )
{
    sw::AutomationScenario scenario;
    sw::string             error;
    SW_ASSERT_TRUE_MSG( scenario.parse( R"(<Scenario name="t"><At frame="0"><Pass/></At></Scenario>)", "", error ), error.c_str() );
    SW_EXPECT_EQUAL( sw::AutomationScenario::kDefaultWallTimeoutSeconds, scenario.getWallTimeoutSeconds() );
    SW_ASSERT_TRUE_MSG( scenario.parse( R"(<Scenario name="t" wallTimeoutSeconds="12.5"><At frame="0"><Pass/></At></Scenario>)", "", error ), error.c_str() );
    SW_EXPECT_EQUAL( 12.5f, scenario.getWallTimeoutSeconds() );
    SW_ASSERT_TRUE_MSG( scenario.parse( R"(<Scenario name="t" wallTimeoutSeconds="169"><At frame="0"><Pass/></At></Scenario>)", "", error ), error.c_str() ); // 시계 보류(170) 바로 아래

    SW_TEST_DEFENSIVE_SCOPE( "a limit that is not positive would switch the watch off, and one at the clock-hold timeout or above outlasts the process kill" );
    for ( const utf8* pBadValue : { "0", "-3", "soon", "170", "400" } )
    {
        const sw::string       text = sw::string( R"(<Scenario name="t" wallTimeoutSeconds=")" ) + pBadValue + R"("><At frame="0"><Pass/></At></Scenario>)";
        sw::AutomationScenario bad;
        SW_EXPECT_FALSE( bad.parse( text, "", error ) );
        SW_EXPECT_TRUE_MSG( error.find( "wallTimeoutSeconds" ) != sw::string::npos, error.c_str() );
    }
}

/**
 * @brief [AutomationRunnerWatchTest] `startAfter="Immediately"` 도 시작 차단 조건이 풀린 뒤에 시작한다 — 씬이 플레이 중일 필요만 없다
 * @details 변이: `Immediately` 가 차단을 다시 무시하면 첫 프레임에 시작해 둘째 프레임에 통과한다. 끝내 풀리지 않으면 시작 시한 뒤 시간 초과로 끝나고 이유에 차단이 든다.
 */
SW_TEST_CASE( AutomationRunnerWatchTest, ImmediatelyWaitsForStartBlockers )
{
    using Internal = TestAutomationRunnerWatchInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    {
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" startTimeoutFrames="100" timeoutFrames="20">
        <At frame="1"><Pass/></At>
      </Scenario>)" ) );
        Internal::s_bStartBlocked           = true;
        sw::AutomationOutcome result        = sw::AutomationOutcome::Running;
        uint32                passedAtFrame = 0; // 통과한 하네스 프레임(1 부터)
        for ( uint32 frame = 1; frame <= 40 && result == sw::AutomationOutcome::Running; ++frame )
        {
            if ( frame == 6 )
                Internal::s_bStartBlocked = false; // 여섯째 프레임부터 풀린다
            result        = Internal::stepFrame( runner, input );
            passedAtFrame = frame;
        }
        Internal::s_bStartBlocked = false;
        SW_EXPECT_TRUE_MSG( result == sw::AutomationOutcome::Passed, runner.getFinishReason().c_str() );
        SW_EXPECT_EQUAL( 7u, passedAtFrame ); // 여섯째 프레임에 시작하고 `frame="1"` 의 Pass 는 다음 프레임이다. 차단을 무시하면 둘째 프레임에 끝난다
    }
    {
        SW_TEST_DEFENSIVE_SCOPE( "a start blocker that never clears ends the scenario with a timeout" );
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" startTimeoutFrames="3"><At frame="0"><Pass/></At></Scenario>)" ) );
        Internal::s_bStartBlocked    = true;
        sw::AutomationOutcome result = sw::AutomationOutcome::Running;
        for ( uint32 frame = 0; frame < 40 && result == sw::AutomationOutcome::Running; ++frame )
        {
            result = Internal::stepFrame( runner, input );
        }
        Internal::s_bStartBlocked = false;
        SW_EXPECT_TRUE( result == sw::AutomationOutcome::TimedOut );
        SW_EXPECT_TRUE_MSG( runner.getFinishReason().find( "start blocker" ) != sw::string::npos, runner.getFinishReason().c_str() );
    }
    input.shutdown();
}

/**
 * @brief [AutomationRunnerWatchTest] 진 실행도 로그 게이트를 지나 처리하지 않은 Warning 줄을 보고의 `unhandledLogs` 에 싣는다 — 결과(10)는 그대로다
 * @details 변이: 게이트를 `Passed` 에서만 돌리면 `getUnhandledLogs` 가 비고 보고의 `unhandledLogs` 가 `[]` 다.
 */
SW_TEST_CASE( AutomationRunnerWatchTest, FailedRunStillReportsUnhandledLogs )
{
    using Internal = TestAutomationRunnerWatchInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    const sw::string     reportPath = Internal::makeReportPath( "failed" );
    sw::AutomationRunner runner;
    SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" timeoutFrames="20"><At frame="3"><Fail reason="expected by the test"/></At></Scenario>)", reportPath ) );
    sw::AutomationOutcome result = sw::AutomationOutcome::Running;
    for ( uint32 frame = 0; frame < 2 && result == sw::AutomationOutcome::Running; ++frame )
    {
        runner.onFrameBegin( input );
        input.beginFrame( 1.0f / 60.0f );
        if ( frame == 1 )
            SW_LOG_WARNING( "[AutoProbe] undeclared in a failed run" ); // 방어 구간 밖 — 게이트가 센다. id 가 없어 허용 목록으로 가릴 수 없다
        result = runner.onFrameEnd( input );
        input.endFrame();
    }
    {
        SW_TEST_DEFENSIVE_SCOPE( "a failed scenario logs an error line" );
        for ( uint32 frame = 0; frame < 20 && result == sw::AutomationOutcome::Running; ++frame )
        {
            result = Internal::stepFrame( runner, input );
        }
    }
    SW_EXPECT_TRUE_MSG( result == sw::AutomationOutcome::Failed, runner.getFinishReason().c_str() ); // 진 결과를 UnhandledLog 가 덮지 않는다
    SW_EXPECT_EQUAL( 10, sw::getAutomationExitCode( result ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( runner.getUnhandledLogs().size() ) );
    const sw::string json = Internal::readReport( reportPath );
    SW_EXPECT_TRUE_MSG( json.find( "\"exitCode\": 10" ) != sw::string::npos, json.c_str() );
    SW_EXPECT_TRUE_MSG( json.find( "undeclared in a failed run" ) != sw::string::npos, json.c_str() );
    input.shutdown();
}

/**
 * @brief [AutomationRunnerWatchTest] 실행기는 시나리오 · 단계 · 프레임을 크래시 컨텍스트(`Scenario` 키)에 싣고, 단계 전환을 빵부스러기에 남기며, 끝나면 끝났다고 고친다
 * @details 변이: 갱신을 빼면 `Scenario` 키가 없거나 앞 테스트의 값이 남는다.
 */
SW_TEST_CASE( AutomationRunnerWatchTest, CrashContextCarriesTheScenarioPosition )
{
    using Internal = TestAutomationRunnerWatchInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::AutomationRunner runner;
    SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="crashwatch" startAfter="Immediately" timeoutFrames="20"><At frame="3"><Pass/></At></Scenario>)" ) );
    sw::AutomationOutcome result = sw::AutomationOutcome::Running;
    for ( uint32 frame = 0; frame < 20 && result == sw::AutomationOutcome::Running; ++frame )
    {
        result = Internal::stepFrame( runner, input );
    }
    SW_EXPECT_TRUE_MSG( result == sw::AutomationOutcome::Passed, runner.getFinishReason().c_str() );
    const sw::CrashContextStore& store = sw::CrashContextStore::get();
    sw::string                   value;
    for ( uint32 entryIndex = 0; entryIndex < store._entryCount; ++entryIndex )
    {
        if ( store._arrEntry[entryIndex]._key.view() == "Scenario" )
            value = sw::string( store._arrEntry[entryIndex]._value.view() );
    }
    SW_EXPECT_TRUE_MSG( value.find( "crashwatch" ) != sw::string::npos, value.c_str() );
    SW_EXPECT_TRUE_MSG( value.find( "ended PASS" ) != sw::string::npos, value.c_str() );
    utf8 arrBreadcrumb[sw::CrashBreadcrumbStore::kMaxBreadcrumb * ( sw::CrashBreadcrumbStore::kMaxLength + 1 )]{};
    (void)sw::CrashBreadcrumbStore::get().writeText( arrBreadcrumb, static_cast<uint32>( sizeof( arrBreadcrumb ) ) );
    SW_EXPECT_TRUE_MSG( sw::string_view{ arrBreadcrumb }.find( "[Scenario] crashwatch step" ) != sw::string_view::npos, arrBreadcrumb );
    input.shutdown();
}

/**
 * @brief [AutomationRunnerWatchTest] 보고는 게임 · 시작 시각 · 벽시계 시간 · 마지막 자리 · 재현 명령을 담고, 기존 필드 이름은 그대로다
 */
SW_TEST_CASE( AutomationRunnerWatchTest, ReportCarriesTheRunEnvironmentAndReproduceCommand )
{
    using Internal = TestAutomationRunnerWatchInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    const sw::string reportPath = Internal::makeReportPath( "environment" );

    sw::AutomationReproduceInput reproduce;
    reproduce._game             = "Shooter3D";
    reproduce._backendName      = "Vulkan";
    reproduce._scenarioArgument = "engine/automation/a.scenario.xml,b.scenario.xml";
    reproduce._bEditor          = true;
    reproduce._bKeepGoing       = true;
    sw::AutomationRunner runner;
    runner.setReproduceInput( reproduce );
    SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" timeoutFrames="20"><At frame="1"><Pass/></At></Scenario>)", reportPath ) );
    sw::AutomationOutcome result = sw::AutomationOutcome::Running;
    for ( uint32 frame = 0; frame < 20 && result == sw::AutomationOutcome::Running; ++frame )
    {
        result = Internal::stepFrame( runner, input );
    }
    SW_EXPECT_TRUE_MSG( result == sw::AutomationOutcome::Passed, runner.getFinishReason().c_str() );
    const sw::string json = Internal::readReport( reportPath );
    for ( const utf8* pExpected : { "\"name\": \"t\"",
                                    "\"result\": \"PASS\"",
                                    "\"exitCode\": 0",
                                    "\"frames\":",
                                    "\"reason\":",
                                    "\"failures\":",
                                    "\"unhandledLogs\":",
                                    "\"allowedLogs\":",
                                    "\"records\":",
                                    "\"timeline\": {",
                                    "\"scheduledWaitFrames\":",
                                    "\"heldMilliseconds\":",
                                    "\"slowestWaits\": [",
                                    "\"game\": \"Shooter3D\"",
                                    "\"wallLimitMilliseconds\": 150000,",
                                    "\"lastStep\": \"t ",
                                    "-gv_rhiBackend=Vulkan",
                                    "-EnableEditor",
                                    "\\\"-scenario=engine/automation/a.scenario.xml,b.scenario.xml\\\"",
                                    "-scenario-keep-going" } )
    {
        SW_EXPECT_TRUE_MSG( json.find( pExpected ) != sw::string::npos, ( sw::string( "missing " ) + pExpected + " in " + json ).c_str() );
    }
    SW_EXPECT_TRUE_MSG( json.find( "\"startedAtUnixMs\": 0," ) == sw::string::npos, json.c_str() );  // 시작 시각이 적혔다
    SW_EXPECT_TRUE_MSG( json.rfind( "\"records\"" ) > json.rfind( "\"reproduce\"" ), json.c_str() ); // `records` 는 끝에 둔다(`readReportRecords` 가 거기서부터 읽는다)
    input.shutdown();
}
