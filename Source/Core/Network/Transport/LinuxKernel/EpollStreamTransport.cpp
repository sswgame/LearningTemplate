#include "pch.h"

#include "Core/Network/Transport/LinuxKernel/EpollStreamTransport.h"

#include "Core/Concurrency/OnceInit.h"
#include "Core/Concurrency/StopToken.h"
#include "Core/Concurrency/ThreadName.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/IndexFreeList.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Network/Transport/NativeSocketAddress.h"
#include "Core/Network/Transport/StreamSendQueue.h"
#include "Core/Time/MonotonicClock.h"

#include <cerrno>
#include <cstring>
#include <mutex>
#include <thread>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

namespace sw
{
    namespace
    {
        enum class EpollConnectionState : uint8
        {
            Free = 0,
            Connecting,
            Open,
            Closing,
            Closed
        };

        struct EpollStreamTransportInternal
        {
            static constexpr int32  kMaxSendSpans             = 16;
            static constexpr int32  kMaxSendBytes             = 256 * 1024;
            static constexpr int32  kEventBatch               = 64;
            static constexpr int32  kLoopWaitMilli            = 100;
            static constexpr int64  kSweepIntervalNanoseconds = 100 * constant::time::kNanosecondsPerMillisecond;
            static constexpr uint64 kListenTag                = ~0ull;      ///< epoll 데이터. 리슨 소켓이다(연결은 핸들 packed 값이며, 세대가 0xFFFFFFFF 에 닿기 전에는 겹치지 않는다).
            static constexpr uint64 kWakeTag                  = ~0ull - 1u; ///< epoll 데이터. eventfd 이다.

            static void setNoDelay( int32 fd, bool bTCPNoDelay )
            {
                const int32 value = bTCPNoDelay ? 1 : 0;
                (void)setsockopt( fd, IPPROTO_TCP, TCP_NODELAY, &value, sizeof( value ) );
            }
            static void closeAbortive( int32 fd )
            {
                linger value{};
                value.l_onoff  = 1;
                value.l_linger = 0;
                (void)setsockopt( fd, SOL_SOCKET, SO_LINGER, &value, sizeof( value ) );
                ::close( fd );
            }
            static bool isWouldBlock( int32 error ) { return error == EAGAIN || error == EWOULDBLOCK; }
        };

        /**
         * @class EpollConnection
         * @brief 연결 슬롯 하나와 그 슬롯을 바꾸는 일입니다(열기 · 읽기 차례 · 보내기 비우기 · 닫기 · 정리 · 시한 · 요청 반영). 슬롯 객체는 지워지지 않고 세대 번호로 다시 쓴다.
         * @details `…Locked` 멤버는 부르는 쪽이 `getMutex()` 를 잡은 채로 부른다 — 수송 객체가 콜백 · recv 를 잠금 밖에서 하도록 잠금 범위를 직접 정한다.
         *          루프 번호는 열 때 정하고 슬롯이 비기 전에는 바뀌지 않아 잠금 없이 읽는다(`getLoopIndex`).
         */
        class EpollConnection
        {
        public:
            /** @brief 보내기를 비운 결과입니다. 통계는 수송 객체가 더한다. */
            struct FlushResult
            {
                uint64 _sentBytes{ 0 };
                uint64 _sendCallCount{ 0 };
                uint8  _bWritable{ SW_FALSE }; ///< 높은 워터마크를 넘었던 큐가 내려왔고 연결이 열려 있다 — 쓰기 가능을 알릴 차례다
            };

            EpollConnection()
                : _mutex{}
                , _sendQueue{}
                , _remote{}
                , _lastReceiveNanoseconds{ 0 }
                , _deadlineNanoseconds{ 0 }
                , _fd{ -1 }
                , _loopIndex{ 0 }
                , _generation{ 1 }
                , _closeReason{ StreamCloseReason::None }
                , _state{ EpollConnectionState::Free }
                , _bAccepted{ SW_FALSE }
                , _bReceivePaused{ SW_FALSE }
                , _bShutdownSent{ SW_FALSE }
                , _bRemoteFinished{ SW_FALSE }
                , _bReadPending{ SW_FALSE }
                , _bAbortRequested{ SW_FALSE }
                , _bOpenPending{ SW_FALSE }
            {
            }

            mutex&                 getMutex() { return _mutex; }
            int32                  getLoopIndex() const { return _loopIndex; }
            StreamConnectionHandle makeHandleLocked( uint32 index ) const { return StreamConnectionHandle::make( index, _generation ); }
            bool                   hasSocketLocked() const { return _fd >= 0; }
            bool                   isClosedLocked() const { return _state == EpollConnectionState::Closed; }
            bool                   isConnectingLocked() const { return _state == EpollConnectionState::Connecting; }

            /** @brief 핸들이 지금 이 슬롯의 살아 있는 연결을 가리키는지입니다(잠금 안). */
            bool isLiveLocked( StreamConnectionHandle handle ) const { return _generation == handle.generation() && _state != EpollConnectionState::Free; }
            /** @brief 사건 @p eventHandle 이 지금 이 슬롯(같은 세대 · 같은 루프)의 것인지입니다 — 같은 배치의 앞에서 닫혀 슬롯이 다른 연결로 다시 쓰였으면 옛 fd 의 사건이다. */
            bool isEventCurrentLocked( StreamConnectionHandle eventHandle, int32 loopIndex ) const { return _generation == eventHandle.generation() && _loopIndex == loopIndex; }

