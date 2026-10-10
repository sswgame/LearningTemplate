/**
 * @file LoopbackPkceLoginClient.h
 * @brief PC 외부 로그인 — 시스템 브라우저로 제공자의 인증 주소를 열고, 127.0.0.1 의 임시 포트로 돌아온 리다이렉트에서 코드를 받아 토큰 주소에서 PKCE 로 바꿉니다(RFC 8252 · 7636).
 * @details - 요청마다 code_verifier(난수 32 B · base64url) · S256 code_challenge · state(위조 막기) · nonce(OIDC 재사용 막기)를 만든다.
 *          - 리다이렉트는 `http://127.0.0.1:<포트>/callback?code=…&state=…` — state 가 맞아야 코드를 쓴다. `error=` 면 취소 · 실패.
 *          - 토큰 교환은 `POST <토큰 주소>`(폼: grant_type · code · redirect_uri · client_id · code_verifier). 표 = `id_token|nonce`(OIDC) 또는 `access_token`.
 *          - client secret 은 쓰지 않는다(배포된 클라이언트는 비밀을 지킬 수 없다 — 공개 클라이언트 + PKCE).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Network/Transport/StreamTypes.h"

#include "GameFramework/Base/Online/Http/HttpClient.h"
#include "GameFramework/Base/Online/Http/HttpServer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Shared/Api/PlatformLoginClient.h"

namespace sw
{
    class INetSecurityProvider;
    class IStreamTransport;

    /** @brief 제공자 하나의 PKCE 설정입니다(데이터 — 앱 등록의 client id 와 주소). */
    struct PkceLoginProviderSettings
    {
        string _provider{};         ///< 계정 서버의 제공자 이름과 같다
        string _authorizationUrl{}; ///< 인증 주소(브라우저가 연다)
        string _tokenUrl{};         ///< 토큰 주소(이 객체가 POST)
        string _clientId{};
        string _scope{ "openid" };
        int64  _timeoutMs{ 300000 };    ///< 사용자가 브라우저에서 끝낼 때까지(5 분)
        uint8  _bUseIdToken{ SW_TRUE }; ///< 표 = id_token|nonce(OIDC), 아니면 access_token(프로필 API 형)
    };
} // namespace sw

namespace sw
{
    /**
     * @class LoopbackPkceLoginClient
     * @brief PC 루프백 + PKCE 외부 로그인 클라이언트입니다.
     */
    class SW_GF_API LoopbackPkceLoginClient final : public IPlatformLoginClient, public IHttpRequestHandler
    {
    public:
        LoopbackPkceLoginClient();
        ~LoopbackPkceLoginClient() override;

        /**
         * @brief 리다이렉트 서버(127.0.0.1:0)와 토큰 교환 클라이언트를 띄웁니다. 전송 둘은 넘겨받고, @p pProvider(난수 · SHA-256) · @p pBrowser 는 빌려 쓴다.
         * @details 토큰 주소가 `https://` 면 그 호스트의 TLS 컨텍스트를 `getHttpClient().registerTLSContext` 로 올린다.
         */
        [[nodiscard]] bool initialize( unique_ptr<IStreamTransport> serverTransport, unique_ptr<IStreamTransport> clientTransport,
                                       const StreamTransportSettings& transportSettings, INetSecurityProvider* pProvider, IExternalBrowser* pBrowser,
                                       const vector<PkceLoginProviderSettings>& listProviderSettings );
        void               shutdown();

        uint64 beginLogin( string_view provider, int64 nowMs ) override;
        void   tick( int64 nowMs ) override;
        int32  pollResults( vector<PlatformLoginClientResult>& outListResult ) override;

        HttpClient& getHttpClient() { return _httpClient; }
        uint16      getRedirectPort() const { return _redirectServer.getListenPort(); }

        // IHttpRequestHandler — `tick` 의 스레드
        void onHttpRequest( const HttpServerRequest& request, HttpServerResponse& outResponse ) override;

    private:
        struct PendingLogin
        {
            string _provider{};
            string _codeVerifier{};
            string _state{};
            string _nonce{};
            uint64 _requestId{ 0 };
            uint64 _tokenRequestId{ 0 }; ///< 토큰 교환 중이면 0 이 아니다
            int64  _deadlineMs{ 0 };
        };

        const PkceLoginProviderSettings* findSettings( string_view provider ) const;
        [[nodiscard]] bool               makeRandomText( int32 byteCount, string& outText );
        void                             finish( PendingLogin& pending, bool bSucceeded, bool bCancelled, string_view failureText, vector<uint8> ticketBytes );
        string                           makeRedirectUri() const;

        HttpServer                        _redirectServer;
        HttpClient                        _httpClient;
        vector<PkceLoginProviderSettings> _listProviderSettings;
        vector<PendingLogin>              _listPending;
        vector<PlatformLoginClientResult> _listDone;
        INetSecurityProvider*             _pProvider;
        IExternalBrowser*                 _pBrowser;
        int64                             _nowMs;
        uint64                            _nextRequestId;
    };
} // namespace sw
