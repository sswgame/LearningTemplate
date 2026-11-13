// LoopbackPkceLoginClientTest — PC 외부 로그인(RFC 8252 루프백 리다이렉트 + RFC 7636 PKCE): 브라우저에 여는 인증 주소의 쿼리, 리다이렉트의 state 대조, 토큰 주소에 보내는 폼과
// code_challenge = base64url(SHA-256(code_verifier)) 의 짝, 티켓 모양(`id_token|nonce` · `access_token`), 거절 · 실패 · 시한. 브라우저는 가짜(주소를 적는다)이고 리다이렉트는 테스트의
// HTTP 클라이언트가, 토큰 주소는 루프백 망의 가짜 서버가 맡는다. SHA-256 은 가짜 요약이다(짝만 본다). 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Encoding/Base64Util.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Network/Transport/StreamTypes.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/API/LoopbackPkceLoginClient.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPClient.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPServer.h"
#include "GameFramework/Runtime/Online/Security/Tests/FakeNetSecurityProvider.h"

using namespace sw;

namespace
{
    constexpr int64 kNowMs = 1000;

    /** @brief 연 주소를 적는 가짜 시스템 브라우저입니다. */
    class FakeBrowser final : public IExternalBrowser
    {
    public:
        vector<string> _listURL{};
        uint8          _bOpenOk{ SW_TRUE };

        bool openURL( string_view url ) override
        {
            _listURL.emplace_back( url );
            return _bOpenOk == SW_TRUE;
        }
    };

    struct PkceTestUtil
    {
        /** @brief 주소의 `?` 뒤 쿼리를 풉니다. */
        static vector<HTTPHeader> readQuery( string_view url )
        {
            vector<HTTPHeader> listPair;
            const size_t       question = url.find( '?' );
            if ( question != string_view::npos )
                SW_EXPECT_TRUE( HTTPUtil::decodeForm( url.substr( question + 1 ), listPair ) );
            return listPair;
        }

        static string find( const vector<HTTPHeader>& listPair, string_view name )
        {
            for ( const HTTPHeader& pair : listPair )
            {
                if ( pair._name == name )
                    return pair._value;
            }
            return {};
        }
    };

    /** @brief 가짜 토큰 서버 · 리다이렉트를 보낼 테스트 클라이언트 · 로그인 클라이언트입니다. 선언 순서가 수명이다 — 망이 전송들보다 오래 산다. */
    struct PkceFixture
    {
        LoopbackStreamNetwork         _network{ 13u };
        ManualClock                   _clock{ MonotonicMilliseconds{ 0 } };
        test::FakeNetSecurityProvider _security{};
        FakeBrowser                   _browser{};
        HTTPServer                    _tokenServer{};
        HTTPClient                    _redirectSender{ _clock }; ///< 브라우저가 리다이렉트를 따라가는 몫
        LoopbackPkceLoginClient       _login{ _clock };
        vector<HTTPServerRequest>     _listTokenRequest{};
        vector<HTTPClientResponse>    _listRedirectResponse{};
        string                        _tokenBody{ R"({"id_token":"ID-TOKEN","access_token":"ACCESS","token_type":"Bearer"})" };
        int32                         _tokenStatus{ HTTPConstant::kStatusOk };

        explicit PkceFixture( bool bUseIDToken = true )
        {
            _security._bRandomOk = SW_TRUE;
            _security._bHashOk   = SW_TRUE;
            StreamTransportConfig transportSettings;
            transportSettings._ioThreadCount = 0;
            SW_EXPECT_TRUE( _tokenServer.initialize( _network.createTransport(), transportSettings, HTTPServerConfig{}, HTTPRequestFunction::create<&PkceFixture::onTokenRequest>( this ) ) );
            SW_EXPECT_TRUE( _redirectSender.initialize( _network.createTransport(), transportSettings, HTTPClientConfig{} ) );
            PkceLoginProviderConfig settings;
            settings._provider         = "google";
            settings._authorizationURL = "https://accounts.example/o/oauth2/v2/auth?prompt=select_account";
            settings._tokenURL         = "http://127.0.0.1:" + to_string( static_cast<int32>( _tokenServer.getListenPort() ) ) + "/token";
            settings._clientID         = "desktop-client";
            settings._scope            = "openid profile";
            settings._timeoutMs        = 5000;
            settings._bUseIDToken      = bUseIDToken ? SW_TRUE : SW_FALSE;
            SW_EXPECT_TRUE( _login.initialize( _network.createTransport(), _network.createTransport(), transportSettings, &_security, &_browser, { settings } ) );
        }

