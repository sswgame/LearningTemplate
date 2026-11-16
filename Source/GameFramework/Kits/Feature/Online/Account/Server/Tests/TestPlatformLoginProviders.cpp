// OidcLoginProviderTest · ProfileAPILoginProviderTest · TicketQueryLoginProviderTest · AppleLoginProviderTest — 외부 로그인 제공자 본체의 요청 모양과 응답 대응.
// 실제 HTTP 클라이언트가 루프백 망의 가짜 제공자 서버와 말한다 — 바깥 서비스는 없다. 서명은 가짜 보안 제공자(서명 확인은 늘 맞다)라 서명 바깥의 규칙(iss · aud · exp · nonce · kid · 경로)을 본다.
// 애플 주소 · 폼 이름은 애플 문서(Sign in with Apple REST API — Generate and validate tokens, Revoke tokens, 2026-11-07 대조)와 같게 비교한다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Network/Transport/StreamTypes.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"

#include "Engine/Serialization/JSON/JSONWriter.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/OidcLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/ProfileAPILoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/Provider/Apple/AppleLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/TicketQueryLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Security/JSONWebToken.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPClient.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPServer.h"
#include "GameFramework/Runtime/Online/Security/Tests/FakeNetSecurityProvider.h"

using namespace sw;

namespace
{
    constexpr int64       kNowMs    = 1700000000000ll;
    constexpr const utf8* kIssuer   = "https://issuer.test";
    constexpr const utf8* kClientID = "game-client";

    /** @brief 가짜 제공자 서버 하나(경로마다 상태 · 몸)와 HTTP 클라이언트 둘(JWKS 용, 애플 교환 · 철회 용)입니다. 선언 순서가 수명이다 — 망이 전송들보다 오래 산다. */
    struct ProviderHTTPFixture
    {
        LoopbackStreamNetwork         _network{ 11u };
        ManualClock                   _clock{ MonotonicMilliseconds{ 0 } };
        HTTPServer                    _server{};
        HTTPClient                    _client{ _clock };
        HTTPClient                    _appleClient{ _clock };
        test::FakeNetSecurityProvider _security{};
        vector<HTTPServerRequest>     _listRequest{};
        string                        _baseURL{};
        string                        _jwksBody{};
        string                        _profileBody{};
        string                        _tokenBody{};
        string                        _revokeBody{};
        int32                         _jwksStatus{ HTTPConstant::kStatusOk };
        int32                         _profileStatus{ HTTPConstant::kStatusOk };
        int32                         _tokenStatus{ HTTPConstant::kStatusOk };
        int32                         _revokeStatus{ HTTPConstant::kStatusOk };

        ProviderHTTPFixture()
        {
            StreamTransportConfig transportSettings;
            transportSettings._ioThreadCount = 0;
            SW_EXPECT_TRUE( _server.initialize( _network.createTransport(), transportSettings, HTTPServerConfig{}, HTTPRequestFunction::create<&ProviderHTTPFixture::onRequest>( this ) ) );
            SW_EXPECT_TRUE( _client.initialize( _network.createTransport(), transportSettings, HTTPClientConfig{} ) );
            SW_EXPECT_TRUE( _appleClient.initialize( _network.createTransport(), transportSettings, HTTPClientConfig{} ) );
            _baseURL  = "http://127.0.0.1:" + to_string( static_cast<int32>( _server.getListenPort() ) );
            _jwksBody = makeJWKS( "kid-1" );
        }

        void onRequest( const HTTPServerRequest& request, HTTPServerResponse& outResponse )
        {
            _listRequest.push_back( request );
            string body;
            if ( request._path == "/jwks" )
            {
                body                    = _jwksBody;
                outResponse._statusCode = _jwksStatus;
            }
            else if ( request._path == "/auth/token" )
            {
                body                    = _tokenBody;
                outResponse._statusCode = _tokenStatus;
            }
            else if ( request._path == "/auth/revoke" )
            {
                body                    = _revokeBody;
                outResponse._statusCode = _revokeStatus;
            }
            else
            {
                body                    = _profileBody;
                outResponse._statusCode = _profileStatus;
            }
            outResponse._bodyBytes.assign( body.begin(), body.end() );
        }

        /** @brief 제공자와 서버를 번갈아 돌려 왕복을 끝냅니다. */
        void poll( IPlatformLoginProvider& provider, int64 nowMs = kNowMs )
        {
            for ( int32 round = 0; round < 40; ++round )
            {
                provider.update( UnixMilliseconds{ nowMs } );
                _server.update();
            }
        }

