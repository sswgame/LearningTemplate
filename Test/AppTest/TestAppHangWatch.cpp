#include "pch.h"

#include "AppTest/AppTestUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"

#include "Engine/Automation/AutomationExitCode.h"
#include "Engine/Automation/AutomationOutcome.h"
#include "Engine/Graphics/RHI/RHIInitResult.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// 실제 App.exe 를 띄워 게임 스레드를 멈춘다 — GPU · 창 · 셰이더가 필요하다.
SW_TEST_REQUIRES_HOST( AppHangWatchTest, "launches the real App.exe and freezes its game thread, which needs a GPU, a window and cooked shaders" );

// ------------------------------------------------------------------------------
// AppHangWatchTest — 행 감시(`AutomationHangWatchdog`)의 통합 시험
//
// 감시 스레드가 프로세스를 `_Exit` 로 끝내므로 같은 프로세스에서는 시험할 수 없다(`AutomationHangWatchTest` 가 순수 부분만 본다).
// 시나리오는 `AppScenarioTest` 가 훑는 `engine/automation` 밖에 둔다 — 일부러 12 로 끝나기 때문이다.
// ------------------------------------------------------------------------------

/**
 * @brief [AppHangWatchTest] 게임 스레드가 상한(3 초) + 유예(5 초)를 넘겨 멈추면 감시가 보고(`"hang": true`)를 쓰고 종료 코드 12 로 프로세스를 끝낸다
 * @details 시나리오 `engine/automationwatch/hang.scenario.xml` 이 개발 명령 `automation.hang 30` 으로 프레임 0 에서 게임 스레드를 붙잡는다. 감시가 없으면 30 초 뒤에야 풀려
 *          시나리오가 통과(0)로 끝나거나 프로세스 시한(180 초)까지 간다. 변이: `startHangWatchdog` 를 부르지 않으면 종료 코드 0 이거나 시한 초과가 되어 진다.
 */
SW_TEST_CASE( AppHangWatchTest, FrozenGameThreadEndsWithExit12AndAHangReport )
{
    SW_TEST_REQUIRES_FEATURE( DevelopmentBuild ); // `automation.hang` 은 개발 명령이라 배포본에 없다
    const string reportPath = test::makeTempPath( "hangwatch.json" );
    (void)FileUtil::tryRemoveFile( reportPath ); // 지난 실행이 없으면 지울 것이 없다 — 없는 것이 정상이다
    const string arguments = test::AppTestUtil::makeValueArgument( CommandLineArgument::SCENARIO, "engine/automationwatch/hang.scenario.xml" ) + " " +
                             test::AppTestUtil::makeValueArgument( CommandLineArgument::SCENARIO_REPORT, reportPath );

    const test::ProcessRunResult run = test::AppTestUtil::runApp( arguments, test::AppTestUtil::kScenarioTimeoutSeconds, []( Process&, const string& ) {} );
    if ( run._bLaunched == false || ( run._bTimedOut == false && run._exitCode == kRHIUnusableHereExitCode ) )
        SW_TEST_SKIP( "App could not be launched or the default backend is not usable on this machine" );

    SW_EXPECT_TRUE_MSG( run._bTimedOut == false, "the hang watchdog did not end the frozen process - the test harness had to kill it" );
    SW_EXPECT_EQUAL( getAutomationExitCode( AutomationOutcome::TimedOut ), run._exitCode );
    const string hang        = test::AppTestUtil::readReportScalar( reportPath, "hang" );
    const string hangMessage = "the report has no \"hang\": true (the scenario may have timed out before it started - raise wallTimeoutSeconds if boot takes over 3 s): " +
                               test::AppTestUtil::readReportScalar( reportPath, "reason" );
    SW_EXPECT_TRUE_MSG( hang == "true", hangMessage.c_str() );
    const string expectedExitCode = to_string( getAutomationExitCode( AutomationOutcome::TimedOut ) );
    const string reportExitCode   = test::AppTestUtil::readReportScalar( reportPath, "exitCode" );
    SW_EXPECT_STREQ( expectedExitCode.c_str(), reportExitCode.c_str() );
}
