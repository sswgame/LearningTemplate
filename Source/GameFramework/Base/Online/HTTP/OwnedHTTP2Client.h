/**
 * @file OwnedHTTP2Client.h
 * @brief HTTP/2 를 쓰는 외부 서비스 제공자(APNs)의 HTTP 클라이언트 묶음입니다 — 플랫폼 전송을 띄운 `HTTP2Client` 와, 그 클라이언트가 빌려 쓰는 호스트별 클라이언트 TLS 컨텍스트(ALPN `h2`)를 함께 소유합니다.
 * @details `OwnedHTTPClient` 와 같은 모양이다(HTTP/1.1 용). TLS 컨텍스트가 `h2` 를 협상하도록 만든다는 점만 다르다. `https://` 주소가 있는데 신뢰 파일이 없으면 오류다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Base/Online/HTTP/HTTP2Client.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ITLSContext;

    /**
     * @class OwnedHTTP2Client
     * @brief `HTTP2Client` 와 TLS 컨텍스트의 소유자입니다. 서비스 스레드 하나에서 씁니다.
     */
    class SW_GF_API OwnedHTTP2Client
    {
    public:
        /** @param clock 요청 시한을 재는 단조 시계입니다. 빌려 쓰므로 이 객체보다 오래 살아야 합니다. */
        explicit OwnedHTTP2Client( const IClock& clock );
        ~OwnedHTTP2Client();

        OwnedHTTP2Client( const OwnedHTTP2Client& )            = delete;
        OwnedHTTP2Client& operator=( const OwnedHTTP2Client& ) = delete;

        /** @brief 플랫폼 전송으로 클라이언트를 띄웁니다. 실패하면 false 입니다. */
        [[nodiscard]] bool initialize();
        /** @brief 날아가던 요청을 끝내고 TLS 컨텍스트를 놓습니다. 두 번 불러도 됩니다. */
        void shutdown();

        /**
         * @brief @p url 의 호스트에 쓸 클라이언트 TLS 컨텍스트(ALPN `h2`)를 @p trustFile(CA 번들 PEM)로 만들어 등록합니다. 이미 등록한 호스트는 건너뜁니다.
         * @details 빈 @p url 은 건너뜁니다. 주소가 틀렸거나 `https://` 가 아니거나 신뢰 파일이 비었으면 false 와 @p outError 입니다(@p trustOption 은 그 파일을 정하는 설정 이름).
         */
        [[nodiscard]] bool registerHostTLS( string_view url, string_view trustFile, string_view trustOption, string& outError );

        HTTP2Client& get() { return _client; }

    private:
        vector<unique_ptr<ITLSContext>> _listTLSContext; ///< `_client` 보다 오래 산다(먼저 선언)
        unordered_set<string>           _uniqueSetHost;  ///< TLS 컨텍스트를 등록한 호스트
        HTTP2Client                     _client;
    };
} // namespace sw
