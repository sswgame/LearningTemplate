#include "pch.h"

#include "Core/Network/Message/StreamMessageEndpoint.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Time/MonotonicClock.h"

#include <cstring>
#include <mutex>

SW_LOG_CALLER( "StreamMessageEndpoint" );

namespace sw
{
    namespace
    {
        enum class EndpointEventKind : uint8
        {
            Opened = 0,
            Frame,
            Closed
        };

        struct StreamMessageEndpointInternal
        {
            static constexpr int32 kPingBodySize = 8;

            static void writeInt64( uint8* pOut, int64 value )
            {
                for ( int32 index = 0; index < 8; ++index )
                    pOut[index] = static_cast<uint8>( static_cast<uint64>( value ) >> ( index * 8 ) );
            }
            static int64 readInt64( const uint8* pData )
            {
                uint64 value = 0;
                for ( int32 index = 0; index < 8; ++index )
                    value |= static_cast<uint64>( pData[index] ) << ( index * 8 );
                return static_cast<int64>( value );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /** @brief 끝점이 보는 연결 하나 — I/O 스레드가 열고 닫고, 보내는 스레드는 shared_ptr 로 붙든다(닫힌 뒤 지워져도 안전). */
    struct StreamMessageEndpoint::Connection
    {
        mutex                   _mutex; ///< 해독기 · 보낼 프레임 버퍼 · TLS 세션
        StreamFrameDecoder      _decoder;
        vector<uint8>           _frameScratch{};      ///< 보낼 프레임을 짓는 자리
        unique_ptr<ITlsSession> _tlsSession{};        ///< 있으면 오가는 모든 바이트가 이것을 지난다
        vector<uint8>           _plainScratch{};      ///< 세션이 푼 평문
        vector<uint8>           _cipherScratch{};     ///< 세션이 내놓은 보낼 암호문
        vector<uint8>           _compressScratch{};   ///< 보낼 몸의 압축 봉투
        vector<uint8>           _decompressScratch{}; ///< 받은 봉투를 푼 몸
        NetAddress              _remote{};
        StreamConnectionHandle  _handle{};
        atomic<int64>           _roundTripNanoseconds{ -1 };
        atomic<int32>           _pendingBytes{ 0 };                      ///< 줄에 쌓였지만 아직 pump 가 넘기지 않은 몸 바이트
        StreamCloseReason       _errorReason{ StreamCloseReason::None }; ///< 끝점이 끊은 까닭(ProtocolError · SendQueueOverflow · SecurityFailure) — 전송의 까닭보다 앞선다
        uint8                   _bAccepted{ SW_FALSE };
        uint8                   _bOpenAnnounced{ SW_FALSE };
        uint8                   _bReceivePaused{ SW_FALSE };

        explicit Connection( int32 maxBodySize )
            : _decoder{ maxBodySize }
        {
        }
    };

    /** @brief pump 로 넘길 사건 하나 — 프레임 몸은 아레나의 [_offset, _offset + _size). */
    struct StreamMessageEndpoint::Event
    {
        NetAddress             _remote{};
        StreamConnectionHandle _handle{};
        int32                  _offset{ 0 };
        int32                  _size{ 0 };
        StreamCloseReason      _reason{ StreamCloseReason::None };
        StreamFrameKind        _frameKind{ StreamFrameKind::Message };
        EndpointEventKind      _kind{ EndpointEventKind::Opened };
        uint8                  _bAccepted{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    StreamMessageEndpoint::StreamMessageEndpoint()
        : _tableMutex{}
        , _mapConnection{}
        , _inboxMutex{}
        , _listInbox{}
        , _inboxBytes{}
        , _listPumping{}
        , _pumpingBytes{}
        , _mapLastPingTime{}
        , _settings{}
        , _pTransport{ nullptr }
    {
    }

    StreamMessageEndpoint::~StreamMessageEndpoint() = default;

    bool StreamMessageEndpoint::initialize( IStreamTransport* pTransport, const StreamEndpointSettings& settings )
    {
        if ( pTransport == nullptr )
            return false;
        _pTransport = pTransport;
        _settings   = settings;
        return true;
    }

    void StreamMessageEndpoint::shutdown()
    {
        // 전송을 먼저 내린다(소유자 몫) — 그 뒤에는 콜백이 오지 않으니 표 · 줄을 비운다.
        std::scoped_lock<mutex> lock{ _tableMutex };
        _mapConnection.clear();
        _pTransport = nullptr;
    }

    shared_ptr<StreamMessageEndpoint::Connection> StreamMessageEndpoint::findConnection( StreamConnectionHandle handle ) const
    {
        std::scoped_lock<mutex> lock{ _tableMutex };
        const auto              found = _mapConnection.find( handle.packed() );
        return found != _mapConnection.end() ? found->second : shared_ptr<Connection>{};
    }

    void StreamMessageEndpoint::pushEvent( Event&& event, const uint8* pBody, int32 bodySize )
    {
        std::scoped_lock<mutex> lock{ _inboxMutex };
        event._offset = static_cast<int32>( _inboxBytes.size() );
        event._size   = bodySize;
        if ( bodySize > 0 )
            _inboxBytes.insert( _inboxBytes.end(), pBody, pBody + bodySize );
        _listInbox.push_back( std::move( event ) );
    }

    void StreamMessageEndpoint::closeForError( Connection& connection, StreamCloseReason reason )
    {
        if ( connection._errorReason == StreamCloseReason::None )
        {
            connection._errorReason = reason;
            SW_LOG_WARNING( "Closing stream %# (%#): %#", connection._remote.toString().c_str(), connection._handle.packed(), toString( reason ) );
        }
        _pTransport->close( connection._handle, StreamCloseMode::Abort );
    }

    // ---- I/O 스레드 ----

    void StreamMessageEndpoint::onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted )
    {
        shared_ptr<Connection> connection = make_shared<Connection>( _settings._maxFrameBodySize );
        connection->_handle               = handle;
        connection->_remote               = remote;
        connection->_bAccepted            = bAccepted ? SW_TRUE : SW_FALSE;
        // 세션은 표에 넣기 전에 — 표에서 찾은 다른 스레드의 보내기가 세션 없이 평문을 내보내지 않게.
        if ( _settings._security._pTlsContext != nullptr )
            connection->_tlsSession = _settings._security._pTlsContext->createSession();
        {
            std::scoped_lock<mutex> lock{ _tableMutex };
            _mapConnection[handle.packed()] = connection;
        }
        std::scoped_lock<mutex> lock{ connection->_mutex };
        if ( _settings._security._pTlsContext == nullptr )
        {
            announceOpenLocked( *connection );
            return;
        }
        if ( connection->_tlsSession == nullptr || connection->_tlsSession->getState() == TlsSessionState::Failed )
        {
            closeForError( *connection, StreamCloseReason::SecurityFailure );
            return;
        }
        // 클라이언트는 만들 때 ClientHello 를 지었다 — 보낸다. 서버는 저쪽 ClientHello 를 기다린다. 열림은 핸드셰이크 뒤.
        (void)flushCiphertextLocked( *connection );
    }

    void StreamMessageEndpoint::announceOpenLocked( Connection& connection )
    {
        connection._bOpenAnnounced = SW_TRUE;
        pushEvent( Event{ connection._remote, connection._handle, 0, 0, StreamCloseReason::None, StreamFrameKind::Message, EndpointEventKind::Opened, connection._bAccepted },
                   nullptr, 0 );
    }

    void StreamMessageEndpoint::onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        shared_ptr<Connection> connection = findConnection( handle );
        if ( connection == nullptr )
            return;
        std::scoped_lock<mutex> lock{ connection->_mutex };
        if ( connection->_errorReason != StreamCloseReason::None )
            return; // 이미 끊는 중 — 남은 바이트는 버린다
        if ( acceptIncomingLocked( *connection, pData, size ) == false )
            return;
        if ( connection->_bReceivePaused == SW_FALSE && connection->_pendingBytes.load( std::memory_order_relaxed ) > _settings._maxPendingReceiveBytes )
        {
            connection->_bReceivePaused = SW_TRUE;
            _pTransport->setReceivePaused( handle, true );
        }
    }

    bool StreamMessageEndpoint::acceptIncomingLocked( Connection& connection, const uint8* pData, int32 size )
    {
        if ( connection._tlsSession == nullptr )
            return decodeFramesLocked( connection, pData, size );
        ITlsSession& session = *connection._tlsSession;
        const bool   bFed    = session.feedCiphertext( pData, size );
        (void)flushCiphertextLocked( connection ); // 핸드셰이크 답 · 세션 표 · 실패 경고
        if ( bFed == false || session.getState() == TlsSessionState::Failed )
        {
            SW_LOG_WARNING( "TLS handshake failed on stream %#: %#", connection._remote.toString().c_str(), session.getFailureText() );
            closeForError( connection, StreamCloseReason::SecurityFailure );
            return false;
        }
        if ( session.getState() == TlsSessionState::Established && connection._bOpenAnnounced == SW_FALSE )
        {
            announceOpenLocked( connection );
            (void)flushCiphertextLocked( connection ); // 핸드셰이크 중에 모아 둔 평문이 이제 레코드로 나왔다
        }
        connection._plainScratch.clear();
        if ( session.readPlaintext( connection._plainScratch ) == false )
        {
            SW_LOG_WARNING( "TLS record rejected on stream %#: %#", connection._remote.toString().c_str(), session.getFailureText() );
            closeForError( connection, StreamCloseReason::SecurityFailure ); // 변조된 레코드(bad record mac)
            return false;
        }
        (void)flushCiphertextLocked( connection ); // 읽기가 내놓은 레코드(세션 표 답 등)
        if ( session.getState() == TlsSessionState::Closed )
            _pTransport->close( connection._handle, StreamCloseMode::Graceful ); // 저쪽 close_notify
        return connection._plainScratch.empty() ||
               decodeFramesLocked( connection, connection._plainScratch.data(), static_cast<int32>( connection._plainScratch.size() ) );
    }

    bool StreamMessageEndpoint::decodeFramesLocked( Connection& connection, const uint8* pData, int32 size )
    {
        connection._decoder.append( pData, size );
        StreamFrameView frame;
        for ( ;; )
        {
            const StreamFrameDecodeResult result = connection._decoder.next( frame );
            if ( result == StreamFrameDecodeResult::NeedMore )
                return true;
            if ( result == StreamFrameDecodeResult::Malformed )
            {
                closeForError( connection, StreamCloseReason::ProtocolError );
                return false;
            }
            switch ( frame._kind )
            {
                case StreamFrameKind::Ping:
                {
                    // 끝점이 스스로 답한다 — 게임 스레드를 거치지 않으니 RTT 에 프레임 길이가 섞이지 않는다.
                    connection._frameScratch.clear();
                    if ( StreamFrameEncoder::appendFrame( connection._frameScratch, StreamFrameKind::Pong, 0, frame._pBody, frame._bodySize, _settings._maxFrameBodySize ) )
                        (void)writeOutgoingLocked( connection, connection._frameScratch );
                    break;
                }
                case StreamFrameKind::Pong:
                {
                    if ( frame._bodySize == StreamMessageEndpointInternal::kPingBodySize )
                        connection._roundTripNanoseconds.store( MonotonicClock::nowNanoseconds() - StreamMessageEndpointInternal::readInt64( frame._pBody ), std::memory_order_relaxed );
                    break;
                }
                case StreamFrameKind::Message:
                case StreamFrameKind::Request:
                case StreamFrameKind::Response:
                case StreamFrameKind::Cancel:
                {
                    const uint8* pFrameBody    = frame._pBody;
                    int32        frameBodySize = frame._bodySize;
                    if ( ( frame._flags & StreamFrameFlag::kCompressed ) != 0 )
                    {
                        // 원래 크기가 몸 상한을 넘으면 풀기 전에 끊는다(압축 폭탄). 모르는 코덱 · 깨진 봉투도.
                        if ( NetCompressionUtil::decompressEnvelope( frame._pBody, frame._bodySize, _settings._maxFrameBodySize, connection._decompressScratch ) == false )
                        {
                            closeForError( connection, StreamCloseReason::ProtocolError );
                            return false;
                        }
                        pFrameBody    = connection._decompressScratch.data();
                        frameBodySize = static_cast<int32>( connection._decompressScratch.size() );
                    }
                    connection._pendingBytes.fetch_add( frameBodySize, std::memory_order_relaxed );
                    pushEvent( Event{ {}, connection._handle, 0, 0, StreamCloseReason::None, frame._kind, EndpointEventKind::Frame, SW_FALSE }, pFrameBody, frameBodySize );
                    break;
                }
                case StreamFrameKind::Count:
                {
                    break;
                }
            }
        }
    }

    void StreamMessageEndpoint::onStreamWritable( StreamConnectionHandle handle ) { (void)handle; }

    void StreamMessageEndpoint::onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason )
    {
        shared_ptr<Connection> connection;
        {
            std::scoped_lock<mutex> lock{ _tableMutex };
            const auto              found = _mapConnection.find( handle.packed() );
            if ( found != _mapConnection.end() )
            {
                connection = found->second;
                _mapConnection.erase( found );
            }
        }
        const StreamCloseReason finalReason = connection != nullptr && connection->_errorReason != StreamCloseReason::None ? connection->_errorReason : reason;
        pushEvent( Event{ {}, handle, 0, 0, finalReason, StreamFrameKind::Message, EndpointEventKind::Closed, SW_FALSE }, nullptr, 0 );
    }

