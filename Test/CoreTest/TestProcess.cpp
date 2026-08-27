#include "pch.h"

#include "Core/Process/Process.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_Process — 프로세스 생성, 라인 읽기, 실행 헬퍼, 강제 종료
// ------------------------------------------------------------------------------
/**
 * @brief [Core_Process] 프로세스 생성 및 표준 출력 라인 읽기
 */

SW_TEST_CASE( Core_Process, LaunchAndReadOutput )
{
	sw::Process proc;
#if defined( SW_PLATFORM_WINDOWS )
	const sw::string cmd = "cmd.exe /c echo SW_PROCESS_TEST_OUTPUT";
#else
	const sw::string cmd = "echo SW_PROCESS_TEST_OUTPUT";
#endif

	const bool bLaunched = proc.launch( cmd );
	SW_EXPECT_TRUE( bLaunched );

	sw::string line;
	bool	   bFound = false;
	while ( proc.readOutputLine( line ) )
	{
		if ( line.find( "SW_PROCESS_TEST_OUTPUT" ) != sw::string::npos )
		{
			bFound = true;
		}
	}

	const int32 exitCode = proc.waitForExit();
	SW_EXPECT_TRUE( bFound );
	SW_EXPECT_EQUAL( 0, exitCode );
}

/**
 * @brief [Core_Process] 정적 execute 헬퍼를 통한 출력 콜백 수신
 */
SW_TEST_CASE( Core_Process, ExecuteHelper )
{
#if defined( SW_PLATFORM_WINDOWS )
	const sw::string cmd = "cmd.exe /c echo LINE1 && echo LINE2";
#else
	const sw::string cmd = "echo LINE1 && echo LINE2";
#endif

	sw::vector<sw::string> listLines;
	const int32			   exitCode = sw::Process::execute(
		   cmd,
		   {},
		   sw::ProcessOutputDelegate::create( [&listLines]( string_view line, bool bIsStdErr )
	   {
		   (void)bIsStdErr;
		   listLines.push_back( sw::string( line ) );
	   } ) );

	SW_EXPECT_EQUAL( 0, exitCode );
	SW_EXPECT_TRUE( listLines.size() >= 2 );
}

/**
 * @brief [Core_Process] 실행 중인 프로세스 강제 종료
 */
SW_TEST_CASE( Core_Process, TerminateProcess )
{
	sw::Process proc;
#if defined( SW_PLATFORM_WINDOWS )
	const sw::string cmd = "ping.exe 127.0.0.1 -n 10";
#else
	const sw::string cmd = "sleep 10";
#endif

	const bool bLaunched = proc.launch( cmd );
	SW_EXPECT_TRUE( bLaunched );
	SW_EXPECT_TRUE( proc.isRunning() );

	const bool bTerminated = proc.terminate( 99 );
	SW_EXPECT_TRUE( bTerminated );
	SW_EXPECT_FALSE( proc.isRunning() );
}
