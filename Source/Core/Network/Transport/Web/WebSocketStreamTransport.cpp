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
                (void)releaseSlotLocked( static_cast<int32>( slotIndex ), true );
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
            int32             slotIndex = findSlotLocked( 0 );
            if ( slotIndex == kNoSlot )
            {
                slotIndex = static_cast<int32>( _listSlot.size() );
                _listSlot.push_back( Slot{} );
            }
            Slot& slot       = _listSlot[static_cast<size_t>( slotIndex )];
            slot._socket     = socket;
            slot._generation = SlotHandle::nextGeneration( slot._generation );
            slot._remote     = remote;
            slot._bOpen      = SW_FALSE;
            slot._bClosing   = SW_FALSE;
            handle           = StreamConnectionHandle::make( static_cast<uint32>( slotIndex ), slot._generation );
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
        ScopedLock<mutex> lock{ _mutex };
        const int32       slotIndex = findSlotLocked( handle );
        if ( slotIndex == kNoSlot )
            return StreamSendResult::Closed;
        const Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
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
        ScopedLock<mutex> lock{ _mutex };
        const int32       slotIndex = findSlotLocked( handle );
        if ( slotIndex == kNoSlot )
            return;
        Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
        if ( slot._bClosing != SW_FALSE )
            return;
        slot._bClosing = SW_TRUE;
        (void)emscripten_websocket_close( slot._socket, WebSocketProtocol::kCloseCodeNormal, "" ); // 닫힘은 onclose 가 알린다(LocalClose)
    }

    void WebSocketStreamTransport::setReceivePaused( StreamConnectionHandle handle, bool bPaused )
    {
        // shortcut: 브라우저 WebSocket 에 읽기 멈춤이 없어 받는 대로 올린다, 위 층 받은 줄 상한(_maxPendingReceiveBytes)이 넘으면 위 층이 닫는다 — WebSocketStream(백프레셔) API 가 널리 쓰이면 그것으로.
        (void)handle;
        (void)bPaused;
    }

    int32 WebSocketStreamTransport::pollIO( int32 timeoutMilli )
    {
        (void)timeoutMilli;
        return 0; // 이벤트는 브라우저 이벤트 루프가 메인 스레드로 준다
    }

    NetAddress WebSocketStreamTransport::getRemoteAddress( StreamConnectionHandle handle ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        const int32       slotIndex = findSlotLocked( handle );
        return slotIndex == kNoSlot ? NetAddress{} : _listSlot[static_cast<size_t>( slotIndex )]._remote;
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
            ScopedLock<mutex> lock{ _mutex };
            const int32       slotIndex = findSlotLocked( socket );
            if ( slotIndex == kNoSlot )
                return;
            Slot& slot  = _listSlot[static_cast<size_t>( slotIndex )];
            slot._bOpen = SW_TRUE;
            handle      = StreamConnectionHandle::make( static_cast<uint32>( slotIndex ), slot._generation );
            remote      = slot._remote;
            ++_stats._connectedCount;
            ++_stats._openCount;
        }
        _pHandler->onStreamOpened( handle, remote, false );
    }

    void WebSocketStreamTransport::onSocketMessage( int32 socket, const uint8* pData, int32 size, bool bText )
    {
        StreamConnectionHandle handle;
        {
            ScopedLock<mutex> lock{ _mutex };
            const int32       slotIndex = findSlotLocked( socket );
            if ( slotIndex == kNoSlot )
                return;
            handle = StreamConnectionHandle::make( static_cast<uint32>( slotIndex ), _listSlot[static_cast<size_t>( slotIndex )]._generation );
            _stats._receivedBytes += static_cast<uint64>( size );
            ++_stats._receiveCallCount;
        }
        if ( bText )
        {
            close( handle, StreamCloseMode::Abort ); // 수락 쪽은 이진만 보낸다 — 텍스트는 다른 서버다
            return;
        }
        _pHandler->onStreamReceived( handle, pData, size );
    }

    void WebSocketStreamTransport::onSocketClose( int32 socket, bool bWasClean )
    {
        StreamConnectionHandle handle;
        StreamCloseReason      reason = StreamCloseReason::None;
        {
            ScopedLock<mutex> lock{ _mutex };
            const int32       slotIndex = findSlotLocked( socket );
            if ( slotIndex == kNoSlot )
                return; // shutdown 이 이미 알렸다
            handle = StreamConnectionHandle::make( static_cast<uint32>( slotIndex ), _listSlot[static_cast<size_t>( slotIndex )]._generation );
            reason = releaseSlotLocked( slotIndex, bWasClean );
        }
        _pHandler->onStreamClosed( handle, reason );
    }

    int32 WebSocketStreamTransport::findSlotLocked( int32 socket ) const
    {
        for ( size_t slotIndex = 0; slotIndex < _listSlot.size(); ++slotIndex )
        {
            if ( _listSlot[slotIndex]._socket == socket )
                return static_cast<int32>( slotIndex );
        }
        return kNoSlot;
    }

    int32 WebSocketStreamTransport::findSlotLocked( StreamConnectionHandle handle ) const
    {
        const uint32 slotIndex = handle.index();
        if ( handle.isValid() == false || slotIndex >= _listSlot.size() )
            return kNoSlot;
        const Slot& slot = _listSlot[slotIndex];
        return ( slot._socket != 0 && slot._generation == handle.generation() ) ? static_cast<int32>( slotIndex ) : kNoSlot;
    }

    StreamCloseReason WebSocketStreamTransport::releaseSlotLocked( int32 slotIndex, bool bWasClean )
    {
        Slot&             slot   = _listSlot[static_cast<size_t>( slotIndex )];
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
        return reason;
    }
} // namespace sw
