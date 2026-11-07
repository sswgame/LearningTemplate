/**
 * @file HTTPLink.h
 * @brief HTTP 연결 하나의 바이트 경로입니다. 스트림 핸들과 선택적 TLS 세션으로 이루어집니다.
 *        평문을 네트워크로 보내고(TLS 면 세션을 거쳐) 받은 바이트를 평문으로 복호화합니다. 클라이언트와 서버가 같이 씁니다.
 * @details 스레드 안전하지 않다. 주인(`HTTPClient`, `HTTP2Client`, `HTTPServer`)이 잠금을 잡고 부른다.
 *          링크는 연결 핸들을 받아야 생긴다(`open`). 핸들을 나중에 넣던 모양(`setHandle`)은 빠뜨려도 컴파일되어 HTTP/2 전송이 모두 졌다 —
 *          주인은 연결 전에는 링크 없이(`unique_ptr` 빈 값) TLS 맥락만 들고, 연결을 맺거나 받은 자리에서 링크를 연다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Transport/StreamTypes.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class IStreamTransport;
    class ITLSContext;
    class ITLSSession;

    /**
     * @class HTTPLink
     * @brief 바이트 길 하나입니다.
     */
    class SW_GF_API HTTPLink
    {
    public:
        /** @brief @p tlsSession 이 비면 평문 링크입니다. 보통은 `open` 으로 만든다. */
        HTTPLink( StreamConnectionHandle handle, unique_ptr<ITLSSession> tlsSession );
        ~HTTPLink();

        HTTPLink( const HTTPLink& )            = delete;
        HTTPLink& operator=( const HTTPLink& ) = delete;

        /**
         * @brief 맺거나 받은 연결 @p handle 의 링크를 엽니다. @p pTLSContext 가 있으면 TLS 세션을 만들고, 만들지 못하면 nullptr 입니다.
         * @details 연결이 없으면 링크도 없다 — 핸들이 무효면 nullptr 입니다.
         */
        [[nodiscard]] static unique_ptr<HTTPLink> open( StreamConnectionHandle handle, ITLSContext* pTLSContext );
        /** @brief 평문을 연결에 싣습니다(TLS 핸드셰이크 전이면 세션이 모았다가 보낸다). */
        [[nodiscard]] bool writePlain( IStreamTransport& transport, const uint8* pData, size_t size );
        /** @brief 받은 바이트를 평문으로 복호화해 @p outPlainBytes 뒤에 붙입니다. TLS 실패면 false. */
        [[nodiscard]] bool readReceived( IStreamTransport& transport, const uint8* pData, int32 size, vector<uint8>& outPlainBytes );
        /** @brief TLS 핸드셰이크를 시작합니다. 클라이언트가 연결이 열린 직후 부릅니다. */
        [[nodiscard]] bool flushHandshake( IStreamTransport& transport );

        StreamConnectionHandle getHandle() const { return _handle; }
        bool                   isSecure() const { return _tlsSession != nullptr; }
        const utf8*            getFailureText() const;
        /** @brief TLS 핸드셰이크가 끝났는가(평문 연결은 늘 true). */
        bool isEstablished() const;
        /** @brief 핸드셰이크가 끝난 뒤 협상된 ALPN 프로토콜 이름입니다. 평문이거나 합의가 없으면 빈 문자열. */
        string getNegotiatedProtocol() const;

    private:
        [[nodiscard]] bool flushCiphertext( IStreamTransport& transport );

    private:
        vector<uint8>                _cipherBytes;
        unique_ptr<ITLSSession>      _tlsSession;
        const StreamConnectionHandle _handle;
    };
} // namespace sw