            /** @brief 빈 슬롯을 fd @p fd 의 연결로 엽니다(잠금 안). */
            void openLocked( int32 fd, const NetAddress& remote, bool bAccepted, EpollConnectionState state, int32 loopIndex, const StreamTransportConfig& settings )
            {
                _sendQueue.clear();
                _sendQueue.configure( settings._sendHighWatermarkBytes, settings._sendLowWatermarkBytes, settings._maxQueuedSendBytes );
                _remote                 = remote;
                _fd                     = fd;
                _loopIndex              = loopIndex;
                _lastReceiveNanoseconds = MonotonicClock::nowNanoseconds();
                _deadlineNanoseconds    = _lastReceiveNanoseconds + MonotonicClock::secondsToNanoseconds( settings._connectTimeoutSeconds );
                _closeReason            = StreamCloseReason::None;
                _state                  = state;
                _bAccepted              = bAccepted ? SW_TRUE : SW_FALSE;
                _bReceivePaused         = SW_FALSE;
                _bShutdownSent          = SW_FALSE;
                _bRemoteFinished        = SW_FALSE;
                _bReadPending           = SW_FALSE;
                _bAbortRequested        = SW_FALSE;
                _bOpenPending           = SW_FALSE;
            }

            /** @brief 바로 연결됐음을 적습니다(잠금 안). 열림은 루프에서 처리한다. */
            void markOpenPendingLocked() { _bOpenPending = SW_TRUE; }

            void closeFdLocked( StreamCloseReason reason, bool bAbortive )
            {
                if ( _fd < 0 )
                    return;
                if ( _closeReason == StreamCloseReason::None )
                    _closeReason = reason;
                if ( bAbortive )
                    EpollStreamTransportInternal::closeAbortive( _fd ); // close 가 epoll 등록도 지운다(dup 하지 않았다).
                else
                    ::close( _fd );
                _fd    = -1;
                _state = EpollConnectionState::Closed;
                _sendQueue.clear();
            }

            /** @brief 큐를 EAGAIN 까지 보냅니다(잠금 안). 닫는 중이고 큐가 비면 FIN 을 보내고, 상대의 FIN 도 받았으면 닫는다. */
            FlushResult flushLocked()
            {
                FlushResult result{};
                bool        bWritable = false;
                while ( _fd >= 0 && _sendQueue.isEmpty() == false )
                {
                    StreamSendSpan arrSpan[EpollStreamTransportInternal::kMaxSendSpans];
                    iovec          arrVector[EpollStreamTransportInternal::kMaxSendSpans];
                    const int32    spanCount = _sendQueue.collectSpans( arrSpan, EpollStreamTransportInternal::kMaxSendSpans, EpollStreamTransportInternal::kMaxSendBytes );
                    for ( int32 index = 0; index < spanCount; ++index )
                    {
                        arrVector[index].iov_base = const_cast<uint8*>( arrSpan[index]._pData );
                        arrVector[index].iov_len  = static_cast<size_t>( arrSpan[index]._size );
                    }
                    msghdr message{};
                    message.msg_iov    = arrVector;
                    message.msg_iovlen = static_cast<size_t>( spanCount );
                    const ssize_t sent = sendmsg( _fd, &message, MSG_NOSIGNAL );
                    if ( sent < 0 )
                    {
                        if ( EpollStreamTransportInternal::isWouldBlock( errno ) == false && errno != EINTR )
                            closeFdLocked( StreamCloseReason::Reset, true );
                        break; // EAGAIN 이다. EPOLLOUT 이 이어서 보낸다.
                    }
                    bWritable = _sendQueue.consume( static_cast<int32>( sent ) ) || bWritable;
                    result._sentBytes += static_cast<uint64>( sent );
                    ++result._sendCallCount;
                }
                const bool bClosingQueueEmpty = _fd >= 0 && _sendQueue.isEmpty() && _state == EpollConnectionState::Closing;
                if ( bClosingQueueEmpty )
                {
                    if ( _bShutdownSent == SW_FALSE )
                    {
                        (void)::shutdown( _fd, SHUT_WR );
                        _bShutdownSent = SW_TRUE;
                    }
                    if ( _bRemoteFinished == SW_TRUE )
                        closeFdLocked( _closeReason, false );
                }
                result._bWritable = ( bWritable && _state == EpollConnectionState::Open ) ? SW_TRUE : SW_FALSE;
                return result;
            }

            /** @brief 닫혔으면 정리할 핸들 · 이유를 담고 true 입니다(잠금 안). 부르는 쪽이 잠금을 푼 뒤 닫힘을 알리고 `freeLocked` 한다. */
            bool findClosedLocked( uint32 index, StreamConnectionHandle& outHandle, StreamCloseReason& outReason ) const
            {
                if ( _state != EpollConnectionState::Closed )
                    return false;
                outHandle = makeHandleLocked( index );
                outReason = _closeReason;
                return true;
            }

            /** @brief 슬롯을 비웁니다(잠금 안). 세대를 올려 옛 핸들이 이 슬롯을 다시 가리키지 않게 한다(0 은 무효 핸들이라 건너뛴다). */
            void freeLocked()
            {
                _generation = SlotHandle::nextGeneration( _generation ); // 핸들 표와 같은 세대 규칙(0 은 건너뛴다)
                if ( _generation == 0 )
                    _generation = 1;
                _state = EpollConnectionState::Free;
            }

            /** @brief 읽을 차례인지 봅니다(잠금 안). 읽을 차례면 fd · 핸들을 담고 true 다. 멈춰 있으면 다음 재개 때 읽도록 적어 둔다. */
            bool beginReadLocked( uint32 index, int32& outFd, StreamConnectionHandle& outHandle )
            {
                const bool bReading = _state == EpollConnectionState::Open || _state == EpollConnectionState::Closing;
                if ( bReading == false || _bRemoteFinished == SW_TRUE )
                    return false;
                if ( _bReceivePaused == SW_TRUE )
                {
                    _bReadPending = SW_TRUE;
                    return false;
                }
                outFd     = _fd;
                outHandle = makeHandleLocked( index );
                return true;
            }

            void markReceivedLocked() { _lastReceiveNanoseconds = MonotonicClock::nowNanoseconds(); }

