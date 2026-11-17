#include "pch.h"

#include "Engine/Window/Linux/LinuxSplashThread.h"

#if defined( SW_PLATFORM_LINUX )
    #include <cerrno>
    #include <cstring>
    #include <poll.h>
    #include <sys/eventfd.h>
    #include <unistd.h>

namespace sw
{
    namespace
    {
        struct LinuxSplashThreadInternal
        {
            static constexpr int32 kNoFileDescriptor = -1;
            static constexpr uint8 kStartReady       = 1u << 0;
            static constexpr uint8 kStartFailed      = 1u << 1;
            static constexpr int   kWaitForever      = -1; ///< poll 시간 제한 없음 — 깨우기는 eventfd 가 한다

            static uint8 toBit( LinuxSplashWakeReason wake ) { return static_cast<uint8>( wake ); }

            /** @brief eventfd 에 1 을 더해 기다리는 쪽을 깨웁니다. */
            static void signal( int32 fd )
            {
                const uint64 one = 1;
                (void)::write( fd, &one, sizeof( one ) ); // 넘치면(2^64 - 1) 이미 깨울 일이 쌓여 있다
            }
        };
    } // namespace

    LinuxSplashThread::LinuxSplashThread()
        : _wakeFD{ LinuxSplashThreadInternal::kNoFileDescriptor }
    {
    }

    LinuxSplashThread::~LinuxSplashThread()
    {
        stop();
    }

    bool LinuxSplashThread::start( RunFunction run, const PaintState& initialState )
    {
        using Internal = LinuxSplashThreadInternal;
        _wakeFD        = ::eventfd( 0, EFD_CLOEXEC | EFD_NONBLOCK );
        if ( _wakeFD == Internal::kNoFileDescriptor )
        {
            SW_LOG_ERROR_ID( "LinuxSplash.WakeEventFailed", "Splash: eventfd failed (%#) - no splash thread", std::strerror( errno ) );
            return false;
        }
        {
            const ScopedLock<mutex> lock{ _mutex };
            _paintState  = initialState;
            _requestMask = Internal::toBit( LinuxSplashWakeReason::Redraw ); // 첫 `waitForEvents` 가 곧 첫 그림을 받는다
            _startMask   = 0;
        }
        Internal::signal( _wakeFD );
        _thread = thread{ ThreadCreationKey{}, &LinuxSplashThread::run, this, std::move( run ) };

        UniqueLock<mutex> lock{ _mutex };
        _readyCondition.wait( lock, [this]() SW_REQUIRES_LOCK( _mutex )
        { return _startMask != 0; } );
        return ( _startMask & Internal::kStartReady ) != 0;
    }

    void LinuxSplashThread::run( RunFunction function )
    {
        function( *this );
        markFailed(); // 본문이 어느 쪽도 알리지 않고 돌아왔으면 `start` 가 영원히 기다리지 않게 한다(이미 알렸으면 바꾸지 않는다)
    }

    void LinuxSplashThread::requestRedraw( const PaintState& state )
    {
        if ( _thread.joinable() == false )
            return;
        {
            const ScopedLock<mutex> lock{ _mutex };
            _paintState = state;
            _requestMask |= LinuxSplashThreadInternal::toBit( LinuxSplashWakeReason::Redraw );
        }
        LinuxSplashThreadInternal::signal( _wakeFD );
    }

    void LinuxSplashThread::stop()
    {
        using Internal = LinuxSplashThreadInternal;
        if ( _thread.joinable() )
        {
            {
                const ScopedLock<mutex> lock{ _mutex };
                _requestMask |= Internal::toBit( LinuxSplashWakeReason::Quit );
            }
            Internal::signal( _wakeFD );
            _thread.join();
        }
        if ( _wakeFD != Internal::kNoFileDescriptor )
        {
            (void)::close( _wakeFD );
            _wakeFD = Internal::kNoFileDescriptor;
        }
    }

    void LinuxSplashThread::markReady()
    {
        {
            const ScopedLock<mutex> lock{ _mutex };
            if ( _startMask == 0 )
                _startMask = LinuxSplashThreadInternal::kStartReady;
        }
        _readyCondition.notify_all();
    }

    void LinuxSplashThread::markFailed()
    {
        {
            const ScopedLock<mutex> lock{ _mutex };
            if ( _startMask == 0 )
                _startMask = LinuxSplashThreadInternal::kStartFailed;
        }
        _readyCondition.notify_all();
    }

    uint8 LinuxSplashThread::waitForEvents( int32 connectionFD, PaintState& outState )
    {
        using Internal = LinuxSplashThreadInternal;
        pollfd arrEntry[2]{
            {connectionFD, POLLIN, 0},
            {     _wakeFD, POLLIN, 0},
        };
        uint8 wakeMask = 0;
        if ( ::poll( arrEntry, 2, Internal::kWaitForever ) < 0 )
            return errno == EINTR ? wakeMask : Internal::toBit( LinuxSplashWakeReason::ConnectionLost );
        if ( ( arrEntry[0].revents & POLLIN ) != 0 )
            wakeMask |= Internal::toBit( LinuxSplashWakeReason::ConnectionReadable );
        if ( ( arrEntry[0].revents & ( POLLHUP | POLLERR | POLLNVAL ) ) != 0 )
            wakeMask |= Internal::toBit( LinuxSplashWakeReason::ConnectionLost );
        if ( ( arrEntry[1].revents & POLLIN ) != 0 )
        {
            uint64 count = 0;
            (void)::read( _wakeFD, &count, sizeof( count ) ); // 쌓인 깨우기를 비운다 — 요청은 아래 잠금 안의 표지가 말한다
            const ScopedLock<mutex> lock{ _mutex };
            wakeMask |= _requestMask;
            if ( ( _requestMask & Internal::toBit( LinuxSplashWakeReason::Redraw ) ) != 0 )
                outState = _paintState;
            _requestMask = 0;
        }
        return wakeMask;
    }
} // namespace sw
#endif
