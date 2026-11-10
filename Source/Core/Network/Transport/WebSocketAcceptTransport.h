/**
 * @file WebSocketAcceptTransport.h
 * @brief 다른 스트림 전송을 감싸, 받은 연결 중 WebSocket 업그레이드로 시작하는 것을 바이트 관으로 풀어 주는 장식자입니다 — 브라우저 클라이언트가 같은 리슨에 붙는다.
 * @details 받은 연결의 첫 바이트로 가른다: `GET ` 이면 WebSocket(업그레이드 응답 → 이진 프레임의 몸만 위로), 아니면 원시 스트림(그대로 통과).
 *          원시 스트림의 첫 바이트는 TLS 레코드(0x16) 또는 `StreamFrame` 길이(LE u32 — `GET ` 은 약 542 MB 라 프레임 상한을 넘는다)라 겹치지 않는다.
 *          그래서 포트 · 서버 목록 · 설정을 따로 두지 않는다(유니티 Mirror 의 다중 전송처럼 포트를 나누지 않는 길).
 *          **전제**: 이 리슨의 프로토콜은 클라이언트가 먼저 말한다(TLS ClientHello · 요청). 첫 바이트가 오기 전에 위 층이 보낸 것은 갈래가 정해질 때까지 쌓아 둔다.
 *          위 층은 수락 때 `onStreamOpened` 를 받는다(원시 스트림과 같다). 내가 건 연결(`connect`)은 감싸지 않는다.
 *          Origin 은 보지 않는다 — 이 프로토콜은 쿠키 같은 주변 자격이 없고 로그인 토큰을 스트림 안에서 주고받아, 다른 출처 페이지가 사용자 자격을 빌릴 길이 없다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/WebSocketProtocol.h"

namespace sw
{
    class SW_API WebSocketAcceptTransport final : public IStreamTransport
    {
    public:
        /** @param pInner 감쌀 전송(소유권을 받는다 — 보통 `StreamTransportFactory::createPlatformTransport()`). */
        explicit WebSocketAcceptTransport( unique_ptr<IStreamTransport> pInner );
        ~WebSocketAcceptTransport() override;

        [[nodiscard]] bool     initialize( IStreamHandler* pHandler, const StreamTransportConfig& settings ) override;
        void                   shutdown() override { _pInner->shutdown(); }
        [[nodiscard]] bool     listen( const NetAddress& bindAddress ) override { return _pInner->listen( bindAddress ); }
        uint16                 getListenPort() const override { return _pInner->getListenPort(); }
        StreamConnectionHandle connect( const NetAddress& remote ) override { return _pInner->connect( remote ); }
        StreamSendResult       send( StreamConnectionHandle handle, const uint8* pData, int32 size ) override;
        void                   close( StreamConnectionHandle handle, StreamCloseMode mode ) override;
        void                   setReceivePaused( StreamConnectionHandle handle, bool bPaused ) override { _pInner->setReceivePaused( handle, bPaused ); }
        int32                  pollIO( int32 timeoutMilli ) override { return _pInner->pollIO( timeoutMilli ); }
        NetAddress             getRemoteAddress( StreamConnectionHandle handle ) const override { return _pInner->getRemoteAddress( handle ); }
        StreamTransportStats   getStats() const override { return _pInner->getStats(); }

    private:
        /** @brief 받은 연결의 갈래입니다. 받기 길(한 연결에 한 스레드씩)만 바꾸고, 바꿀 때는 그 연결의 보내기 잠금을 쥔다. */
        enum class ConnectionMode : uint8
        {
            Sniff,     ///< 첫 바이트를 기다린다
            Handshake, ///< `GET ` 을 봤다 — 머리 끝(빈 줄)을 기다린다
            WebSocket, ///< 업그레이드했다 — 받기는 프레임을 풀고 보내기는 이진 프레임으로 싼다
            Raw        ///< 원시 스트림 — 그대로 통과
        };

        struct Connection
        {
            mutex                 _sendMutex{};                   ///< 갈래 바꿈 · 보내기 · 쌓인 것 흘리기의 순서를 지킨다(안쪽 `send` 를 쥔 채 부른다 — 안쪽 콜백은 이 잠금을 잡지 않는다)
            ConnectionMode        _mode{ ConnectionMode::Sniff }; ///< `_sendMutex` 로 읽고 쓴다
            vector<uint8>         _pendingSend{};                 ///< 갈래가 정해지기 전에 위 층이 보낸 바이트
            vector<uint8>         _handshake{};                   ///< 받기 길 전용 — 갈래를 정할 때까지 받은 바이트
            WebSocketFrameDecoder _decoder{};                     ///< 받기 길 전용(몸 상한 = 받기 적체 기본 상한)
        };

        /** @brief 안쪽 전송의 처리기 — 받은 사건을 바깥 전송의 길로 넘긴다(전송이 처리기를 겸하지 않게 멤버로 든다). */
        class InnerHandler final : public IStreamHandler
        {
        public:
            explicit InnerHandler( WebSocketAcceptTransport& owner )
                : _owner{ owner }
            {
            }

            void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override { _owner.handleOpened( handle, remote, bAccepted ); }
            void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override { _owner.handleReceived( handle, pData, size ); }
            void onStreamWritable( StreamConnectionHandle handle ) override { _owner._pHandler->onStreamWritable( handle ); }
            void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override { _owner.handleClosed( handle, reason ); }

        private:
            WebSocketAcceptTransport& _owner;
        };

        void handleOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted );
        void handleReceived( StreamConnectionHandle handle, const uint8* pData, int32 size );
        void handleClosed( StreamConnectionHandle handle, StreamCloseReason reason );

        shared_ptr<Connection> findConnection( StreamConnectionHandle handle ) const;
        /** @brief 첫 바이트를 모아 갈래를 정합니다. 업그레이드가 끝나면 머리 뒤에 붙어 온 바이트를 프레임 해독기에 넘깁니다. */
        void detectProtocol( StreamConnectionHandle handle, Connection& connection );
        void receiveFrames( StreamConnectionHandle handle, Connection& connection, const uint8* pData, int32 size );
        /** @brief 프레임 하나를 싸서 보냅니다 — `_sendMutex` 를 쥔 채 부른다. */
        StreamSendResult sendFrameLocked( StreamConnectionHandle handle, WebSocketFrameKind kind, const uint8* pData, int32 size );

    private:
        unique_ptr<IStreamTransport>                  _pInner;
        InnerHandler                                  _innerHandler;
        IStreamHandler*                               _pHandler;
        int32                                         _maxQueuedSendBytes;
        mutable mutex                                 _mutex;         ///< `_mapConnection` 만 지킨다(안쪽을 부르는 동안 쥐지 않는다)
        unordered_map<uint64, shared_ptr<Connection>> _mapConnection; ///< 받은 연결만(키 `StreamConnectionHandle::packed`)
    };
} // namespace sw