            /**
             * @brief 받은 바이트가 없던 recv 의 결과를 반영합니다(잠금 안). 다시 읽어야 하면(EINTR) true 다.
             * @details 0 은 상대의 FIN 이다 — 이쪽이 닫는 중이 아니었으면 이쪽도 우아하게 닫는다. EAGAIN 이면 다음 에지를 기다린다. 그 밖의 오류는 끊는다.
             */
            bool finishReadLocked( ssize_t received, int32 error, int64 closeLingerNanoseconds, FlushResult& outFlush )
            {
                if ( received == 0 )
                {
                    _bRemoteFinished = SW_TRUE;
                    if ( _state == EpollConnectionState::Open )
                    {
                        _state               = EpollConnectionState::Closing;
                        _closeReason         = StreamCloseReason::RemoteClose;
                        _deadlineNanoseconds = MonotonicClock::nowNanoseconds() + closeLingerNanoseconds;
                    }
                    outFlush = flushLocked();
                    return false;
                }
                if ( EpollStreamTransportInternal::isWouldBlock( error ) )
                    return false;
                if ( error != EINTR )
                {
                    closeFdLocked( StreamCloseReason::Reset, true );
                    return false;
                }
                return true;
            }

            /** @brief 연결 중이던 fd 의 결과를 반영합니다(잠금 안). 열렸으면 핸들 · 상대 주소를 담고 true, 실패면 닫는다. */
            bool completeConnectLocked( uint32 index, bool bTCPNoDelay, StreamConnectionHandle& outHandle, NetAddress& outRemote )
            {
                if ( _state != EpollConnectionState::Connecting )
                    return false;
                int32     error  = 0;
                socklen_t length = sizeof( error );
                (void)getsockopt( _fd, SOL_SOCKET, SO_ERROR, &error, &length );
                if ( error != 0 )
                {
                    closeFdLocked( StreamCloseReason::ConnectFailed, true );
                    return false;
                }
                EpollStreamTransportInternal::setNoDelay( _fd, bTCPNoDelay );
                _state                  = EpollConnectionState::Open;
                _lastReceiveNanoseconds = MonotonicClock::nowNanoseconds();
                outHandle               = makeHandleLocked( index );
                outRemote               = _remote;
                return true;
            }

            /** @brief 다른 스레드가 맡긴 할 일을 반영합니다(잠금 안). 열기 · 읽기가 남았으면 @p outbOpen · @p outbRead 를 켠다. */
            FlushResult applyCommandLocked( bool& outbOpen, bool& outbRead )
            {
                FlushResult flush{};
                if ( _bAbortRequested == SW_TRUE )
                {
                    _bAbortRequested = SW_FALSE;
                    closeFdLocked( StreamCloseReason::LocalClose, true );
                }
                else if ( _state == EpollConnectionState::Closing )
                {
                    flush = flushLocked();
                }
                outbOpen      = _bOpenPending == SW_TRUE;
                _bOpenPending = SW_FALSE;
                outbRead      = _bReadPending == SW_TRUE && _bReceivePaused == SW_FALSE;
                if ( outbRead )
                    _bReadPending = SW_FALSE;
                return flush;
            }

            /** @brief 받기 쉼 · 연결 시한 · 우아한 종료 상한을 넘었으면 닫습니다(잠금 안, 주기 점검). 멈추는 중이면 살아 있는 연결을 닫는다(shutdown 이 테이블을 훑은 뒤에 받은 연결이다). */
            void closeIfExpiredLocked( int64 now, int64 idleNanoseconds, bool bStopping )
            {
                const bool bLive = _state != EpollConnectionState::Free && _state != EpollConnectionState::Closed;
                if ( bStopping && bLive )
                    closeFdLocked( StreamCloseReason::Shutdown, true );
                switch ( _state )
                {
                    case EpollConnectionState::Open:
                    {
                        if ( idleNanoseconds > 0 && now - _lastReceiveNanoseconds > idleNanoseconds )
                            closeFdLocked( StreamCloseReason::IdleTimeout, true );
                        break;
                    }
                    case EpollConnectionState::Connecting:
                    {
                        if ( now > _deadlineNanoseconds )
                            closeFdLocked( StreamCloseReason::ConnectFailed, true );
                        break;
                    }
                    case EpollConnectionState::Closing:
                    {
                        if ( now > _deadlineNanoseconds )
                            closeFdLocked( _closeReason, true );
                        break;
                    }
                    case EpollConnectionState::Free:
                    case EpollConnectionState::Closed:
                    {
                        break;
                    }
                }
            }

            /** @brief 수송을 닫을 때 살아 있는 연결을 끊습니다(잠금 안). 끊었으면 루프가 닫힘을 내도록 true 다. */
            bool closeForShutdownLocked()
            {
                if ( _state == EpollConnectionState::Free )
                    return false;
                closeFdLocked( StreamCloseReason::Shutdown, true );
                return true;
            }

            /**
             * @brief 보낼 바이트를 큐에 넣습니다(잠금 안). 큐가 비어 있었고 열려 있으면 바로 보낸다(쓰기 가능 알림은 큐가 넘친 적이 없으면 오지 않는다).
             * @return 살아 있는 열린 · 연결 중 연결이 아니면 닫힘이다.
             */
            StreamSendResult appendSendLocked( StreamConnectionHandle handle, const uint8* pData, int32 size, FlushResult& outFlush )
            {
                const bool bAccepting = _state == EpollConnectionState::Open || _state == EpollConnectionState::Connecting;
                if ( isLiveLocked( handle ) == false || bAccepting == false )
                    return StreamSendResult::Closed;
                const bool             bWasEmpty       = _sendQueue.isEmpty();
                const StreamSendResult result          = _sendQueue.append( pData, size );
                const bool             bShouldFlushNow = result != StreamSendResult::QueueFull && bWasEmpty && _state == EpollConnectionState::Open;
                if ( bShouldFlushNow )
                    outFlush = flushLocked();
                return result;
            }

