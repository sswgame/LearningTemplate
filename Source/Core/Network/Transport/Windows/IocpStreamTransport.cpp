#include "pch.h"

#include "Core/Network/Transport/Windows/IocpStreamTransport.h"

#include "Core/Concurrency/ThreadName.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Network/Transport/PlatformSocketUtil.h"
#include "Core/Network/Transport/StreamSendQueue.h"
#include "Core/Time/MonotonicClock.h"

#include <cstring>
#include <mutex>
#include <thread>
#include <WinSock2.h>
#include <MSWSock.h>
#include <WS2tcpip.h>

SW_LOG_CALLER( "IocpStreamTransport" );

namespace sw
{
    namespace
    {
        enum class IocpOperationKind : uint8
        {
            Receive = 0,
            Send,
            Connect,
            Accept
        };

        /** @brief 걸어 둔 작업 하나 — OVERLAPPED 가 맨 앞이라 완료의 포인터에서 바로 되찾는다. 자리는 움직이지 않는다(연결 · 수락 자리가 unique_ptr · 고정 배열). */
        struct IocpOperation
        {
            OVERLAPPED        _overlapped;
            uint32            _index;
            IocpOperationKind _kind;
        };

        enum class IocpConnectionState : uint8
        {
            Free = 0,
            Connecting,
            Open,
            Closing, ///< 우아한 종료 — 줄을 비우고 FIN, 저쪽 FIN 을 기다린다
            Closed,  ///< 소켓을 닫았다 — 걸린 작업이 돌아오기를 기다린다
            Retiring ///< 닫힘 콜백을 내는 중(한 번만)
        };

        struct IocpStreamTransportInternal
        {
            static constexpr ULONG_PTR kSocketKey                = 1;
            static constexpr ULONG_PTR kStopKey                  = 2;
            static constexpr ULONG_PTR kRetireKey                = 3; ///< 걸린 작업이 없는 연결을 닫았을 때 — 바이트 칸에 자리 번호
            static constexpr int32     kAcceptPostCount          = 16;
            static constexpr int32     kMaxSendSpans             = 16;
            static constexpr int32     kMaxSendBytes             = 256 * 1024;
            static constexpr int32     kCompletionBatch          = 64;
            static constexpr int32     kLoopWaitMilli            = 100;
            static constexpr int64     kSweepIntervalNanoseconds = 100 * 1000 * 1000;
            static constexpr DWORD     kAcceptAddressBytes       = sizeof( sockaddr_in ) + 16;

            static int64 toNanoseconds( float64 seconds ) { return static_cast<int64>( seconds * 1.0e9 ); }

            static NetAddress makeNetAddress( const sockaddr_in& native ) { return NetAddress{ ntohl( native.sin_addr.s_addr ), ntohs( native.sin_port ) }; }

            static sockaddr_in makeNativeAddress( const NetAddress& address )
            {
                sockaddr_in native{};
                native.sin_family      = AF_INET;
                native.sin_port        = htons( address._port );
                native.sin_addr.s_addr = htonl( address._ipv4 );
                return native;
            }

            /** @brief 확장 함수(AcceptEx · ConnectEx)를 얻습니다 — 함수 포인터 칸에 바로 받아 객체 ↔ 함수 포인터 캐스트가 없다. */
            template <typename TFunction>
            static TFunction findExtension( SOCKET socket, GUID guid )
            {
                TFunction pFunction = nullptr;
                DWORD     bytes     = 0;
                if ( WSAIoctl( socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof( guid ), &pFunction, sizeof( pFunction ), &bytes, nullptr, nullptr ) != 0 )
                    return nullptr;
                return pFunction;
            }

            static SOCKET openStreamSocket() { return WSASocketW( AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED ); }

            static void setNoDelay( SOCKET socket, bool bTCPNoDelay )
            {
                const BOOL value = bTCPNoDelay ? TRUE : FALSE;
                (void)setsockopt( socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const utf8*>( &value ), sizeof( value ) );
            }

            /** @brief RST 로 닫는다(SO_LINGER 0) — 끊기 · 시한 · 종료. */
            static void closeAbortive( SOCKET socket )
            {
                LINGER linger{};
                linger.l_onoff  = 1;
                linger.l_linger = 0;
                (void)setsockopt( socket, SOL_SOCKET, SO_LINGER, reinterpret_cast<const utf8*>( &linger ), sizeof( linger ) );
                closesocket( socket );
            }
        };

        struct IocpConnection
        {
            mutex               _mutex;
            StreamSendQueue     _sendQueue{};
            vector<uint8>       _receiveBuffer{};
            IocpOperation       _receiveOperation{}; ///< 연결 중에는 ConnectEx 가 같은 자리를 쓴다(받기는 열린 뒤에 건다)
            IocpOperation       _sendOperation{};
            WSABUF              _arrSendBuffer[IocpStreamTransportInternal::kMaxSendSpans]{};
            NetAddress          _remote{};
            SOCKET              _socket{ INVALID_SOCKET };
            int64               _lastReceiveNanoseconds{ 0 };
            int64               _deadlineNanoseconds{ 0 }; ///< 연결 시한 · 우아한 종료 상한
            int32               _pendingOperationCount{ 0 };
            uint32              _generation{ 1 };
            StreamCloseReason   _closeReason{ StreamCloseReason::None };
            IocpConnectionState _state{ IocpConnectionState::Free };
            uint8               _bAccepted{ SW_FALSE };
            uint8               _bReceivePosted{ SW_FALSE };
            uint8               _bSendPosted{ SW_FALSE };
            uint8               _bReceivePaused{ SW_FALSE };
            uint8               _bShutdownSent{ SW_FALSE };
            uint8               _bRemoteFinished{ SW_FALSE };
        };