        /** @brief 티켓 하나를 확인하고 결과 하나를 받습니다. */
        PlatformLoginVerification verify( IPlatformLoginProvider& provider, string_view ticket, int64 nowMs = kNowMs )
        {
            const uint64 verificationID = provider.submitVerification( StringUtil::toBytes( ticket ), UnixMilliseconds{ nowMs } );
            poll( provider, nowMs );
            vector<PlatformLoginVerification> listVerification;
            (void)provider.pollVerifications( listVerification );
            SW_EXPECT_EQUAL( size_t( 1 ), listVerification.size() );
            if ( listVerification.empty() )
                return PlatformLoginVerification{};
            SW_EXPECT_EQUAL( verificationID, listVerification[0]._verificationID );
            return listVerification[0];
        }

        int32 countRequests( string_view path ) const
        {
            int32 count = 0;
            for ( const HTTPServerRequest& request : _listRequest )
            {
                count += request._path == path ? 1 : 0;
            }
            return count;
        }

        const HTTPServerRequest* findLastRequest( string_view path ) const
        {
            for ( size_t index = _listRequest.size(); index > 0; --index )
            {
                if ( _listRequest[index - 1]._path == path )
                    return &_listRequest[index - 1];
            }
            return nullptr;
        }

        static string makeJWKS( string_view keyID )
        {
            NetPublicKey key;
            key._algorithm = NetSignatureAlgorithm::RsaPkcs1SHA256;
            key._modulus   = { 0xC1, 0x02, 0x03, 0x04 };
            key._exponent  = { 0x01, 0x00, 0x01 };
            return "{\"keys\":[" + JWKSKeyCache::writeJwk( key, keyID ) + "]}";
        }
    };

    /** @brief 토큰 본문의 클레임입니다. 빈 글은 싣지 않습니다. */
    struct TokenClaims
    {
        string _issuer{ kIssuer };
        string _audience{ kClientID };
        string _subject{ "player-7" };
        string _nonce{};
        string _name{};
        int64  _expiresAtSec{ kNowMs / 1000 + 3600 };
    };

    struct PlatformLoginProviderTestUtil
    {
        /** @brief 가짜 서명(RS256)으로 ID 토큰을 만듭니다. */
        static string makeIDToken( INetSecurityProvider& security, const TokenClaims& claims, string_view keyID = "kid-1" )
        {
            JSONWriter writer;
            writer.beginObject();
            writer.key( "iss" );
            writer.valueString( claims._issuer );
            writer.key( "aud" );
            writer.valueString( claims._audience );
            writer.key( "sub" );
            writer.valueString( claims._subject );
            writer.key( "exp" );
            writer.valueInt( claims._expiresAtSec );
            if ( claims._nonce.empty() == false )
            {
                writer.key( "nonce" );
                writer.valueString( claims._nonce );
            }
            if ( claims._name.empty() == false )
            {
                writer.key( "name" );
                writer.valueString( claims._name );
            }
            writer.endObject();
            const auto payload = writer.finish();
            SW_EXPECT_TRUE( payload.has_value() );
            string compact;
            if ( payload.has_value() )
                SW_EXPECT_TRUE( JWTUtil::makeSigned( security, NetSignatureAlgorithm::RsaPkcs1SHA256, keyID, *payload, "fake-pem", compact ) );
            return compact;
        }

        static PlatformLoginProviderConfig makeOidcSettings( const ProviderHTTPFixture& rig )
        {
            PlatformLoginProviderConfig settings;
            settings._name            = "oidc_test";
            settings._kind            = PlatformLoginProviderKind::Oidc;
            settings._issuer          = kIssuer;
            settings._jwksURL         = rig._baseURL + "/jwks";
            settings._listClientID    = { kClientID };
            settings._displayNamePath = "name";
            return settings;
        }

        /** @brief 폼 몸(`이름=값&…`)에서 값 하나를 찾습니다. 없으면 빈 글입니다. */
        static string findForm( const HTTPServerRequest& request, string_view name )
        {
            vector<HTTPHeader> listPair;
            if ( HTTPUtil::decodeForm( request.getBodyText(), listPair ) == false )
                return {};
            for ( const HTTPHeader& pair : listPair )
            {
                if ( pair._name == name )
                    return pair._value;
            }
            return {};
        }

        static string findHeader( const HTTPServerRequest& request, string_view name )
        {
            const HTTPHeader* pHeader = request.findHeader( name );
            return pHeader != nullptr ? pHeader->_value : string();
        }
    };

    using TestUtil = PlatformLoginProviderTestUtil;
} // namespace