            /** @brief 닫기를 요청합니다(잠금 안). 루프에 맡길 일이 생겼으면 true 다 — 끊기는 루프가, 우아한 종료는 큐를 비운 뒤 상대의 FIN 을 읽어 끝낸다. */
            bool requestCloseLocked( StreamConnectionHandle handle, StreamCloseMode mode, int64 closeLingerNanoseconds )
            {
                if ( isLiveLocked( handle ) == false || _state == EpollConnectionState::Closed )
                    return false;
                if ( mode == StreamCloseMode::Abort || _state == EpollConnectionState::Connecting )
                {
                    _bAbortRequested = SW_TRUE;
                }
                else if ( _state == EpollConnectionState::Open )
                {
                    _state               = EpollConnectionState::Closing;
                    _closeReason         = StreamCloseReason::LocalClose;
                    _deadlineNanoseconds = MonotonicClock::nowNanoseconds() + closeLingerNanoseconds;
                    _bReceivePaused      = SW_FALSE;
                    _bReadPending        = SW_TRUE; // 상대의 FIN 을 읽어야 끝난다.
                }
                return true;
            }

            /** @brief 받기 쉼을 바꿉니다(잠금 안). 받아들였으면 true 다. */
            bool setReceivePausedLocked( StreamConnectionHandle handle, bool bPaused )
            {
                if ( isLiveLocked( handle ) == false || _state == EpollConnectionState::Closing )
                    return false;
                _bReceivePaused = bPaused ? SW_TRUE : SW_FALSE;
                return true;
            }

            NetAddress findRemoteLocked( StreamConnectionHandle handle ) const { return isLiveLocked( handle ) ? _remote : NetAddress{}; }

        private:
            mutex                _mutex;
            StreamSendQueue      _sendQueue;
            NetAddress           _remote;
            int64                _lastReceiveNanoseconds;
            int64                _deadlineNanoseconds;
            int32                _fd;
            int32                _loopIndex;
            uint32               _generation;
            StreamCloseReason    _closeReason;
            EpollConnectionState _state;
            uint8                _bAccepted       : 1;
            uint8                _bReceivePaused  : 1;
            uint8                _bShutdownSent   : 1;
            uint8                _bRemoteFinished : 1;
            uint8                _bReadPending    : 1; ///< 멈춘 동안 에지를 받았다. 다시 읽을 때 EAGAIN 까지 읽는다.
            uint8                _bAbortRequested : 1;
            uint8                _bOpenPending    : 1; ///< 연결이 즉시 성공했다. 루프가 연다.
        };

        struct EpollLoop
        {
            mutex          _commandMutex;
            vector<uint32> _listCommandIndex;   ///< 다른 스레드가 맡긴 할 일(닫기, 읽기 재개, 열기).
            vector<uint32> _listJobIndex;       ///< 루프 스레드 전용. 할 일을 맞바꿔서 꺼낸다.
            vector<uint8>  _listReceiveScratch; ///< 루프 스레드 전용 읽기 버퍼(`_receiveChunkBytes`).
            sw::thread     _thread;
            int32          _epollFd{ -1 };
            int32          _wakeFd{ -1 };
        };
    } // namespace
} // namespace sw

namespace sw
{
    struct EpollStreamTransport::State
    {
        using Internal = EpollStreamTransportInternal;

        mutable mutex                       _tableMutex;
        vector<unique_ptr<EpollConnection>> _listConnection;
        IndexFreeList                       _freeSlot;
        vector<unique_ptr<EpollLoop>>       _listLoop;
        int32                               _loopCount{ 0 };
        OnceInit<StreamTransportConfig>     _settings;                        ///< initialize 가 I/O 스레드를 띄우기 전에 한 번 정한다(shutdown 이 비운다).
        OnceInit<IStreamHandler*>           _pHandler;                        ///< 위와 같다.
        int64                               _manualNextSweepNanoseconds{ 0 }; ///< `pollIO` 로 도는 루프 0 의 다음 시한 점검 시각.
        int32                               _listenFd{ -1 };
        atomic<uint32>                      _nextLoop{ 0 };
        atomic<int32>                       _openCount{ 0 };
        atomic<uint64>                      _acceptedCount{ 0 };
        atomic<uint64>                      _connectedCount{ 0 };
        atomic<uint64>                      _closedCount{ 0 };
        atomic<uint64>                      _receivedBytes{ 0 };
        atomic<uint64>                      _sentBytes{ 0 };
        atomic<uint64>                      _receiveCallCount{ 0 };
        atomic<uint64>                      _sendCallCount{ 0 };
        StopSource                          _stop;
        uint16                              _listenPort{ 0 };
        uint8                               _bThreaded{ SW_FALSE };
        uint8                               _bInitialized{ SW_FALSE };

        /**
         * @brief 루프 하나를 반환합니다. 루프 테이블(`_listLoop`)은 initialize 뒤 바뀌지 않으므로 const 로 읽습니다.
         * @note 비 const `operator[]` 는 경합 검출기에 쓰기로 잡힌다. 루프 스레드 여럿과 부르는 스레드가 함께 읽으면 경합 보고가 난다.
         */
        EpollLoop& getLoop( int32 loopIndex ) const { return *_listLoop[static_cast<size_t>( loopIndex )]; }

        EpollConnection& getConnection( uint32 index ) const
        {
            ScopedLock<mutex> lock{ _tableMutex };
            return *_listConnection[index];
        }

        EpollConnection* findConnection( StreamConnectionHandle handle ) const
        {
            ScopedLock<mutex> lock{ _tableMutex };
            if ( handle.isValid() == false || handle.index() >= _listConnection.size() )
                return nullptr;
            return _listConnection[handle.index()].get();
        }