        struct IocpAcceptSlot
        {
            IocpOperation _operation{};
            SOCKET        _socket{ INVALID_SOCKET };
            uint8         _arrAddress[IocpStreamTransportInternal::kAcceptAddressBytes * 2]{};
        };
    } // namespace
} // namespace sw

namespace sw
{
    struct IocpStreamTransport::State
    {
        using Internal = IocpStreamTransportInternal;

        mutable mutex                      _tableMutex;
        vector<unique_ptr<IocpConnection>> _listConnection;
        vector<uint32>                     _listFreeIndex;
        unique_ptr<IocpAcceptSlot[]>       _arrAcceptSlot;
        vector<std::thread>                _listThread;
        StreamTransportSettings            _settings{};
        IStreamHandler*                    _pHandler{ nullptr };
        HANDLE                             _completionPort{ nullptr };
        SOCKET                             _listenSocket{ INVALID_SOCKET };
        LPFN_ACCEPTEX                      _pAcceptEx{ nullptr };
        atomic<LPFN_CONNECTEX>             _pConnectEx{ nullptr };
        atomic<int64>                      _nextSweepNanoseconds{ 0 };
        atomic<int32>                      _pendingAcceptCount{ 0 };
        atomic<int32>                      _openCount{ 0 };
        atomic<uint64>                     _acceptedCount{ 0 };
        atomic<uint64>                     _connectedCount{ 0 };
        atomic<uint64>                     _closedCount{ 0 };
        atomic<uint64>                     _receivedBytes{ 0 };
        atomic<uint64>                     _sentBytes{ 0 };
        atomic<uint64>                     _receiveCallCount{ 0 };
        atomic<uint64>                     _sendCallCount{ 0 };
        atomic<bool>                       _bStopping{ false };
        atomic<bool>                       _bExitThreads{ false }; ///< I/O 스레드가 끝난다 — 멈춤 표는 깨우기만 한다(한 묶음에 표 여럿을 한 스레드가 가져갈 수 있다)
        uint16                             _listenPort{ 0 };
        uint8                              _bInitialized{ SW_FALSE };

        // ---- 자리 ----