/** @brief [OidcLoginProviderTest] 처음 확인에서 JWKS 를 한 번 받아 서명 키를 찾고, 주체 · 표시 이름을 돌려준다. 다음 토큰은 캐시로 확인한다 */
SW_TEST_CASE( OidcLoginProviderTest, VerifiesWithFetchedKeysAndCachesThem )
{
    ProviderHTTPFixture rig;
    OidcLoginProvider   provider( TestUtil::makeOidcSettings( rig ), &rig._security, &rig._client );

    TokenClaims claims;
    claims._name                                 = "Kim";
    const PlatformLoginVerification verification = rig.verify( provider, TestUtil::makeIDToken( rig._security, claims ) );
    SW_EXPECT_TRUE( verification.isVerified() );
    SW_EXPECT_EQUAL( string( "player-7" ), verification._subject );
    SW_EXPECT_EQUAL( string( "Kim" ), verification._displayName );
    SW_EXPECT_TRUE( verification._linkSecret.empty() );
    SW_EXPECT_TRUE( rig.verify( provider, TestUtil::makeIDToken( rig._security, TokenClaims{} ) ).isVerified() );

    SW_EXPECT_EQUAL( 1, rig.countRequests( "/jwks" ) );
    SW_EXPECT_EQUAL( 1, provider.getJWKSFetchCount() );
    const HTTPServerRequest* pFetch = rig.findLastRequest( "/jwks" );
    SW_REQUIRE_TRUE( pFetch != nullptr );
    SW_EXPECT_TRUE( pFetch->_method == HTTPMethod::Get );
    SW_EXPECT_EQUAL( string( "application/json" ), TestUtil::findHeader( *pFetch, "Accept" ) );
}

/** @brief [OidcLoginProviderTest] 다른 발급자 · 다른 대상 · 지난 토큰 · 깨진 토큰은 거절이고, nonce 를 요구하면 티켓의 nonce 와 토큰의 nonce 가 같아야 한다 */
SW_TEST_CASE( OidcLoginProviderTest, RejectsClaimsThatDoNotMatch )
{
    ProviderHTTPFixture         rig;
    PlatformLoginProviderConfig settings = TestUtil::makeOidcSettings( rig );
    settings._bRequireNonce              = SW_TRUE;
    OidcLoginProvider provider( settings, &rig._security, &rig._client );

    TokenClaims withNonce;
    withNonce._nonce   = "n-1";
    const string token = TestUtil::makeIDToken( rig._security, withNonce );
    SW_EXPECT_TRUE( rig.verify( provider, token + "|n-1" ).isVerified() );
    SW_EXPECT_TRUE( rig.verify( provider, token + "|n-2" )._bRejected == SW_TRUE );
    SW_EXPECT_TRUE( rig.verify( provider, token )._bRejected == SW_TRUE ); // 티켓에 nonce 가 없다

    TokenClaims otherIssuer = withNonce;
    otherIssuer._issuer     = "https://evil.test";
    SW_EXPECT_TRUE( rig.verify( provider, TestUtil::makeIDToken( rig._security, otherIssuer ) + "|n-1" )._bRejected == SW_TRUE );
    TokenClaims otherAudience = withNonce;
    otherAudience._audience   = "other-client";
    SW_EXPECT_TRUE( rig.verify( provider, TestUtil::makeIDToken( rig._security, otherAudience ) + "|n-1" )._bRejected == SW_TRUE );
    TokenClaims expired   = withNonce;
    expired._expiresAtSec = kNowMs / 1000 - 3600;
    SW_EXPECT_TRUE( rig.verify( provider, TestUtil::makeIDToken( rig._security, expired ) + "|n-1" )._bRejected == SW_TRUE );
    const PlatformLoginVerification malformed = rig.verify( provider, "not-a-token|n-1" );
    SW_EXPECT_TRUE( malformed._bRejected == SW_TRUE );
    SW_EXPECT_TRUE( malformed._subject.empty() );
}

