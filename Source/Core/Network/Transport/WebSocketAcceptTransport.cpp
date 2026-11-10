#include "pch.h"

#include "Core/Network/Transport/WebSocketAcceptTransport.h"

#include "Core/Concurrency/ScopedLock.h"

namespace sw
{
    WebSocketAcceptTransport::WebSocketAcceptTransport( unique_ptr<IStreamTransport> pInner )
        : _pInner{ std::move( pInner ) }
        , _innerHandler{ *this }
        , _pHandler{ nullptr }
        , _maxQueuedSendBytes{ StreamLimit::kDefaultMaxQueuedSendBytes }
        , _mutex{}
        , _mapConnection{}
    {
    }

    WebSocketAcceptTransport::~WebSocketAcceptTransport()
    {
        if ( _pInner != nullptr )
            _pInner->shutdown(); // 안쪽이 닫힘 콜백을 이 객체로 부르므로 멤버가 사라지기 전에 내린다
    }

    bool WebSocketAcceptTransport::initialize( IStreamHandler* pHandler, const StreamTransportConfig& settings )
    {
        if ( _pInner == nullptr || pHandler == nullptr )
            return false;
        _pHandler           = pHandler;
        _maxQueuedSendBytes = settings._maxQueuedSendBytes;
        return _pInner->initialize( &_innerHandler, settings );
    }

    shared_ptr<WebSocketAcceptTransport::Connection> WebSocketAcceptTransport::findConnection( StreamConnectionHandle handle ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        found = _mapConnection.find( handle.packed() );
        return found != _mapConnection.end() ? found->second : nullptr;
    }

    StreamSendResult WebSocketAcceptTransport::send( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        const shared_ptr<Connection> connection = findConnection( handle );
        if ( connection == nullptr )
            return _pInner->send( handle, pData, size ); // 내가 건 연결 · 이미 닫힌 핸들 — 안쪽이 답한다
        ScopedLock<mutex> lock{ connection->_sendMutex };
        switch ( connection->_mode )
        {
            case ConnectionMode::Sniff:
            case ConnectionMode::Handshake:
                if ( static_cast<int64>( connection->_pendingSend.size() ) + size > _maxQueuedSendBytes )
                    return StreamSendResult::QueueFull;
                connection->_pendingSend.insert( connection->_pendingSend.end(), pData, pData + size );
                return StreamSendResult::Queued;
            case ConnectionMode::WebSocket:
                return sendFrameLocked( handle, WebSocketFrameKind::Binary, pData, size );
            case ConnectionMode::Raw:
                break;
        }
        return _pInner->send( handle, pData, size );
    }

    StreamSendResult WebSocketAcceptTransport::sendFrameLocked( StreamConnectionHandle handle, WebSocketFrameKind kind, const uint8* pData, int32 size )
    {
        vector<uint8> frame;
        WebSocketProtocol::appendFrame( frame, kind, pData, size );
        return _pInner->send( handle, frame.data(), static_cast<int32>( frame.size() ) );
    }

    void WebSocketAcceptTransport::close( StreamConnectionHandle handle, StreamCloseMode mode )
    {
        const shared_ptr<Connection> connection = findConnection( handle );
        if ( connection != nullptr && mode == StreamCloseMode::Graceful )
        {
            ScopedLock<mutex> lock{ connection->_sendMutex };
            if ( connection->_mode == ConnectionMode::WebSocket )
            {
                // 닫는 까닭 코드를 앞에 둔 Close 프레임(RFC 6455 5.5.1) — 브라우저의 close 이벤트가 깨끗한 닫힘(wasClean)으로 본다
                const uint8 arrCode[] = { static_cast<uint8>( WebSocketProtocol::kCloseCodeNormal >> 8 ), static_cast<uint8>( WebSocketProtocol::kCloseCodeNormal & 0xFFu ) };
                (void)sendFrameLocked( handle, WebSocketFrameKind::Close, arrCode, static_cast<int32>( sizeof( arrCode ) ) );
            }
        }
        _pInner->close( handle, mode );
    }

