/**
 * @file WebSocketStreamTransport.h
 * @brief 웹의 스트림 전송 — 브라우저에는 원시 TCP 가 없어 연결 하나를 WebSocket(이진 프레임) 하나로 연다. 서버는 같은 리슨에서 `WebSocketAcceptTransport` 가 받는다.
 * @details Emscripten `emscripten/websocket.h`(-lwebsocket.js)를 쓴다. 소켓은 늘 브라우저 메인 스레드에 있고(라이브러리가 그렇게 만든다 — 다른 스레드의 호출은 메인으로
 *          동기 위임된다), 콜백도 메인 런타임 스레드로 받는다 — 메인 루프가 프레임마다 브라우저에 돌려주므로 그 사이에 온다. 그래서 I/O 스레드를 두지 않고 `pollIO` 는 0 이다.
 *          연결 주소는 `ws://<IPv4>:<포트>/` 다. 할 수 없는 것: 리슨(브라우저는 받지 못한다 — false), 읽기 멈춤(브라우저 WebSocket 에 흐름 제어가 없다).
 *          shortcut: `ws://` 만 연다 — https 로 띄운 페이지는 혼합 콘텐츠라 `wss://`(서버 쪽 TLS 종단)가 필요하다, 배포 페이지를 https 로 올릴 때 수락 쪽 TLS 와 함께 바꾼다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Transport/IStreamTransport.h"

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
        /** @brief 연결 칸 하나입니다. 칸 번호가 핸들의 번호, `_generation` 이 세대입니다(닫힌 칸을 다시 쓰면 오른다). */
        struct Slot
        {
            int32      _socket{ 0 }; ///< Emscripten 소켓 번호(0 = 빈 칸)
            uint32     _generation{ 0 };
            NetAddress _remote{};
            uint8      _bOpen{ SW_FALSE };    ///< onopen 을 받았다
            uint8      _bClosing{ SW_FALSE }; ///< 이쪽이 닫았다 — 닫힘 까닭은 LocalClose
        };

        static constexpr int32 kNoSlot = constant::invalid_index::kInt32;

        int32 findSlotLocked( int32 socket ) const;
        int32 findSlotLocked( StreamConnectionHandle handle ) const;
        /** @brief 칸을 비우고 소켓을 놓습니다. 닫힘을 알릴 까닭을 돌려줍니다. */
        StreamCloseReason releaseSlotLocked( int32 slotIndex, bool bWasClean );

    private:
        IStreamHandler*       _pHandler;
        StreamTransportConfig _settings;
        mutable mutex         _mutex; ///< `_listSlot` · `_stats` — 보내기는 아무 스레드에서나 온다
        vector<Slot>          _listSlot;
        StreamTransportStats  _stats;
    };
} // namespace sw