        void onTokenRequest( const HTTPServerRequest& request, HTTPServerResponse& outResponse )
        {
            _listTokenRequest.push_back( request );
            outResponse._statusCode = _tokenStatus;
            outResponse._bodyBytes.assign( _tokenBody.begin(), _tokenBody.end() );
        }

        void poll( int64 nowMs = kNowMs )
        {
            for ( int32 round = 0; round < 40; ++round )
            {
                _redirectSender.update();
                (void)_redirectSender.pollResponses( _listRedirectResponse );
                _login.update( MonotonicMilliseconds{ nowMs } );
                _tokenServer.update();
            }
        }

        /** @brief 브라우저가 리다이렉트 주소를 연 것처럼 보냅니다. @p query 는 `code=…&state=…` 꼴입니다. */
        void redirect( string_view query )
        {
            HTTPClientRequest request;
            request._url = "http://127.0.0.1:" + to_string( static_cast<int32>( _login.getRedirectPort() ) ) + "/callback?" + string( query );
            (void)_redirectSender.submitRequest( request );
            poll();
        }

        string getAuthorizationState() const { return PkceTestUtil::find( PkceTestUtil::readQuery( _browser._listURL.back() ), "state" ); }

        vector<PlatformLoginClientResult> takeResults()
        {
            vector<PlatformLoginClientResult> listResult;
            (void)_login.pollResults( listResult );
            return listResult;
        }
    };
} // namespace

/** @brief [LoopbackPkceLoginClientTest] 인증 주소는 기존 쿼리 뒤에 response_type · client_id · 루프백 redirect_uri · scope · state · nonce · S256 challenge 를 덧붙인다 */
SW_TEST_CASE( LoopbackPkceLoginClientTest, OpensTheAuthorizationURL )
{
    PkceFixture rig;
    (void)rig._login.beginLogin( "google", MonotonicMilliseconds{ kNowMs } );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._browser._listURL.size() );
    const string& url = rig._browser._listURL[0];
    SW_EXPECT_TRUE( url.rfind( "https://accounts.example/o/oauth2/v2/auth?prompt=select_account&", 0 ) == 0 );

    const vector<HTTPHeader> listQuery = PkceTestUtil::readQuery( url );
    SW_EXPECT_EQUAL( string( "select_account" ), PkceTestUtil::find( listQuery, "prompt" ) );
    SW_EXPECT_EQUAL( string( "code" ), PkceTestUtil::find( listQuery, "response_type" ) );
    SW_EXPECT_EQUAL( string( "desktop-client" ), PkceTestUtil::find( listQuery, "client_id" ) );
    SW_EXPECT_EQUAL( "http://127.0.0.1:" + to_string( static_cast<int32>( rig._login.getRedirectPort() ) ) + "/callback", PkceTestUtil::find( listQuery, "redirect_uri" ) );
    SW_EXPECT_EQUAL( string( "openid profile" ), PkceTestUtil::find( listQuery, "scope" ) );
    SW_EXPECT_EQUAL( string( "S256" ), PkceTestUtil::find( listQuery, "code_challenge_method" ) );
    SW_EXPECT_FALSE( PkceTestUtil::find( listQuery, "state" ).empty() );
    SW_EXPECT_FALSE( PkceTestUtil::find( listQuery, "nonce" ).empty() );
    SW_EXPECT_NOT_EQUAL( PkceTestUtil::find( listQuery, "state" ), PkceTestUtil::find( listQuery, "nonce" ) );
    SW_EXPECT_EQUAL( size_t( 43 ), PkceTestUtil::find( listQuery, "code_challenge" ).size() ); // 32 바이트 요약의 base64url(채움 없음)
    SW_EXPECT_TRUE( rig.takeResults().empty() );

    SW_EXPECT_LOG( "LoopbackPkceLoginClient.ExternalSignDidNot" );
    rig._login.shutdown(); // 기다리던 로그인은 취소로 끝난다
    const vector<PlatformLoginClientResult> listResult = rig.takeResults();
    SW_REQUIRE_EQUAL( size_t( 1 ), listResult.size() );
    SW_EXPECT_TRUE( listResult[0]._bCancelled == SW_TRUE );
}