        StreamConnectionHandle allocateConnection( SOCKET socket, const NetAddress& remote, bool bAccepted, IocpConnectionState state, IocpConnection*& pOutConnection )
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
                _listConnection.push_back( make_unique<IocpConnection>() );
            }
            IocpConnection&         connection = *_listConnection[index];
            std::scoped_lock<mutex> connectionLock{ connection._mutex };
            connection._sendQueue.clear();
            connection._sendQueue.configure( _settings._sendHighWatermarkBytes, _settings._sendLowWatermarkBytes, _settings._maxQueuedSendBytes );
            connection._receiveBuffer.resize( static_cast<size_t>( _settings._receiveChunkBytes ) );
            connection._receiveOperation._index = index;
            connection._sendOperation._index    = index;
            connection._remote                  = remote;
            connection._socket                  = socket;
            connection._lastReceiveNanoseconds  = MonotonicClock::nowNanoseconds();
            connection._deadlineNanoseconds     = connection._lastReceiveNanoseconds + Internal::toNanoseconds( _settings._connectTimeoutSeconds );
            connection._pendingOperationCount   = 1; // 여는 쪽이 든다 — 열림 콜백(수락) · ConnectEx(연결)가 끝나면 놓는다
            connection._closeReason             = StreamCloseReason::None;
            connection._state                   = state;
            connection._bAccepted               = bAccepted ? SW_TRUE : SW_FALSE;
            connection._bReceivePosted          = SW_FALSE;
            connection._bSendPosted             = SW_FALSE;
            connection._bReceivePaused          = SW_FALSE;
            connection._bShutdownSent           = SW_FALSE;
            connection._bRemoteFinished         = SW_FALSE;
            _openCount.fetch_add( 1, std::memory_order_relaxed );
            pOutConnection = &connection;
            return StreamConnectionHandle::make( index, connection._generation );
        }

        /** @brief 핸들의 자리입니다(세대는 부르는 쪽이 연결 잠금 안에서 `isLive` 로 본다 — 자리 객체는 지워지지 않는다). */
        IocpConnection* findConnection( StreamConnectionHandle handle ) const
        {
            std::scoped_lock<mutex> lock{ _tableMutex };
            if ( handle.isValid() == false || handle._index >= _listConnection.size() )
                return nullptr;
            return _listConnection[handle._index].get();
        }

        IocpConnection& getConnection( uint32 index ) const
        {
            std::scoped_lock<mutex> lock{ _tableMutex };
            return *_listConnection[index];
        }

        static bool isLive( const IocpConnection& connection, StreamConnectionHandle handle )
        {
            return connection._generation == handle._generation && connection._state != IocpConnectionState::Free && connection._state != IocpConnectionState::Retiring;
        }

        // ---- 연결 잠금 안에서 ----

        void closeSocketLocked( IocpConnection& connection, StreamCloseReason reason, bool bAbortive )
        {
            if ( connection._socket == INVALID_SOCKET )
                return;
            if ( connection._closeReason == StreamCloseReason::None )
                connection._closeReason = reason;
            if ( bAbortive )
                Internal::closeAbortive( connection._socket );
            else
                closesocket( connection._socket );
            connection._socket = INVALID_SOCKET;
            connection._state  = IocpConnectionState::Closed;
            connection._sendQueue.clear();
            // 걸린 작업은 취소 완료로 돌아온다. 하나도 없으면 아무것도 오지 않으니 정리 표를 넣는다 — 닫힘 콜백도 I/O 스레드에서.
            (void)PostQueuedCompletionStatus( _completionPort, connection._receiveOperation._index, Internal::kRetireKey, nullptr );
        }

        void postReceiveLocked( IocpConnection& connection )
        {
            const bool bReceiving = connection._state == IocpConnectionState::Open || connection._state == IocpConnectionState::Closing;
            if ( bReceiving == false || connection._socket == INVALID_SOCKET || connection._bReceivePosted == SW_TRUE || connection._bReceivePaused == SW_TRUE ||
                 connection._bRemoteFinished == SW_TRUE )
                return;
            IocpOperation& operation = connection._receiveOperation;
            std::memset( &operation._overlapped, 0, sizeof( operation._overlapped ) );
            operation._kind = IocpOperationKind::Receive;
            WSABUF buffer{};
            buffer.len  = static_cast<ULONG>( connection._receiveBuffer.size() );
            buffer.buf  = reinterpret_cast<CHAR*>( connection._receiveBuffer.data() );
            DWORD flags = 0;
            if ( WSARecv( connection._socket, &buffer, 1, nullptr, &flags, &operation._overlapped, nullptr ) == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING )
            {
                closeSocketLocked( connection, StreamCloseReason::Reset, true );
                return;
            }
            connection._bReceivePosted = SW_TRUE;
            ++connection._pendingOperationCount;
        }

        void postSendLocked( IocpConnection& connection )
        {
            const bool bSending = connection._state == IocpConnectionState::Open || connection._state == IocpConnectionState::Closing;
            if ( bSending == false || connection._socket == INVALID_SOCKET || connection._bSendPosted == SW_TRUE )
                return;
            if ( connection._sendQueue.isEmpty() )
            {
                if ( connection._state == IocpConnectionState::Closing && connection._bShutdownSent == SW_FALSE )
                {
                    (void)::shutdown( connection._socket, SD_SEND );
                    connection._bShutdownSent = SW_TRUE;
                }
                // 저쪽 FIN 을 이미 받았고 이쪽도 다 보냈다 — 닫는다.
                if ( connection._state == IocpConnectionState::Closing && connection._bRemoteFinished == SW_TRUE )
                    closeSocketLocked( connection, connection._closeReason, false );
                return;
            }
            StreamSendSpan arrSpan[Internal::kMaxSendSpans];
            const int32    spanCount = connection._sendQueue.collectSpans( arrSpan, Internal::kMaxSendSpans, Internal::kMaxSendBytes );
            for ( int32 index = 0; index < spanCount; ++index )
            {
                connection._arrSendBuffer[index].len = static_cast<ULONG>( arrSpan[index]._size );
                connection._arrSendBuffer[index].buf = reinterpret_cast<CHAR*>( const_cast<uint8*>( arrSpan[index]._pData ) );
            }
            IocpOperation& operation = connection._sendOperation;
            std::memset( &operation._overlapped, 0, sizeof( operation._overlapped ) );
            operation._kind = IocpOperationKind::Send;
            if ( WSASend( connection._socket, connection._arrSendBuffer, static_cast<DWORD>( spanCount ), nullptr, 0, &operation._overlapped, nullptr ) == SOCKET_ERROR &&
                 WSAGetLastError() != WSA_IO_PENDING )
            {
                closeSocketLocked( connection, StreamCloseReason::Reset, true );
                return;
            }
            connection._bSendPosted = SW_TRUE;
            ++connection._pendingOperationCount;
        }

        /** @brief 소켓이 닫혔고 걸린 작업이 없으면 정리 상태로 바꾸고 true — 부르는 쪽이 잠금을 푼 뒤 `retire` 한다. */
        static bool takeRetireLocked( IocpConnection& connection, StreamConnectionHandle& outHandle, StreamCloseReason& outReason, uint32 index )
        {
            if ( connection._state != IocpConnectionState::Closed || connection._pendingOperationCount != 0 )
                return false;
            connection._state = IocpConnectionState::Retiring;
            outHandle         = StreamConnectionHandle::make( index, connection._generation );
            outReason         = connection._closeReason;
            return true;
        }

        void retire( IocpConnection& connection, StreamConnectionHandle handle, StreamCloseReason reason )
        {
            _pHandler->onStreamClosed( handle, reason );
            std::scoped_lock<mutex> lock{ _tableMutex };
            {
                std::scoped_lock<mutex> connectionLock{ connection._mutex };
                ++connection._generation;
                if ( connection._generation == 0 )
                    connection._generation = 1;
                connection._state = IocpConnectionState::Free;
            }
            _listFreeIndex.push_back( handle._index );
            _openCount.fetch_sub( 1, std::memory_order_relaxed );
            _closedCount.fetch_add( 1, std::memory_order_relaxed );
        }

        /** @brief 작업 하나(또는 콜백 중 표시)를 놓고, 마지막이면 닫힘을 냅니다. */
        void finishOperation( IocpConnection& connection, uint32 index )
        {
            StreamConnectionHandle handle{};
            StreamCloseReason      reason  = StreamCloseReason::None;
            bool                   bRetire = false;
            {
                std::scoped_lock<mutex> lock{ connection._mutex };
                --connection._pendingOperationCount;
                bRetire = takeRetireLocked( connection, handle, reason, index );
            }
            if ( bRetire )
                retire( connection, handle, reason );
        }

        // ---- 완료 ----

        void handleReceive( uint32 index, bool bSucceeded, DWORD transferred )
        {
            IocpConnection&        connection = getConnection( index );
            StreamConnectionHandle handle{};
            bool                   bDeliver = false;
            {
                std::scoped_lock<mutex> lock{ connection._mutex };
                connection._bReceivePosted = SW_FALSE;
                handle                     = StreamConnectionHandle::make( index, connection._generation );
                if ( bSucceeded == false )
                {
                    closeSocketLocked( connection, StreamCloseReason::Reset, true ); // 이미 닫힌 소켓이면(우리가 닫았다) 아무것도 하지 않는다
                }
                else if ( transferred == 0 )
                {
                    // 저쪽 FIN — 이쪽이 닫는 중이 아니었으면 이쪽도 우아하게 닫는다(반쯤 열린 연결은 두지 않는다).
                    connection._bRemoteFinished = SW_TRUE;
                    if ( connection._state == IocpConnectionState::Open )
                    {
                        connection._state               = IocpConnectionState::Closing;
                        connection._closeReason         = StreamCloseReason::RemoteClose;
                        connection._deadlineNanoseconds = MonotonicClock::nowNanoseconds() + Internal::toNanoseconds( _settings._closeLingerSeconds );
                    }
                    if ( connection._bSendPosted == SW_FALSE )
                        postSendLocked( connection ); // 줄이 비었으면 FIN 을 보내고 닫는다
                }
                else
                {
                    bDeliver                           = true;
                    connection._lastReceiveNanoseconds = MonotonicClock::nowNanoseconds();
                    _receivedBytes.fetch_add( transferred, std::memory_order_relaxed );
                    _receiveCallCount.fetch_add( 1, std::memory_order_relaxed );
                }
            }
            if ( bDeliver )
            {
                // 받기는 다시 걸기 전이라 버퍼가 그대로다. 작업 수는 아직 들고 있다 — 콜백 중에 닫혀도 자리가 돌아가지 않는다.
                _pHandler->onStreamReceived( handle, connection._receiveBuffer.data(), static_cast<int32>( transferred ) );
                std::scoped_lock<mutex> lock{ connection._mutex };
                postReceiveLocked( connection );
            }
            finishOperation( connection, index );
        }

        void handleSend( uint32 index, bool bSucceeded, DWORD transferred )
        {
            IocpConnection&        connection = getConnection( index );
            StreamConnectionHandle handle{};
            bool                   bWritable = false;
            {
                std::scoped_lock<mutex> lock{ connection._mutex };
                connection._bSendPosted = SW_FALSE;
                handle                  = StreamConnectionHandle::make( index, connection._generation );
                if ( bSucceeded == false )
                {
                    closeSocketLocked( connection, StreamCloseReason::Reset, true );
                }
                else
                {
                    bWritable = connection._sendQueue.consume( static_cast<int32>( transferred ) );
                    _sentBytes.fetch_add( transferred, std::memory_order_relaxed );
                    _sendCallCount.fetch_add( 1, std::memory_order_relaxed );
                    postSendLocked( connection );
                    bWritable = bWritable && connection._state == IocpConnectionState::Open;
                }
            }
            if ( bWritable )
                _pHandler->onStreamWritable( handle );
            finishOperation( connection, index );
        }

        void handleConnect( uint32 index, bool bSucceeded )
        {
            IocpConnection&        connection = getConnection( index );
            StreamConnectionHandle handle{};
            NetAddress             remote{};
            bool                   bOpened = false;
            {
                std::scoped_lock<mutex> lock{ connection._mutex };
                handle = StreamConnectionHandle::make( index, connection._generation );
                remote = connection._remote;
                if ( bSucceeded && connection._socket != INVALID_SOCKET && connection._state == IocpConnectionState::Connecting )
                {
                    (void)setsockopt( connection._socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0 );
                    Internal::setNoDelay( connection._socket, _settings._bTCPNoDelay == SW_TRUE );
                    connection._state                  = IocpConnectionState::Open;
                    connection._lastReceiveNanoseconds = MonotonicClock::nowNanoseconds();
                    bOpened                            = true;
                }
                else
                {
                    closeSocketLocked( connection, StreamCloseReason::ConnectFailed, true );
                }
            }
            if ( bOpened )
            {
                _connectedCount.fetch_add( 1, std::memory_order_relaxed );
                _pHandler->onStreamOpened( handle, remote, false );
                std::scoped_lock<mutex> lock{ connection._mutex };
                postReceiveLocked( connection );
                postSendLocked( connection ); // 연결 중에 쌓인 것
            }
            finishOperation( connection, index );
        }

        void postAccept( uint32 slotIndex )
        {
            IocpAcceptSlot& slot = _arrAcceptSlot[slotIndex];
            slot._socket         = Internal::openStreamSocket();
            if ( slot._socket == INVALID_SOCKET )
            {
                SW_LOG_ERROR( "WSASocketW failed for an accept slot (%#)", WSAGetLastError() );
                return;
            }
            std::memset( &slot._operation._overlapped, 0, sizeof( slot._operation._overlapped ) );
            slot._operation._kind  = IocpOperationKind::Accept;
            slot._operation._index = slotIndex;
            DWORD bytes            = 0;
            _pendingAcceptCount.fetch_add( 1, std::memory_order_relaxed );
            const BOOL bStarted = _pAcceptEx( _listenSocket, slot._socket, slot._arrAddress, 0, Internal::kAcceptAddressBytes, Internal::kAcceptAddressBytes, &bytes,
                                              &slot._operation._overlapped );
            if ( bStarted == FALSE && WSAGetLastError() != ERROR_IO_PENDING )
            {
                _pendingAcceptCount.fetch_sub( 1, std::memory_order_relaxed );
                closesocket( slot._socket );
                slot._socket = INVALID_SOCKET;
            }
        }

        void handleAccept( uint32 slotIndex, bool bSucceeded )
        {
            IocpAcceptSlot& slot     = _arrAcceptSlot[slotIndex];
            const SOCKET    accepted = slot._socket;
            slot._socket             = INVALID_SOCKET;
            _pendingAcceptCount.fetch_sub( 1, std::memory_order_relaxed );
            const bool bStopping = _bStopping.load( std::memory_order_acquire );
            if ( bSucceeded && bStopping == false && accepted != INVALID_SOCKET )
            {
                (void)setsockopt( accepted, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<const utf8*>( &_listenSocket ), sizeof( _listenSocket ) );
                sockaddr_in remote{};
                int32       length = sizeof( remote );
                (void)getpeername( accepted, reinterpret_cast<sockaddr*>( &remote ), &length );
                const bool bFull = _openCount.load( std::memory_order_relaxed ) >= _settings._maxConnections;
                if ( bFull || CreateIoCompletionPort( reinterpret_cast<HANDLE>( accepted ), _completionPort, Internal::kSocketKey, 0 ) == nullptr )
                {
                    Internal::closeAbortive( accepted );
                }
                else
                {
                    Internal::setNoDelay( accepted, _settings._bTCPNoDelay == SW_TRUE );
                    IocpConnection*              pConnection = nullptr;
                    const NetAddress             address     = Internal::makeNetAddress( remote );
                    const StreamConnectionHandle handle      = allocateConnection( accepted, address, true, IocpConnectionState::Open, pConnection );
                    _acceptedCount.fetch_add( 1, std::memory_order_relaxed );
                    _pHandler->onStreamOpened( handle, address, true ); // 받기는 열림 뒤에 건다 — 열림이 언제나 첫 콜백
                    {
                        std::scoped_lock<mutex> lock{ pConnection->_mutex };
                        postReceiveLocked( *pConnection );
                        postSendLocked( *pConnection );
                    }
                    finishOperation( *pConnection, handle._index );
                }
            }
            else if ( accepted != INVALID_SOCKET )
            {
                closesocket( accepted );
            }
            if ( bStopping == false && _listenSocket != INVALID_SOCKET )
                postAccept( slotIndex );
        }

        void handleRetireProbe( uint32 index )
        {
            IocpConnection&        connection = getConnection( index );
            StreamConnectionHandle handle{};
            StreamCloseReason      reason  = StreamCloseReason::None;
            bool                   bRetire = false;
            {
                std::scoped_lock<mutex> lock{ connection._mutex };
                bRetire = takeRetireLocked( connection, handle, reason, index );
            }
            if ( bRetire )
                retire( connection, handle, reason );
        }

        void sweepIfDue()
        {
            const int64 now      = MonotonicClock::nowNanoseconds();
            int64       expected = _nextSweepNanoseconds.load( std::memory_order_relaxed );
            if ( now < expected || _nextSweepNanoseconds.compare_exchange_strong( expected, now + Internal::kSweepIntervalNanoseconds ) == false )
                return;
            vector<IocpConnection*> listConnection;
            {
                std::scoped_lock<mutex> lock{ _tableMutex };
                listConnection.reserve( _listConnection.size() );
                for ( const unique_ptr<IocpConnection>& connection : _listConnection )
                {
                    listConnection.push_back( connection.get() );
                }
            }
            const int64 idleNanoseconds = Internal::toNanoseconds( _settings._idleTimeoutSeconds );
            for ( IocpConnection* pConnection : listConnection )
            {
                std::scoped_lock<mutex> lock{ pConnection->_mutex };
                switch ( pConnection->_state )
                {
                    case IocpConnectionState::Open:
                    {
                        if ( idleNanoseconds > 0 && now - pConnection->_lastReceiveNanoseconds > idleNanoseconds )
                            closeSocketLocked( *pConnection, StreamCloseReason::IdleTimeout, true );
                        break;
                    }
                    case IocpConnectionState::Connecting:
                    {
                        if ( now > pConnection->_deadlineNanoseconds )
                            closeSocketLocked( *pConnection, StreamCloseReason::ConnectFailed, true );
                        break;
                    }
                    case IocpConnectionState::Closing:
                    {
                        if ( now > pConnection->_deadlineNanoseconds )
                            closeSocketLocked( *pConnection, pConnection->_closeReason, true );
                        break;
                    }
                    case IocpConnectionState::Free:
                    case IocpConnectionState::Closed:
                    case IocpConnectionState::Retiring:
                    {
                        break;
                    }
                }
            }
        }

        /** @brief 완료를 한 묶음 기다려 처리합니다. 처리한 수 — 멈춤 표를 받으면 @p outbStop. */
        int32 runOnce( int32 timeoutMilli, bool& outbStop )
        {
            OVERLAPPED_ENTRY arrEntry[Internal::kCompletionBatch];
            ULONG            removed = 0;
            const BOOL       bResult = GetQueuedCompletionStatusEx( _completionPort, arrEntry, Internal::kCompletionBatch, &removed, static_cast<DWORD>( timeoutMilli ), FALSE );
            int32            handled = 0;
            for ( ULONG index = 0; bResult == TRUE && index < removed; ++index )
            {
                const OVERLAPPED_ENTRY& entry = arrEntry[index];
                if ( entry.lpCompletionKey == Internal::kStopKey )
                {
                    outbStop = true;
                    continue;
                }
                ++handled;
                if ( entry.lpCompletionKey == Internal::kRetireKey )
                {
                    handleRetireProbe( static_cast<uint32>( entry.dwNumberOfBytesTransferred ) );
                    continue;
                }
                IocpOperation* pOperation = reinterpret_cast<IocpOperation*>( entry.lpOverlapped ); // OVERLAPPED 가 맨 앞 칸
                const bool     bSucceeded = entry.lpOverlapped->Internal == 0;                      // NTSTATUS — 0 이 성공(취소 · RST 는 아니다)
                switch ( pOperation->_kind )
                {
                    case IocpOperationKind::Receive:
                    {
                        handleReceive( pOperation->_index, bSucceeded, entry.dwNumberOfBytesTransferred );
                        break;
                    }
                    case IocpOperationKind::Send:
                    {
                        handleSend( pOperation->_index, bSucceeded, entry.dwNumberOfBytesTransferred );
                        break;
                    }
                    case IocpOperationKind::Connect:
                    {
                        handleConnect( pOperation->_index, bSucceeded );
                        break;
                    }
                    case IocpOperationKind::Accept:
                    {
                        handleAccept( pOperation->_index, bSucceeded );
                        break;
                    }
                }
            }
            sweepIfDue();
            return handled;
        }

        void runThread()
        {
            ThreadName::setCurrentThreadName( "StreamIO" );
            bool bStop = false;
            while ( bStop == false && _bExitThreads.load( std::memory_order_acquire ) == false )
            {
                (void)runOnce( Internal::kLoopWaitMilli, bStop );
            }
        }
    };
} // namespace sw

