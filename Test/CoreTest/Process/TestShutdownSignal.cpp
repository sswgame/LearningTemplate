#include "pch.h"

#include "Core/Process/ShutdownSignal.h"

#include "TestFramework/TestFramework.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"
#else
    #include <csignal>
#endif

#include <chrono>
#include <thread>

using namespace sw;

/**
 * @brief [ShutdownSignalTest] 처음 요청만 남는다 — 신호와 콘솔 명령이 겹쳐도 까닭이 덮이지 않는다
 */
SW_TEST_CASE( ShutdownSignalTest, FirstRequestWins )
{
    ShutdownSignal::resetForTest();
    SW_EXPECT_FALSE( ShutdownSignal::isRequested() );
    ShutdownSignal::request( ShutdownCause::Terminate );
    ShutdownSignal::request( ShutdownCause::ConsoleCommand );
    SW_EXPECT_TRUE( ShutdownSignal::getRequestedCause() == ShutdownCause::Terminate );
    SW_EXPECT_STREQ( "Terminate", ShutdownSignal::getCauseName( ShutdownSignal::getRequestedCause() ) );
    ShutdownSignal::resetForTest();
}

/**
 * @brief [ShutdownSignalTest] OS 가 보낸 종료 신호(POSIX SIGTERM · Windows Ctrl+Break)가 프로세스를 죽이지 않고 깃발만 세운다
 * @details POSIX 는 이 프로세스에 진짜 SIGTERM 을 `raise` 한다 — 처리기를 설치하지 않으면 시험 프로세스가 죽어 이 시험이 진다.
 *          Windows 는 콘솔 그룹 전체(CTest 포함)로 가는 `GenerateConsoleCtrlEvent` 대신 처리기 본문을 직접 부른다.
 */
SW_TEST_CASE( ShutdownSignalTest, TerminateSignalSetsTheFlag )
{
    ShutdownSignal::resetForTest();
    SW_ASSERT_TRUE( ShutdownSignal::install() );
#if defined( SW_PLATFORM_WINDOWS )
    SW_EXPECT_TRUE( ShutdownSignal::dispatchConsoleControlForTest( CTRL_BREAK_EVENT ) );
    SW_EXPECT_TRUE( ShutdownSignal::getRequestedCause() == ShutdownCause::Interrupt );
#else
    SW_ASSERT_EQUAL( 0, std::raise( SIGTERM ) );
    SW_EXPECT_TRUE( ShutdownSignal::getRequestedCause() == ShutdownCause::Terminate );
#endif
    ShutdownSignal::uninstall();
    ShutdownSignal::resetForTest();
}

#if defined( SW_PLATFORM_WINDOWS )
/**
 * @brief [ShutdownSignalTest] 콘솔 창 닫기는 정리가 끝났다는 알림을 기다린 뒤 돌아간다 — 돌아가는 순간 OS 가 프로세스를 끝내므로
 * @details 처리기 본문을 다른 스레드에서 부르고, 알림 전에는 돌아오지 않음을, 알림 뒤에는 돌아옴을 본다. 기다림을 빼면 첫 기대가 진다.
 *          200 ms 는 "아직 안 돌아왔다" 의 하한이라 느린 기계에서 거짓 실패가 없다(느리면 더 늦게 돌아올 뿐).
 */
SW_TEST_CASE( ShutdownSignalTest, ConsoleCloseWaitsForShutdownComplete )
{
    ShutdownSignal::resetForTest();
    atomic<uint32> returned{ 0 };
    std::thread    handlerThread( [&returned]()
    {
        (void)ShutdownSignal::dispatchConsoleControlForTest( CTRL_CLOSE_EVENT );
        returned.store( 1 );
    } );
    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    SW_EXPECT_EQUAL( 0u, returned.load() );
    SW_EXPECT_TRUE( ShutdownSignal::getRequestedCause() == ShutdownCause::ConsoleClose );
    ShutdownSignal::notifyShutdownComplete();
    handlerThread.join();
    SW_EXPECT_EQUAL( 1u, returned.load() );
    ShutdownSignal::resetForTest();
}
#endif
