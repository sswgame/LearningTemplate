#include "pch.h"

#include "Core/Network/Transport/Linux/EpollStreamTransport.h"

#include "Core/Concurrency/ThreadName.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
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

SW_LOG_CALLER( "EpollStreamTransport" );

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
            static constexpr int64  kSweepIntervalNanoseconds = 100 * 1000 * 1000;
            static constexpr uint64 kListenTag                = ~0ull;      ///< epoll 데이터 — 리슨 소켓(연결은 핸들 packed — 세대가 0xFFFFFFFF 에 닿기 전엔 겹치지 않는다)
            static constexpr uint64 kWakeTag                  = ~0ull - 1u; ///< epoll 데이터 — eventfd

            static int64       toNanoseconds( float64 seconds ) { return static_cast<int64>( seconds * 1.0e9 ); }
            static NetAddress  makeNetAddress( const sockaddr_in& native ) { return NetAddress{ ntohl( native.sin_addr.s_addr ), ntohs( native.sin_port ) }; }
            static sockaddr_in makeNativeAddress( const NetAddress& address )
            {
                sockaddr_in native{};
                native.sin_family      = AF_INET;
                native.sin_port        = htons( address._port );
                native.sin_addr.s_addr = htonl( address._ipv4 );
                return native;
            }
            static void setNoDelay( int32 fd, bool bNoDelay )
            {
                const int32 value = bNoDelay ? 1 : 0;
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

        struct EpollConnection
        {
            mutex                _mutex;
            StreamSendQueue      _sendQueue{};
            NetAddress           _remote{};
            int64                _lastReceiveNanoseconds{ 0 };
            int64                _deadlineNanoseconds{ 0 };
            int32                _fd{ -1 };
            int32                _loopIndex{ 0 };
            uint32               _generation{ 1 };
            StreamCloseReason    _closeReason{ StreamCloseReason::None };
            EpollConnectionState _state{ EpollConnectionState::Free };
            uint8                _bAccepted{ SW_FALSE };
            uint8                _bReceivePaused{ SW_FALSE };
            uint8                _bShutdownSent{ SW_FALSE };
            uint8                _bRemoteFinished{ SW_FALSE };
            uint8                _bReadPending{ SW_FALSE }; ///< 멈춘 동안 에지를 받았다 — 다시 읽을 때 EAGAIN 까지 읽는다
            uint8                _bAbortRequested{ SW_FALSE };
            uint8                _bOpenPending{ SW_FALSE }; ///< 연결이 즉시 성공 — 루프가 연다
        };

        struct EpollLoop
        {
            mutex          _commandMutex;
            vector<uint32> _listCommandIndex; ///< 다른 스레드가 맡긴 할 일(닫기 · 읽기 재개 · 열기)
            vector<uint32> _listWorkIndex;    ///< 루프 스레드 전용 — 할 일을 맞바꿔 꺼낸다
            std::thread    _thread;
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
        vector<uint32>                      _listFreeIndex;
        unique_ptr<EpollLoop[]>             _arrLoop;
        int32                               _loopCount{ 0 };
        vector<uint8>                       _receiveScratchBytes; ///< 읽기 버퍼 — 루프마다 한 칸(루프 수 × `_receiveChunkBytes`, getScratch)
        StreamTransportSettings             _settings{};
        IStreamHandler*                     _pHandler{ nullptr };
        int64                               _manualNextSweepNanoseconds{ 0 }; ///< `pollIo` 로 도는 루프 0 의 다음 시한 훑기
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
        atomic<bool>                        _bStopping{ false };
        uint16                              _listenPort{ 0 };
        uint8                               _bThreaded{ SW_FALSE };
        uint8                               _bInitialized{ SW_FALSE };

        uint8* getScratch( int32 loopIndex ) { return _receiveScratchBytes.data() + static_cast<size_t>( loopIndex ) * static_cast<size_t>( _settings._receiveChunkBytes ); }

        EpollConnection& getConnection( uint32 index ) const
        {
            std::scoped_lock<mutex> lock{ _tableMutex };
            return *_listConnection[index];
        }

        EpollConnection* findConnection( StreamConnectionHandle handle ) const
        {
            std::scoped_lock<mutex> lock{ _tableMutex };
            if ( handle.isValid() == false || handle._index >= _listConnection.size() )
                return nullptr;
            return _listConnection[handle._index].get();
        }

        static bool isLive( const EpollConnection& connection, StreamConnectionHandle handle )
        {
            return connection._generation == handle._generation && connection._state != EpollConnectionState::Free;
        }

        StreamConnectionHandle allocateConnection( int32 fd, const NetAddress& remote, bool bAccepted, EpollConnectionState state, EpollConnection*& pOutConnection )
        {
            std::scoped_lock<mutex> lock{ _tableMutex };
            uint32                  index = 0;
            if ( _listFreeIndex.empty() == false )
            {
                index = _listFreeIndex.back();
                _listFreeIndex.pop_back();
            }
            else
            {
                index = static_cast<uint32>( _listConnection.size() );
                _listConnection.push_back( make_unique<EpollConnection>() );
            }
            EpollConnection&        connection = *_listConnection[index];
            std::scoped_lock<mutex> connectionLock{ connection._mutex };
            connection._sendQueue.clear();
            connection._sendQueue.configure( _settings._sendHighWatermarkBytes, _settings._sendLowWatermarkBytes, _settings._maxQueuedSendBytes );
            connection._remote                 = remote;
            connection._fd                     = fd;
            connection._loopIndex              = static_cast<int32>( _nextLoop.fetch_add( 1, std::memory_order_relaxed ) % static_cast<uint32>( _loopCount ) );
            connection._lastReceiveNanoseconds = MonotonicClock::nowNanoseconds();
            connection._deadlineNanoseconds    = connection._lastReceiveNanoseconds + Internal::toNanoseconds( _settings._connectTimeoutSeconds );
            connection._closeReason            = StreamCloseReason::None;
            connection._state                  = state;
            connection._bAccepted              = bAccepted ? SW_TRUE : SW_FALSE;
            connection._bReceivePaused         = SW_FALSE;
            connection._bShutdownSent          = SW_FALSE;
            connection._bRemoteFinished        = SW_FALSE;
            connection._bReadPending           = SW_FALSE;
            connection._bAbortRequested        = SW_FALSE;
            connection._bOpenPending           = SW_FALSE;
            _openCount.fetch_add( 1, std::memory_order_relaxed );
            pOutConnection = &connection;
            return StreamConnectionHandle::make( index, connection._generation );
        }

        bool registerFd( int32 loopIndex, int32 fd, uint64 tag )
        {
            epoll_event event{};
            event.events   = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
            event.data.u64 = tag;
            return epoll_ctl( _arrLoop[loopIndex]._epollFd, EPOLL_CTL_ADD, fd, &event ) == 0;
        }

        void wakeLoop( int32 loopIndex )
        {
            const uint64 value = 1;
            (void)::write( _arrLoop[loopIndex]._wakeFd, &value, sizeof( value ) );
        }

        void scheduleCommand( int32 loopIndex, uint32 index )
        {
            {
                std::scoped_lock<mutex> lock{ _arrLoop[loopIndex]._commandMutex };
                _arrLoop[loopIndex]._listCommandIndex.push_back( index );
            }
            wakeLoop( loopIndex );
        }

        // ---- 연결 잠금 안에서 ----

        void closeFdLocked( EpollConnection& connection, StreamCloseReason reason, bool bAbortive )
        {
            if ( connection._fd < 0 )
                return;
            if ( connection._closeReason == StreamCloseReason::None )
                connection._closeReason = reason;
            if ( bAbortive )
                Internal::closeAbortive( connection._fd ); // close 가 epoll 등록도 지운다(dup 하지 않았다)
            else
                ::close( connection._fd );
            connection._fd    = -1;
            connection._state = EpollConnectionState::Closed;
            connection._sendQueue.clear();
        }

        /** @brief 줄을 EAGAIN 까지 보냅니다. 높은 물금을 넘었던 줄이 내려오면 true. */
        bool flushLocked( EpollConnection& connection )
        {
            bool bWritable = false;
            while ( connection._fd >= 0 && connection._sendQueue.isEmpty() == false )
            {
                StreamSendSpan arrSpan[Internal::kMaxSendSpans];
                iovec          arrVector[Internal::kMaxSendSpans];
                const int32    spanCount = connection._sendQueue.collectSpans( arrSpan, Internal::kMaxSendSpans, Internal::kMaxSendBytes );
                for ( int32 index = 0; index < spanCount; ++index )
                {
                    arrVector[index].iov_base = const_cast<uint8*>( arrSpan[index]._pData );
                    arrVector[index].iov_len  = static_cast<size_t>( arrSpan[index]._size );
                }
                msghdr message{};
                message.msg_iov    = arrVector;
                message.msg_iovlen = static_cast<size_t>( spanCount );
                const ssize_t sent = sendmsg( connection._fd, &message, MSG_NOSIGNAL );
                if ( sent < 0 )
                {
                    if ( Internal::isWouldBlock( errno ) == false && errno != EINTR )
                        closeFdLocked( connection, StreamCloseReason::Reset, true );
                    break; // EAGAIN — EPOLLOUT 이 이어 보낸다
                }
                bWritable = connection._sendQueue.consume( static_cast<int32>( sent ) ) || bWritable;
                _sentBytes.fetch_add( static_cast<uint64>( sent ), std::memory_order_relaxed );
                _sendCallCount.fetch_add( 1, std::memory_order_relaxed );
            }
            if ( connection._fd >= 0 && connection._sendQueue.isEmpty() && connection._state == EpollConnectionState::Closing )
            {
                if ( connection._bShutdownSent == SW_FALSE )
                {
                    (void)::shutdown( connection._fd, SHUT_WR );
                    connection._bShutdownSent = SW_TRUE;
                }
                if ( connection._bRemoteFinished == SW_TRUE )
                    closeFdLocked( connection, connection._closeReason, false );
            }
            return bWritable && connection._state == EpollConnectionState::Open;
        }

        // ---- 루프 스레드 ----

        /** @brief 닫혔으면 콜백을 내고 자리를 돌려줍니다(루프 스레드만 — 연결의 소유자). */
        void retireIfClosed( EpollConnection& connection, uint32 index )
        {
            StreamConnectionHandle handle{};
            StreamCloseReason      reason = StreamCloseReason::None;
            {
                std::scoped_lock<mutex> lock{ connection._mutex };
                if ( connection._state != EpollConnectionState::Closed )
                    return;
                handle = StreamConnectionHandle::make( index, connection._generation );
                reason = connection._closeReason;
            }
            _pHandler->onStreamClosed( handle, reason );
            std::scoped_lock<mutex> lock{ _tableMutex };
            {
                std::scoped_lock<mutex> connectionLock{ connection._mutex };
                ++connection._generation;
                if ( connection._generation == 0 )
                    connection._generation = 1;
                connection._state = EpollConnectionState::Free;
            }
            _listFreeIndex.push_back( index );
            _openCount.fetch_sub( 1, std::memory_order_relaxed );
            _closedCount.fetch_add( 1, std::memory_order_relaxed );
        }

        void readAvailable( EpollConnection& connection, uint32 index, int32 loopIndex )
        {
            uint8* pScratch = getScratch( loopIndex );
            for ( ;; )
            {
                StreamConnectionHandle handle{};
                int32                  fd = -1;
                {
                    std::scoped_lock<mutex> lock{ connection._mutex };
                    const bool              bReading = connection._state == EpollConnectionState::Open || connection._state == EpollConnectionState::Closing;
                    if ( bReading == false || connection._bRemoteFinished == SW_TRUE )
                        return;
                    if ( connection._bReceivePaused == SW_TRUE )
                    {
                        connection._bReadPending = SW_TRUE;
                        return;
                    }
                    fd     = connection._fd;
                    handle = StreamConnectionHandle::make( index, connection._generation );
                }
                const ssize_t received = ::recv( fd, pScratch, static_cast<size_t>( _settings._receiveChunkBytes ), 0 );
                if ( received > 0 )
                {
                    {
                        std::scoped_lock<mutex> lock{ connection._mutex };
                        connection._lastReceiveNanoseconds = MonotonicClock::nowNanoseconds();
                    }
                    _receivedBytes.fetch_add( static_cast<uint64>( received ), std::memory_order_relaxed );
                    _receiveCallCount.fetch_add( 1, std::memory_order_relaxed );
                    _pHandler->onStreamReceived( handle, pScratch, static_cast<int32>( received ) );
                    continue;
                }
                std::scoped_lock<mutex> lock{ connection._mutex };
                if ( received == 0 )
                {
                    connection._bRemoteFinished = SW_TRUE;
                    if ( connection._state == EpollConnectionState::Open )
                    {
                        connection._state               = EpollConnectionState::Closing;
                        connection._closeReason         = StreamCloseReason::RemoteClose;
                        connection._deadlineNanoseconds = MonotonicClock::nowNanoseconds() + Internal::toNanoseconds( _settings._closeLingerSeconds );
                    }
                    (void)flushLocked( connection );
                    return;
                }
                if ( Internal::isWouldBlock( errno ) )
                    return;
                if ( errno != EINTR )
                {
                    closeFdLocked( connection, StreamCloseReason::Reset, true );
                    return;
                }
            }
        }

        void openConnection( EpollConnection& connection, uint32 index, int32 loopIndex )
        {
            StreamConnectionHandle handle{};
            NetAddress             remote{};
            {
                std::scoped_lock<mutex> lock{ connection._mutex };
                if ( connection._state != EpollConnectionState::Connecting )
                    return;
                int32     error  = 0;
                socklen_t length = sizeof( error );
                (void)getsockopt( connection._fd, SOL_SOCKET, SO_ERROR, &error, &length );
                if ( error != 0 )
                {
                    closeFdLocked( connection, StreamCloseReason::ConnectFailed, true );
                    return;
                }
                Internal::setNoDelay( connection._fd, _settings._bNoDelay == SW_TRUE );
                connection._state                  = EpollConnectionState::Open;
                connection._lastReceiveNanoseconds = MonotonicClock::nowNanoseconds();
                handle                             = StreamConnectionHandle::make( index, connection._generation );
                remote                             = connection._remote;
            }
            _connectedCount.fetch_add( 1, std::memory_order_relaxed );
            _pHandler->onStreamOpened( handle, remote, false );
            bool bWritable = false;
            {
                std::scoped_lock<mutex> lock{ connection._mutex };
                bWritable = flushLocked( connection ); // 연결 중에 쌓인 것
            }
            if ( bWritable )
                _pHandler->onStreamWritable( handle );
            readAvailable( connection, index, loopIndex );
        }

        void handleConnectionEvent( StreamConnectionHandle eventHandle, uint32 events, int32 loopIndex )
        {
            const uint32         index      = eventHandle._index;
            EpollConnection&     connection = getConnection( index );
            EpollConnectionState state      = EpollConnectionState::Free;
            {
                std::scoped_lock<mutex> lock{ connection._mutex };
                // 같은 묶음 앞에서 닫혀 자리가 다른 연결(다른 루프일 수도)로 다시 쓰였으면 이 사건은 옛 fd 의 것이다.
                if ( connection._generation != eventHandle._generation || connection._loopIndex != loopIndex )
                    return;
                state = connection._state;
            }
            if ( state == EpollConnectionState::Connecting )
            {
                if ( ( events & ( EPOLLOUT | EPOLLERR | EPOLLHUP ) ) != 0 )
                    openConnection( connection, index, loopIndex );
            }
            else
            {
                if ( ( events & EPOLLERR ) != 0 )
                {
                    std::scoped_lock<mutex> lock{ connection._mutex };
                    closeFdLocked( connection, StreamCloseReason::Reset, true );
                }
                if ( ( events & ( EPOLLIN | EPOLLRDHUP | EPOLLHUP ) ) != 0 )
                    readAvailable( connection, index, loopIndex );
                if ( ( events & EPOLLOUT ) != 0 )
                {
                    StreamConnectionHandle handle{};
                    bool                   bWritable = false;
                    {
                        std::scoped_lock<mutex> lock{ connection._mutex };
                        bWritable = flushLocked( connection );
                        handle    = StreamConnectionHandle::make( index, connection._generation );
                    }
                    if ( bWritable )
                        _pHandler->onStreamWritable( handle );
                }
            }
            retireIfClosed( connection, index );
        }

        void handleCommand( uint32 index, int32 loopIndex )
        {
            EpollConnection& connection = getConnection( index );
            bool             bOpen      = false;
            bool             bRead      = false;
            {
                std::scoped_lock<mutex> lock{ connection._mutex };
                if ( connection._bAbortRequested == SW_TRUE )
                {
                    connection._bAbortRequested = SW_FALSE;
                    closeFdLocked( connection, StreamCloseReason::LocalClose, true );
                }
                else if ( connection._state == EpollConnectionState::Closing )
                {
                    (void)flushLocked( connection );
                }
                bOpen                    = connection._bOpenPending == SW_TRUE;
                connection._bOpenPending = SW_FALSE;
                bRead                    = connection._bReadPending == SW_TRUE && connection._bReceivePaused == SW_FALSE;
                if ( bRead )
                    connection._bReadPending = SW_FALSE;
            }
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
                    return; // EAGAIN · 그 밖(EMFILE 은 로그)
                }
                if ( _bStopping.load( std::memory_order_acquire ) || _openCount.load( std::memory_order_relaxed ) >= _settings._maxConnections )
                {
                    Internal::closeAbortive( fd );
                    continue;
                }
                Internal::setNoDelay( fd, _settings._bNoDelay == SW_TRUE );
                EpollConnection*             pConnection = nullptr;
                const NetAddress             address     = Internal::makeNetAddress( remote );
                const StreamConnectionHandle handle      = allocateConnection( fd, address, true, EpollConnectionState::Open, pConnection );
                _acceptedCount.fetch_add( 1, std::memory_order_relaxed );
                _pHandler->onStreamOpened( handle, address, true ); // 등록 전 — 열림이 첫 콜백
                if ( registerFd( pConnection->_loopIndex, fd, handle.packed() ) == false )
                {
                    std::scoped_lock<mutex> lock{ pConnection->_mutex };
                    closeFdLocked( *pConnection, StreamCloseReason::Reset, true );
                }
                // 열림 콜백 안에서 쌓인 보내기 · 등록 실패의 닫힘은 그 루프가 처리한다.
                scheduleCommand( pConnection->_loopIndex, handle._index );
            }
        }

        void sweepLoop( int32 loopIndex, int64 now )
        {
            vector<uint32> listIndex;
            {
                std::scoped_lock<mutex> lock{ _tableMutex };
                for ( uint32 index = 0; index < static_cast<uint32>( _listConnection.size() ); ++index )
                {
                    if ( _listConnection[index]->_loopIndex == loopIndex )
                        listIndex.push_back( index );
                }
            }
            const int64 idleNanoseconds = Internal::toNanoseconds( _settings._idleTimeoutSeconds );
            const bool  bStopping       = _bStopping.load( std::memory_order_acquire );
            for ( const uint32 index : listIndex )
            {
                EpollConnection& connection = getConnection( index );
                {
                    std::scoped_lock<mutex> lock{ connection._mutex };
                    const bool              bLive = connection._state != EpollConnectionState::Free && connection._state != EpollConnectionState::Closed;
                    if ( bStopping && bLive )
                        closeFdLocked( connection, StreamCloseReason::Shutdown, true ); // shutdown 이 표를 훑은 뒤에 받은 연결
                    switch ( connection._state )
                    {
                        case EpollConnectionState::Open:
                        {
                            if ( idleNanoseconds > 0 && now - connection._lastReceiveNanoseconds > idleNanoseconds )
                                closeFdLocked( connection, StreamCloseReason::IdleTimeout, true );
                            break;
                        }
                        case EpollConnectionState::Connecting:
                        {
                            if ( now > connection._deadlineNanoseconds )
                                closeFdLocked( connection, StreamCloseReason::ConnectFailed, true );
                            break;
                        }
                        case EpollConnectionState::Closing:
                        {
                            if ( now > connection._deadlineNanoseconds )
                                closeFdLocked( connection, connection._closeReason, true );
                            break;
                        }
                        case EpollConnectionState::Free:
                        case EpollConnectionState::Closed:
                        {
                            break;
                        }
                    }
                }
                retireIfClosed( connection, index );
            }
        }

        int32 runOnce( int32 loopIndex, int32 timeoutMilli, int64& inoutNextSweep )
        {
            EpollLoop&  loop = _arrLoop[loopIndex];
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
                std::scoped_lock<mutex> lock{ loop._commandMutex };
                loop._listWorkIndex.clear();
                loop._listWorkIndex.swap( loop._listCommandIndex );
            }
            for ( const uint32 index : loop._listWorkIndex )
            {
                handleCommand( index, loopIndex );
            }
            handled += static_cast<int32>( loop._listWorkIndex.size() );
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
            ThreadName::setCurrentThreadName( "StreamIo" );
            int64 nextSweep = 0;
            while ( _bStopping.load( std::memory_order_acquire ) == false || _openCount.load( std::memory_order_acquire ) > 0 )
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

    EpollStreamTransport::~EpollStreamTransport() { shutdown(); }

    bool EpollStreamTransport::initialize( IStreamHandler* pHandler, const StreamTransportSettings& settings )
    {
        State& state = *_state;
        if ( pHandler == nullptr || state._bInitialized == SW_TRUE )
            return false;
        state._settings  = settings;
        state._pHandler  = pHandler;
        state._loopCount = settings._ioThreadCount > 0 ? settings._ioThreadCount : 1;
        state._bThreaded = settings._ioThreadCount > 0 ? SW_TRUE : SW_FALSE;
        state._arrLoop   = make_unique<EpollLoop[]>( static_cast<size_t>( state._loopCount ) );
        state._receiveScratchBytes.resize( static_cast<size_t>( state._loopCount ) * static_cast<size_t>( settings._receiveChunkBytes ) );
        for ( int32 index = 0; index < state._loopCount; ++index )
        {
            EpollLoop& loop = state._arrLoop[index];
            loop._epollFd   = epoll_create1( EPOLL_CLOEXEC );
            loop._wakeFd    = eventfd( 0, EFD_NONBLOCK | EFD_CLOEXEC );
            if ( loop._epollFd < 0 || loop._wakeFd < 0 || state.registerFd( index, loop._wakeFd, EpollStreamTransportInternal::kWakeTag ) == false )
            {
                SW_LOG_ERROR( "epoll_create1 / eventfd failed (%#)", errno );
                return false; // shutdown 이 연 것을 닫는다
            }
        }
        state._bStopping.store( false, std::memory_order_release );
        state._bInitialized = SW_TRUE;
        if ( state._bThreaded == SW_TRUE )
        {
            for ( int32 index = 0; index < state._loopCount; ++index )
            {
                state._arrLoop[index]._thread = std::thread( &State::runThread, &state, index );
            }
        }
        return true;
    }

    void EpollStreamTransport::shutdown()
    {
        State& state = *_state;
        if ( state._arrLoop == nullptr )
            return;
        state._bStopping.store( true, std::memory_order_release );
        if ( state._listenFd >= 0 )
        {
            ::close( state._listenFd );
            state._listenFd = -1;
        }
        {
            std::scoped_lock<mutex> lock{ state._tableMutex };
            for ( uint32 index = 0; index < static_cast<uint32>( state._listConnection.size() ); ++index )
            {
                EpollConnection& connection = *state._listConnection[index];
                bool             bSchedule  = false;
                {
                    std::scoped_lock<mutex> connectionLock{ connection._mutex };
                    if ( connection._state != EpollConnectionState::Free )
                    {
                        state.closeFdLocked( connection, StreamCloseReason::Shutdown, true );
                        bSchedule = true;
                    }
                }
                if ( bSchedule )
                {
                    std::scoped_lock<mutex> commandLock{ state._arrLoop[connection._loopIndex]._commandMutex };
                    state._arrLoop[connection._loopIndex]._listCommandIndex.push_back( index );
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
                if ( state._arrLoop[index]._thread.joinable() )
                    state._arrLoop[index]._thread.join(); // 루프는 열린 연결이 0 이 될 때까지 돈다
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
        for ( int32 index = 0; index < state._loopCount; ++index )
        {
            if ( state._arrLoop[index]._epollFd >= 0 )
                ::close( state._arrLoop[index]._epollFd );
            if ( state._arrLoop[index]._wakeFd >= 0 )
                ::close( state._arrLoop[index]._wakeFd );
        }
        state._arrLoop.reset();
        state._listenPort   = 0;
        state._bInitialized = SW_FALSE;
    }

    bool EpollStreamTransport::listen( const NetAddress& bindAddress )
    {
        State& state = *_state;
        if ( state._bInitialized == SW_FALSE || state._listenFd >= 0 )
            return false;
        const int32 fd = socket( AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP );
        if ( fd < 0 )
            return false;
        const int32 reuse = 1; // TIME_WAIT 에 남은 이전 서버의 포트를 바로 다시 연다(SO_REUSEPORT 는 아니다 — 다른 프로세스와 나누지 않는다)
        (void)setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
        sockaddr_in local  = EpollStreamTransportInternal::makeNativeAddress( bindAddress );
        socklen_t   length = sizeof( local );
        if ( bind( fd, reinterpret_cast<const sockaddr*>( &local ), sizeof( local ) ) != 0 || ::listen( fd, state._settings._listenBacklog ) != 0 ||
             getsockname( fd, reinterpret_cast<sockaddr*>( &local ), &length ) != 0 || state.registerFd( 0, fd, EpollStreamTransportInternal::kListenTag ) == false )
        {
            SW_LOG_ERROR( "Could not listen on stream address %# (%#)", bindAddress.toString().c_str(), errno );
            ::close( fd );
            return false;
        }
        state._listenFd   = fd;
        state._listenPort = ntohs( local.sin_port );
        return true;
    }

    uint16 EpollStreamTransport::getListenPort() const { return _state->_listenPort; }

    StreamConnectionHandle EpollStreamTransport::connect( const NetAddress& remote )
    {
        State& state = *_state;
        if ( state._bInitialized == SW_FALSE || state._bStopping.load() )
            return StreamConnectionHandle{};
        const int32 fd = socket( AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP );
        if ( fd < 0 )
            return StreamConnectionHandle{};
        EpollConnection*             pConnection = nullptr;
        const StreamConnectionHandle handle      = state.allocateConnection( fd, remote, false, EpollConnectionState::Connecting, pConnection );
        const sockaddr_in            native      = EpollStreamTransportInternal::makeNativeAddress( remote );
        const int32                  result      = ::connect( fd, reinterpret_cast<const sockaddr*>( &native ), sizeof( native ) );
        {
            std::scoped_lock<mutex> lock{ pConnection->_mutex };
            if ( result == 0 )
                pConnection->_bOpenPending = SW_TRUE; // 바로 됐다 — 열림은 루프에서
            else if ( errno != EINPROGRESS )
                state.closeFdLocked( *pConnection, StreamCloseReason::ConnectFailed, true );
        }
        if ( pConnection->_fd >= 0 && state.registerFd( pConnection->_loopIndex, fd, handle.packed() ) == false )
        {
            std::scoped_lock<mutex> lock{ pConnection->_mutex };
            state.closeFdLocked( *pConnection, StreamCloseReason::ConnectFailed, true );
        }
        state.scheduleCommand( pConnection->_loopIndex, handle._index ); // 즉시 성공 · 즉시 실패를 루프가 처리한다(콜백은 루프 스레드)
        return handle;
    }

    StreamSendResult EpollStreamTransport::send( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        EpollConnection* pConnection = _state->findConnection( handle );
        if ( pConnection == nullptr )
            return StreamSendResult::Closed;
        std::scoped_lock<mutex> lock{ pConnection->_mutex };
        const bool              bAccepting = pConnection->_state == EpollConnectionState::Open || pConnection->_state == EpollConnectionState::Connecting;
        if ( State::isLive( *pConnection, handle ) == false || bAccepting == false )
            return StreamSendResult::Closed;
        const bool             bWasEmpty = pConnection->_sendQueue.isEmpty();
        const StreamSendResult result    = pConnection->_sendQueue.append( pData, size );
        if ( result != StreamSendResult::QueueFull && bWasEmpty && pConnection->_state == EpollConnectionState::Open )
            (void)_state->flushLocked( *pConnection ); // 줄이 비어 있었다 — 바로 보낸다(쓰기 가능 알림은 넘친 적이 없으니 없다)
        if ( pConnection->_state == EpollConnectionState::Closed )
            _state->scheduleCommand( pConnection->_loopIndex, handle._index ); // 보내다 RST — 닫힘은 루프가 낸다
        return result;
    }

    void EpollStreamTransport::close( StreamConnectionHandle handle, StreamCloseMode mode )
    {
        EpollConnection* pConnection = _state->findConnection( handle );
        if ( pConnection == nullptr )
            return;
        {
            std::scoped_lock<mutex> lock{ pConnection->_mutex };
            if ( State::isLive( *pConnection, handle ) == false || pConnection->_state == EpollConnectionState::Closed )
                return;
            if ( mode == StreamCloseMode::Abort || pConnection->_state == EpollConnectionState::Connecting )
            {
                pConnection->_bAbortRequested = SW_TRUE;
            }
            else if ( pConnection->_state == EpollConnectionState::Open )
            {
                pConnection->_state               = EpollConnectionState::Closing;
                pConnection->_closeReason         = StreamCloseReason::LocalClose;
                pConnection->_deadlineNanoseconds = MonotonicClock::nowNanoseconds() + EpollStreamTransportInternal::toNanoseconds( _state->_settings._closeLingerSeconds );
                pConnection->_bReceivePaused      = SW_FALSE;
                pConnection->_bReadPending        = SW_TRUE; // 저쪽 FIN 을 읽어야 끝난다
            }
        }
        _state->scheduleCommand( pConnection->_loopIndex, handle._index );
    }

    void EpollStreamTransport::setReceivePaused( StreamConnectionHandle handle, bool bPaused )
    {
        EpollConnection* pConnection = _state->findConnection( handle );
        if ( pConnection == nullptr )
            return;
        {
            std::scoped_lock<mutex> lock{ pConnection->_mutex };
            if ( State::isLive( *pConnection, handle ) == false || pConnection->_state == EpollConnectionState::Closing )
                return;
            pConnection->_bReceivePaused = bPaused ? SW_TRUE : SW_FALSE;
        }
        if ( bPaused == false )
            _state->scheduleCommand( pConnection->_loopIndex, handle._index ); // 에지 트리거 — 멈춘 동안 온 것은 다시 알리지 않으니 루프가 읽는다
    }

    int32 EpollStreamTransport::pollIo( int32 timeoutMilli )
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
        std::scoped_lock<mutex> lock{ pConnection->_mutex };
        return State::isLive( *pConnection, handle ) ? pConnection->_remote : NetAddress{};
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