namespace sw
{
    IocpStreamTransport::IocpStreamTransport()
        : _state{ make_unique<State>() }
    {
    }

    IocpStreamTransport::~IocpStreamTransport() { shutdown(); }

    bool IocpStreamTransport::initialize( IStreamHandler* pHandler, const StreamTransportSettings& settings )
    {
        if ( pHandler == nullptr || _state->_bInitialized == SW_TRUE || PlatformSocketUtil::initialize() == false )
            return false;
        const int32 threadCount = settings._ioThreadCount > 0 ? settings._ioThreadCount : 1;
        _state->_completionPort = CreateIoCompletionPort( INVALID_HANDLE_VALUE, nullptr, 0, static_cast<DWORD>( threadCount ) );
        if ( _state->_completionPort == nullptr )
        {
            SW_LOG_ERROR( "CreateIoCompletionPort failed (%#)", GetLastError() );
            return false;
        }
        _state->_pHandler = pHandler;
        _state->_settings = settings;
        _state->_bStopping.store( false, std::memory_order_release );
        _state->_bExitThreads.store( false, std::memory_order_release );
        _state->_bInitialized = SW_TRUE;
        for ( int32 index = 0; index < settings._ioThreadCount; ++index )
        {
            _state->_listThread.emplace_back( &State::runThread, _state.get() );
        }
        return true;
    }