/** @brief [OidcLoginProviderTest] 받은 JWKS 에도 없는 kid 는 거절이고, JWKS 를 받지 못했으면 쓸 수 없음(다시 시도)이다 */
SW_TEST_CASE( OidcLoginProviderTest, UnknownKeyIsRejectedAndUnreachableJWKSIsUnavailable )
{
    {
        ProviderHTTPFixture             rig;
        OidcLoginProvider               provider( TestUtil::makeOidcSettings( rig ), &rig._security, &rig._client );
        const PlatformLoginVerification unknownKey = rig.verify( provider, TestUtil::makeIDToken( rig._security, TokenClaims{}, "kid-rotated" ) );
        SW_EXPECT_TRUE( unknownKey._bRejected == SW_TRUE );
        SW_EXPECT_TRUE( unknownKey._bUnavailable == SW_FALSE );
        SW_EXPECT_EQUAL( 1, rig.countRequests( "/jwks" ) ); // 받는 중에 온 모르는 kid 는 그 받기를 기다린다 — 따로 받지 않는다
    }
    {
        ProviderHTTPFixture rig;
        rig._jwksStatus = HTTPConstant::kStatusServerError;
        OidcLoginProvider provider( TestUtil::makeOidcSettings( rig ), &rig._security, &rig._client );
        SW_EXPECT_LOG( "OidcLoginProvider.JWKSFetchFailed" );
        const PlatformLoginVerification unreachable = rig.verify( provider, TestUtil::makeIDToken( rig._security, TokenClaims{} ) );
        SW_EXPECT_TRUE( unreachable._bUnavailable == SW_TRUE );
        SW_EXPECT_TRUE( unreachable._bRejected == SW_FALSE );
        SW_EXPECT_FALSE( provider.getKeyCache().hasKeys() );
    }
}

/** @brief [ProfileAPILoginProviderTest] 액세스 토큰을 Bearer 로 프로필 주소에 보내고, 점 경로의 주체 · 표시 이름을 읽는다 */
SW_TEST_CASE( ProfileAPILoginProviderTest, ReadsSubjectFromTheProfile )
{
    ProviderHTTPFixture         rig;
    PlatformLoginProviderConfig settings;
    settings._name            = "profile_test";
    settings._kind            = PlatformLoginProviderKind::AccessTokenProfile;
    settings._profileURL      = rig._baseURL + "/v1/nid/me";
    settings._subjectPath     = "response.id";
    settings._displayNamePath = "response.nickname";
    ProfileAPILoginProvider provider( settings, &rig._client );
    rig._profileBody = R"({"resultcode":"00","response":{"id":"naver-9","nickname":"Lee"}})";

    const PlatformLoginVerification verification = rig.verify( provider, "access-token-1" );
    SW_EXPECT_TRUE( verification.isVerified() );
    SW_EXPECT_EQUAL( string( "naver-9" ), verification._subject );
    SW_EXPECT_EQUAL( string( "Lee" ), verification._displayName );
    const HTTPServerRequest* pRequest = rig.findLastRequest( "/v1/nid/me" );
    SW_REQUIRE_TRUE( pRequest != nullptr );
    SW_EXPECT_TRUE( pRequest->_method == HTTPMethod::Get );
    SW_EXPECT_EQUAL( string( "Bearer access-token-1" ), TestUtil::findHeader( *pRequest, "Authorization" ) );
    SW_EXPECT_EQUAL( string( "application/json" ), TestUtil::findHeader( *pRequest, "Accept" ) );
}

/** @brief [ProfileAPILoginProviderTest] 401 · 403 · 주체 없는 200 은 거절이고, 5xx · 깨진 JSON 은 쓸 수 없음이다 */
SW_TEST_CASE( ProfileAPILoginProviderTest, MapsStatusAndBodyToRejectedOrUnavailable )
{
    ProviderHTTPFixture         rig;
    PlatformLoginProviderConfig settings;
    settings._name        = "profile_test";
    settings._kind        = PlatformLoginProviderKind::AccessTokenProfile;
    settings._profileURL  = rig._baseURL + "/me";
    settings._subjectPath = "response.id";
    ProfileAPILoginProvider provider( settings, &rig._client );

    rig._profileStatus = HTTPConstant::kStatusUnauthorized;
    SW_EXPECT_TRUE( rig.verify( provider, "t" )._bRejected == SW_TRUE );
    rig._profileStatus = HTTPConstant::kStatusForbidden;
    SW_EXPECT_TRUE( rig.verify( provider, "t" )._bRejected == SW_TRUE );
    rig._profileStatus = HTTPConstant::kStatusOk;
    rig._profileBody   = R"({"response":{}})";
    SW_EXPECT_TRUE( rig.verify( provider, "t" )._bRejected == SW_TRUE );
    rig._profileBody = "not json";
    SW_EXPECT_TRUE( rig.verify( provider, "t" )._bUnavailable == SW_TRUE );
    rig._profileStatus                     = HTTPConstant::kStatusServerError;
    rig._profileBody                       = R"({"response":{"id":"x"}})";
    const PlatformLoginVerification failed = rig.verify( provider, "t" );
    SW_EXPECT_TRUE( failed._bUnavailable == SW_TRUE );
    SW_EXPECT_TRUE( failed._subject.empty() );
}