/** @brief [LoopbackPkceLoginClientTest] 맞는 state 의 리다이렉트 코드를 폼(grant_type · code · redirect_uri · client_id · code_verifier)으로 바꾸고, 티켓은 `id_token|nonce` 이며 challenge 는 그 verifier 의 요약이다 */
SW_TEST_CASE( LoopbackPkceLoginClientTest, ExchangesTheCodeWithTheVerifier )
{
    PkceFixture  rig;
    const uint64 requestID = rig._login.beginLogin( "google", MonotonicMilliseconds{ kNowMs } );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._browser._listURL.size() );
    const vector<HTTPHeader> listQuery = PkceTestUtil::readQuery( rig._browser._listURL[0] );
    rig.redirect( "code=AUTH-CODE&state=" + PkceTestUtil::find( listQuery, "state" ) );

    SW_REQUIRE_EQUAL( size_t( 1 ), rig._listRedirectResponse.size() );
    SW_EXPECT_EQUAL( HTTPConstant::kStatusOk, rig._listRedirectResponse[0]._statusCode ); // 브라우저에 "끝났다" 쪽이 뜬다
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._listTokenRequest.size() );
    const HTTPServerRequest& token = rig._listTokenRequest[0];
    SW_EXPECT_TRUE( token._method == HTTPMethod::Post );
    SW_EXPECT_EQUAL( string( "/token" ), token._path );
    const HTTPHeader* pContentType = token.findHeader( "Content-Type" );
    SW_REQUIRE_TRUE( pContentType != nullptr );
    SW_EXPECT_EQUAL( string( "application/x-www-form-urlencoded" ), pContentType->_value );
    vector<HTTPHeader> listForm;
    SW_REQUIRE_TRUE( HTTPUtil::decodeForm( token.getBodyText(), listForm ) );
    SW_EXPECT_EQUAL( string( "authorization_code" ), PkceTestUtil::find( listForm, "grant_type" ) );
    SW_EXPECT_EQUAL( string( "AUTH-CODE" ), PkceTestUtil::find( listForm, "code" ) );
    SW_EXPECT_EQUAL( PkceTestUtil::find( listQuery, "redirect_uri" ), PkceTestUtil::find( listForm, "redirect_uri" ) );
    SW_EXPECT_EQUAL( string( "desktop-client" ), PkceTestUtil::find( listForm, "client_id" ) );
    SW_EXPECT_TRUE( PkceTestUtil::find( listForm, "client_secret" ).empty() ); // 공개 클라이언트는 비밀을 싣지 않는다

    const string verifier = PkceTestUtil::find( listForm, "code_verifier" );
    SW_EXPECT_EQUAL( size_t( 43 ), verifier.size() ); // RFC 7636 4.1 — 43~128 자
    uint8 arrDigest[NetSecurityConstant::kSHA256Size];
    test::FakeNetSecurityProvider::computeFakeDigest( reinterpret_cast<const uint8*>( verifier.data() ), static_cast<int32>( verifier.size() ), arrDigest );
    SW_EXPECT_EQUAL( Base64Util::encodeURL( arrDigest, sizeof( arrDigest ) ), PkceTestUtil::find( listQuery, "code_challenge" ) );

    const vector<PlatformLoginClientResult> listResult = rig.takeResults();
    SW_REQUIRE_EQUAL( size_t( 1 ), listResult.size() );
    SW_EXPECT_EQUAL( requestID, listResult[0]._requestID );
    SW_EXPECT_TRUE( listResult[0]._bSucceeded == SW_TRUE );
    SW_EXPECT_EQUAL( string( "google" ), listResult[0]._provider );
    const string ticket( reinterpret_cast<const utf8*>( listResult[0]._ticket.data() ), listResult[0]._ticket.size() );
    SW_EXPECT_EQUAL( "ID-TOKEN|" + PkceTestUtil::find( listQuery, "nonce" ), ticket );
}

/** @brief [LoopbackPkceLoginClientTest] 프로필 API 방식이면 티켓은 access_token 그대로이다 */
SW_TEST_CASE( LoopbackPkceLoginClientTest, AccessTokenModeUsesTheAccessToken )
{
    PkceFixture rig( false );
    (void)rig._login.beginLogin( "google", MonotonicMilliseconds{ kNowMs } );
    rig.redirect( "code=C&state=" + rig.getAuthorizationState() );
    const vector<PlatformLoginClientResult> listResult = rig.takeResults();
    SW_REQUIRE_EQUAL( size_t( 1 ), listResult.size() );
    SW_EXPECT_TRUE( listResult[0]._bSucceeded == SW_TRUE );
    SW_EXPECT_EQUAL( string( "ACCESS" ), string( reinterpret_cast<const utf8*>( listResult[0]._ticket.data() ), listResult[0]._ticket.size() ) );
}