    void IocpStreamTransport::shutdown()
    {
        State& state = *_state;
        if ( state._bInitialized == SW_FALSE )
            return;
        state._bStopping.store( true, std::memory_order_release );
        if ( state._listenSocket != INVALID_SOCKET )
        {
            closesocket( state._listenSocket ); // 걸린 AcceptEx 가 실패로 돌아온다
            state._listenSocket = INVALID_SOCKET;
        }
        {
            std::scoped_lock<mutex> lock{ state._tableMutex };
            for ( const unique_ptr<IocpConnection>& connection : state._listConnection )
            {
                std::scoped_lock<mutex> connectionLock{ connection->_mutex };
                state.closeSocketLocked( *connection, StreamCloseReason::Shutdown, true );
            }
        }
        // 닫힘 콜백이 모두 나가고 걸린 수락이 돌아올 때까지 돈다(스레드가 없으면 여기서).
        const Deadline deadline = Deadline::afterMilliseconds( 5000 );
        while ( ( state._openCount.load() > 0 || state._pendingAcceptCount.load() > 0 ) && deadline.isExpired() == false )
        {
            if ( state._listThread.empty() )
            {
                bool bStop = false;
                (void)state.runOnce( 10, bStop );
            }
            else
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
        }
        if ( state._openCount.load() > 0 || state._pendingAcceptCount.load() > 0 )
            SW_LOG_ERROR( "IocpStreamTransport::shutdown timed out with %# connections and %# accepts outstanding", state._openCount.load(), state._pendingAcceptCount.load() );
        state._bExitThreads.store( true, std::memory_order_release );
        for ( size_t index = 0; index < state._listThread.size(); ++index )
        {
            (void)PostQueuedCompletionStatus( state._completionPort, 0, IocpStreamTransportInternal::kStopKey, nullptr );
        }
        for ( std::thread& thread : state._listThread )
        {
            thread.join();
        }
        state._listThread.clear();
        CloseHandle( state._completionPort );
        state._completionPort = nullptr;
        state._arrAcceptSlot.reset();
        state._listenPort   = 0;
        state._bInitialized = SW_FALSE;
    }