/** @brief [TicketQueryLoginProviderTest] 키 · 티켓 · 받을 서비스 이름(identity)을 쿼리 인자로 덧붙여(주소에 `?` 가 있으면 `&`) 보내고, 결과 글이 성공 값이면 주체를 읽는다 — 스팀 AuthenticateUserTicket 모양 */
SW_TEST_CASE( TicketQueryLoginProviderTest, SendsKeyAndTicketAndReadsTheResult )
{
    ProviderHTTPFixture         rig;
    PlatformLoginProviderConfig settings;
    settings._name          = "steam";
    settings._kind          = PlatformLoginProviderKind::TicketQuery;
    settings._profileURL    = rig._baseURL + "/ISteamUserAuth/AuthenticateUserTicket/v1/?appid=480";
    settings._ticketParam   = "ticket";
    settings._keyParam      = "key";
    settings._identityParam = "identity";
    settings._identity      = "swlogin";
    settings._resultPath    = "response.params.result";
    settings._resultValue   = "OK";
    settings._subjectPath   = "response.params.steamid";
    TicketQueryLoginProvider provider( settings, "SECRETKEY", &rig._client );
    SW_EXPECT_EQUAL( rig._baseURL + "/ISteamUserAuth/AuthenticateUserTicket/v1/?appid=480&key=SECRETKEY&ticket=AB12&identity=swlogin", provider.makeRequestURL( "AB12" ) );

    rig._profileBody                             = R"({"response":{"params":{"result":"OK","steamid":"76561197960287930"}}})";
    const PlatformLoginVerification verification = rig.verify( provider, "AB12" );
    SW_EXPECT_TRUE( verification.isVerified() );
    SW_EXPECT_EQUAL( string( "76561197960287930" ), verification._subject );
    const HTTPServerRequest* pRequest = rig.findLastRequest( "/ISteamUserAuth/AuthenticateUserTicket/v1/" );
    SW_REQUIRE_TRUE( pRequest != nullptr );
    const HTTPHeader* pApp      = pRequest->findQuery( "appid" );
    const HTTPHeader* pKey      = pRequest->findQuery( "key" );
    const HTTPHeader* pTicket   = pRequest->findQuery( "ticket" );
    const HTTPHeader* pIdentity = pRequest->findQuery( "identity" );
    SW_REQUIRE_TRUE( pApp != nullptr && pKey != nullptr && pTicket != nullptr && pIdentity != nullptr );
    SW_EXPECT_EQUAL( string( "480" ), pApp->_value );
    SW_EXPECT_EQUAL( string( "SECRETKEY" ), pKey->_value );
    SW_EXPECT_EQUAL( string( "AB12" ), pTicket->_value );
    SW_EXPECT_EQUAL( string( "swlogin" ), pIdentity->_value ); // 클라이언트가 GetAuthTicketForWebApi 에 넘긴 이름과 같아야 스팀이 받는다

    rig._profileBody = R"({"response":{"error":{"errorcode":101,"errordesc":"Invalid ticket"}}})";
    SW_EXPECT_TRUE( rig.verify( provider, "AB12" )._bRejected == SW_TRUE );
    rig._profileBody = R"({"response":{"params":{"result":"Denied","steamid":"7"}}})";
    SW_EXPECT_TRUE( rig.verify( provider, "AB12" )._bRejected == SW_TRUE );
    rig._profileStatus = HTTPConstant::kStatusForbidden;
    SW_EXPECT_TRUE( rig.verify( provider, "AB12" )._bRejected == SW_TRUE );
    rig._profileStatus = HTTPConstant::kStatusServerError;
    SW_EXPECT_TRUE( rig.verify( provider, "AB12" )._bUnavailable == SW_TRUE );
}

