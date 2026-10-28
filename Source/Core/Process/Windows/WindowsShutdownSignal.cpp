#include "pch.h"

#include "Core/Process/ShutdownSignal.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOSHeaders.h"

namespace sw
{
    namespace
    {
        struct WindowsShutdownSignalInternal
        {
            /** @brief 창 닫기에 OS 가 주는 시간(약 5 초)보다 짧게 기다린다 — 넘기면 OS 가 정리 도중에 프로세스를 끝낸다. */
            static constexpr DWORD kCloseWaitMilliseconds = 4500;

            static inline atomic<bool>   s_bInstalled{ false };
            static inline atomic<uint32> s_serviceMode{ 0 };
            static inline atomic<uint32> s_interruptCount{ 0 };

            /** @brief 종료 완료 사건 — 첫 호출이 만든다. 함수 안 정적 변수라 여러 스레드가 동시에 처음 불러도 한 번만 만든다(실패하면 nullptr). */
            static HANDLE completeEvent()
            {
                static const HANDLE s_hComplete = CreateEventW( nullptr, TRUE, FALSE, nullptr );
                return s_hComplete;
            }

            static void waitForShutdownComplete()
            {
                const HANDLE hComplete = completeEvent();
                if ( hComplete != nullptr )
                    (void)WaitForSingleObject( hComplete, kCloseWaitMilliseconds );
            }

            /** @brief 콘솔 제어 처리기 — OS 가 새 스레드에서 부른다. TRUE 를 돌려주면 다음 처리기(기본 = 즉시 종료)로 넘기지 않는다. */
            static BOOL WINAPI onConsoleControl( DWORD controlType )
            {
                switch ( controlType )
                {
                    case CTRL_C_EVENT:
                    case CTRL_BREAK_EVENT:
                    {
                        // 두 번째 Ctrl+C 는 기본 처리기(즉시 종료)로 넘긴다 — 멈춘 종료를 사람이 끊는 경로(POSIX 의 _exit(130) 에 해당).
                        if ( s_interruptCount.fetch_add( 1, std::memory_order_relaxed ) >= 1 )
                            return FALSE;
                        ShutdownSignal::request( ShutdownCause::Interrupt );
                        return TRUE;
                    }
                    case CTRL_CLOSE_EVENT:
                    {
                        ShutdownSignal::request( ShutdownCause::ConsoleClose );
                        waitForShutdownComplete();
                        return TRUE;
                    }
                    case CTRL_LOGOFF_EVENT:
                    {
                        if ( s_serviceMode.load( std::memory_order_relaxed ) != 0 )
                            return FALSE; // 서비스는 사용자가 로그오프해도 돈다
                        ShutdownSignal::request( ShutdownCause::SystemShutdown );
                        waitForShutdownComplete();
                        return TRUE;
                    }
                    case CTRL_SHUTDOWN_EVENT:
                    {
                        ShutdownSignal::request( ShutdownCause::SystemShutdown );
                        waitForShutdownComplete();
                        return TRUE;
                    }
                    default:
                    {
                        return FALSE;
                    }
                }
            }
        };
    } // namespace

    bool ShutdownSignal::install()
    {
        // 교환으로 "내가 설치할 차례" 를 한 스레드만 얻는다 — 두 스레드가 동시에 부르면 같은 처리기가 두 번 걸려 첫 Ctrl+C 가 둘째 호출로 세어진다.
        if ( WindowsShutdownSignalInternal::s_bInstalled.exchange( true, std::memory_order_acq_rel ) )
            return true;
        const bool bInstalled = SetConsoleCtrlHandler( &WindowsShutdownSignalInternal::onConsoleControl, TRUE ) != FALSE;
        if ( bInstalled == false )
            WindowsShutdownSignalInternal::s_bInstalled.store( false, std::memory_order_release );
        return bInstalled && WindowsShutdownSignalInternal::completeEvent() != nullptr;
    }

    void ShutdownSignal::uninstall()
    {
        if ( WindowsShutdownSignalInternal::s_bInstalled.exchange( false, std::memory_order_acq_rel ) )
            (void)SetConsoleCtrlHandler( &WindowsShutdownSignalInternal::onConsoleControl, FALSE );
        // 사건 핸들은 프로세스 끝까지 둔다 — 처리기 스레드가 아직 기다리고 있을 수 있다.
    }

    void ShutdownSignal::notifyShutdownComplete()
    {
        const HANDLE hComplete = WindowsShutdownSignalInternal::completeEvent();
        if ( hComplete != nullptr )
            (void)SetEvent( hComplete );
    }

    void ShutdownSignal::setServiceMode( bool bServiceMode )
    {
        WindowsShutdownSignalInternal::s_serviceMode.store( bServiceMode ? 1u : 0u, std::memory_order_relaxed );
    }

    void ShutdownSignal::resetForTest()
    {
        _s_requestedCause.store( static_cast<uint8>( ShutdownCause::None ), std::memory_order_release );
        WindowsShutdownSignalInternal::s_interruptCount.store( 0, std::memory_order_relaxed );
        const HANDLE hComplete = WindowsShutdownSignalInternal::completeEvent();
        if ( hComplete != nullptr )
            (void)ResetEvent( hComplete );
    }

    bool ShutdownSignal::dispatchConsoleControlForTest( uint32 controlType )
    {
        return WindowsShutdownSignalInternal::onConsoleControl( static_cast<DWORD>( controlType ) ) != FALSE;
    }
} // namespace sw

#endif