    bool IocpStreamTransport::listen( const NetAddress& bindAddress )
    {
        State& state = *_state;
        if ( state._bInitialized == SW_FALSE || state._listenSocket != INVALID_SOCKET )
            return false;
        const SOCKET listenSocket = IocpStreamTransportInternal::openStreamSocket();
        if ( listenSocket == INVALID_SOCKET )
            return false;
        const BOOL exclusive = TRUE; // 다른 프로세스가 같은 포트를 가로채지 못하게
        (void)setsockopt( listenSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const utf8*>( &exclusive ), sizeof( exclusive ) );
        sockaddr_in local     = IocpStreamTransportInternal::makeNativeAddress( bindAddress );
        int32       length    = sizeof( local );
        const bool  bListened = bind( listenSocket, reinterpret_cast<const sockaddr*>( &local ), sizeof( local ) ) == 0 &&
                               ::listen( listenSocket, state._settings._listenBacklog ) == 0 &&
                               CreateIoCompletionPort( reinterpret_cast<HANDLE>( listenSocket ), state._completionPort, IocpStreamTransportInternal::kSocketKey, 0 ) != nullptr &&
                               getsockname( listenSocket, reinterpret_cast<sockaddr*>( &local ), &length ) == 0;
        state._pAcceptEx = bListened ? IocpStreamTransportInternal::findExtension<LPFN_ACCEPTEX>( listenSocket, WSAID_ACCEPTEX ) : nullptr;
        if ( state._pAcceptEx == nullptr )
        {
            SW_LOG_ERROR( "Could not listen on stream address %# (%#)", bindAddress.toString().c_str(), WSAGetLastError() );
            closesocket( listenSocket );
            return false;
        }
        state._listenSocket  = listenSocket;
        state._listenPort    = ntohs( local.sin_port );
        state._arrAcceptSlot = make_unique<IocpAcceptSlot[]>( IocpStreamTransportInternal::kAcceptPostCount );
        for ( uint32 index = 0; index < static_cast<uint32>( IocpStreamTransportInternal::kAcceptPostCount ); ++index )
        {
            state.postAccept( index );
        }
        return true;
    }