/** @brief [TicketQueryLoginProviderTest] 쿼리에 실을 수 없는 티켓은 묻지 않고 거절하고(인자 끼워 넣기 차단), 키 환경 변수가 없으면 제공자를 만들지 않는다 */
SW_TEST_CASE( TicketQueryLoginProviderTest, UnsafeTicketAndMissingKeyAreRefused )
{
    SW_EXPECT_TRUE( TicketQueryLoginProvider::isQuerySafe( "aZ09-_." ) );
    SW_EXPECT_FALSE( TicketQueryLoginProvider::isQuerySafe( "" ) );
    SW_EXPECT_FALSE( TicketQueryLoginProvider::isQuerySafe( "a&key=x" ) );
    SW_EXPECT_FALSE( TicketQueryLoginProvider::isQuerySafe( "a b" ) );

    ProviderHTTPFixture         rig;
    PlatformLoginProviderConfig settings;
    settings._name          = "steam";
    settings._kind          = PlatformLoginProviderKind::TicketQuery;
    settings._profileURL    = rig._baseURL + "/verify";
    settings._ticketParam   = "ticket";
    settings._keyParam      = "key";
    settings._identityParam = "identity";
    settings._identity      = "swlogin";
    settings._resultPath    = "result";
    settings._resultValue   = "OK";
    TicketQueryLoginProvider provider( settings, "K", &rig._client );
    SW_EXPECT_TRUE( rig.verify( provider, "AB&key=evil" )._bRejected == SW_TRUE );
    SW_EXPECT_TRUE( rig._listRequest.empty() );

    settings._keySecretEnvironment = "SW_TEST_LOGIN_KEY_THAT_IS_NEVER_SET_3F7A";
    SW_EXPECT_LOG( "Server.SecretNotSet" );
    SW_EXPECT_LOG( "TicketQueryLoginProvider.LoginProviderKeyEnvironment" );
    SW_EXPECT_TRUE( TicketQueryLoginProvider::create( settings, &rig._client ) == nullptr );
}

namespace
{
    /** @brief 애플 제공자(교환 · 철회 주소는 가짜 서버)와 그 서버입니다. */
    struct AppleFixture
    {
        ProviderHTTPFixture            _http{};
        unique_ptr<AppleLoginProvider> _pProvider{};

        AppleFixture()
        {
            PlatformLoginProviderConfig settings = TestUtil::makeOidcSettings( _http );
            settings._name                       = "apple";
            settings._kind                       = PlatformLoginProviderKind::AppleSignIn;
            settings._listClientID               = { "com.example.game", "com.example.web" };
            settings._teamID                     = "TEAM123";
            settings._signingKeyID               = "KEY456";
            settings._tokenURL                   = _http._baseURL + "/auth/token";
            settings._revokeURL                  = _http._baseURL + "/auth/revoke";
            _pProvider                           = make_unique<AppleLoginProvider>( settings, "fake-p8-pem", &_http._security, &_http._client, &_http._appleClient );
        }

        string makeIDToken( string_view nonce = {} )
        {
            TokenClaims claims;
            claims._audience = "com.example.game";
            claims._nonce    = string( nonce );
            return TestUtil::makeIDToken( _http._security, claims );
        }

        PlatformLoginRevocation revoke( string_view linkSecret )
        {
            const uint64 revocationID = _pProvider->submitRevocation( linkSecret, UnixMilliseconds{ kNowMs } );
            _http.poll( *_pProvider );
            vector<PlatformLoginRevocation> listRevocation;
            (void)_pProvider->pollRevocations( listRevocation );
            SW_EXPECT_EQUAL( size_t( 1 ), listRevocation.size() );
            if ( listRevocation.empty() )
                return PlatformLoginRevocation{};
            SW_EXPECT_EQUAL( revocationID, listRevocation[0]._revocationID );
            return listRevocation[0];
        }
    };
} // namespace

/** @brief [AppleLoginProviderTest] 공식 주소와 audience 가 애플 문서의 글과 같다(경로 한 글자도 틀리면 운영에서만 실패한다) */
SW_TEST_CASE( AppleLoginProviderTest, OfficialEndpointsMatchTheDocumentation )
{
    SW_EXPECT_EQUAL( string( "https://appleid.apple.com" ), string( AppleLoginProvider::kAudience ) );
    SW_EXPECT_EQUAL( string( "https://appleid.apple.com/auth/token" ), string( AppleLoginProvider::kDefaultTokenURL ) );
    SW_EXPECT_EQUAL( string( "https://appleid.apple.com/auth/revoke" ), string( AppleLoginProvider::kDefaultRevokeURL ) );
}

