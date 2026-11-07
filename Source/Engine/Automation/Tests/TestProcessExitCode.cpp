// ProcessExitCodeTest — 종료 요청은 표(ProcessExitCodes.json)의 열거형만 받고, 표는 초기화 실패 · 사진 쓰기 실패를 크래시와 가른다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Automation/Runner/AutomationExitCode.h"
#include "Engine/Foundation/QuitRequest.h"

#include "sw/config/ProcessExitCodes.gen.h"

using namespace sw;

/** @brief [ProcessExitCodeTest] 초기화 실패 · 사진 쓰기 실패는 표에 이름이 있어 자동화가 "crashed" 로 세지 않는다. 표에 없는 -1 은 이름이 없다 */
SW_TEST_CASE( ProcessExitCodeTest, InitializationAndCaptureFailuresHaveNames )
{
    const char* const pInit    = findProcessExitCodeLabel( static_cast<int>( ProcessExitCode::InitializationFailed ) );
    const char* const pCapture = findProcessExitCodeLabel( static_cast<int>( ProcessExitCode::CaptureWriteFailed ) );
    SW_REQUIRE_NOT_NULL( pInit );
    SW_REQUIRE_NOT_NULL( pCapture );
    SW_EXPECT_EQUAL( string( "initializationFailed" ), string( pInit ) );
    SW_EXPECT_EQUAL( string( "captureWriteFailed" ), string( pCapture ) );
    SW_EXPECT_TRUE( findProcessExitCodeLabel( -1 ) == nullptr );
}

/** @brief [ProcessExitCodeTest] 종료 요청은 처음 것의 코드를 둔다. 도는 시나리오는 종료 코드가 없고(빈 값), 끝난 결과는 표의 코드다 */
SW_TEST_CASE( ProcessExitCodeTest, QuitRequestKeepsTheFirstTableCode )
{
    QuitRequest quit;
    SW_EXPECT_FALSE( quit.isRequested() );
    SW_EXPECT_TRUE( quit.getExitCode() == ProcessExitCode::Passed );
    quit.request( ProcessExitCode::Failed );
    quit.request( ProcessExitCode::Passed );
    SW_EXPECT_TRUE( quit.isRequested() );
    SW_EXPECT_TRUE( quit.getExitCode() == ProcessExitCode::Failed );

    SW_EXPECT_FALSE( getAutomationExitCode( AutomationResult::Running ).has_value() );
    SW_EXPECT_TRUE( getAutomationExitCode( AutomationResult::TimedOut ) == ProcessExitCode::TimedOut );
    SW_EXPECT_EQUAL( static_cast<int32>( ProcessExitCode::Skipped ), getAutomationExitStatus( AutomationResult::Skipped ) );
}
