#include "pch.h"

#include "Core/Network/Transport/Web/WebSocketStreamTransport.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/Foundation/Constants.h"
#include "Core/Network/Transport/WebSocketProtocol.h"

#include <emscripten/websocket.h>

namespace sw
{
    namespace
    {
        /** @brief Emscripten 콜백(C 함수 포인터) — userData 가 전송이다. 늘 메인 런타임 스레드에서 온다(EM_CALLBACK_THREAD_CONTEXT_MAIN_RUNTIME_THREAD). */
        struct WebSocketStreamTransportInternal
        {
            static bool onOpen( int eventType, const EmscriptenWebSocketOpenEvent* pEvent, void* pUserData )
            {
                (void)eventType;
                static_cast<WebSocketStreamTransport*>( pUserData )->onSocketOpen( pEvent->socket );
                return true;
            }
            static bool onMessage( int eventType, const EmscriptenWebSocketMessageEvent* pEvent, void* pUserData )
            {
                (void)eventType;
                static_cast<WebSocketStreamTransport*>( pUserData )->onSocketMessage( pEvent->socket, pEvent->data, static_cast<int32>( pEvent->numBytes ), pEvent->isText );
                return true;
            }
            static bool onError( int eventType, const EmscriptenWebSocketErrorEvent* pEvent, void* pUserData )
            {
                (void)eventType;
                (void)pEvent;
                (void)pUserData;
                return true; // 브라우저는 오류 뒤에 늘 close 를 보낸다 — 닫힘은 그쪽 한 길로 알린다
            }
            static bool onClose( int eventType, const EmscriptenWebSocketCloseEvent* pEvent, void* pUserData )
            {
                (void)eventType;
                static_cast<WebSocketStreamTransport*>( pUserData )->onSocketClose( pEvent->socket, pEvent->wasClean );
                return true;
            }
        };
    } // namespace

    WebSocketStreamTransport::WebSocketStreamTransport()
        : _pHandler{ nullptr }
        , _settings{}
        , _mutex{}
        , _listSlot{}
        , _stats{}
    {
    }

    WebSocketStreamTransport::~WebSocketStreamTransport()
    {
        shutdown();
    }

    bool WebSocketStreamTransport::initialize( IStreamHandler* pHandler, const StreamTransportConfig& settings )
    {
        if ( pHandler == nullptr || emscripten_websocket_is_supported() == false )
            return false;
        _pHandler = pHandler;
        _settings = settings;
        return true;
    }

    void WebSocketStreamTransport::shutdown()
    {
        vector<StreamConnectionHandle> listClosed;
        {
            ScopedLock<mutex> lock{ _mutex };
            for ( size_t slotIndex = 0; slotIndex < _listSlot.size(); ++slotIndex )
            {
                Slot& slot = _listSlot[slotIndex];
                if ( slot._socket == 0 )
                    continue;
                listClosed.push_back( StreamConnectionHandle::make( static_cast<uint32>( slotIndex ), slot._generation ) );
                (void)emscripten_websocket_close( slot._socket, WebSocketProtocol::kCloseCodeNormal, "shutdown" );
                (void)releaseSlotLocked( static_cast<uint32>( slotIndex ), true );
            }
        }
        for ( const StreamConnectionHandle handle : listClosed )
        {
            _pHandler->onStreamClosed( handle, StreamCloseReason::Shutdown );
        }
    }

    bool WebSocketStreamTransport::listen( const NetAddress& bindAddress )
    {
        SW_LOG_ERROR_ID( "WebSocketStreamTransport.ListenUnsupported", "The browser cannot accept connections (listen on %#) - run the server natively", bindAddress.toString().c_str() );
        return false;
    }