/** @brief [AppleLoginProviderTest] 티켓 `ID 토큰|nonce|인가 코드` 를 나눈다 — nonce 없이 `||`, 인가 코드 없이 `|nonce`, 구분자 없이 토큰만 */
SW_TEST_CASE( AppleLoginProviderTest, SplitsTheTicket )
{
    string idToken;
    string nonce;
    string code;
    AppleLoginProvider::splitTicket( "T|N|C", idToken, nonce, code );
    SW_EXPECT_EQUAL( string( "T" ), idToken );
    SW_EXPECT_EQUAL( string( "N" ), nonce );
    SW_EXPECT_EQUAL( string( "C" ), code );
    AppleLoginProvider::splitTicket( "T||C", idToken, nonce, code );
    SW_EXPECT_TRUE( nonce.empty() );
    SW_EXPECT_EQUAL( string( "C" ), code );
    AppleLoginProvider::splitTicket( "T|N", idToken, nonce, code );
    SW_EXPECT_EQUAL( string( "N" ), nonce );
    SW_EXPECT_TRUE( code.empty() );
    AppleLoginProvider::splitTicket( "T", idToken, nonce, code );
    SW_EXPECT_EQUAL( string( "T" ), idToken );
    SW_EXPECT_TRUE( nonce.empty() && code.empty() );
}

/** @brief [AppleLoginProviderTest] client secret 은 ES256 JWT(kid = 서명 키 id, iss = 팀 id, sub = 첫 client id, aud = appleid, 수명 24 시간)이고 만료 1 시간 전까지 다시 쓴다 */
SW_TEST_CASE( AppleLoginProviderTest, ClientSecretCarriesTheDocumentedClaims )
{
    AppleFixture rig;
    const string secret = rig._pProvider->makeClientSecret( UnixMilliseconds{ kNowMs } );
    SW_REQUIRE_FALSE( secret.empty() );
    JWTUnverified token;
    SW_REQUIRE_TRUE( token.parse( secret ) );
    SW_EXPECT_TRUE( token.getAlgorithm() == NetSignatureAlgorithm::EcdsaP256Sha256 );
    SW_EXPECT_EQUAL( string( "KEY456" ), token.getKeyID() );
    NetPublicKey anyKey;
    anyKey._algorithm                      = NetSignatureAlgorithm::EcdsaP256Sha256;
    const sw::optional<JWTVerified> claims = std::move( token ).verify( rig._http._security, anyKey, JWTClaimRule::makeSignatureOnly() );
    SW_REQUIRE_TRUE( claims.has_value() );
    string issuer;
    string subject;
    int64  issuedAt  = 0;
    int64  expiresAt = 0;
    SW_EXPECT_TRUE( claims->findText( "iss", issuer ) && issuer == "TEAM123" );
    SW_EXPECT_TRUE( claims->findText( "sub", subject ) && subject == "com.example.game" );
    SW_EXPECT_TRUE( claims->hasAudience( "https://appleid.apple.com" ) );
    SW_EXPECT_TRUE( claims->findInteger( "iat", issuedAt ) && issuedAt == kNowMs / 1000 );
    SW_EXPECT_TRUE( claims->findInteger( "exp", expiresAt ) && expiresAt == kNowMs / 1000 + 24 * 3600 );

    const int64 kHourMs = 3600 * 1000;
    SW_EXPECT_EQUAL( secret, rig._pProvider->makeClientSecret( UnixMilliseconds{ kNowMs + 22 * kHourMs } ) ); // 아직 23 시간 전이다
    SW_EXPECT_NOT_EQUAL( secret, rig._pProvider->makeClientSecret( UnixMilliseconds{ kNowMs + 23 * kHourMs } ) );

    rig._http._security._bSignOk = SW_FALSE;
    SW_EXPECT_LOG( "AppleLoginProvider.CouldNotSignClient" );
    SW_EXPECT_TRUE( rig._pProvider->makeClientSecret( UnixMilliseconds{ kNowMs + 48 * kHourMs } ).empty() );
}

/** @brief [AppleLoginProviderTest] ID 토큰을 확인한 뒤 인가 코드를 `POST /auth/token`(grant_type=authorization_code)으로 바꿔 refresh token 을 연동 비밀로 싣는다 */
SW_TEST_CASE( AppleLoginProviderTest, ExchangesTheCodeForALinkSecret )
{
    AppleFixture rig;
    rig._http._tokenBody                         = R"({"access_token":"A","token_type":"Bearer","expires_in":3600,"refresh_token":"R-1","id_token":"I"})";
    const PlatformLoginVerification verification = rig._http.verify( *rig._pProvider, rig.makeIDToken() + "||code-9" );
    SW_EXPECT_TRUE( verification.isVerified() );
    SW_EXPECT_EQUAL( string( "player-7" ), verification._subject );
    SW_EXPECT_EQUAL( string( "R-1" ), verification._linkSecret );

    const HTTPServerRequest* pExchange = rig._http.findLastRequest( "/auth/token" );
    SW_REQUIRE_TRUE( pExchange != nullptr );
    SW_EXPECT_TRUE( pExchange->_method == HTTPMethod::Post );
    SW_EXPECT_EQUAL( string( "application/x-www-form-urlencoded" ), TestUtil::findHeader( *pExchange, "Content-Type" ) );
    SW_EXPECT_EQUAL( string( "com.example.game" ), TestUtil::findForm( *pExchange, "client_id" ) );
    SW_EXPECT_EQUAL( string( "code-9" ), TestUtil::findForm( *pExchange, "code" ) );
    SW_EXPECT_EQUAL( string( "authorization_code" ), TestUtil::findForm( *pExchange, "grant_type" ) );
    SW_EXPECT_EQUAL( rig._pProvider->makeClientSecret( UnixMilliseconds{ kNowMs } ), TestUtil::findForm( *pExchange, "client_secret" ) );
}