    uint16 IocpStreamTransport::getListenPort() const { return _state->_listenPort; }

    StreamConnectionHandle IocpStreamTransport::connect( const NetAddress& remote )
    {
        State& state = *_state;
        if ( state._bInitialized == SW_FALSE || state._bStopping.load() )
            return StreamConnectionHandle{};
        const SOCKET socket = IocpStreamTransportInternal::openStreamSocket();
        if ( socket == INVALID_SOCKET )
            return StreamConnectionHandle{};
        const sockaddr_in any = IocpStreamTransportInternal::makeNativeAddress( NetAddress{ 0, 0 } );
        if ( bind( socket, reinterpret_cast<const sockaddr*>( &any ), sizeof( any ) ) != 0 ||
             CreateIoCompletionPort( reinterpret_cast<HANDLE>( socket ), state._completionPort, IocpStreamTransportInternal::kSocketKey, 0 ) == nullptr )
        {
            closesocket( socket );
            return StreamConnectionHandle{};
        }
        LPFN_CONNECTEX pConnectEx = state._pConnectEx.load( std::memory_order_acquire );
        if ( pConnectEx == nullptr )
        {
            pConnectEx = IocpStreamTransportInternal::findExtension<LPFN_CONNECTEX>( socket, WSAID_CONNECTEX );
            state._pConnectEx.store( pConnectEx, std::memory_order_release );
        }
        IocpConnection*              pConnection = nullptr;
        const StreamConnectionHandle handle      = state.allocateConnection( socket, remote, false, IocpConnectionState::Connecting, pConnection );
        std::scoped_lock<mutex>      lock{ pConnection->_mutex };
        IocpOperation&               operation = pConnection->_receiveOperation;
        std::memset( &operation._overlapped, 0, sizeof( operation._overlapped ) );
        operation._kind            = IocpOperationKind::Connect;
        const sockaddr_in native   = IocpStreamTransportInternal::makeNativeAddress( remote );
        const BOOL        bStarted = pConnectEx != nullptr &&
                              pConnectEx( socket, reinterpret_cast<const sockaddr*>( &native ), sizeof( native ), nullptr, 0, nullptr, &operation._overlapped ) == TRUE;
        if ( bStarted == FALSE && ( pConnectEx == nullptr || WSAGetLastError() != ERROR_IO_PENDING ) )
        {
            // 바로 실패 — 닫힘은 I/O 스레드에서 내야 하므로 실패한 완료를 손으로 넣는다(여는 쪽의 작업 수를 그 완료가 놓는다).
            operation._overlapped.Internal = static_cast<ULONG_PTR>( 1 );
            (void)PostQueuedCompletionStatus( state._completionPort, 0, IocpStreamTransportInternal::kSocketKey, &operation._overlapped );
        }
        return handle;
    }

