/**
 * @file LoopbackPkceLoginClient.h
 * @brief PC 의 외부 로그인입니다. 시스템 브라우저로 제공자의 인증 주소를 열고, 127.0.0.1 의 임시 포트로 돌아온 리다이렉트에서 코드를 받아 토큰 주소에서 PKCE 로 교환합니다(RFC 8252, RFC 7636).
 * @details - 요청마다 code_verifier(난수 32 B, base64url), S256 code_challenge, state(위조 방지), nonce(OIDC 재사용 방지)를 만든다.
 *          - 리다이렉트는 `http://127.0.0.1:<포트>/callback?code=…&state=…` 형식이며 state 가 맞아야 코드를 쓴다. `error=` 가 오면 취소나 실패로 처리한다.
 *          - 토큰 교환은 `POST <토큰 주소>` 로 하며 폼 필드는 grant_type, code, redirect_uri, client_id, code_verifier 이다. 티켓은 `id_token|nonce`(OIDC) 또는 `access_token` 이다.
 *          - client secret 은 쓰지 않는다. 배포된 클라이언트는 비밀을 지킬 수 없으므로 공개 클라이언트와 PKCE 를 쓴다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/IDAllocator.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Network/Transport/StreamTypes.h"

#include "GameFramework/Base/Online/HTTP/HTTPClient.h"
#include "GameFramework/Base/Online/HTTP/HTTPServer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Runtime/API/PlatformLoginClient.h"

namespace sw
{
    class INetSecurityProvider;
    class IStreamTransport;

    /** @brief 제공자 하나의 PKCE 설정입니다. 앱 등록에서 받은 client id 와 주소를 담은 데이터입니다. */
    struct PkceLoginProviderConfig
    {
        string _provider{};         ///< 계정 서버의 제공자 이름과 같은 값이다
        string _authorizationURL{}; ///< 인증 주소이다. 브라우저가 연다
        string _tokenURL{};         ///< 토큰 주소이다. 이 객체가 POST 한다
        string _clientID{};
        string _scope{ "openid" };
        int64  _timeoutMs{ 300000 };    ///< 사용자가 브라우저에서 끝낼 때까지 기다리는 시간이다(5분)
        uint8  _bUseIDToken{ SW_TRUE }; ///< true 이면 티켓이 id_token|nonce(OIDC)이고, 아니면 access_token(프로필 API 방식)이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class LoopbackPkceLoginClient
     * @brief PC 루프백과 PKCE 를 쓰는 외부 로그인 클라이언트입니다.
     */
    class SW_KIT_API LoopbackPkceLoginClient final : public IPlatformLoginClient
    {
    public:
        /** @param clock 토큰 요청(HTTP)의 시한을 재는 단조 시계입니다. 빌려 쓰므로 이 객체보다 오래 살아야 합니다. */
        explicit LoopbackPkceLoginClient( const IClock& clock );
        ~LoopbackPkceLoginClient() override;

        /**
         * @brief 리다이렉트 서버(127.0.0.1:0)와 토큰 교환 클라이언트를 시작합니다. 전송 둘은 소유권을 넘겨받고, @p pProvider(난수, SHA-256)와 @p pBrowser 는 빌려 씁니다.
         * @details 토큰 주소가 `https://` 이면 그 호스트의 TLS 컨텍스트를 `getHTTPClient().registerTLSContext` 로 등록한다.
         */
        [[nodiscard]] bool initialize( unique_ptr<IStreamTransport> serverTransport, unique_ptr<IStreamTransport> clientTransport,
                                       const StreamTransportConfig& transportSettings, INetSecurityProvider* pProvider, IExternalBrowser* pBrowser,
                                       const vector<PkceLoginProviderConfig>& listProviderSettings );
        void               shutdown();

        uint64 beginLogin( string_view provider, MonotonicMilliseconds nowMs ) override;
        void   update( MonotonicMilliseconds nowMs ) override;
        int32  pollResults( vector<PlatformLoginClientResult>& outListResult ) override;

        HTTPClient& getHTTPClient() { return _httpClient; }
        uint16      getRedirectPort() const { return _redirectServer.getListenPort(); }

        // 요청 처리 함수(`HTTPRequestFunction`)이며 `tick` 을 부르는 스레드에서 실행한다
        void onHTTPRequest( const HTTPServerRequest& request, HTTPServerResponse& outResponse );

    private:
        struct PendingLogin
        {
            string                _provider{};
            string                _codeVerifier{};
            string                _state{};
            string                _nonce{};
            uint64                _requestID{ 0 };
            uint64                _tokenRequestID{ 0 }; ///< 토큰 교환 중이면 0이 아니다
            MonotonicMilliseconds _deadlineMs{ 0 };
        };

        const PkceLoginProviderConfig* findSettings( string_view provider ) const;
        [[nodiscard]] bool             makeRandomText( int32 byteCount, string& outText );
        void                           finish( PendingLogin& pending, bool bSucceeded, bool bCancelled, string_view failureText, vector<uint8> ticketBytes );
        string                         makeRedirectURI() const;

        HTTPServer                        _redirectServer;
        HTTPClient                        _httpClient;
        vector<PkceLoginProviderConfig>   _listProviderSettings;
        vector<PendingLogin>              _listPending;
        vector<PlatformLoginClientResult> _listDone;
        INetSecurityProvider*             _pProvider;
        IExternalBrowser*                 _pBrowser;
        MonotonicMilliseconds             _nowMs;
        IDAllocator<uint64>               _requestIDAllocator;
    };
} // namespace sw