/** @brief [AppleLoginProviderTest] 교환이 실패해도 로그인은 성공이고 연동 비밀만 없다. 인가 코드가 없으면 교환하지 않고, ID 토큰이 틀리면 교환하지 않고 거절이다 */
SW_TEST_CASE( AppleLoginProviderTest, ExchangeFailureStillLogsIn )
{
    AppleFixture rig;
    rig._http._tokenStatus = 400;
    rig._http._tokenBody   = R"({"error":"invalid_grant"})";
    SW_EXPECT_LOG( "AppleLoginProvider.LoginProviderCouldNot" );
    const PlatformLoginVerification failedExchange = rig._http.verify( *rig._pProvider, rig.makeIDToken( "n" ) + "|n|code-1" );
    SW_EXPECT_TRUE( failedExchange.isVerified() );
    SW_EXPECT_TRUE( failedExchange._linkSecret.empty() );

    const int32 exchangeCount = rig._http.countRequests( "/auth/token" );
    SW_EXPECT_TRUE( rig._http.verify( *rig._pProvider, rig.makeIDToken() ).isVerified() );
    SW_EXPECT_EQUAL( exchangeCount, rig._http.countRequests( "/auth/token" ) );
    SW_EXPECT_TRUE( rig._http.verify( *rig._pProvider, "broken||code-2" )._bRejected == SW_TRUE );
    SW_EXPECT_EQUAL( exchangeCount, rig._http.countRequests( "/auth/token" ) );
}

/** @brief [AppleLoginProviderTest] 철회는 `POST /auth/revoke`(token · token_type_hint=refresh_token)이고, 200 과 400 invalid_grant 는 끝, 400 invalid_client · 5xx 는 다시 시도다 */
SW_TEST_CASE( AppleLoginProviderTest, RevokesTheRefreshToken )
{
    AppleFixture rig;
    SW_EXPECT_TRUE( rig._pProvider->supportsRevocation() );
    SW_EXPECT_TRUE( rig.revoke( "R-1" )._bUnavailable == SW_FALSE );
    const HTTPServerRequest* pRevoke = rig._http.findLastRequest( "/auth/revoke" );
    SW_REQUIRE_TRUE( pRevoke != nullptr );
    SW_EXPECT_TRUE( pRevoke->_method == HTTPMethod::Post );
    SW_EXPECT_EQUAL( string( "application/x-www-form-urlencoded" ), TestUtil::findHeader( *pRevoke, "Content-Type" ) );
    SW_EXPECT_EQUAL( string( "com.example.game" ), TestUtil::findForm( *pRevoke, "client_id" ) );
    SW_EXPECT_EQUAL( string( "R-1" ), TestUtil::findForm( *pRevoke, "token" ) );
    SW_EXPECT_EQUAL( string( "refresh_token" ), TestUtil::findForm( *pRevoke, "token_type_hint" ) );
    SW_EXPECT_FALSE( TestUtil::findForm( *pRevoke, "client_secret" ).empty() );

    rig._http._revokeStatus = 400;
    rig._http._revokeBody   = R"({"error":"invalid_grant"})";
    SW_EXPECT_TRUE( rig.revoke( "R-2" )._bUnavailable == SW_FALSE );
    rig._http._revokeBody = R"({"error":"invalid_client"})";
    SW_EXPECT_LOG( "AppleLoginProvider.LoginProviderRejectedRevocation" );
    SW_EXPECT_TRUE( rig.revoke( "R-3" )._bUnavailable == SW_TRUE );
    rig._http._revokeStatus = HTTPConstant::kStatusServerError;
    rig._http._revokeBody.clear();
    SW_EXPECT_TRUE( rig.revoke( "R-4" )._bUnavailable == SW_TRUE );
}