    // ---- 아무 스레드 ----

    StreamSendResult StreamMessageEndpoint::writeOutgoingLocked( Connection& connection, const vector<uint8>& frameBytes )
    {
        if ( connection._tlsSession != nullptr )
        {
            if ( connection._tlsSession->writePlaintext( frameBytes.data(), static_cast<int32>( frameBytes.size() ) ) == false )
                return StreamSendResult::Closed; // 세션이 실패했거나 닫혔다
            return flushCiphertextLocked( connection );
        }
        const StreamSendResult result = _pTransport->send( connection._handle, frameBytes.data(), static_cast<int32>( frameBytes.size() ) );
        if ( result == StreamSendResult::QueueFull )
            closeForError( connection, StreamCloseReason::SendQueueOverflow ); // 느린 상대 — 메시지를 버리면 그 위의 순서가 깨지므로 끊는다
        return result;
    }

    StreamSendResult StreamMessageEndpoint::flushCiphertextLocked( Connection& connection )
    {
        connection._cipherScratch.clear();
        connection._tlsSession->takeCiphertext( connection._cipherScratch );
        if ( connection._cipherScratch.empty() )
            return StreamSendResult::Queued;
        const StreamSendResult result = _pTransport->send( connection._handle, connection._cipherScratch.data(), static_cast<int32>( connection._cipherScratch.size() ) );
        if ( result == StreamSendResult::QueueFull )
            closeForError( connection, StreamCloseReason::SendQueueOverflow );
        return result;
    }