        StreamConnectionHandle allocateConnection( int32 fd, const NetAddress& remote, bool bAccepted, EpollConnectionState state, EpollConnection*& pOutConnection )
        {
            ScopedLock<mutex> lock{ _tableMutex };
            uint32            index = 0;
            if ( _freeSlot.tryPop( index ) == false )
            {
                index = static_cast<uint32>( _listConnection.size() );
                _listConnection.push_back( make_unique<EpollConnection>() );
            }
            EpollConnection&  connection = *_listConnection[index];
            ScopedLock<mutex> connectionLock{ connection.getMutex() };
            const int32       loopIndex = static_cast<int32>( _nextLoop.fetch_add( 1, std::memory_order_relaxed ) % static_cast<uint32>( _loopCount ) );
            connection.openLocked( fd, remote, bAccepted, state, loopIndex, _settings.get() );
            _openCount.fetch_add( 1, std::memory_order_relaxed );
            pOutConnection = &connection;
            return connection.makeHandleLocked( index );
        }

        bool registerFd( int32 loopIndex, int32 fd, uint64 tag )
        {
            epoll_event event{};
            event.events   = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
            event.data.u64 = tag;
            return epoll_ctl( getLoop( loopIndex )._epollFd, EPOLL_CTL_ADD, fd, &event ) == 0;
        }

        void wakeLoop( int32 loopIndex )
        {
            const uint64 value = 1;
            (void)::write( getLoop( loopIndex )._wakeFd, &value, sizeof( value ) );
        }

        void scheduleCommand( int32 loopIndex, uint32 index )
        {
            {
                EpollLoop&        loop = getLoop( loopIndex );
                ScopedLock<mutex> lock{ loop._commandMutex };
                loop._listCommandIndex.push_back( index );
            }
            wakeLoop( loopIndex );
        }

        /** @brief 보내기를 비운 결과의 통계를 더하고 쓰기 가능 여부를 돌려줍니다. */
        bool addFlushStats( const EpollConnection::FlushResult& flush )
        {
            if ( flush._sendCallCount > 0 )
            {
                _sentBytes.fetch_add( flush._sentBytes, std::memory_order_relaxed );
                _sendCallCount.fetch_add( flush._sendCallCount, std::memory_order_relaxed );
            }
            return flush._bWritable == SW_TRUE;
        }

        // ---- 루프 스레드 ----

        /** @brief 닫혔으면 콜백을 내고 슬롯을 반환합니다(루프 스레드만 호출합니다. 연결의 소유자이기 때문입니다). */
        void retireIfClosed( EpollConnection& connection, uint32 index )
        {
            StreamConnectionHandle handle{};
            StreamCloseReason      reason = StreamCloseReason::None;
            {
                ScopedLock<mutex> lock{ connection.getMutex() };
                if ( connection.findClosedLocked( index, handle, reason ) == false )
                    return;
            }
            _pHandler->onStreamClosed( handle, reason );
            ScopedLock<mutex> lock{ _tableMutex };
            {
                ScopedLock<mutex> connectionLock{ connection.getMutex() };
                connection.freeLocked();
            }
            _freeSlot.push( index );
            _openCount.fetch_sub( 1, std::memory_order_relaxed );
            _closedCount.fetch_add( 1, std::memory_order_relaxed );
        }

        void readAvailable( EpollConnection& connection, uint32 index, int32 loopIndex )
        {
            uint8* pScratch = getLoop( loopIndex )._listReceiveScratch.data();
            for ( ;; )
            {
                StreamConnectionHandle handle{};
                int32                  fd = -1;
                {
                    ScopedLock<mutex> lock{ connection.getMutex() };
                    if ( connection.beginReadLocked( index, fd, handle ) == false )
                        return;
                }
                const ssize_t received = ::recv( fd, pScratch, static_cast<size_t>( _settings->_receiveChunkBytes ), 0 );
                if ( received > 0 )
                {
                    {
                        ScopedLock<mutex> lock{ connection.getMutex() };
                        connection.markReceivedLocked();
                    }
                    _receivedBytes.fetch_add( static_cast<uint64>( received ), std::memory_order_relaxed );
                    _receiveCallCount.fetch_add( 1, std::memory_order_relaxed );
                    _pHandler->onStreamReceived( handle, pScratch, static_cast<int32>( received ) );
                    continue;
                }
                const int32                  error = errno;
                EpollConnection::FlushResult flush{};
                bool                         bRetry = false;
                {
                    ScopedLock<mutex> lock{ connection.getMutex() };
                    bRetry = connection.finishReadLocked( received, error, MonotonicClock::secondsToNanoseconds( _settings->_closeLingerSeconds ), flush );
                }
                (void)addFlushStats( flush );
                if ( bRetry == false )
                    return;
            }
        }

        void openConnection( EpollConnection& connection, uint32 index, int32 loopIndex )
        {
            StreamConnectionHandle handle{};
            NetAddress             remote{};
            {
                ScopedLock<mutex> lock{ connection.getMutex() };
                if ( connection.completeConnectLocked( index, _settings->_bTCPNoDelay == SW_TRUE, handle, remote ) == false )
                    return;
            }
            _connectedCount.fetch_add( 1, std::memory_order_relaxed );
            _pHandler->onStreamOpened( handle, remote, false );
            EpollConnection::FlushResult flush{};
            {
                ScopedLock<mutex> lock{ connection.getMutex() };
                flush = connection.flushLocked(); // 연결 중에 쌓인 것을 보낸다.
            }
            if ( addFlushStats( flush ) )
                _pHandler->onStreamWritable( handle );
            readAvailable( connection, index, loopIndex );
        }

        void handleConnectionEvent( StreamConnectionHandle eventHandle, uint32 events, int32 loopIndex )
        {
            const uint32     index       = eventHandle.index();
            EpollConnection& connection  = getConnection( index );
            bool             bConnecting = false;
            {
                ScopedLock<mutex> lock{ connection.getMutex() };
                if ( connection.isEventCurrentLocked( eventHandle, loopIndex ) == false )
                    return;
                bConnecting = connection.isConnectingLocked();
            }
            if ( bConnecting )
            {
                if ( ( events & ( EPOLLOUT | EPOLLERR | EPOLLHUP ) ) != 0 )
                    openConnection( connection, index, loopIndex );
            }
            else
            {
                if ( ( events & EPOLLERR ) != 0 )
                {
                    ScopedLock<mutex> lock{ connection.getMutex() };
                    connection.closeFdLocked( StreamCloseReason::Reset, true );
                }
                if ( ( events & ( EPOLLIN | EPOLLRDHUP | EPOLLHUP ) ) != 0 )
                    readAvailable( connection, index, loopIndex );
                if ( ( events & EPOLLOUT ) != 0 )
                {
                    StreamConnectionHandle       handle{};
                    EpollConnection::FlushResult flush{};
                    {
                        ScopedLock<mutex> lock{ connection.getMutex() };
                        flush  = connection.flushLocked();
                        handle = connection.makeHandleLocked( index );
                    }
                    if ( addFlushStats( flush ) )
                        _pHandler->onStreamWritable( handle );
                }
            }
            retireIfClosed( connection, index );
        }