    void WebSocketAcceptTransport::handleOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted )
    {
        if ( bAccepted )
        {
            ScopedLock<mutex> lock{ _mutex };
            _mapConnection[handle.packed()] = make_shared<Connection>();
        }
        _pHandler->onStreamOpened( handle, remote, bAccepted );
    }

    void WebSocketAcceptTransport::handleClosed( StreamConnectionHandle handle, StreamCloseReason reason )
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            _mapConnection.erase( handle.packed() );
        }
        _pHandler->onStreamClosed( handle, reason );
    }

    void WebSocketAcceptTransport::handleReceived( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        const shared_ptr<Connection> connection = findConnection( handle );
        if ( connection == nullptr )
        {
            _pHandler->onStreamReceived( handle, pData, size );
            return;
        }
        // 갈래는 이 받기 길만 바꾸므로 잠금 없이 읽어도 된다(쓰는 쪽이 이 스레드다).
        switch ( connection->_mode )
        {
            case ConnectionMode::Raw:
                _pHandler->onStreamReceived( handle, pData, size );
                return;
            case ConnectionMode::WebSocket:
                receiveFrames( handle, *connection, pData, size );
                return;
            case ConnectionMode::Sniff:
            case ConnectionMode::Handshake:
                connection->_handshake.insert( connection->_handshake.end(), pData, pData + size );
                detectProtocol( handle, *connection );
                return;
        }
    }

    void WebSocketAcceptTransport::detectProtocol( StreamConnectionHandle handle, Connection& connection )
    {
        const string_view received{ reinterpret_cast<const utf8*>( connection._handshake.data() ), connection._handshake.size() };
        const string_view prefix{ WebSocketProtocol::kRequestPrefix };
        if ( connection._mode == ConnectionMode::Sniff )
        {
            const size_t compared = received.size() < prefix.size() ? received.size() : prefix.size();
            if ( received.substr( 0, compared ) != prefix.substr( 0, compared ) )
            {
                // 원시 스트림 — 쌓아 둔 보내기를 먼저 흘리고(위 층이 보낸 순서) 받은 바이트를 그대로 올린다.
                {
                    ScopedLock<mutex> lock{ connection._sendMutex };
                    connection._mode = ConnectionMode::Raw;
                    if ( connection._pendingSend.empty() == false )
                        (void)_pInner->send( handle, connection._pendingSend.data(), static_cast<int32>( connection._pendingSend.size() ) );
                    connection._pendingSend = {};
                }
                vector<uint8> bytes   = std::move( connection._handshake );
                connection._handshake = {};
                _pHandler->onStreamReceived( handle, bytes.data(), static_cast<int32>( bytes.size() ) );
                return;
            }
            if ( compared < prefix.size() )
                return;                                   // `GE` 까지만 왔다 — 더 받는다
            connection._mode = ConnectionMode::Handshake; // 아직 보내기와 겹치지 않는다 — 보내기는 Sniff · Handshake 를 같게 다룬다
        }

        const size_t headerEnd = received.find( WebSocketProtocol::kHeaderTerminator );
        if ( headerEnd == string_view::npos )
        {
            if ( static_cast<int32>( received.size() ) > WebSocketProtocol::kMaxHandshakeBytes )
                _pInner->close( handle, StreamCloseMode::Abort );
            return;
        }
        const size_t headerSize = headerEnd + string_view{ WebSocketProtocol::kHeaderTerminator }.size();
        string       clientKey;
        if ( static_cast<int32>( headerSize ) > WebSocketProtocol::kMaxHandshakeBytes ||
             WebSocketProtocol::parseUpgradeRequest( received.substr( 0, headerSize ), clientKey ) == false )
        {
            const string reject = WebSocketProtocol::makeRejectResponse();
            (void)_pInner->send( handle, reinterpret_cast<const uint8*>( reject.data() ), static_cast<int32>( reject.size() ) );
            _pInner->close( handle, StreamCloseMode::Graceful );
            return;
        }
        {
            ScopedLock<mutex> lock{ connection._sendMutex };
            const string      response = WebSocketProtocol::makeUpgradeResponse( clientKey );
            (void)_pInner->send( handle, reinterpret_cast<const uint8*>( response.data() ), static_cast<int32>( response.size() ) );
            connection._mode = ConnectionMode::WebSocket;
            if ( connection._pendingSend.empty() == false )
                (void)sendFrameLocked( handle, WebSocketFrameKind::Binary, connection._pendingSend.data(), static_cast<int32>( connection._pendingSend.size() ) );
            connection._pendingSend = {};
        }
        vector<uint8> rest( connection._handshake.begin() + static_cast<ptrdiff_t>( headerSize ), connection._handshake.end() );
        connection._handshake = {};
        if ( rest.empty() == false )
            receiveFrames( handle, connection, rest.data(), static_cast<int32>( rest.size() ) );
    }

    void WebSocketAcceptTransport::receiveFrames( StreamConnectionHandle handle, Connection& connection, const uint8* pData, int32 size )
    {
        connection._decoder.append( pData, size );
        WebSocketFrameView frame;
        while ( true )
        {
            const WebSocketDecodeResult result = connection._decoder.next( frame );
            if ( result == WebSocketDecodeResult::NeedMore )
                return;
            if ( result == WebSocketDecodeResult::Malformed )
            {
                _pInner->close( handle, StreamCloseMode::Abort ); // 위 층은 onStreamClosed 로 안다
                return;
            }
            switch ( frame._kind )
            {
                case WebSocketFrameKind::Binary:
                case WebSocketFrameKind::Continuation: // 조각난 이진 메시지 — 스트림이라 몸을 그대로 잇는다
                    if ( frame._payloadSize > 0 )
                        _pHandler->onStreamReceived( handle, frame._pPayload, frame._payloadSize );
                    break;
                case WebSocketFrameKind::Ping:
                {
                    ScopedLock<mutex> lock{ connection._sendMutex };
                    (void)sendFrameLocked( handle, WebSocketFrameKind::Pong, frame._pPayload, frame._payloadSize );
                    break;
                }
                case WebSocketFrameKind::Pong:
                    break;
                case WebSocketFrameKind::Close:
                {
                    // 저쪽이 닫는다 — 같은 코드로 답하고 닫는다(RFC 6455 5.5.1). 위 층은 안쪽의 닫힘 까닭을 받는다.
                    {
                        ScopedLock<mutex> lock{ connection._sendMutex };
                        (void)sendFrameLocked( handle, WebSocketFrameKind::Close, frame._pPayload, frame._payloadSize );
                    }
                    _pInner->close( handle, StreamCloseMode::Graceful );
                    return;
                }
                case WebSocketFrameKind::Text: // 이 관은 이진만 쓴다 — 텍스트는 프로토콜 오류다
                {
                    {
                        ScopedLock<mutex> lock{ connection._sendMutex };
                        const uint8       arrCode[] = { static_cast<uint8>( WebSocketProtocol::kCloseCodeProtocol >> 8 ), static_cast<uint8>( WebSocketProtocol::kCloseCodeProtocol & 0xFFu ) };
                        (void)sendFrameLocked( handle, WebSocketFrameKind::Close, arrCode, static_cast<int32>( sizeof( arrCode ) ) );
                    }
                    _pInner->close( handle, StreamCloseMode::Graceful );
                    return;
                }
            }
        }
    }
} // namespace sw
