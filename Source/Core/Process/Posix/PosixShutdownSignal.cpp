#include "pch.h"

#include "Core/Process/ShutdownSignal.h"

#if !defined( SW_PLATFORM_WINDOWS )
    #include <signal.h>
    #include <unistd.h>

namespace sw
{
    namespace
    {
        struct PosixShutdownSignalInternal
        {
            static constexpr int32 kArrHandledSignal[] = { SIGINT, SIGTERM, SIGHUP };

            static inline struct sigaction s_arrPreviousAction[SW_COUNT_OF( kArrHandledSignal )]{};
            static inline struct sigaction s_previousPipeAction{};
            static inline bool             s_bInstalled{ false };
            static inline atomic<uint32>   s_interruptCount{ 0 };

            /** @brief 신호 처리기 — async-signal-safe 함수만 부른다(원자 연산 · write · _exit). */
            static void onSignal( int32 signalNumber )
            {
                if ( signalNumber == SIGINT && s_interruptCount.fetch_add( 1, std::memory_order_relaxed ) >= 1 )
                {
                    static constexpr utf8 kMessage[] = "\nSecond interrupt - exiting immediately\n";
                    (void)write( STDERR_FILENO, kMessage, sizeof( kMessage ) - 1 );
                    _exit( 130 );
                }
                const ShutdownCause cause = signalNumber == SIGTERM ? ShutdownCause::Terminate : ( signalNumber == SIGHUP ? ShutdownCause::Hangup : ShutdownCause::Interrupt );
                ShutdownSignal::request( cause );
            }
        };
    } // namespace

    bool ShutdownSignal::install()
    {
        if ( PosixShutdownSignalInternal::s_bInstalled )
            return true;
        struct sigaction action{};
        action.sa_handler = &PosixShutdownSignalInternal::onSignal;
        sigemptyset( &action.sa_mask );
        action.sa_flags = SA_RESTART;
        bool bInstalled = true;
        for ( size_t index = 0; index < SW_COUNT_OF( PosixShutdownSignalInternal::kArrHandledSignal ); ++index )
            bInstalled = sigaction( PosixShutdownSignalInternal::kArrHandledSignal[index], &action, &PosixShutdownSignalInternal::s_arrPreviousAction[index] ) == 0 && bInstalled;

        struct sigaction ignore{};
        ignore.sa_handler = SIG_IGN;
        sigemptyset( &ignore.sa_mask );
        bInstalled                                = sigaction( SIGPIPE, &ignore, &PosixShutdownSignalInternal::s_previousPipeAction ) == 0 && bInstalled;
        PosixShutdownSignalInternal::s_bInstalled = true;
        return bInstalled;
    }

    void ShutdownSignal::uninstall()
    {
        if ( PosixShutdownSignalInternal::s_bInstalled == false )
            return;
        for ( size_t index = 0; index < SW_COUNT_OF( PosixShutdownSignalInternal::kArrHandledSignal ); ++index )
            (void)sigaction( PosixShutdownSignalInternal::kArrHandledSignal[index], &PosixShutdownSignalInternal::s_arrPreviousAction[index], nullptr );
        (void)sigaction( SIGPIPE, &PosixShutdownSignalInternal::s_previousPipeAction, nullptr );
        PosixShutdownSignalInternal::s_bInstalled = false;
    }

    void ShutdownSignal::notifyShutdownComplete()
    {
    }

    void ShutdownSignal::setServiceMode( [[maybe_unused]] bool bServiceMode )
    {
    }

    void ShutdownSignal::resetForTest()
    {
        _s_requestedCause.store( static_cast<uint8>( ShutdownCause::None ), std::memory_order_release );
        PosixShutdownSignalInternal::s_interruptCount.store( 0, std::memory_order_relaxed );
    }
} // namespace sw

#endif