    StreamConnectionHandle WebSocketStreamTransport::connect( const NetAddress& remote )
    {
        const string                        url = "ws://" + remote.toString() + "/";
        EmscriptenWebSocketCreateAttributes attributes;
        emscripten_websocket_init_create_attributes( &attributes );
        attributes.url                      = url.c_str();
        attributes.protocols                = nullptr; // 하위 프로토콜 없음 — 수락 쪽은 Sec-WebSocket-Protocol 을 답하지 않는다
        attributes.createOnMainThread       = true;
        const EMSCRIPTEN_WEBSOCKET_T socket = emscripten_websocket_new( &attributes );
        if ( socket <= 0 )
        {
            SW_LOG_ERROR_ID( "WebSocketStreamTransport.CreateFailed", "Could not create a WebSocket to %# (%#)", url.c_str(), static_cast<int32>( socket ) );
            return {};
        }
        StreamConnectionHandle handle;
        {
            ScopedLock<mutex> lock{ _mutex };
            const uint32      slotIndex = findSlotLocked( 0 ).value_or( static_cast<uint32>( _listSlot.size() ) ); // 빈 칸이 없으면 끝에 새 칸
            if ( slotIndex == _listSlot.size() )
                _listSlot.push_back( Slot{} );
            Slot& slot       = _listSlot[slotIndex];
            slot._socket     = socket;
            slot._generation = SlotHandle::nextGeneration( slot._generation );
            slot._remote     = remote;
            slot._bOpen      = SW_FALSE;
            slot._bClosing   = SW_FALSE;
            handle           = StreamConnectionHandle::make( slotIndex, slot._generation );
        }
        // 콜백을 건 뒤에야 이벤트가 온다 — 소켓은 메인 스레드에 있고 이벤트는 이 호출이 돌아간 뒤 브라우저 이벤트 루프에서 온다.
        (void)emscripten_websocket_set_onopen_callback_on_thread( socket, this, &WebSocketStreamTransportInternal::onOpen, EM_CALLBACK_THREAD_CONTEXT_MAIN_RUNTIME_THREAD );
        (void)emscripten_websocket_set_onmessage_callback_on_thread( socket, this, &WebSocketStreamTransportInternal::onMessage, EM_CALLBACK_THREAD_CONTEXT_MAIN_RUNTIME_THREAD );
        (void)emscripten_websocket_set_onerror_callback_on_thread( socket, this, &WebSocketStreamTransportInternal::onError, EM_CALLBACK_THREAD_CONTEXT_MAIN_RUNTIME_THREAD );
        (void)emscripten_websocket_set_onclose_callback_on_thread( socket, this, &WebSocketStreamTransportInternal::onClose, EM_CALLBACK_THREAD_CONTEXT_MAIN_RUNTIME_THREAD );
        return handle;
    }

    StreamSendResult WebSocketStreamTransport::send( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        ScopedLock<mutex>      lock{ _mutex };
        const optional<uint32> slotIndex = findSlotLocked( handle );
        if ( slotIndex.has_value() == false )
            return StreamSendResult::Closed;
        const Slot& slot = _listSlot[*slotIndex];
        if ( slot._bOpen == SW_FALSE || slot._bClosing != SW_FALSE )
            return StreamSendResult::Closed; // 열리기 전 보내기는 위 층이 onStreamOpened 를 기다리는 약속이다
        size_t buffered = 0;
        if ( emscripten_websocket_get_buffered_amount( slot._socket, &buffered ) == EMSCRIPTEN_RESULT_SUCCESS &&
             static_cast<int64>( buffered ) + size > _settings._maxQueuedSendBytes )
            return StreamSendResult::QueueFull;
        // 물금(QueuedAboveHighWatermark)은 내지 않는다 — 브라우저는 줄이 비었다는 이벤트가 없어 onStreamWritable 을 줄 수 없다.
        if ( emscripten_websocket_send_binary( slot._socket, const_cast<uint8*>( pData ), static_cast<uint32_t>( size ) ) != EMSCRIPTEN_RESULT_SUCCESS )
            return StreamSendResult::Closed;
        _stats._sentBytes += static_cast<uint64>( size );
        ++_stats._sendCallCount;
        return StreamSendResult::Queued;
    }

