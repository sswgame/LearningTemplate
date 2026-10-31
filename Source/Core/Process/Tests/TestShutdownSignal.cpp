// ShutdownSignal — 처음 요청만 남고, OS 종료 신호는 프로세스를 죽이지 않고 깃발만 세우며, Windows 창 닫기는 정리 완료 알림을 기다린다.
#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Process/ShutdownSignal.h"
#include "Core/Testing/TestFramework.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Foundation/PlatformOSHeaders.h"
#else
    #include <csignal>
#endif

using namespace sw;

SW_TEST_CASE( ShutdownSignalTest, FirstRequestWins )
{
    ShutdownSignal::resetForTest();
    SW_EXPECT_FALSE( ShutdownSignal::isRequested() );
    ShutdownSignal::request( ShutdownCause::Terminate );
    ShutdownSignal::request( ShutdownCause::ConsoleCommand );
    SW_EXPECT_TRUE( ShutdownSignal::getRequestedCause() == ShutdownCause::Terminate );
    SW_EXPECT_EQUAL( string( "Terminate" ), string( ShutdownSignal::getCauseName( ShutdownSignal::getRequestedCause() ) ) );
    ShutdownSignal::resetForTest();
}

/**
 * @brief OS 가 보낸 종료 신호(POSIX SIGTERM · Windows Ctrl+Break)가 프로세스를 죽이지 않고 깃발만 세운다.
 * @details POSIX 는 이 프로세스에 진짜 SIGTERM 을 `raise` 한다 — 처리기가 없으면 테스트 프로세스가 죽는다.
 *          Windows 는 콘솔 그룹 전체(CTest 포함)로 가는 `GenerateConsoleCtrlEvent` 대신 처리기 본문을 직접 부른다.
 *          App 은 처리기를 설치하지 않으므로(전용 서버만 한다) 케이스 끝의 `uninstall` 이 남의 설치를 걷지 않는다.
 */
SW_TEST_CASE( ShutdownSignalTest, TerminateSignalSetsTheFlag )
{
    ShutdownSignal::resetForTest();
    SW_REQUIRE_TRUE( ShutdownSignal::install() );
#if defined( SW_PLATFORM_WINDOWS )
    SW_EXPECT_TRUE( ShutdownSignal::dispatchConsoleControlForTest( CTRL_BREAK_EVENT ) );
    SW_EXPECT_TRUE( ShutdownSignal::getRequestedCause() == ShutdownCause::Interrupt );
#else
    SW_EXPECT_EQUAL( 0, std::raise( SIGTERM ) );
    SW_EXPECT_TRUE( ShutdownSignal::getRequestedCause() == ShutdownCause::Terminate );
#endif
    ShutdownSignal::uninstall();
    ShutdownSignal::resetForTest();
}

#if defined( SW_PLATFORM_WINDOWS )
/**
 * @brief 콘솔 창 닫기는 정리가 끝났다는 알림을 기다린 뒤 돌아간다 — 돌아가는 순간 OS 가 프로세스를 끝내므로.
 * @details 처리기 본문을 다른 스레드에서 부르고, 알림 전에는 돌아오지 않음을, 알림 뒤에는 돌아옴을 본다. 200 ms 는 "아직 안 돌아왔다" 의 하한이다
 *          (처리기의 상한 4.5 초보다 훨씬 짧아 느린 기계에서도 거짓 실패가 없다).
 */
SW_TEST_CASE( ShutdownSignalTest, ConsoleCloseWaitsForShutdownComplete )
{
    ShutdownSignal::resetForTest();
    atomic<uint32> returned{ 0 };
    thread         handlerThread( [&returned]()
    {
        (void)ShutdownSignal::dispatchConsoleControlForTest( CTRL_CLOSE_EVENT );
        returned.store( 1 );
    } );
    ThreadUtil::sleepForMilliseconds( 200 );
    SW_EXPECT_EQUAL( 0u, returned.load() );
    SW_EXPECT_TRUE( ShutdownSignal::getRequestedCause() == ShutdownCause::ConsoleClose );
    ShutdownSignal::notifyShutdownComplete();
    handlerThread.join();
    SW_EXPECT_EQUAL( 1u, returned.load() );
    ShutdownSignal::resetForTest();
}
#endif
