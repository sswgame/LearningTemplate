/**
 * @file OwnedHTTPClient.h
 * @brief 외부 서비스 제공자 하나가 쓰는 HTTP 클라이언트 묶음입니다 — 플랫폼 전송을 띄운 `HTTPClient` 와, 그 클라이언트가 빌려 쓰는 호스트별 클라이언트 TLS 컨텍스트를 함께 소유합니다.
 * @details 제공자(외부 로그인, 푸시, 영수증)는 응답을 자기만 회수하므로 제공자마다 하나를 둡니다. TLS 컨텍스트는 클라이언트보다 오래 살아야 해서 한 객체에 담고 선언 순서로 소멸을 맞춥니다.
 *          `https://` 주소가 있는데 신뢰 파일이 없으면 오류입니다 — 개발용 인증서로 조용히 바꾸면 실제 제공자에 닿지 못한 채 기동이 끝나기 때문입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Base/Online/HTTP/HTTPClient.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ITLSContext;

    /**
     * @class OwnedHTTPClient
     * @brief `HTTPClient` 와 TLS 컨텍스트의 소유자입니다. 서비스 스레드 하나에서 씁니다.
     */
    class SW_GF_API OwnedHTTPClient
    {
    public:
        /** @param clock 요청 시한을 재는 단조 시계입니다. 빌려 쓰므로 이 객체보다 오래 살아야 합니다. */
        explicit OwnedHTTPClient( const IClock& clock );
        ~OwnedHTTPClient();

        OwnedHTTPClient( const OwnedHTTPClient& )            = delete;
        OwnedHTTPClient& operator=( const OwnedHTTPClient& ) = delete;

        /** @brief 플랫폼 전송으로 클라이언트를 띄웁니다. 실패하면 false 입니다. */
        [[nodiscard]] bool initialize();
        /** @brief 날아가던 요청을 끝내고 TLS 컨텍스트를 놓습니다. 두 번 불러도 됩니다. */
        void shutdown();

        /**
         * @brief @p url 이 `https://` 면 그 호스트의 클라이언트 TLS 컨텍스트를 @p trustFile(CA 번들 PEM)로 만들어 등록합니다. `http://` 와 이미 등록한 호스트는 건너뜁니다.
         * @details 빈 @p url 은 건너뜁니다. 주소가 틀렸거나 `https://` 인데 신뢰 파일이 비었으면 false 와 @p outError 입니다(@p trustOption 은 그 파일을 정하는 설정 이름으로, 오류 글에 적는다).
         */
        [[nodiscard]] bool registerHostTLS( string_view url, string_view trustFile, string_view trustOption, string& outError );

        HTTPClient& get() { return _client; }

    private:
        vector<unique_ptr<ITLSContext>> _listTLSContext; ///< `_client` 보다 오래 산다(먼저 선언)
        vector<string>                  _listHost;       ///< TLS 컨텍스트를 등록한 호스트
        HTTPClient                      _client;
    };
} // namespace sw