    void WebSocketStreamTransport::close( StreamConnectionHandle handle, StreamCloseMode mode )
    {
        (void)mode; // 브라우저 close 는 늘 쌓인 것을 다 보낸 뒤 닫는다(HTML 명세) — 끊기(RST)는 없다
        ScopedLock<mutex>      lock{ _mutex };
        const optional<uint32> slotIndex = findSlotLocked( handle );
        if ( slotIndex.has_value() == false )
            return;
        Slot& slot = _listSlot[*slotIndex];
        if ( slot._bClosing != SW_FALSE )
            return;
        slot._bClosing = SW_TRUE;
        (void)emscripten_websocket_close( slot._socket, WebSocketProtocol::kCloseCodeNormal, "" ); // 닫힘은 onclose 가 알린다(LocalClose)
    }

    void WebSocketStreamTransport::setReceivePaused( StreamConnectionHandle handle, bool bPaused )
    {
        // 브라우저 WebSocket 에는 읽기 멈춤이 없다 — 멈춘 동안 받은 것은 칸의 `_receiveHold` 가 들고, 다시 열면 다음 pollIO 가 넘긴다.
        // 여기서 바로 넘기지 않는다: 위 층은 자기 연결 잠금을 쥔 채 이것을 부른다.
        if ( bPaused && _settings._ioThreadCount != 0 )
        {
            // pollIO 를 도는 쪽이 없으면 든 바이트가 영영 넘어가지 않아 연결이 조용히 멈춘다 — 그 대신 끊고 알린다.
            SW_LOG_ERROR_ID( "WebSocketStreamTransport.PauseNeedsManualPoll", "WebSocketStreamTransport: pausing receive needs _ioThreadCount == 0 (held bytes are released by pollIO) - closing the connection" );
            close( handle, StreamCloseMode::Abort );
            return;
        }
        ScopedLock<mutex>      lock{ _mutex };
        const optional<uint32> slotIndex = findSlotLocked( handle );
        if ( slotIndex.has_value() )
            _listSlot[*slotIndex]._receiveHold.setPaused( bPaused );
    }

    int32 WebSocketStreamTransport::pollIO( int32 timeoutMilli )
    {
        (void)timeoutMilli; // 소켓 이벤트는 브라우저 이벤트 루프가 메인 스레드로 준다 — 여기서는 다시 연 연결의 든 바이트만 넘긴다
        struct HeldChunk
        {
            StreamConnectionHandle _handle;
            vector<uint8>          _bytes;
        };
        vector<HeldChunk> listReleased;
        {
            ScopedLock<mutex> lock{ _mutex };
            for ( size_t slotIndex = 0; slotIndex < _listSlot.size(); ++slotIndex )
            {
                Slot& slot = _listSlot[slotIndex];
                if ( slot._socket == 0 || slot._bClosing != SW_FALSE )
                    continue;
                vector<uint8> bytes = slot._receiveHold.takeReleasable();
                if ( bytes.empty() == false )
                    listReleased.push_back( HeldChunk{ StreamConnectionHandle::make( static_cast<uint32>( slotIndex ), slot._generation ), std::move( bytes ) } );
            }
        }
        for ( const HeldChunk& released : listReleased )
        {
            _pHandler->onStreamReceived( released._handle, released._bytes.data(), static_cast<int32>( released._bytes.size() ) );
        }
        return static_cast<int32>( listReleased.size() );
    }

    NetAddress WebSocketStreamTransport::getRemoteAddress( StreamConnectionHandle handle ) const
    {
        ScopedLock<mutex>      lock{ _mutex };
        const optional<uint32> slotIndex = findSlotLocked( handle );
        return slotIndex.has_value() ? _listSlot[*slotIndex]._remote : NetAddress{};
    }

