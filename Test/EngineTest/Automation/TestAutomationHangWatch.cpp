#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Automation/AutomationExitCode.h"
#include "Engine/Automation/AutomationHangWatch.h"
#include "Engine/Automation/AutomationRunTitle.h"
#include "Engine/Automation/AutomationTimeLimits.h"

#include "TestFramework/TestFramework.h"

// 행 감시(프레임이 멈춘 실행) — 스냅샷 읽기 · 시한 판정 · 보고 글은 순수 함수로, 재현 명령의 인자 규칙은 글자로만 시험한다. 스레드가 실제로 프로세스를 끝내는 시험은 통합 몫이다.

/**
 * @brief [AutomationHangWatchTest] 스냅샷은 한 번도 쓰지 않았으면 읽히지 않고, 쓴 뒤에는 마지막 값 그대로 읽히며, 긴 글은 UTF-8 글자 경계에서 잘린다
 */
SW_TEST_CASE( AutomationHangWatchTest, SnapshotReturnsTheLastPublishedPayload )
{
    sw::AutomationHangSnapshot snapshot;
    sw::AutomationHangPayload  read;
    SW_EXPECT_FALSE( snapshot.tryRead( read ) );

    sw::AutomationHangPayload payload;
    sw::AutomationHangPayload::assign( payload._scenario, sw::AutomationHangPayload::kScenarioBytes, "editor.flow" );
    sw::AutomationHangPayload::assign( payload._lastStep, sw::AutomationHangPayload::kLastStepBytes, "editor.flow \xC2\xB7 step 3 Expect \xC2\xB7 f40" );
    payload._frame = 40;
    snapshot.publish( payload );
    payload._frame = 41;
    snapshot.publish( payload );
    SW_ASSERT_TRUE( snapshot.tryRead( read ) );
    SW_EXPECT_EQUAL( 41u, read._frame );
    SW_EXPECT_STREQ( "editor.flow", read._scenario );
    SW_EXPECT_STREQ( "editor.flow \xC2\xB7 step 3 Expect \xC2\xB7 f40", read._lastStep );

    // 4 바이트 칸에 "가나"(3 바이트씩)를 넣으면 첫 글자만 남고 늘 0 으로 끝난다
    utf8 arrSmall[4]{};
    sw::AutomationHangPayload::assign( arrSmall, 4, "\xEA\xB0\x80\xEB\x82\x98" );
    SW_EXPECT_STREQ( "\xEA\xB0\x80", arrSmall );
}

/**
 * @brief [AutomationHangWatchTest] 행 시한은 상한과 유예를 모두 넘겨야 선다
 */
SW_TEST_CASE( AutomationHangWatchTest, DeadlineNeedsLimitPlusGrace )
{
    SW_EXPECT_FALSE( sw::AutomationHangReport::isDeadlineExceeded( 150.0, 150.0f, 5.0f ) );
    SW_EXPECT_FALSE( sw::AutomationHangReport::isDeadlineExceeded( 154.9, 150.0f, 5.0f ) );
    SW_EXPECT_TRUE( sw::AutomationHangReport::isDeadlineExceeded( 155.1, 150.0f, 5.0f ) );
}

/**
 * @brief [AutomationHangWatchTest] 행 보고는 실행기 보고와 같은 필드 이름에 종료 코드 12 를 담고 이스케이프하며, `records` 로 끝나고, 작은 버퍼에서도 0 으로 끝난다
 */
SW_TEST_CASE( AutomationHangWatchTest, HangReportHasTheRunnerFieldsAndExitCode )
{
    sw::AutomationHangPayload payload;
    sw::AutomationHangPayload::assign( payload._scenario, sw::AutomationHangPayload::kScenarioBytes, "t" );
    sw::AutomationHangPayload::assign( payload._lastStep, sw::AutomationHangPayload::kLastStepBytes, "t \xC2\xB7 step 2 WaitUntil \xC2\xB7 f9" );
    sw::AutomationHangPayload::assign( payload._reproduce, sw::AutomationHangPayload::kReproduceBytes, "App.exe \"-scenario=a.scenario.xml\"" );
    payload._wallLimitMilliseconds = 150000;
    payload._startedUnixMs         = 1700000000000;
    utf8             arrText[4096]{};
    const uint32     length = sw::AutomationHangReport::format( payload, 156000, arrText, 4096 );
    const sw::string json( arrText, length );
    SW_EXPECT_EQUAL( static_cast<uint32>( json.size() ), length );
    for ( const utf8* pExpected : { "\"name\": \"t\"", "\"result\": \"TIMEOUT\"", "\"exitCode\": 12,", "\"frames\": 0,", "\"hang\": true", "\"wallMilliseconds\": 156000,",
                                    "\"wallLimitMilliseconds\": 150000,", "\"startedAtUnixMs\": 1700000000000,", "\"lastStep\": \"t \xC2\xB7 step 2 WaitUntil \xC2\xB7 f9\"",
                                    "\"reproduce\": \"App.exe \\\"-scenario=a.scenario.xml\\\"\"", "\"unhandledLogs\": []", "\"allowedLogs\": []" } )
    {
        SW_EXPECT_TRUE_MSG( json.find( pExpected ) != sw::string::npos, ( sw::string( "missing " ) + pExpected + " in " + json ).c_str() );
    }
    SW_EXPECT_EQUAL( sw::getAutomationExitCode( sw::AutomationOutcome::TimedOut ), 12 );
    SW_EXPECT_TRUE( json.rfind( "\"records\"" ) > json.rfind( "\"reproduce\"" ) ); // `readReportRecords` 가 `"records"` 부터 읽는다

    utf8         arrSmall[64]{};
    const uint32 smallLength = sw::AutomationHangReport::format( payload, 156000, arrSmall, 64 );
    SW_EXPECT_TRUE( smallLength < 64 );
    SW_EXPECT_EQUAL( '\0', arrSmall[smallLength] );
}