    StreamSendResult StreamMessageEndpoint::sendFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize )
    {
        shared_ptr<Connection> connection = findConnection( handle );
        if ( connection == nullptr || _pTransport == nullptr )
            return StreamSendResult::Closed;
        std::scoped_lock<mutex> lock{ connection->_mutex };
        connection->_frameScratch.clear();
        // 압축 → (TLS) 순서. 줄지 않으면 원문. 상한은 원문 크기로 본다(받는 쪽도 푼 크기로 본다).
        const uint8* pFrameBody    = pBody;
        int32        frameBodySize = bodySize;
        uint8        flags         = 0;
        if ( bodySize <= _settings._maxFrameBodySize && NetCompressionUtil::compressEnvelope( _settings._compression, pBody, bodySize, connection->_compressScratch ) )
        {
            pFrameBody    = connection->_compressScratch.data();
            frameBodySize = static_cast<int32>( connection->_compressScratch.size() );
            flags         = StreamFrameFlag::kCompressed;
        }
        if ( bodySize > _settings._maxFrameBodySize ||
             StreamFrameEncoder::appendFrame( connection->_frameScratch, kind, flags, pFrameBody, frameBodySize, _settings._maxFrameBodySize ) == false )
        {
            SW_LOG_ERROR( "Stream frame of %# bytes exceeds the limit of %# bytes - not sent", bodySize, _settings._maxFrameBodySize );
            return StreamSendResult::QueueFull;
        }
        return writeOutgoingLocked( *connection, connection->_frameScratch );
    }

