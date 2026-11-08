// ShutdownSignal — 처음 요청만 남고, OS 종료 신호는 프로세스를 죽이지 않고 깃발만 세우며, 처리기가 할 일은 순수 결정(decideConsoleControl · decideSignal)이 정한다.
// 프로세스에 하나인 깃발을 건드리는 케이스는 격리(자식 프로세스)로 돈다 — 상태를 되돌리는 창구를 배포 코드에 두지 않는다.
#include "pch.h"

#include "Core/Process/ShutdownSignal.h"
#include "Core/Testing/TestFramework.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Foundation/PlatformOSHeaders.h"
#elif defined( SW_PLATFORM_POSIX )
    #include <csignal>
#else
    #error "Unsupported platform"
#endif

using namespace sw;

SW_TEST_CASE( ShutdownSignalTest, FirstRequestWins, ._timeoutSeconds = 60.0f, ._bIsolated = true )
{
    SW_EXPECT_FALSE( ShutdownSignal::isRequested() );
    ShutdownSignal::request( ShutdownCause::Terminate );
    ShutdownSignal::request( ShutdownCause::ConsoleCommand );
    SW_EXPECT_TRUE( ShutdownSignal::getRequestedCause() == ShutdownCause::Terminate );
    SW_EXPECT_EQUAL( string( "Terminate" ), string( ShutdownSignal::getCauseName( ShutdownSignal::getRequestedCause() ) ) );
}

#if defined( SW_PLATFORM_WINDOWS )
/**
 * @brief Ctrl+C · Ctrl+Break 는 깃발만 세우고 두 번째는 기본 처리기(즉시 종료)로 넘긴다. 창 닫기 · 시스템 종료는 정리 완료를 기다린다.
 * @details 콘솔 그룹 전체(CTest 포함)로 가는 `GenerateConsoleCtrlEvent` 대신 처리기가 따르는 결정을 본다.
 */
SW_TEST_CASE( ShutdownSignalTest, ConsoleControlDecisions )
{
    const ShutdownSignalAction first = ShutdownSignal::decideConsoleControl( CTRL_BREAK_EVENT, 0, false );
    SW_EXPECT_TRUE( first._cause == ShutdownCause::Interrupt );
    SW_EXPECT_FALSE( first._bPassToDefault );
    SW_EXPECT_TRUE( ShutdownSignal::decideConsoleControl( CTRL_C_EVENT, 1, false )._bPassToDefault );

    const ShutdownSignalAction close = ShutdownSignal::decideConsoleControl( CTRL_CLOSE_EVENT, 0, false );
    SW_EXPECT_TRUE( close._cause == ShutdownCause::ConsoleClose );
    SW_EXPECT_TRUE( close._bWaitForCompletion );
    SW_EXPECT_TRUE( ShutdownSignal::decideConsoleControl( CTRL_SHUTDOWN_EVENT, 0, false )._cause == ShutdownCause::SystemShutdown );

    SW_EXPECT_TRUE( ShutdownSignal::decideConsoleControl( CTRL_LOGOFF_EVENT, 0, false )._cause == ShutdownCause::SystemShutdown );
    const ShutdownSignalAction serviceLogoff = ShutdownSignal::decideConsoleControl( CTRL_LOGOFF_EVENT, 0, true );
    SW_EXPECT_TRUE( serviceLogoff._cause == ShutdownCause::None );
    SW_EXPECT_TRUE( serviceLogoff._bPassToDefault );
}
#elif defined( SW_PLATFORM_POSIX )
/** @brief 실제 SIGTERM 을 이 프로세스에 보낸다 — 처리기가 없으면 프로세스가 죽는다. 깃발을 건드리므로 격리로 돈다. */
SW_TEST_CASE( ShutdownSignalTest, TerminateSignalSetsTheFlag, ._timeoutSeconds = 60.0f, ._bIsolated = true )
{
    SW_REQUIRE_TRUE( ShutdownSignal::install() );
    SW_EXPECT_EQUAL( 0, std::raise( SIGTERM ) );
    SW_EXPECT_TRUE( ShutdownSignal::getRequestedCause() == ShutdownCause::Terminate );
    ShutdownSignal::uninstall();
}

/** @brief 두 번째 SIGINT 는 기다리지 않고 끝낸다. SIGHUP 은 Hangup 이다. */
SW_TEST_CASE( ShutdownSignalTest, SignalDecisions )
{
    SW_EXPECT_TRUE( ShutdownSignal::decideSignal( SIGINT, 0 )._cause == ShutdownCause::Interrupt );
    SW_EXPECT_TRUE( ShutdownSignal::decideSignal( SIGINT, 1 )._bPassToDefault );
    SW_EXPECT_TRUE( ShutdownSignal::decideSignal( SIGHUP, 0 )._cause == ShutdownCause::Hangup );
}
#else
    #error "Unsupported platform"
#endif