/**
 * @brief [AutomationHangWatchTest] 시한 전에 내린 감시는 아무것도 하지 않고 돌아오며(행으로 처리하지 않음), 돌지 않은 감시를 내려도 안전하다
 */
SW_TEST_CASE( AutomationHangWatchTest, StoppedWatchdogDoesNotFire )
{
    sw::AutomationHangWatchdog idle;
    SW_EXPECT_FALSE( idle.stop() );

    sw::AutomationHangWatchdog watchdog;
    sw::AutomationHangPayload  payload;
    watchdog.publish( payload );
    sw::AutomationHangWatchdog::Settings settings;
    settings._startSeconds = static_cast<float64>( sw::MonotonicClock::nowMicroseconds() ) * 1.0e-6;
    settings._limitSeconds = 600.0f;
    settings._graceSeconds = 5.0f;
    settings._exitCode     = 12;
    watchdog.start( settings );
    SW_EXPECT_TRUE( watchdog.isRunning() );
    SW_EXPECT_FALSE( watchdog.stop() );
    SW_EXPECT_FALSE( watchdog.isRunning() );
}

/**
 * @brief [AutomationHangWatchTest] 행 감시의 시한 대소 관계는 감시 + 유예 < 시계 보류 < 프로세스 시한이다(헤더의 `static_assert` 와 같은 값을 런타임에서도 잠근다)
 */
SW_TEST_CASE( AutomationHangWatchTest, TimeLimitsAreOrdered )
{
    using Limits = sw::AutomationTimeLimits;
    SW_EXPECT_TRUE( static_cast<float64>( Limits::kDefaultWallTimeoutSeconds ) + static_cast<float64>( Limits::kHangGraceSeconds ) < Limits::kClockHoldTimeoutSeconds );
    SW_EXPECT_TRUE( Limits::kClockHoldTimeoutSeconds < static_cast<float64>( Limits::kProcessKillSeconds ) );
}

/**
 * @brief [AutomationHangWatchTest] 명령줄 관리자는 파싱한 원본 인자를 적은 순서 그대로 보관한다(프로그램 이름 제외)
 */
SW_TEST_CASE( AutomationHangWatchTest, CommandLineKeepsTheOriginalArguments )
{
    utf8                   arrProgram[]  = "App.exe";
    utf8                   arrGame[]     = "-game=Shooter3D";
    utf8                   arrScenario[] = "-scenario=a.scenario.xml";
    utf8                   arrBench[]    = "-gv_benchMeshes=8";
    utf8*                  arrArgv[]     = { arrProgram, arrGame, arrScenario, arrBench };
    sw::CommandLineManager commandLine;
    commandLine.initialize();
    commandLine.parse( 4, arrArgv );
    const sw::vector<sw::string>& listOriginal = commandLine.getOriginalArguments();
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listOriginal.size() ) );
    SW_EXPECT_STREQ( "-game=Shooter3D", listOriginal[0].c_str() );
    SW_EXPECT_STREQ( "-scenario=a.scenario.xml", listOriginal[1].c_str() );
    SW_EXPECT_STREQ( "-gv_benchMeshes=8", listOriginal[2].c_str() );
}

/**
 * @brief [AutomationHangWatchTest] 재현 명령은 원본 인자를 그대로 옮기되 보고 · 녹화 경로는 빼고 비밀 이름의 값은 가리며, 원본에 없는 것만 실행기가 채운다
 * @details 변이: 가리는 규칙을 빼면 토큰 값이 그대로 보고에 남는다. 원본에 이미 있는 게임 · 백엔드 · 시나리오를 다시 더하면 같은 인자가 두 번 적힌다.
 */
SW_TEST_CASE( AutomationHangWatchTest, ReproduceCommandCopiesOriginalArgumentsWithMasking )
{
    sw::AutomationReproduceInput input;
    for ( const utf8* pArgument : { "-dx12", "-game=Shooter3D", "-scenario=engine/automation/a.scenario.xml", "-scenario-report=Saved/Automation/a.json", "-serverToken=abc123",
                                    "-apiKey=zzz", "--password=hunter2", "-gv_benchMeshes=8", "-scenario-record=Saved/x.scenario.xml", "-crash-reporter=https://u:p@host" } )
    {
        input._listOriginalArgument.push_back( pArgument );
    }
    input._game              = "Other";     // 원본에 -game 이 있으니 쓰이지 않는다
    input._backendName       = "DirectX12"; // 원본에 -dx12 가 있으니 쓰이지 않는다
    input._bKeepGoing        = true;        // 원본에 없으니 채운다
    const sw::string command = sw::AutomationRunTitleUtil::makeReproduceCommand( input );
    SW_EXPECT_STREQ( "App.exe -dx12 -game=Shooter3D \"-scenario=engine/automation/a.scenario.xml\" -serverToken=REDACTED -apiKey=REDACTED --password=REDACTED "
                     "-gv_benchMeshes=8 -scenario-keep-going",
                     command.c_str() );
    SW_EXPECT_TRUE( command.find( "abc123" ) == sw::string::npos );
    SW_EXPECT_TRUE( command.find( "hunter2" ) == sw::string::npos );
    SW_EXPECT_TRUE( command.find( "scenario-report" ) == sw::string::npos );
}