    StreamSendResult StreamMessageEndpoint::sendMessage( StreamConnectionHandle handle, const uint8* pBody, int32 bodySize )
    {
        return sendFrame( handle, StreamFrameKind::Message, pBody, bodySize );
    }

    StreamConnectionHandle StreamMessageEndpoint::connect( const NetAddress& remote ) { return _pTransport != nullptr ? _pTransport->connect( remote ) : StreamConnectionHandle{}; }

    void StreamMessageEndpoint::close( StreamConnectionHandle handle, StreamCloseMode mode )
    {
        if ( _pTransport == nullptr )
            return;
        const shared_ptr<Connection> connection = findConnection( handle );
        if ( connection != nullptr && mode == StreamCloseMode::Graceful )
        {
            // 우아한 종료 — close_notify 를 먼저 보낸다(저쪽이 잘린 연결과 끝을 가른다).
            std::scoped_lock<mutex> lock{ connection->_mutex };
            if ( connection->_tlsSession != nullptr && connection->_tlsSession->getState() == TlsSessionState::Established )
            {
                connection->_tlsSession->close();
                (void)flushCiphertextLocked( *connection );
            }
        }
        _pTransport->close( handle, mode );
    }

    IStreamTransport* StreamMessageEndpoint::getTransport() const { return _pTransport; }