        void handleCommand( uint32 index, int32 loopIndex )
        {
            EpollConnection&             connection = getConnection( index );
            bool                         bOpen      = false;
            bool                         bRead      = false;
            EpollConnection::FlushResult flush{};
            {
                ScopedLock<mutex> lock{ connection.getMutex() };
                flush = connection.applyCommandLocked( bOpen, bRead );
            }
            (void)addFlushStats( flush );
            if ( bOpen )
                openConnection( connection, index, loopIndex );
            if ( bRead )
                readAvailable( connection, index, loopIndex );
            retireIfClosed( connection, index );
        }

        void acceptAvailable()
        {
            for ( ;; )
            {
                sockaddr_in remote{};
                socklen_t   length = sizeof( remote );
                const int32 fd     = accept4( _listenFd, reinterpret_cast<sockaddr*>( &remote ), &length, SOCK_NONBLOCK | SOCK_CLOEXEC );
                if ( fd < 0 )
                {
                    if ( errno == EINTR || errno == ECONNABORTED )
                        continue;
                    return; // EAGAIN 이나 그 밖의 오류다(EMFILE 은 로그를 남긴다).
                }
                if ( _stop.isStopRequested() || _openCount.load( std::memory_order_relaxed ) >= _settings->_maxConnections )
                {
                    Internal::closeAbortive( fd );
                    continue;
                }
                Internal::setNoDelay( fd, _settings->_bTCPNoDelay == SW_TRUE );
                EpollConnection*             pConnection = nullptr;
                const NetAddress             address     = NativeSocketAddress::fromNative( remote );
                const StreamConnectionHandle handle      = allocateConnection( fd, address, true, EpollConnectionState::Open, pConnection );
                _acceptedCount.fetch_add( 1, std::memory_order_relaxed );
                _pHandler->onStreamOpened( handle, address, true ); // 등록 전이다. 열림이 첫 콜백이어야 한다.
                if ( registerFd( pConnection->getLoopIndex(), fd, handle.packed() ) == false )
                {
                    ScopedLock<mutex> lock{ pConnection->getMutex() };
                    pConnection->closeFdLocked( StreamCloseReason::Reset, true );
                }
                // 열림 콜백 안에서 쌓인 보내기와 등록 실패에 따른 닫힘은 그 루프가 처리한다.
                scheduleCommand( pConnection->getLoopIndex(), handle.index() );
            }
        }

        void sweepLoop( int32 loopIndex, int64 now )
        {
            vector<uint32> listIndex;
            {
                ScopedLock<mutex> lock{ _tableMutex };
                for ( uint32 index = 0; index < static_cast<uint32>( _listConnection.size() ); ++index )
                {
                    if ( _listConnection[index]->getLoopIndex() == loopIndex )
                        listIndex.push_back( index );
                }
            }
            const int64 idleNanoseconds = MonotonicClock::secondsToNanoseconds( _settings->_idleTimeoutSeconds );
            const bool  bStopping       = _stop.isStopRequested();
            for ( const uint32 index : listIndex )
            {
                EpollConnection& connection = getConnection( index );
                {
                    ScopedLock<mutex> lock{ connection.getMutex() };
                    connection.closeIfExpiredLocked( now, idleNanoseconds, bStopping );
                }
                retireIfClosed( connection, index );
            }
        }

        int32 runOnce( int32 loopIndex, int32 timeoutMilli, int64& inoutNextSweep )
        {
            EpollLoop&  loop = getLoop( loopIndex );
            epoll_event arrEvent[Internal::kEventBatch];
            const int32 count   = epoll_wait( loop._epollFd, arrEvent, Internal::kEventBatch, timeoutMilli );
            int32       handled = 0;
            for ( int32 index = 0; index < count; ++index )
            {
                const uint64 tag = arrEvent[index].data.u64;
                if ( tag == Internal::kWakeTag )
                {
                    uint64 value = 0;
                    (void)::read( loop._wakeFd, &value, sizeof( value ) );
                    continue;
                }
                ++handled;
                if ( tag == Internal::kListenTag )
                    acceptAvailable();
                else
                    handleConnectionEvent( StreamConnectionHandle::fromPacked( tag ), arrEvent[index].events, loopIndex );
            }
            {
                ScopedLock<mutex> lock{ loop._commandMutex };
                loop._listJobIndex.clear();
                loop._listJobIndex.swap( loop._listCommandIndex );
            }
            for ( const uint32 index : loop._listJobIndex )
            {
                handleCommand( index, loopIndex );
            }
            handled += static_cast<int32>( loop._listJobIndex.size() );
            const int64 now = MonotonicClock::nowNanoseconds();
            if ( now >= inoutNextSweep )
            {
                inoutNextSweep = now + Internal::kSweepIntervalNanoseconds;
                sweepLoop( loopIndex, now );
            }
            return handled;
        }

        void runThread( int32 loopIndex )
        {
            ThreadName::setCurrentThreadName( "StreamIO" );
            int64 nextSweep = 0;
            while ( _stop.isStopRequested() == false || _openCount.load( std::memory_order_acquire ) > 0 )
            {
                (void)runOnce( loopIndex, Internal::kLoopWaitMilli, nextSweep );
            }
        }
    };
} // namespace sw

