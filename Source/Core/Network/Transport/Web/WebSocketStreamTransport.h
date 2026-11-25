/**
 * @file WebSocketStreamTransport.h
 * @brief 웹의 스트림 전송 — 브라우저에는 원시 TCP 가 없어 연결 하나를 WebSocket(이진 프레임) 하나로 연다. 서버는 같은 리슨에서 `WebSocketAcceptTransport` 가 받는다.
 * @details Emscripten `emscripten/websocket.h`(-lwebsocket.js)를 쓴다. 소켓은 늘 브라우저 메인 스레드에 있고(라이브러리가 그렇게 만든다 — 다른 스레드의 호출은 메인으로
 *          동기 위임된다), 콜백도 메인 런타임 스레드로 받는다 — 메인 루프가 프레임마다 브라우저에 돌려주므로 그 사이에 온다. 그래서 I/O 스레드를 두지 않는다(`pollIO` 는 다시 연 연결의 든 바이트만 넘긴다).
 *          연결 주소는 `<ws|wss>://<이름 또는 숫자 주소>:<포트>/` 다. https 페이지는 혼합 콘텐츠라 `wss://` 만 열 수 있고(TLS 는 서버 앞의 종단 프록시가 푼다, `deploy/haproxy/web-tls.cfg`),
 *          인증서는 이름으로 확인하므로 주소를 만든 이름(`PlatformSocketUtil::resolveAddress` — Emscripten 은 이름마다 가짜 주소를 준다)을 되찾아 쓴다.
 *          `connect` 는 아무 스레드에서나 부른다. 만들기부터 칸 등록까지 메인 런타임 스레드에서 한 번에 한다(그 사이 브라우저 이벤트가 끼지 않는다). 할 수 없는 것: 리슨(브라우저는 받지 못해 false). 읽기 멈춤은 브라우저 WebSocket 에 흐름 제어가 없어
 *          전송이 받은 바이트를 들고 있다가 다시 열면 `pollIO` 가 넘긴다(`StreamReceiveHold`, 상한 `_maxHeldReceiveBytes` 를 넘으면 닫는다).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/StreamReceiveHold.h"

namespace sw
{
    class SW_API WebSocketStreamTransport final : public IStreamTransport
    {
    public:
        WebSocketStreamTransport();
        ~WebSocketStreamTransport() override;

        [[nodiscard]] bool     initialize( IStreamHandler* pHandler, const StreamTransportConfig& settings ) override;
        void                   shutdown() override;
        [[nodiscard]] bool     listen( const NetAddress& bindAddress ) override;
        uint16                 getListenPort() const override { return 0; }
        StreamConnectionHandle connect( const NetAddress& remote ) override;
        StreamSendResult       send( StreamConnectionHandle handle, const uint8* pData, int32 size ) override;
        void                   close( StreamConnectionHandle handle, StreamCloseMode mode ) override;
        void                   setReceivePaused( StreamConnectionHandle handle, bool bPaused ) override;
        int32                  pollIO( int32 timeoutMilli ) override;
        NetAddress             getRemoteAddress( StreamConnectionHandle handle ) const override;
        StreamTransportStats   getStats() const override;

        /** @brief 브라우저 콜백이 부르는 길 — 콜백 함수(C 함수 포인터)가 소켓 번호로 연결을 찾아 넘긴다. 바깥에서 부르지 않는다. */
        void onSocketOpen( int32 socket );
        void onSocketMessage( int32 socket, const uint8* pData, int32 size, bool bText );
        void onSocketClose( int32 socket, bool bWasClean );

    private:
        /** @brief `connect` 의 본문입니다 — 메인 런타임 스레드에서 돈다. */
        StreamConnectionHandle connectOnMainThread( const NetAddress& remote );

        /** @brief 연결 칸 하나입니다. 칸 번호가 핸들의 번호, `_generation` 이 세대입니다(닫힌 칸을 다시 쓰면 오른다). */
        struct Slot
        {
            int32             _socket{ 0 }; ///< Emscripten 소켓 번호(0 = 빈 칸)
            uint32            _generation{ 0 };
            NetAddress        _remote{};
            uint8             _bOpen{ SW_FALSE };    ///< onopen 을 받았다
            uint8             _bClosing{ SW_FALSE }; ///< 이쪽이 닫았다 — 닫힘 까닭은 LocalClose
            StreamReceiveHold _receiveHold{};        ///< 위 층이 읽기를 멈춘 동안 받은 바이트
        };

        [[nodiscard]] optional<uint32> findSlotLocked( int32 socket ) const;
        [[nodiscard]] optional<uint32> findSlotLocked( StreamConnectionHandle handle ) const;
        /** @brief 칸을 비우고 소켓을 놓습니다. 닫힘을 알릴 까닭을 돌려줍니다. */
        StreamCloseReason releaseSlotLocked( uint32 slotIndex, bool bWasClean );

    private:
        IStreamHandler*       _pHandler;
        StreamTransportConfig _settings;
        mutable mutex         _mutex; ///< `_listSlot` · `_stats` — 보내기는 아무 스레드에서나 온다
        vector<Slot>          _listSlot;
        StreamTransportStats  _stats;
    };
} // namespace sw
