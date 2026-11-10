/**
 * @file WebSocketProtocol.h
 * @brief WebSocket(RFC 6455)의 서버 쪽 조각 — 업그레이드 요청 읽기 · 101 응답 · 프레임 쓰기 · 클라이언트 프레임 읽기입니다.
 * @details 스트림 전송 위의 바이트 관(管)으로만 쓴다: 데이터는 이진 프레임(이어 붙임 프레임 포함)의 몸을 순서대로 이은 바이트다 — 메시지 경계는 위 층의 길이 접두
 *          프레임(`StreamFrame`)이 정한다. 텍스트 프레임 · 확장(RSV 비트) · 하위 프로토콜은 받지 않는다(브라우저 클라이언트 `WebSocketStreamTransport` 가 쓰지 않는다).
 *          쓰는 곳: `WebSocketAcceptTransport`(서버 수락). 언리얼 WebSocketNetworking 플러그인의 서버(libwebsockets)와 같은 자리다(기억).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Transport/StreamTypes.h"

namespace sw
{
    /** @brief 프레임 종류 — 값이 머리 첫 바이트의 opcode 4 비트다(RFC 6455 5.2). 8 이상은 제어 프레임이다. */
    enum class WebSocketFrameKind : uint8
    {
        Continuation = 0,
        Text         = 1,
        Binary       = 2,
        Close        = 8,
        Ping         = Close + 1,
        Pong         = Close + 2
    };

    /** @brief 업그레이드 요청 · 응답과 서버 프레임 쓰기입니다(전부 static). */
    struct SW_API WebSocketProtocol
    {
        static constexpr int32       kMaxHandshakeBytes = 8 * 1024; ///< 업그레이드 요청 머리의 상한 — 넘으면 닫는다(머리가 끝나지 않는 연결이 메모리를 잡지 않게)
        static constexpr int32       kMaxControlPayload = 125;      ///< 제어 프레임(Close · Ping · Pong) 몸의 상한(RFC 6455 5.5)
        static constexpr uint16      kCloseCodeNormal   = 1000;
        static constexpr uint16      kCloseCodeProtocol = 1002;
        static constexpr const utf8* kRequestPrefix     = "GET ";     ///< 업그레이드 요청의 첫 네 바이트 — 수락 쪽이 원시 스트림과 가르는 표시
        static constexpr const utf8* kHeaderTerminator  = "\r\n\r\n"; ///< HTTP 머리의 끝

        /** @brief `Sec-WebSocket-Accept` 값 — base64( SHA-1( 키 + RFC 6455 GUID ) )입니다. */
        static string makeAcceptKey( string_view clientKey );
        /**
         * @brief 업그레이드 요청 머리(@p request — 끝 빈 줄까지)를 읽어 `Sec-WebSocket-Key` 를 꺼냅니다.
         * @return `GET` · `Upgrade: websocket` · `Connection` 에 `Upgrade` · `Sec-WebSocket-Version: 13` · 키(16 바이트의 base64)가 다 있을 때만 true 입니다.
         */
        [[nodiscard]] static bool parseUpgradeRequest( string_view request, string& outClientKey );
        /** @brief 101 Switching Protocols 응답 머리입니다(하위 프로토콜 · 확장 없음). */
        static string makeUpgradeResponse( string_view clientKey );
        /** @brief 업그레이드를 거절하는 응답(400)입니다 — 보낸 뒤 닫는다. */
        static string makeRejectResponse();
        /** @brief 서버 프레임 하나(FIN, 가리개 없음 — 서버는 가리지 않는다, RFC 6455 5.1)를 @p outBytes 뒤에 붙입니다. */
        static void appendFrame( vector<uint8>& outBytes, WebSocketFrameKind kind, const uint8* pData, int32 size );
    };
} // namespace sw

namespace sw
{
    /** @brief 읽은 클라이언트 프레임 하나 — 몸은 해독기 버퍼(가리개를 벗긴 것)를 가리키며 다음 `append` 전까지 유효합니다. */
    struct WebSocketFrameView
    {
        const uint8*       _pPayload{ nullptr };
        int32              _payloadSize{ 0 };
        WebSocketFrameKind _kind{ WebSocketFrameKind::Binary };
    };

    enum class WebSocketDecodeResult : uint8
    {
        Frame = 0, ///< `outFrame` 에 하나
        NeedMore,  ///< 더 받아야 한다
        Malformed  ///< 가리지 않은 프레임 · RSV 비트 · 모르는 opcode · 상한 초과 · 조각난 제어 프레임 — 연결을 닫는다
    };

    /**
     * @class WebSocketFrameDecoder
     * @brief 클라이언트 → 서버 프레임을 읽습니다. 클라이언트 프레임은 반드시 가려져 있다(RFC 6455 5.1 — 아니면 Malformed).
     * @details 한 연결의 받기 콜백은 순서대로 하나씩 오므로(IStreamHandler 약속) 잠금이 없다. 몸의 상한은 받기 전에 검사한다 — 머리만 와도 큰 버퍼를 잡지 않는다.
     */
    class SW_API WebSocketFrameDecoder
    {
    public:
        explicit WebSocketFrameDecoder( int32 maxPayloadBytes = StreamLimit::kDefaultMaxPendingReceiveBytes );

        void                                append( const uint8* pData, int32 size );
        [[nodiscard]] WebSocketDecodeResult next( WebSocketFrameView& outFrame );

    private:
        vector<uint8> _buffer;
        size_t        _readOffset;
        int32         _maxPayloadBytes;
    };
} // namespace sw
