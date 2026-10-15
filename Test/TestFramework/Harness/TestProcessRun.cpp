#include "pch.h"

#include "TestFramework/Harness/TestProcessRun.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace test
{
    namespace
    {
        struct TestProcessRunInternal
        {
            /** @brief 새니타이저 빌드의 시한 배수 — CTest 시한 배수(`sw_applySanitizerTestProperties`)와 같다. */
            static constexpr uint32 kSanitizerTimeoutScale = 10;
        };
    } // namespace
} // namespace test

namespace test
{
    struct ProcessDeadlineWatch::State
    {
        std::mutex              _mutex;
        std::condition_variable _wake;
        std::thread             _watchdog;
        bool                    _bFinished{ false };
        bool                    _bTimedOut{ false };
    };

    sw::string ProcessRunResult::getOutputTail( uint32 lineCount ) const
    {
        size_t start = _output.size();
        for ( uint32 line = 0; line <= lineCount && start > 0; ++line )
        {
            const size_t newline = _output.rfind( '\n', start - 1 );
            if ( newline == sw::string::npos )
            {
                start = 0;
                break;
            }
            start = newline;
        }
        return _output.substr( start );
    }

    ProcessDeadlineWatch::ProcessDeadlineWatch( sw::Process& process, uint32 timeoutSeconds )
        : _pState{ sw::make_unique<State>() }
    {
        const uint32 scaledSeconds = scaleTimeoutSeconds( timeoutSeconds );
        State*       pState        = _pState.get();
        pState->_watchdog          = std::thread( [pState, &process, scaledSeconds]()
        {
            std::unique_lock<std::mutex> lock( pState->_mutex );
            const bool                   bFinishedInTime = pState->_wake.wait_for( lock, std::chrono::seconds( scaledSeconds ), [pState]()
                                       {
                return pState->_bFinished;
            } );
            if ( bFinishedInTime )
                return;
            pState->_bTimedOut = true;
            (void)process.terminate( 1 ); // 죽이지 못하면 readOutputLine 이 끝나지 않아 CTest 시한이 마지막 그물이다
        } );
    }

    ProcessDeadlineWatch::~ProcessDeadlineWatch()
    {
        stop();
    }

    void ProcessDeadlineWatch::stop()
    {
        if ( _pState->_watchdog.joinable() == false )
            return;
        {
            const std::lock_guard<std::mutex> lock( _pState->_mutex );
            _pState->_bFinished = true;
        }
        _pState->_wake.notify_one();
        _pState->_watchdog.join();
    }

    bool ProcessDeadlineWatch::isTimedOut() const
    {
        const std::lock_guard<std::mutex> lock( _pState->_mutex );
        return _pState->_bTimedOut;
    }

    uint32 ProcessDeadlineWatch::scaleTimeoutSeconds( uint32 timeoutSeconds )
    {
#if defined( SW_SANITIZER_ADDRESS ) || defined( SW_SANITIZER_THREAD )
        return timeoutSeconds * TestProcessRunInternal::kSanitizerTimeoutScale;
#else
        (void)TestProcessRunInternal::kSanitizerTimeoutScale;
        return timeoutSeconds;
#endif
    }

    ProcessRunResult runProcessWithDeadline( sw::string_view command, const sw::ProcessOptions& options, uint32 timeoutSeconds )
    {
        return runProcessWithDeadline( command, options, timeoutSeconds, []( sw::Process&, const sw::string& ) {} );
    }
} // namespace test