    StreamTransportStats WebSocketStreamTransport::getStats() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _stats;
    }

    void WebSocketStreamTransport::onSocketOpen( int32 socket )
    {
        StreamConnectionHandle handle;
        NetAddress             remote;
        {
            ScopedLock<mutex>      lock{ _mutex };
            const optional<uint32> slotIndex = findSlotLocked( socket );
            if ( slotIndex.has_value() == false )
                return;
            Slot& slot  = _listSlot[*slotIndex];
            slot._bOpen = SW_TRUE;
            handle      = StreamConnectionHandle::make( *slotIndex, slot._generation );
            remote      = slot._remote;
            ++_stats._connectedCount;
            ++_stats._openCount;
        }
        _pHandler->onStreamOpened( handle, remote, false );
    }

    void WebSocketStreamTransport::onSocketMessage( int32 socket, const uint8* pData, int32 size, bool bText )
    {
        StreamConnectionHandle  handle;
        StreamReceiveHoldResult holdResult = StreamReceiveHoldResult::Deliver;
        {
            ScopedLock<mutex>      lock{ _mutex };
            const optional<uint32> slotIndex = findSlotLocked( socket );
            if ( slotIndex.has_value() == false )
                return;
            Slot& slot = _listSlot[*slotIndex];
            handle     = StreamConnectionHandle::make( *slotIndex, slot._generation );
            _stats._receivedBytes += static_cast<uint64>( size );
            ++_stats._receiveCallCount;
            if ( bText == false )
                holdResult = slot._receiveHold.accept( pData, size, _settings._maxHeldReceiveBytes );
        }
        // 수락 쪽은 이진만 보낸다 — 텍스트는 다른 서버다. 멈춘 동안 든 바이트가 상한을 넘으면 느린 소비자다(OS 소켓이면 TCP 윈도우가 상대를 멈췄다).
        if ( bText || holdResult == StreamReceiveHoldResult::Overflow )
        {
            close( handle, StreamCloseMode::Abort );
            return;
        }
        if ( holdResult == StreamReceiveHoldResult::Deliver )
            _pHandler->onStreamReceived( handle, pData, size );
    }

    void WebSocketStreamTransport::onSocketClose( int32 socket, bool bWasClean )
    {
        StreamConnectionHandle handle;
        StreamCloseReason      reason = StreamCloseReason::None;
        {
            ScopedLock<mutex>      lock{ _mutex };
            const optional<uint32> slotIndex = findSlotLocked( socket );
            if ( slotIndex.has_value() == false )
                return; // shutdown 이 이미 알렸다
            handle = StreamConnectionHandle::make( *slotIndex, _listSlot[*slotIndex]._generation );
            reason = releaseSlotLocked( *slotIndex, bWasClean );
        }
        _pHandler->onStreamClosed( handle, reason );
    }

    optional<uint32> WebSocketStreamTransport::findSlotLocked( int32 socket ) const
    {
        for ( size_t slotIndex = 0; slotIndex < _listSlot.size(); ++slotIndex )
        {
            if ( _listSlot[slotIndex]._socket == socket )
                return static_cast<uint32>( slotIndex );
        }
        return nullopt;
    }

    optional<uint32> WebSocketStreamTransport::findSlotLocked( StreamConnectionHandle handle ) const
    {
        const uint32 slotIndex = handle.index();
        if ( handle.isValid() == false || slotIndex >= _listSlot.size() )
            return nullopt;
        const Slot& slot = _listSlot[slotIndex];
        if ( slot._socket == 0 || slot._generation != handle.generation() )
            return nullopt;
        return slotIndex;
    }

    StreamCloseReason WebSocketStreamTransport::releaseSlotLocked( uint32 slotIndex, bool bWasClean )
    {
        Slot&             slot   = _listSlot[slotIndex];
        StreamCloseReason reason = StreamCloseReason::RemoteClose;
        if ( slot._bOpen == SW_FALSE )
            reason = StreamCloseReason::ConnectFailed; // 열리기 전 닫힘 — 거절 · 주소 · 시한(브라우저는 까닭을 가리지 않는다)
        else if ( slot._bClosing != SW_FALSE )
            reason = StreamCloseReason::LocalClose;
        else if ( bWasClean == false )
            reason = StreamCloseReason::Reset;
        if ( slot._bOpen != SW_FALSE )
            --_stats._openCount;
        ++_stats._closedCount;
        (void)emscripten_websocket_delete( slot._socket );
        slot._socket   = 0;
        slot._bOpen    = SW_FALSE;
        slot._bClosing = SW_FALSE;
        slot._receiveHold.clear();
        return reason;
    }
} // namespace sw