/** @brief [LoopbackPkceLoginClientTest] 모르는 state 는 400 이고 어떤 로그인도 바꾸지 않으며, 다른 경로는 404 다. 사용자가 거절하면(access_denied) 취소다 */
SW_TEST_CASE( LoopbackPkceLoginClientTest, CraftedStateIsIgnoredAndDenialCancels )
{
    PkceFixture rig;
    (void)rig._login.beginLogin( "google", MonotonicMilliseconds{ kNowMs } );
    rig.redirect( "code=STOLEN&state=forged" );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._listRedirectResponse.size() );
    SW_EXPECT_EQUAL( 400, rig._listRedirectResponse[0]._statusCode );
    SW_EXPECT_TRUE( rig._listTokenRequest.empty() );
    SW_EXPECT_TRUE( rig.takeResults().empty() );

    HTTPClientRequest other;
    other._url = "http://127.0.0.1:" + to_string( static_cast<int32>( rig._login.getRedirectPort() ) ) + "/favicon.ico";
    (void)rig._redirectSender.submitRequest( other );
    rig.poll();
    SW_REQUIRE_EQUAL( size_t( 2 ), rig._listRedirectResponse.size() );
    SW_EXPECT_EQUAL( HTTPConstant::kStatusNotFound, rig._listRedirectResponse[1]._statusCode );

    SW_EXPECT_LOG( "LoopbackPkceLoginClient.ExternalSignDidNot" );
    rig.redirect( "error=access_denied&state=" + rig.getAuthorizationState() );
    const vector<PlatformLoginClientResult> listResult = rig.takeResults();
    SW_REQUIRE_EQUAL( size_t( 1 ), listResult.size() );
    SW_EXPECT_TRUE( listResult[0]._bSucceeded == SW_FALSE );
    SW_EXPECT_TRUE( listResult[0]._bCancelled == SW_TRUE );
    SW_EXPECT_TRUE( rig._listTokenRequest.empty() );
}

/** @brief [LoopbackPkceLoginClientTest] 토큰 주소가 거절하거나, 시한이 지나거나, 모르는 제공자이거나, 브라우저를 열지 못하면 실패로 한 번 끝난다 */
SW_TEST_CASE( LoopbackPkceLoginClientTest, FailuresEndTheLoginOnce )
{
    SW_EXPECT_LOG( "LoopbackPkceLoginClient.ExternalSignDidNot" );
    {
        PkceFixture rig;
        rig._tokenStatus = 400;
        rig._tokenBody   = R"({"error":"invalid_grant"})";
        (void)rig._login.beginLogin( "google", MonotonicMilliseconds{ kNowMs } );
        rig.redirect( "code=C&state=" + rig.getAuthorizationState() );
        const vector<PlatformLoginClientResult> listResult = rig.takeResults();
        SW_REQUIRE_EQUAL( size_t( 1 ), listResult.size() );
        SW_EXPECT_TRUE( listResult[0]._bSucceeded == SW_FALSE && listResult[0]._bCancelled == SW_FALSE );
        SW_EXPECT_TRUE( listResult[0]._ticket.empty() );
    }
    {
        PkceFixture rig;
        (void)rig._login.beginLogin( "google", MonotonicMilliseconds{ kNowMs } );
        rig.poll( kNowMs + 4999 );
        SW_EXPECT_TRUE( rig.takeResults().empty() );
        rig.poll( kNowMs + 5000 );
        const vector<PlatformLoginClientResult> listResult = rig.takeResults();
        SW_REQUIRE_EQUAL( size_t( 1 ), listResult.size() );
        SW_EXPECT_TRUE( listResult[0]._bCancelled == SW_TRUE );
    }
    {
        PkceFixture rig;
        (void)rig._login.beginLogin( "nobody", MonotonicMilliseconds{ kNowMs } );
        SW_EXPECT_TRUE( rig._browser._listURL.empty() );
        rig._browser._bOpenOk = SW_FALSE;
        (void)rig._login.beginLogin( "google", MonotonicMilliseconds{ kNowMs } );
        const vector<PlatformLoginClientResult> listResult = rig.takeResults();
        SW_REQUIRE_EQUAL( size_t( 2 ), listResult.size() );
        SW_EXPECT_EQUAL( string( "unknown provider" ), listResult[0]._failureText );
        SW_EXPECT_EQUAL( string( "could not open the system browser" ), listResult[1]._failureText );
        rig._security._bHashOk = SW_FALSE;
        rig._browser._bOpenOk  = SW_TRUE;
        (void)rig._login.beginLogin( "google", MonotonicMilliseconds{ kNowMs } );
        const vector<PlatformLoginClientResult> listNoHash = rig.takeResults();
        SW_REQUIRE_EQUAL( size_t( 1 ), listNoHash.size() );
        SW_EXPECT_EQUAL( string( "could not make PKCE secrets" ), listNoHash[0]._failureText );
    }
}