namespace sw
{
    EpollStreamTransport::EpollStreamTransport()
        : _state{ make_unique<State>() }
    {
    }

    EpollStreamTransport::~EpollStreamTransport()
    {
        shutdown();
    }

    bool EpollStreamTransport::initialize( IStreamHandler* pHandler, const StreamTransportConfig& settings )
    {
        State& state = *_state;
        if ( pHandler == nullptr || state._bInitialized == SW_TRUE )
            return false;
        state._settings.set( settings );
        state._pHandler.set( pHandler );
        state._loopCount = settings._ioThreadCount > 0 ? settings._ioThreadCount : 1;
        state._bThreaded = settings._ioThreadCount > 0 ? SW_TRUE : SW_FALSE;
        for ( int32 index = 0; index < state._loopCount; ++index )
        {
            state._listLoop.push_back( make_unique<EpollLoop>() );
        }
        for ( int32 index = 0; index < state._loopCount; ++index )
        {
            EpollLoop& loop = state.getLoop( index );
            loop._listReceiveScratch.resize( static_cast<size_t>( settings._receiveChunkBytes ) );
            loop._epollFd                    = epoll_create1( EPOLL_CLOEXEC );
            loop._wakeFd                     = eventfd( 0, EFD_NONBLOCK | EFD_CLOEXEC );
            const bool bEventLoopSetupFailed = loop._epollFd < 0 || loop._wakeFd < 0 || state.registerFd( index, loop._wakeFd, EpollStreamTransportInternal::kWakeTag ) == false;
            if ( bEventLoopSetupFailed )
            {
                SW_LOG_ERROR_ID( "EpollStreamTransport.EpollCreate1EventfdFailed", "epoll_create1 / eventfd failed (%#)", errno );
                return false; // shutdown 이 연 것을 닫는다.
            }
        }
        state._stop         = StopSource{};
        state._bInitialized = SW_TRUE;
        if ( state._bThreaded == SW_TRUE )
        {
            for ( int32 index = 0; index < state._loopCount; ++index )
            {
                state.getLoop( index )._thread = sw::thread( ThreadCreationKey{}, &State::runThread, &state, index );
            }
        }
        return true;
    }

    void EpollStreamTransport::shutdown()
    {
        State& state = *_state;
        if ( state._listLoop.empty() )
            return;
        // 리슨 소켓은 루프를 모두 멈춘 뒤에 닫는다. 루프 0 이 acceptAvailable 에서 `_listenFd` 를 읽고 있기 때문이다. 멈추는 동안 받은 연결은 acceptAvailable 이 바로 끊는다.
        state._stop.requestStop();
        {
            ScopedLock<mutex> lock{ state._tableMutex };
            for ( uint32 index = 0; index < static_cast<uint32>( state._listConnection.size() ); ++index )
            {
                EpollConnection& connection = *state._listConnection[index];
                bool             bSchedule  = false;
                {
                    ScopedLock<mutex> connectionLock{ connection.getMutex() };
                    bSchedule = connection.closeForShutdownLocked();
                }
                if ( bSchedule )
                {
                    EpollLoop&        loop = state.getLoop( connection.getLoopIndex() );
                    ScopedLock<mutex> commandLock{ loop._commandMutex };
                    loop._listCommandIndex.push_back( index );
                }
            }
        }
        for ( int32 index = 0; index < state._loopCount; ++index )
        {
            state.wakeLoop( index );
        }
        if ( state._bThreaded == SW_TRUE )
        {
            for ( int32 index = 0; index < state._loopCount; ++index )
            {
                EpollLoop& loop = state.getLoop( index );
                if ( loop._thread.joinable() )
                    loop._thread.join(); // 루프는 열린 연결이 0이 될 때까지 돈다.
            }
        }
        else
        {
            const Deadline deadline = Deadline::afterMilliseconds( 5000 );
            while ( state._openCount.load() > 0 && deadline.isExpired() == false )
            {
                (void)state.runOnce( 0, 10, state._manualNextSweepNanoseconds );
            }
        }
        if ( state._listenFd >= 0 )
        {
            ::close( state._listenFd );
            state._listenFd = -1;
        }
        for ( int32 index = 0; index < state._loopCount; ++index )
        {
            const EpollLoop& loop = state.getLoop( index );
            if ( loop._epollFd >= 0 )
                ::close( loop._epollFd );
            if ( loop._wakeFd >= 0 )
                ::close( loop._wakeFd );
        }
        state._listLoop.clear();
        state._listenPort   = 0;
        state._bInitialized = SW_FALSE;
        state._settings.reset(); // I/O 스레드를 모두 합류한 뒤 — 다시 initialize 할 수 있게 비운다
        state._pHandler.reset();
    }

    bool EpollStreamTransport::listen( const NetAddress& bindAddress )
    {
        State& state = *_state;
        if ( state._bInitialized == SW_FALSE || state._listenFd >= 0 )
            return false;
        const int32 fd = socket( AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP );
        if ( fd < 0 )
            return false;
        const int32 reuse = 1; // TIME_WAIT 에 남은 이전 서버의 포트를 바로 다시 연다(SO_REUSEPORT 가 아니므로 다른 프로세스와 포트를 나누지 않는다).
        (void)setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
        sockaddr_in local              = NativeSocketAddress::toNative( bindAddress );
        socklen_t   length             = sizeof( local );
        const bool  bListenSetupFailed = bind( fd, reinterpret_cast<const sockaddr*>( &local ), sizeof( local ) ) != 0 || ::listen( fd, state._settings->_listenBacklog ) != 0 ||
                                        getsockname( fd, reinterpret_cast<sockaddr*>( &local ), &length ) != 0;
        if ( bListenSetupFailed )
        {
            SW_LOG_ERROR_ID( "EpollStreamTransport.CouldNotListenStream", "Could not listen on stream address %# (%#)", bindAddress.toString().c_str(), errno );
            ::close( fd );
            return false;
        }
        // 등록하기 전에 적는다. 등록하는 순간 루프 0 이 받기 사건으로 `_listenFd` 를 읽는다(epoll_ctl 이 그 순서를 보장한다).
        state._listenFd   = fd;
        state._listenPort = ntohs( local.sin_port );
        if ( state.registerFd( 0, fd, EpollStreamTransportInternal::kListenTag ) == false )
        {
            SW_LOG_ERROR_ID( "EpollStreamTransport.CouldNotListenStream", "Could not listen on stream address %# (%#)", bindAddress.toString().c_str(), errno );
            state._listenFd   = -1;
            state._listenPort = 0;
            ::close( fd );
            return false;
        }
        return true;
    }