    float64 StreamMessageEndpoint::getRoundTripSeconds( StreamConnectionHandle handle ) const
    {
        const shared_ptr<Connection> connection  = findConnection( handle );
        const int64                  nanoseconds = connection != nullptr ? connection->_roundTripNanoseconds.load( std::memory_order_relaxed ) : -1;
        return nanoseconds >= 0 ? static_cast<float64>( nanoseconds ) * 1.0e-9 : -1.0;
    }

    // ---- pump 스레드 ----

    int32 StreamMessageEndpoint::pump( IStreamEndpointListener& listener )
    {
        {
            std::scoped_lock<mutex> lock{ _inboxMutex };
            _listPumping.clear();
            _pumpingBytes.clear();
            _listPumping.swap( _listInbox );
            _pumpingBytes.swap( _inboxBytes );
        }
        int32 frameCount = 0;
        for ( const Event& event : _listPumping )
        {
            switch ( event._kind )
            {
                case EndpointEventKind::Opened:
                {
                    _mapLastPingTime[event._handle.packed()] = MonotonicClock::nowNanoseconds();
                    listener.onEndpointOpened( event._handle, event._remote, event._bAccepted == SW_TRUE );
                    break;
                }
                case EndpointEventKind::Frame:
                {
                    ++frameCount;
                    listener.onEndpointFrame( event._handle, event._frameKind, _pumpingBytes.data() + event._offset, event._size );
                    const shared_ptr<Connection> connection = findConnection( event._handle );
                    if ( connection != nullptr )
                    {
                        const int32 remaining = connection->_pendingBytes.fetch_sub( event._size, std::memory_order_relaxed ) - event._size;
                        if ( remaining <= _settings._maxPendingReceiveBytes / 2 )
                        {
                            std::scoped_lock<mutex> lock{ connection->_mutex };
                            if ( connection->_bReceivePaused == SW_TRUE )
                            {
                                connection->_bReceivePaused = SW_FALSE;
                                _pTransport->setReceivePaused( event._handle, false );
                            }
                        }
                    }
                    break;
                }
                case EndpointEventKind::Closed:
                {
                    _mapLastPingTime.erase( event._handle.packed() );
                    listener.onEndpointClosed( event._handle, event._reason );
                    break;
                }
            }
        }
        // 핑 — 열린 연결마다 간격이 지났으면(전송의 유휴 시한을 넘지 않게 · RTT).
        if ( _settings._pingIntervalSeconds > 0.0 )
        {
            const int64 now      = MonotonicClock::nowNanoseconds();
            const int64 interval = static_cast<int64>( _settings._pingIntervalSeconds * 1.0e9 );
            uint8       arrBody[StreamMessageEndpointInternal::kPingBodySize];
            for ( auto& [packed, lastPingNanoseconds] : _mapLastPingTime )
            {
                if ( now - lastPingNanoseconds < interval )
                    continue;
                lastPingNanoseconds = now;
                StreamMessageEndpointInternal::writeInt64( arrBody, now );
                (void)sendFrame( StreamConnectionHandle::fromPacked( packed ), StreamFrameKind::Ping, arrBody, StreamMessageEndpointInternal::kPingBodySize );
            }
        }
        return frameCount;
    }
} // namespace sw
