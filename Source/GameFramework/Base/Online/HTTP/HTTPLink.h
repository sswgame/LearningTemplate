/**
 * @file HTTPLink.h
 * @brief HTTP 연결 하나의 바이트 길 — 스트림 핸들 + 선택 TLS 세션. 평문을 선에 올리고(TLS 면 세션을 지나) 받은 바이트를 평문으로 풉니다. 클라이언트 · 서버가 같이 씁니다.
 * @details 스레드 안전하지 않다 — 주인(`HTTPClient` · `HTTPServer`)이 잠금을 쥐고 부른다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
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
        HTTPLink();
        ~HTTPLink();

        HTTPLink( const HTTPLink& )            = delete;
        HTTPLink& operator=( const HTTPLink& ) = delete;

        /** @brief @p pTLSContext 가 있으면 세션을 만든다(실패하면 false). 핸들은 연결을 건 · 받은 뒤 `setHandle`. */
        [[nodiscard]] bool initialize( ITLSContext* pTLSContext );
        void               setHandle( StreamConnectionHandle handle ) { _handle = handle; }
        /** @brief 평문을 선에 올립니다(TLS 핸드셰이크 전이면 세션이 모았다가 보낸다). */
        [[nodiscard]] bool writePlain( IStreamTransport& transport, const uint8* pData, size_t size );
        /** @brief 받은 바이트를 평문으로 풀어 @p outPlainBytes 뒤에 붙입니다. TLS 실패면 false. */
        [[nodiscard]] bool readReceived( IStreamTransport& transport, const uint8* pData, int32 size, vector<uint8>& outPlainBytes );
        /** @brief TLS 핸드셰이크를 시작합니다(클라이언트 — 열린 직후). */
        [[nodiscard]] bool flushHandshake( IStreamTransport& transport );

        StreamConnectionHandle getHandle() const { return _handle; }
        bool                   isSecure() const { return _tlsSession != nullptr; }
        const utf8*            getFailureText() const;

    private:
        [[nodiscard]] bool flushCiphertext( IStreamTransport& transport );

        vector<uint8>           _cipherBytes;
        unique_ptr<ITLSSession> _tlsSession;
        StreamConnectionHandle  _handle;
    };
} // namespace sw