    uint16 EpollStreamTransport::getListenPort() const
    {
        return _state->_listenPort;
    }

    StreamConnectionHandle EpollStreamTransport::connect( const NetAddress& remote )
    {
        State& state = *_state;
        if ( state._bInitialized == SW_FALSE || state._stop.isStopRequested() )
            return StreamConnectionHandle{};
        const int32 fd = socket( AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP );
        if ( fd < 0 )
            return StreamConnectionHandle{};
        EpollConnection*             pConnection = nullptr;
        const StreamConnectionHandle handle      = state.allocateConnection( fd, remote, false, EpollConnectionState::Connecting, pConnection );
        const sockaddr_in            native      = NativeSocketAddress::toNative( remote );
        const int32                  result      = ::connect( fd, reinterpret_cast<const sockaddr*>( &native ), sizeof( native ) );
        const int32                  error       = errno;
        bool                         bHasSocket  = false;
        {
            ScopedLock<mutex> lock{ pConnection->getMutex() };
            if ( result == 0 )
                pConnection->markOpenPendingLocked(); // 바로 연결됐다. 열림은 루프에서 처리한다.
            else if ( error != EINPROGRESS )
                pConnection->closeFdLocked( StreamCloseReason::ConnectFailed, true );
            bHasSocket = pConnection->hasSocketLocked();
        }
        if ( bHasSocket && state.registerFd( pConnection->getLoopIndex(), fd, handle.packed() ) == false )
        {
            ScopedLock<mutex> lock{ pConnection->getMutex() };
            pConnection->closeFdLocked( StreamCloseReason::ConnectFailed, true );
        }
        state.scheduleCommand( pConnection->getLoopIndex(), handle.index() ); // 즉시 성공이나 즉시 실패를 루프가 처리한다(콜백은 루프 스레드에서 나간다).
        return handle;
    }

    StreamSendResult EpollStreamTransport::send( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        EpollConnection* pConnection = _state->findConnection( handle );
        if ( pConnection == nullptr )
            return StreamSendResult::Closed;
        EpollConnection::FlushResult flush{};
        StreamSendResult             result  = StreamSendResult::Closed;
        bool                         bClosed = false;
        {
            ScopedLock<mutex> lock{ pConnection->getMutex() };
            result  = pConnection->appendSendLocked( handle, pData, size, flush );
            bClosed = result != StreamSendResult::Closed && pConnection->isClosedLocked();
        }
        (void)_state->addFlushStats( flush ); // 바로 보낸 것의 쓰기 가능은 알리지 않는다(큐가 넘친 적이 없다)
        if ( bClosed )
            _state->scheduleCommand( pConnection->getLoopIndex(), handle.index() ); // 보내다가 RST 가 났다. 닫힘은 루프가 낸다.
        return result;
    }

    void EpollStreamTransport::close( StreamConnectionHandle handle, StreamCloseMode mode )
    {
        EpollConnection* pConnection = _state->findConnection( handle );
        if ( pConnection == nullptr )
            return;
        {
            ScopedLock<mutex> lock{ pConnection->getMutex() };
            if ( pConnection->requestCloseLocked( handle, mode, MonotonicClock::secondsToNanoseconds( _state->_settings->_closeLingerSeconds ) ) == false )
                return;
        }
        _state->scheduleCommand( pConnection->getLoopIndex(), handle.index() );
    }

    void EpollStreamTransport::setReceivePaused( StreamConnectionHandle handle, bool bPaused )
    {
        EpollConnection* pConnection = _state->findConnection( handle );
        if ( pConnection == nullptr )
            return;
        {
            ScopedLock<mutex> lock{ pConnection->getMutex() };
            if ( pConnection->setReceivePausedLocked( handle, bPaused ) == false )
                return;
        }
        if ( bPaused == false )
            _state->scheduleCommand( pConnection->getLoopIndex(), handle.index() ); // 에지 트리거라 멈춘 동안 온 것은 다시 알리지 않으므로 루프가 읽는다.
    }

    int32 EpollStreamTransport::pollIO( int32 timeoutMilli )
    {
        if ( _state->_bInitialized == SW_FALSE || _state->_bThreaded == SW_TRUE )
            return 0;
        return _state->runOnce( 0, timeoutMilli, _state->_manualNextSweepNanoseconds );
    }

    NetAddress EpollStreamTransport::getRemoteAddress( StreamConnectionHandle handle ) const
    {
        EpollConnection* pConnection = _state->findConnection( handle );
        if ( pConnection == nullptr )
            return NetAddress{};
        ScopedLock<mutex> lock{ pConnection->getMutex() };
        return pConnection->findRemoteLocked( handle );
    }

    StreamTransportStats EpollStreamTransport::getStats() const
    {
        const State&         state = *_state;
        StreamTransportStats stats;
        stats._acceptedCount    = state._acceptedCount.load();
        stats._connectedCount   = state._connectedCount.load();
        stats._closedCount      = state._closedCount.load();
        stats._receivedBytes    = state._receivedBytes.load();
        stats._sentBytes        = state._sentBytes.load();
        stats._receiveCallCount = state._receiveCallCount.load();
        stats._sendCallCount    = state._sendCallCount.load();
        stats._openCount        = state._openCount.load();
        return stats;
    }
} // namespace sw
