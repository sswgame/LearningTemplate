#include "pch.h"

#include "Core/Network/Transport/Windows/IocpStreamTransport.h"

#include "Core/Concurrency/OnceInit.h"
#include "Core/Concurrency/StopToken.h"
#include "Core/Concurrency/ThreadName.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/IndexFreeList.h"
#include "Core/Container/vector.h"
#include "Core/Network/Transport/NativeSocketAddress.h"
#include "Core/Network/Transport/PlatformSocketUtil.h"
#include "Core/Network/Transport/StreamSendQueue.h"
#include "Core/Time/MonotonicClock.h"

#include <cstring>
#include <mutex>
#include <thread>
#include <WinSock2.h>
#include <MSWSock.h>
#include <WS2tcpip.h>

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

        /** @brief 요청해 둔 비동기 작업 하나입니다. OVERLAPPED 가 맨 앞이라 완료 통지의 포인터에서 바로 되찾을 수 있습니다. 슬롯은 움직이지 않습니다(연결과 수락 슬롯이 unique_ptr 이거나 고정 배열입니다). */
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
            Closing, ///< 우아한 종료 중이다. 큐를 비우고 FIN 을 보낸 뒤 상대의 FIN 을 기다린다.
            Closed,  ///< 소켓을 닫았다. 걸린 작업이 돌아오기를 기다린다.
            Retiring ///< 닫힘 콜백을 내는 중이다(한 번만 낸다).
        };

        struct IocpStreamTransportInternal
        {
            static constexpr ULONG_PTR kSocketKey                = 1;
            static constexpr ULONG_PTR kStopKey                  = 2;
            static constexpr ULONG_PTR kRetireKey                = 3; ///< 걸린 작업이 없는 연결을 닫았을 때 쓰는 키다. 바이트 수 필드에 슬롯 번호를 담는다.
            static constexpr int32     kAcceptPostCount          = 16;
            static constexpr int32     kMaxSendSpans             = 16;
            static constexpr int32     kMaxSendBytes             = 256 * 1024;
            static constexpr int32     kCompletionBatch          = 64;
            static constexpr int32     kLoopWaitMilli            = 100;
            static constexpr int64     kSweepIntervalNanoseconds = 100 * constant::time::kNanosecondsPerMillisecond;
            static constexpr DWORD     kAcceptAddressBytes       = sizeof( sockaddr_in ) + 16;

            /** @brief 확장 함수(AcceptEx, ConnectEx)를 얻습니다. 함수 포인터 변수에 바로 받으므로 객체와 함수 포인터 사이의 캐스트가 없습니다. */
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

            /** @brief RST 로 닫습니다(SO_LINGER 0). 연결 끊기, 시한 초과, 종료에 씁니다. */
            static void closeAbortive( SOCKET socket )
            {
                LINGER linger{};
                linger.l_onoff  = 1;
                linger.l_linger = 0;
                (void)setsockopt( socket, SOL_SOCKET, SO_LINGER, reinterpret_cast<const utf8*>( &linger ), sizeof( linger ) );
                closesocket( socket );
            }
        };

        /**
         * @class IocpConnection
         * @brief 연결 슬롯 하나와 그 슬롯을 바꾸는 일입니다(열기 · 받기/보내기 걸기 · 완료 반영 · 닫기 · 정리 · 시한). 슬롯 객체는 지워지지 않고 세대 번호로 다시 쓴다.
         * @details `…Locked` 멤버는 부르는 쪽이 `getMutex()` 를 잡은 채로 부른다 — 수송 객체가 콜백을 잠금 밖에서 내도록 잠금 범위를 직접 정한다.
         *          닫힘 · 정리 통지는 완료 포트(@p completionPort)로 I/O 스레드에 넘긴다.
         */
        class IocpConnection
        {
        public:
            IocpConnection()
                : _mutex{}
                , _sendQueue{}
                , _receiveBuffer{}
                , _receiveOperation{}
                , _sendOperation{}
                , _arrSendBuffer{}
                , _remote{}
                , _socket{ INVALID_SOCKET }
                , _lastReceiveNanoseconds{ 0 }
                , _deadlineNanoseconds{ 0 }
                , _pendingOperationCount{ 0 }
                , _generation{ 1 }
                , _closeReason{ StreamCloseReason::None }
                , _state{ IocpConnectionState::Free }
                , _bAccepted{ SW_FALSE }
                , _bReceivePosted{ SW_FALSE }
                , _bSendPosted{ SW_FALSE }
                , _bReceivePaused{ SW_FALSE }
                , _bShutdownSent{ SW_FALSE }
                , _bRemoteFinished{ SW_FALSE }
            {
            }

            mutex&                 getMutex() { return _mutex; }
            StreamConnectionHandle makeHandle( uint32 index ) const { return StreamConnectionHandle::make( index, _generation ); }
            const uint8*           getReceiveData() const { return _receiveBuffer.data(); }

            /** @brief 핸들이 지금 이 슬롯의 살아 있는 연결을 가리키는지입니다(잠금 안). */
            bool isLiveLocked( StreamConnectionHandle handle ) const
            {
                return _generation == handle.generation() && _state != IocpConnectionState::Free && _state != IocpConnectionState::Retiring;
            }

            /** @brief 빈 슬롯 @p index 를 소켓 @p socket 의 연결로 엽니다(잠금 안). 여는 쪽이 작업 참조를 하나 잡는다 — 열림 콜백(수락)이나 ConnectEx(연결)가 끝나면 놓는다. */
            void openLocked( uint32 index, SOCKET socket, const NetAddress& remote, bool bAccepted, IocpConnectionState state, const StreamTransportConfig& settings )
            {
                _sendQueue.clear();
                _sendQueue.configure( settings._sendHighWatermarkBytes, settings._sendLowWatermarkBytes, settings._maxQueuedSendBytes );
                _receiveBuffer.resize( static_cast<size_t>( settings._receiveChunkBytes ) );
                _receiveOperation._index = index;
                _sendOperation._index    = index;
                _remote                  = remote;
                _socket                  = socket;
                _lastReceiveNanoseconds  = MonotonicClock::nowNanoseconds();
                _deadlineNanoseconds     = _lastReceiveNanoseconds + MonotonicClock::secondsToNanoseconds( settings._connectTimeoutSeconds );
                _pendingOperationCount   = 1;
                _closeReason             = StreamCloseReason::None;
                _state                   = state;
                _bAccepted               = bAccepted ? SW_TRUE : SW_FALSE;
                _bReceivePosted          = SW_FALSE;
                _bSendPosted             = SW_FALSE;
                _bReceivePaused          = SW_FALSE;
                _bShutdownSent           = SW_FALSE;
                _bRemoteFinished         = SW_FALSE;
            }

            /** @brief ConnectEx 를 걸 작업 칸을 비워 돌려줍니다(받기 칸을 같이 쓴다 — 받기는 연결이 열린 뒤에 건다). */
            IocpOperation& beginConnectOperationLocked()
            {
                std::memset( &_receiveOperation._overlapped, 0, sizeof( _receiveOperation._overlapped ) );
                _receiveOperation._kind = IocpOperationKind::Connect;
                return _receiveOperation;
            }

            void closeSocketLocked( StreamCloseReason reason, bool bAbortive, HANDLE completionPort )
            {
                if ( _socket == INVALID_SOCKET )
                    return;
                if ( _closeReason == StreamCloseReason::None )
                    _closeReason = reason;
                if ( bAbortive )
                    IocpStreamTransportInternal::closeAbortive( _socket );
                else
                    closesocket( _socket );
                _socket = INVALID_SOCKET;
                _state  = IocpConnectionState::Closed;
                _sendQueue.clear();
                // 걸린 작업은 취소 완료로 돌아온다. 하나도 없으면 아무것도 오지 않으므로 정리 키를 넣는다. 닫힘 콜백도 I/O 스레드에서 낸다.
                (void)PostQueuedCompletionStatus( completionPort, _receiveOperation._index, IocpStreamTransportInternal::kRetireKey, nullptr );
            }

            void postReceiveLocked( HANDLE completionPort )
            {
                const bool bReceiving         = _state == IocpConnectionState::Open || _state == IocpConnectionState::Closing;
                const bool bCannotPostReceive = bReceiving == false || _socket == INVALID_SOCKET || _bReceivePosted == SW_TRUE || _bReceivePaused == SW_TRUE || _bRemoteFinished == SW_TRUE;
                if ( bCannotPostReceive )
                    return;
                IocpOperation& operation = _receiveOperation;
                std::memset( &operation._overlapped, 0, sizeof( operation._overlapped ) );
                operation._kind = IocpOperationKind::Receive;
                WSABUF buffer{};
                buffer.len  = static_cast<ULONG>( _receiveBuffer.size() );
                buffer.buf  = reinterpret_cast<CHAR*>( _receiveBuffer.data() );
                DWORD flags = 0;
                if ( WSARecv( _socket, &buffer, 1, nullptr, &flags, &operation._overlapped, nullptr ) == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING )
                {
                    closeSocketLocked( StreamCloseReason::Reset, true, completionPort );
                    return;
                }
                _bReceivePosted = SW_TRUE;
                ++_pendingOperationCount;
            }

            void postSendLocked( HANDLE completionPort )
            {
                const bool bSending        = _state == IocpConnectionState::Open || _state == IocpConnectionState::Closing;
                const bool bCannotPostSend = bSending == false || _socket == INVALID_SOCKET || _bSendPosted == SW_TRUE;
                if ( bCannotPostSend )
                    return;
                if ( _sendQueue.isEmpty() )
                {
                    if ( _state == IocpConnectionState::Closing && _bShutdownSent == SW_FALSE )
                    {
                        (void)::shutdown( _socket, SD_SEND );
                        _bShutdownSent = SW_TRUE;
                    }
                    // 상대의 FIN 을 이미 받았고 이쪽도 다 보냈으므로 닫는다.
                    if ( _state == IocpConnectionState::Closing && _bRemoteFinished == SW_TRUE )
                        closeSocketLocked( _closeReason, false, completionPort );
                    return;
                }
                StreamSendSpan arrSpan[IocpStreamTransportInternal::kMaxSendSpans];
                const int32    spanCount = _sendQueue.collectSpans( arrSpan, IocpStreamTransportInternal::kMaxSendSpans, IocpStreamTransportInternal::kMaxSendBytes );
                for ( int32 index = 0; index < spanCount; ++index )
                {
                    _arrSendBuffer[index].len = static_cast<ULONG>( arrSpan[index]._size );
                    _arrSendBuffer[index].buf = reinterpret_cast<CHAR*>( const_cast<uint8*>( arrSpan[index]._pData ) );
                }
                IocpOperation& operation = _sendOperation;
                std::memset( &operation._overlapped, 0, sizeof( operation._overlapped ) );
                operation._kind = IocpOperationKind::Send;
                if ( WSASend( _socket, _arrSendBuffer, static_cast<DWORD>( spanCount ), nullptr, 0, &operation._overlapped, nullptr ) == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING )
                {
                    closeSocketLocked( StreamCloseReason::Reset, true, completionPort );
                    return;
                }
                _bSendPosted = SW_TRUE;
                ++_pendingOperationCount;
            }

            /** @brief 작업 참조 하나를 놓습니다(잠금 안). 마지막이었는지는 `takeRetireLocked` 가 본다. */
            void releaseOperationLocked() { --_pendingOperationCount; }

            /** @brief 소켓이 닫혔고 걸린 작업이 없으면 정리 상태로 바꾸고 true 를 반환합니다. 부르는 쪽이 잠금을 푼 뒤 닫힘을 알리고 `freeLocked` 해야 합니다. */
            bool takeRetireLocked( uint32 index, StreamConnectionHandle& outHandle, StreamCloseReason& outReason )
            {
                if ( _state != IocpConnectionState::Closed || _pendingOperationCount != 0 )
                    return false;
                _state    = IocpConnectionState::Retiring;
                outHandle = makeHandle( index );
                outReason = _closeReason;
                return true;
            }

            /** @brief 슬롯을 비웁니다(잠금 안). 세대를 올려 옛 핸들이 이 슬롯을 다시 가리키지 않게 한다(0 은 무효 핸들이라 건너뛴다). */
            void freeLocked()
            {
                _generation = SlotHandle::nextGeneration( _generation ); // 핸들 표와 같은 세대 규칙(0 은 건너뛴다)
                if ( _generation == 0 )
                    _generation = 1;
                _state = IocpConnectionState::Free;
            }

            /**
             * @brief 받기 완료를 반영합니다(잠금 안). 받은 바이트를 넘길 차례면 true 다 — 부르는 쪽이 잠금 밖에서 `getReceiveData` 를 넘긴 뒤 받기를 다시 건다.
             * @details 0 바이트는 상대의 FIN 이다. 이쪽이 닫는 중이 아니었으면 이쪽도 우아하게 닫는다(반쯤 열린 연결은 두지 않는다).
             */
            bool completeReceiveLocked( bool bSucceeded, DWORD transferred, int64 closeLingerNanoseconds, HANDLE completionPort )
            {
                _bReceivePosted = SW_FALSE;
                if ( bSucceeded == false )
                {
                    closeSocketLocked( StreamCloseReason::Reset, true, completionPort ); // 이미 닫힌 소켓이면(우리가 닫았다) 아무것도 하지 않는다.
                    return false;
                }
                if ( transferred == 0 )
                {
                    _bRemoteFinished = SW_TRUE;
                    if ( _state == IocpConnectionState::Open )
                    {
                        _state               = IocpConnectionState::Closing;
                        _closeReason         = StreamCloseReason::RemoteClose;
                        _deadlineNanoseconds = MonotonicClock::nowNanoseconds() + closeLingerNanoseconds;
                    }
                    if ( _bSendPosted == SW_FALSE )
                        postSendLocked( completionPort ); // 큐가 비었으면 FIN 을 보내고 닫는다.
                    return false;
                }
                _lastReceiveNanoseconds = MonotonicClock::nowNanoseconds();
                return true;
            }

            /** @brief 보내기 완료를 반영하고 다음 보내기를 겁니다(잠금 안). 큐가 낮은 수위 밑으로 내려가 쓰기 가능을 알릴 차례면 true 다. */
            bool completeSendLocked( bool bSucceeded, DWORD transferred, HANDLE completionPort )
            {
                _bSendPosted = SW_FALSE;
                if ( bSucceeded == false )
                {
                    closeSocketLocked( StreamCloseReason::Reset, true, completionPort );
                    return false;
                }
                const bool bWritable = _sendQueue.consume( static_cast<int32>( transferred ) );
                postSendLocked( completionPort );
                return bWritable && _state == IocpConnectionState::Open;
            }

            /** @brief 연결 완료를 반영합니다(잠금 안). 열렸으면 true 이고 @p outRemote 에 상대 주소를 담는다. 실패면 소켓을 닫는다. */
            bool completeConnectLocked( bool bSucceeded, bool bTCPNoDelay, HANDLE completionPort, NetAddress& outRemote )
            {
                outRemote                    = _remote;
                const bool bConnectCompleted = bSucceeded && _socket != INVALID_SOCKET && _state == IocpConnectionState::Connecting;
                if ( bConnectCompleted == false )
                {
                    closeSocketLocked( StreamCloseReason::ConnectFailed, true, completionPort );
                    return false;
                }
                (void)setsockopt( _socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0 );
                IocpStreamTransportInternal::setNoDelay( _socket, bTCPNoDelay );
                _state                  = IocpConnectionState::Open;
                _lastReceiveNanoseconds = MonotonicClock::nowNanoseconds();
                return true;
            }

            /** @brief 받기 쉼 · 연결 시한 · 우아한 종료 상한을 넘었으면 닫습니다(잠금 안, 주기 점검). */
            void closeIfExpiredLocked( int64 now, int64 idleNanoseconds, HANDLE completionPort )
            {
                switch ( _state )
                {
                    case IocpConnectionState::Open:
                    {
                        if ( idleNanoseconds > 0 && now - _lastReceiveNanoseconds > idleNanoseconds )
                            closeSocketLocked( StreamCloseReason::IdleTimeout, true, completionPort );
                        break;
                    }
                    case IocpConnectionState::Connecting:
                    {
                        if ( now > _deadlineNanoseconds )
                            closeSocketLocked( StreamCloseReason::ConnectFailed, true, completionPort );
                        break;
                    }
                    case IocpConnectionState::Closing:
                    {
                        if ( now > _deadlineNanoseconds )
                            closeSocketLocked( _closeReason, true, completionPort );
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

            /** @brief 보낼 바이트를 큐에 넣고 보내기를 겁니다(잠금 안). 살아 있는 열린 · 연결 중 연결이 아니면 닫힘이다. */
            StreamSendResult appendSendLocked( StreamConnectionHandle handle, const uint8* pData, int32 size, HANDLE completionPort )
            {
                const bool bAccepting = _state == IocpConnectionState::Open || _state == IocpConnectionState::Connecting;
                if ( isLiveLocked( handle ) == false || bAccepting == false )
                    return StreamSendResult::Closed;
                const StreamSendResult result = _sendQueue.append( pData, size );
                if ( result != StreamSendResult::QueueFull )
                    postSendLocked( completionPort );
                return result;
            }

            /** @brief 닫기를 요청합니다(잠금 안). 끊기 · 연결 중이면 바로 닫고, 열려 있으면 큐를 비운 뒤 FIN 을 주고받는 우아한 종료로 간다. */
            void requestCloseLocked( StreamConnectionHandle handle, StreamCloseMode mode, int64 closeLingerNanoseconds, HANDLE completionPort )
            {
                if ( isLiveLocked( handle ) == false )
                    return;
                if ( mode == StreamCloseMode::Abort || _state == IocpConnectionState::Connecting )
                {
                    closeSocketLocked( StreamCloseReason::LocalClose, true, completionPort );
                    return;
                }
                if ( _state != IocpConnectionState::Open )
                    return;
                _state               = IocpConnectionState::Closing;
                _closeReason         = StreamCloseReason::LocalClose;
                _deadlineNanoseconds = MonotonicClock::nowNanoseconds() + closeLingerNanoseconds;
                _bReceivePaused      = SW_FALSE; // 상대의 FIN 을 받아야 끝난다.
                postReceiveLocked( completionPort );
                postSendLocked( completionPort );
            }

            void setReceivePausedLocked( StreamConnectionHandle handle, bool bPaused, HANDLE completionPort )
            {
                if ( isLiveLocked( handle ) == false || _state == IocpConnectionState::Closing )
                    return;
                _bReceivePaused = bPaused ? SW_TRUE : SW_FALSE;
                if ( bPaused == false )
                    postReceiveLocked( completionPort );
            }

            NetAddress findRemoteLocked( StreamConnectionHandle handle ) const { return isLiveLocked( handle ) ? _remote : NetAddress{}; }

        private:
            mutex               _mutex;
            StreamSendQueue     _sendQueue;
            vector<uint8>       _receiveBuffer;
            IocpOperation       _receiveOperation; ///< 연결 중에는 ConnectEx 가 같은 슬롯을 쓴다(받기는 연결이 열린 뒤에 건다).
            IocpOperation       _sendOperation;
            WSABUF              _arrSendBuffer[IocpStreamTransportInternal::kMaxSendSpans];
            NetAddress          _remote;
            SOCKET              _socket;
            int64               _lastReceiveNanoseconds;
            int64               _deadlineNanoseconds; ///< 연결 시한과 우아한 종료 상한.
            int32               _pendingOperationCount;
            uint32              _generation;
            StreamCloseReason   _closeReason;
            IocpConnectionState _state;
            uint8               _bAccepted       : 1;
            uint8               _bReceivePosted  : 1;
            uint8               _bSendPosted     : 1;
            uint8               _bReceivePaused  : 1;
            uint8               _bShutdownSent   : 1;
            uint8               _bRemoteFinished : 1;
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

        mutable mutex                                      _tableMutex;
        vector<unique_ptr<IocpConnection>> _listConnection SW_GUARDED_BY( _tableMutex );
        IndexFreeList _freeSlot                            SW_GUARDED_BY( _tableMutex );
        unique_ptr<IocpAcceptSlot[]>                       _arrAcceptSlot;
        vector<sw::thread>                                 _listThread;
        OnceInit<StreamTransportConfig>                    _settings; ///< initialize 가 I/O 스레드를 띄우기 전에 한 번 정한다(shutdown 이 비운다).
        OnceInit<IStreamHandler*>                          _pHandler; ///< 위와 같다.
        HANDLE                                             _completionPort{ nullptr };
        SOCKET                                             _listenSocket{ INVALID_SOCKET };
        LPFN_ACCEPTEX                                      _pAcceptEx{ nullptr };
        atomic<LPFN_CONNECTEX>                             _pConnectEx{ nullptr };
        atomic<int64>                                      _nextSweepNanoseconds{ 0 };
        atomic<int32>                                      _pendingAcceptCount{ 0 };
        atomic<int32>                                      _openCount{ 0 };
        atomic<uint64>                                     _acceptedCount{ 0 };
        atomic<uint64>                                     _connectedCount{ 0 };
        atomic<uint64>                                     _closedCount{ 0 };
        atomic<uint64>                                     _receivedBytes{ 0 };
        atomic<uint64>                                     _sentBytes{ 0 };
        atomic<uint64>                                     _receiveCallCount{ 0 };
        atomic<uint64>                                     _sendCallCount{ 0 };
        StopSource                                         _stop;
        atomic<bool>                                       _bExitThreads{ false }; ///< I/O 스레드가 끝난다. 종료 키는 깨우기만 한다(한 배치에서 종료 키 여럿을 한 스레드가 가져갈 수 있다).
        uint16                                             _listenPort{ 0 };
        uint8                                              _bInitialized{ SW_FALSE };

        // ---- 슬롯 ----

        StreamConnectionHandle allocateConnection( SOCKET socket, const NetAddress& remote, bool bAccepted, IocpConnectionState state, IocpConnection*& pOutConnection )
        {
            ScopedLock<mutex> lock{ _tableMutex };
            uint32            index = 0;
            if ( _freeSlot.tryPop( index ) == false )
            {
                index = static_cast<uint32>( _listConnection.size() );
                _listConnection.push_back( make_unique<IocpConnection>() );
            }
            IocpConnection&   connection = *_listConnection[index];
            ScopedLock<mutex> connectionLock{ connection.getMutex() };
            connection.openLocked( index, socket, remote, bAccepted, state, _settings.get() );
            _openCount.fetch_add( 1, std::memory_order_relaxed );
            pOutConnection = &connection;
            return connection.makeHandle( index );
        }

        /** @brief 핸들의 슬롯을 반환합니다. 세대는 부르는 쪽이 연결 잠금 안에서 `isLive` 로 확인합니다. 슬롯 객체는 지워지지 않습니다. */
        IocpConnection* findConnection( StreamConnectionHandle handle ) const
        {
            ScopedLock<mutex> lock{ _tableMutex };
            if ( handle.isValid() == false || handle.index() >= _listConnection.size() )
                return nullptr;
            return _listConnection[handle.index()].get();
        }

        IocpConnection& getConnection( uint32 index ) const
        {
            ScopedLock<mutex> lock{ _tableMutex };
            return *_listConnection[index];
        }

        void retire( IocpConnection& connection, StreamConnectionHandle handle, StreamCloseReason reason )
        {
            _pHandler->onStreamClosed( handle, reason );
            ScopedLock<mutex> lock{ _tableMutex };
            {
                ScopedLock<mutex> connectionLock{ connection.getMutex() };
                connection.freeLocked();
            }
            _freeSlot.push( handle.index() );
            _openCount.fetch_sub( 1, std::memory_order_relaxed );
            _closedCount.fetch_add( 1, std::memory_order_relaxed );
        }

        /** @brief 작업 하나(또는 콜백 중 표시)를 놓고, 마지막이면 닫힘을 알립니다. */
        void finishOperation( IocpConnection& connection, uint32 index )
        {
            StreamConnectionHandle handle{};
            StreamCloseReason      reason  = StreamCloseReason::None;
            bool                   bRetire = false;
            {
                ScopedLock<mutex> lock{ connection.getMutex() };
                connection.releaseOperationLocked();
                bRetire = connection.takeRetireLocked( index, handle, reason );
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
                ScopedLock<mutex> lock{ connection.getMutex() };
                handle   = connection.makeHandle( index );
                bDeliver = connection.completeReceiveLocked( bSucceeded, transferred, MonotonicClock::secondsToNanoseconds( _settings->_closeLingerSeconds ), _completionPort );
            }
            if ( bDeliver )
            {
                _receivedBytes.fetch_add( transferred, std::memory_order_relaxed );
                _receiveCallCount.fetch_add( 1, std::memory_order_relaxed );
                // 받기는 다시 걸기 전이라 버퍼가 그대로다. 작업 수는 아직 유지한다. 콜백 중에 닫혀도 슬롯이 반환되지 않는다.
                _pHandler->onStreamReceived( handle, connection.getReceiveData(), static_cast<int32>( transferred ) );
                ScopedLock<mutex> lock{ connection.getMutex() };
                connection.postReceiveLocked( _completionPort );
            }
            finishOperation( connection, index );
        }

        void handleSend( uint32 index, bool bSucceeded, DWORD transferred )
        {
            IocpConnection&        connection = getConnection( index );
            StreamConnectionHandle handle{};
            bool                   bWritable = false;
            {
                ScopedLock<mutex> lock{ connection.getMutex() };
                handle    = connection.makeHandle( index );
                bWritable = connection.completeSendLocked( bSucceeded, transferred, _completionPort );
            }
            if ( bSucceeded )
            {
                _sentBytes.fetch_add( transferred, std::memory_order_relaxed );
                _sendCallCount.fetch_add( 1, std::memory_order_relaxed );
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
                ScopedLock<mutex> lock{ connection.getMutex() };
                handle  = connection.makeHandle( index );
                bOpened = connection.completeConnectLocked( bSucceeded, _settings->_bTCPNoDelay == SW_TRUE, _completionPort, remote );
            }
            if ( bOpened )
            {
                _connectedCount.fetch_add( 1, std::memory_order_relaxed );
                _pHandler->onStreamOpened( handle, remote, false );
                ScopedLock<mutex> lock{ connection.getMutex() };
                connection.postReceiveLocked( _completionPort );
                connection.postSendLocked( _completionPort ); // 연결 중에 쌓인 것을 보낸다.
            }
            finishOperation( connection, index );
        }

        void postAccept( uint32 slotIndex )
        {
            IocpAcceptSlot& slot = _arrAcceptSlot[slotIndex];
            slot._socket         = Internal::openStreamSocket();
            if ( slot._socket == INVALID_SOCKET )
            {
                SW_LOG_ERROR_ID( "IocpStreamTransport.WSASocketWFailedAcceptSlot", "WSASocketW failed for an accept slot (%#)", WSAGetLastError() );
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
            const bool bStopping     = _stop.isStopRequested();
            const bool bAcceptUsable = bSucceeded && bStopping == false && accepted != INVALID_SOCKET;
            if ( bAcceptUsable )
            {
                (void)setsockopt( accepted, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<const utf8*>( &_listenSocket ), sizeof( _listenSocket ) );
                sockaddr_in remote{};
                int32       length = sizeof( remote );
                (void)getpeername( accepted, reinterpret_cast<sockaddr*>( &remote ), &length );
                const bool bFull = _openCount.load( std::memory_order_relaxed ) >= _settings->_maxConnections;
                if ( bFull || CreateIoCompletionPort( reinterpret_cast<HANDLE>( accepted ), _completionPort, Internal::kSocketKey, 0 ) == nullptr )
                {
                    Internal::closeAbortive( accepted );
                }
                else
                {
                    Internal::setNoDelay( accepted, _settings->_bTCPNoDelay == SW_TRUE );
                    IocpConnection*              pConnection = nullptr;
                    const NetAddress             address     = NativeSocketAddress::fromNative( remote );
                    const StreamConnectionHandle handle      = allocateConnection( accepted, address, true, IocpConnectionState::Open, pConnection );
                    _acceptedCount.fetch_add( 1, std::memory_order_relaxed );
                    _pHandler->onStreamOpened( handle, address, true ); // 받기는 열림 뒤에 건다. 열림이 항상 첫 콜백이어야 하기 때문이다.
                    {
                        ScopedLock<mutex> lock{ pConnection->getMutex() };
                        pConnection->postReceiveLocked( _completionPort );
                        pConnection->postSendLocked( _completionPort );
                    }
                    finishOperation( *pConnection, handle.index() );
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
                ScopedLock<mutex> lock{ connection.getMutex() };
                bRetire = connection.takeRetireLocked( index, handle, reason );
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
                ScopedLock<mutex> lock{ _tableMutex };
                listConnection.reserve( _listConnection.size() );
                for ( const unique_ptr<IocpConnection>& connection : _listConnection )
                {
                    listConnection.push_back( connection.get() );
                }
            }
            const int64 idleNanoseconds = MonotonicClock::secondsToNanoseconds( _settings->_idleTimeoutSeconds );
            for ( IocpConnection* pConnection : listConnection )
            {
                ScopedLock<mutex> lock{ pConnection->getMutex() };
                pConnection->closeIfExpiredLocked( now, idleNanoseconds, _completionPort );
            }
        }

        /** @brief 완료를 한 배치 기다려 처리합니다. 반환값은 처리한 수이고, 종료 키를 받으면 @p outbStop 이 켜집니다. */
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
                IocpOperation* pOperation = reinterpret_cast<IocpOperation*>( entry.lpOverlapped ); // OVERLAPPED 가 맨 앞 필드다.
                const bool     bSucceeded = entry.lpOverlapped->Internal == 0;                      // NTSTATUS 이며 0이 성공이다(취소와 RST 는 성공이 아니다).
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

    IocpStreamTransport::~IocpStreamTransport()
    {
        shutdown();
    }

    bool IocpStreamTransport::initialize( IStreamHandler* pHandler, const StreamTransportConfig& settings )
    {
        const bool bCannotInitialize = pHandler == nullptr || _state->_bInitialized == SW_TRUE || PlatformSocketUtil::initialize() == false;
        if ( bCannotInitialize )
            return false;
        const int32 threadCount = settings._ioThreadCount > 0 ? settings._ioThreadCount : 1;
        _state->_completionPort = CreateIoCompletionPort( INVALID_HANDLE_VALUE, nullptr, 0, static_cast<DWORD>( threadCount ) );
        if ( _state->_completionPort == nullptr )
        {
            SW_LOG_ERROR_ID( "IocpStreamTransport.CreateIoCompletionPortFailed", "CreateIoCompletionPort failed (%#)", GetLastError() );
            return false;
        }
        _state->_pHandler.set( pHandler );
        _state->_settings.set( settings );
        _state->_stop = StopSource{};
        _state->_bExitThreads.store( false, std::memory_order_release );
        _state->_bInitialized = SW_TRUE;
        for ( int32 index = 0; index < settings._ioThreadCount; ++index )
        {
            _state->_listThread.emplace_back( ThreadCreationKey{}, &State::runThread, _state.get() );
        }
        return true;
    }

    void IocpStreamTransport::shutdown()
    {
        State& state = *_state;
        if ( state._bInitialized == SW_FALSE )
            return;
        state._stop.requestStop();
        if ( state._listenSocket != INVALID_SOCKET )
        {
            closesocket( state._listenSocket ); // 걸린 AcceptEx 가 실패로 돌아온다.
            state._listenSocket = INVALID_SOCKET;
        }
        {
            ScopedLock<mutex> lock{ state._tableMutex };
            for ( const unique_ptr<IocpConnection>& connection : state._listConnection )
            {
                ScopedLock<mutex> connectionLock{ connection->getMutex() };
                connection->closeSocketLocked( StreamCloseReason::Shutdown, true, state._completionPort );
            }
        }
        // 닫힘 콜백이 모두 나가고 걸린 수락이 돌아올 때까지 돈다(스레드가 없으면 여기서 돈다).
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
                ThreadUtil::sleepForMilliseconds( 1 );
            }
        }
        if ( state._openCount.load() > 0 || state._pendingAcceptCount.load() > 0 )
            SW_LOG_ERROR_ID( "IocpStreamTransport.IocpStreamTransportShutdownTimedOut", "IocpStreamTransport::shutdown timed out with %# connections and %# accepts outstanding", state._openCount.load(), state._pendingAcceptCount.load() );
        state._bExitThreads.store( true, std::memory_order_release );
        for ( size_t index = 0; index < state._listThread.size(); ++index )
        {
            (void)PostQueuedCompletionStatus( state._completionPort, 0, IocpStreamTransportInternal::kStopKey, nullptr );
        }
        for ( sw::thread& thread : state._listThread )
        {
            thread.join();
        }
        state._listThread.clear();
        CloseHandle( state._completionPort );
        state._completionPort = nullptr;
        state._arrAcceptSlot.reset();
        state._listenPort   = 0;
        state._bInitialized = SW_FALSE;
        state._settings.reset(); // I/O 스레드를 모두 합류한 뒤 — 다시 initialize 할 수 있게 비운다
        state._pHandler.reset();
    }

    bool IocpStreamTransport::listen( const NetAddress& bindAddress )
    {
        State& state = *_state;
        if ( state._bInitialized == SW_FALSE || state._listenSocket != INVALID_SOCKET )
            return false;
        const SOCKET listenSocket = IocpStreamTransportInternal::openStreamSocket();
        if ( listenSocket == INVALID_SOCKET )
            return false;
        const BOOL exclusive = TRUE; // 다른 프로세스가 같은 포트를 가로채지 못하게 한다.
        (void)setsockopt( listenSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const utf8*>( &exclusive ), sizeof( exclusive ) );
        sockaddr_in local     = NativeSocketAddress::toNative( bindAddress );
        int32       length    = sizeof( local );
        const bool  bListened = bind( listenSocket, reinterpret_cast<const sockaddr*>( &local ), sizeof( local ) ) == 0 &&
                               ::listen( listenSocket, state._settings->_listenBacklog ) == 0 &&
                               CreateIoCompletionPort( reinterpret_cast<HANDLE>( listenSocket ), state._completionPort, IocpStreamTransportInternal::kSocketKey, 0 ) != nullptr &&
                               getsockname( listenSocket, reinterpret_cast<sockaddr*>( &local ), &length ) == 0;
        state._pAcceptEx = bListened ? IocpStreamTransportInternal::findExtension<LPFN_ACCEPTEX>( listenSocket, WSAID_ACCEPTEX ) : nullptr;
        if ( state._pAcceptEx == nullptr )
        {
            SW_LOG_ERROR_ID( "IocpStreamTransport.CouldNotListenStream", "Could not listen on stream address %# (%#)", bindAddress.toString().c_str(), WSAGetLastError() );
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

    uint16 IocpStreamTransport::getListenPort() const
    {
        return _state->_listenPort;
    }

    StreamConnectionHandle IocpStreamTransport::connect( const NetAddress& remote )
    {
        State& state = *_state;
        if ( state._bInitialized == SW_FALSE || state._stop.isStopRequested() )
            return StreamConnectionHandle{};
        const SOCKET socket = IocpStreamTransportInternal::openStreamSocket();
        if ( socket == INVALID_SOCKET )
            return StreamConnectionHandle{};
        const sockaddr_in any = NativeSocketAddress::toNative( NetAddress{ 0, 0 } );
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
        ScopedLock<mutex>            lock{ pConnection->getMutex() };
        IocpOperation&               operation = pConnection->beginConnectOperationLocked();
        const sockaddr_in            native    = NativeSocketAddress::toNative( remote );
        const BOOL                   bStarted  = pConnectEx != nullptr &&
                              pConnectEx( socket, reinterpret_cast<const sockaddr*>( &native ), sizeof( native ), nullptr, 0, nullptr, &operation._overlapped ) == TRUE;
        if ( bStarted == FALSE && ( pConnectEx == nullptr || WSAGetLastError() != ERROR_IO_PENDING ) )
        {
            // 바로 실패했다. 닫힘은 I/O 스레드에서 내야 하므로 실패한 완료를 직접 넣는다(여는 쪽의 작업 수를 그 완료가 놓는다).
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
        ScopedLock<mutex> lock{ pConnection->getMutex() };
        return pConnection->appendSendLocked( handle, pData, size, _state->_completionPort );
    }

    void IocpStreamTransport::close( StreamConnectionHandle handle, StreamCloseMode mode )
    {
        IocpConnection* pConnection = _state->findConnection( handle );
        if ( pConnection == nullptr )
            return;
        ScopedLock<mutex> lock{ pConnection->getMutex() };
        pConnection->requestCloseLocked( handle, mode, MonotonicClock::secondsToNanoseconds( _state->_settings->_closeLingerSeconds ), _state->_completionPort );
    }

    void IocpStreamTransport::setReceivePaused( StreamConnectionHandle handle, bool bPaused )
    {
        IocpConnection* pConnection = _state->findConnection( handle );
        if ( pConnection == nullptr )
            return;
        ScopedLock<mutex> lock{ pConnection->getMutex() };
        pConnection->setReceivePausedLocked( handle, bPaused, _state->_completionPort );
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
        ScopedLock<mutex> lock{ pConnection->getMutex() };
        return pConnection->findRemoteLocked( handle );
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