    StreamSendResult IocpStreamTransport::send( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        IocpConnection* pConnection = _state->findConnection( handle );
        if ( pConnection == nullptr )
            return StreamSendResult::Closed;
        std::scoped_lock<mutex> lock{ pConnection->_mutex };
        const bool              bAccepting = pConnection->_state == IocpConnectionState::Open || pConnection->_state == IocpConnectionState::Connecting;
        if ( State::isLive( *pConnection, handle ) == false || bAccepting == false )
            return StreamSendResult::Closed;
        const StreamSendResult result = pConnection->_sendQueue.append( pData, size );
        if ( result != StreamSendResult::QueueFull )
            _state->postSendLocked( *pConnection );
        return result;
    }

    void IocpStreamTransport::close( StreamConnectionHandle handle, StreamCloseMode mode )
    {
        IocpConnection* pConnection = _state->findConnection( handle );
        if ( pConnection == nullptr )
            return;
        std::scoped_lock<mutex> lock{ pConnection->_mutex };
        if ( State::isLive( *pConnection, handle ) == false )
            return;
        if ( mode == StreamCloseMode::Abort || pConnection->_state == IocpConnectionState::Connecting )
        {
            _state->closeSocketLocked( *pConnection, StreamCloseReason::LocalClose, true );
            return;
        }
        if ( pConnection->_state != IocpConnectionState::Open )
            return;
        pConnection->_state               = IocpConnectionState::Closing;
        pConnection->_closeReason         = StreamCloseReason::LocalClose;
        pConnection->_deadlineNanoseconds = MonotonicClock::nowNanoseconds() + IocpStreamTransportInternal::toNanoseconds( _state->_settings._closeLingerSeconds );
        pConnection->_bReceivePaused      = SW_FALSE; // 저쪽 FIN 을 받아야 끝난다
        _state->postReceiveLocked( *pConnection );
        _state->postSendLocked( *pConnection );
    }

    void IocpStreamTransport::setReceivePaused( StreamConnectionHandle handle, bool bPaused )
    {
        IocpConnection* pConnection = _state->findConnection( handle );
        if ( pConnection == nullptr )
            return;
        std::scoped_lock<mutex> lock{ pConnection->_mutex };
        if ( State::isLive( *pConnection, handle ) == false || pConnection->_state == IocpConnectionState::Closing )
            return;
        pConnection->_bReceivePaused = bPaused ? SW_TRUE : SW_FALSE;
        if ( bPaused == false )
            _state->postReceiveLocked( *pConnection );
    }

    int32 IocpStreamTransport::pollIO( int32 timeoutMilli )
    {
        if ( _state->_bInitialized == SW_FALSE || _state->_listThread.empty() == false )
            return 0;
        bool bStop = false;
        return _state->runOnce( timeoutMilli, bStop );
    }

    NetAddress IocpStreamTransport::getRemoteAddress( StreamConnectionHandle handle ) const
    {
        IocpConnection* pConnection = _state->findConnection( handle );
        if ( pConnection == nullptr )
            return NetAddress{};
        std::scoped_lock<mutex> lock{ pConnection->_mutex };
        return State::isLive( *pConnection, handle ) ? pConnection->_remote : NetAddress{};
    }

    StreamTransportStats IocpStreamTransport::getStats() const
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
