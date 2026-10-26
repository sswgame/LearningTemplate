// 외부 로그인 — 가짜 발급자(루프백 HTTPS: JWKS · 프로필 · 토큰)로: OIDC ID 토큰의 서명 · iss · aud · exp · nonce · alg 바꿔치기, 키 회전 뒤 재조회 ·
// JWKS 서버가 죽어도 캐시 키로 확인 · 모르는 kid 는 ProviderUnavailable, 프로필 API 형의 정상 · 401 · 다운, 제공자 설정을 데이터에서,
// PC 루프백 + PKCE 흐름(state · S256 · 취소), 로그인 서비스까지 이어서 계정 만들기.
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/String/Base64Util.h"
#include "Core/Time/MonotonicClock.h"

#include "GameFramework/Base/Online/HTTP/HTTPClient.h"
#include "GameFramework/Base/Online/HTTP/HTTPServer.h"
#include "GameFramework/Base/Online/Security/JSONWebToken.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/NetSecurityLoginCrypto.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/OidcLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderConfig.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderSet.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/ProfileAPILoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/TicketQueryLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.h"
#include "GameFramework/Kits/Feature/Online/Account/Shared/API/LoopbackPkceLoginClient.h"
#include "GameFramework/Kits/Feature/Online/Account/Shared/API/SystemExternalBrowser.h"

#include "TestFramework/TestFramework.h"

#include <chrono>
#include <thread>

using namespace sw;

namespace
{
    constexpr int64 kNowMs = 1000000; ///< 테스트 벽시계(초로 1000)

    /** @brief 가짜 제공자 서버 — `/jwks`(바꿀 수 있다) · `/profile`(Bearer good | bad) · `/token`(PKCE 확인 뒤 정한 토큰). `_bDown` 이면 모두 503. */
    class FakeIssuerHandler final : public IHTTPRequestHandler
    {
    public:
        string _jwks{};
        string _idToken{};
        string _expectedCode{ "code-123" };
        string _expectedChallenge{};
        int32  _jwksHitCount{ 0 };
        int32  _tokenHitCount{ 0 };
        uint8  _bDown{ SW_FALSE };

        void onHTTPRequest( const HTTPServerRequest& request, HTTPServerResponse& outResponse ) override
        {
            if ( _bDown == SW_TRUE )
            {
                outResponse._statusCode = 503;
                return;
            }
            if ( request._path == "/jwks" )
            {
                ++_jwksHitCount;
                outResponse._bodyBytes.assign( reinterpret_cast<const uint8*>( _jwks.data() ), reinterpret_cast<const uint8*>( _jwks.data() ) + _jwks.size() );
                outResponse._bChunked = SW_TRUE; // 실제 제공자처럼 chunked 로도 읽힌다
                return;
            }
            if ( request._path == "/profile" )
            {
                const HTTPHeader* pAuthorization = request.findHeader( "authorization" );
                if ( pAuthorization == nullptr || pAuthorization->_value != "Bearer good" )
                {
                    outResponse._statusCode = 401;
                    return;
                }
                const string body = "{\"resultcode\":\"00\",\"response\":{\"id\":\"n-1\",\"nickname\":\"Nick\"}}";
                outResponse._bodyBytes.assign( reinterpret_cast<const uint8*>( body.data() ), reinterpret_cast<const uint8*>( body.data() ) + body.size() );
                return;
            }
            if ( request._path == "/ticket" )
            {
                // 티켓 확인 API 흉내 — 키가 맞고 티켓이 abc123 이면 결과 OK 와 주체, 다른 티켓이면 결과가 다른 200 이다(거절로 읽혀야 한다).
                const HTTPHeader* pKey    = request.findQuery( "key" );
                const HTTPHeader* pTicket = request.findQuery( "ticket" );
                const HTTPHeader* pApp    = request.findQuery( "appid" );
                const bool        bKeyOk  = pKey != nullptr && pKey->_value == "secret-key";
                const bool        bAppOk  = pApp != nullptr && pApp->_value == "480";
                if ( bKeyOk == false || bAppOk == false )
                {
                    outResponse._statusCode = 403;
                    return;
                }
                const bool   bGood = pTicket != nullptr && pTicket->_value == "abc123";
                const string body  = bGood ? "{\"response\":{\"params\":{\"result\":\"OK\",\"steamid\":\"76561198000000001\"}}}"
                                           : "{\"response\":{\"error\":{\"errorcode\":101,\"errordesc\":\"Invalid ticket\"}}}";
                outResponse._bodyBytes.assign( reinterpret_cast<const uint8*>( body.data() ), reinterpret_cast<const uint8*>( body.data() ) + body.size() );
                return;
            }
            if ( request._path == "/token" && request._method == HTTPMethod::Post )
            {
                ++_tokenHitCount;
                vector<HTTPHeader> listForm;
                const bool         bForm = HTTPUtil::decodeForm( request.getBodyText(), listForm );
                string             code;
                string             verifier;
                for ( const HTTPHeader& pair : listForm )
                {
                    if ( pair._name == "code" )
                        code = pair._value;
                    if ( pair._name == "code_verifier" )
                        verifier = pair._value;
                }
                uint8      arrDigest[NetSecurityConstant::kSha256Size];
                const bool bHashed = NetSecurity::getProvider().computeSha256( reinterpret_cast<const uint8*>( verifier.data() ),
                                                                               static_cast<int32>( verifier.size() ), arrDigest );
                const bool bPkceOk = bForm && bHashed && code == _expectedCode && Base64Util::encodeURL( arrDigest, sizeof( arrDigest ) ) == _expectedChallenge;
                if ( bPkceOk == false )
                {
                    outResponse._statusCode = 400;
                    return;
                }
                const string body = "{\"token_type\":\"Bearer\",\"access_token\":\"good\",\"id_token\":\"" + _idToken + "\"}";
                outResponse._bodyBytes.assign( reinterpret_cast<const uint8*>( body.data() ), reinterpret_cast<const uint8*>( body.data() ) + body.size() );
                return;
            }
            outResponse._statusCode = 404;
        }
    };

    /** @brief 시스템 브라우저 대신 연 주소를 적어 둡니다. */
    class RecordingBrowser final : public IExternalBrowser
    {
    public:
        string _lastURL{};

        [[nodiscard]] bool openURL( string_view url ) override
        {
            _lastURL = string( url );
            return true;
        }
    };

    /** @brief 키 하나(kid · 알고리즘 · 비밀 PEM · 공개 키)입니다. */
    struct SigningKey
    {
        string                _keyID{};
        string                _privateKeyPem{};
        NetPublicKey          _publicKey{};
        NetSignatureAlgorithm _algorithm{ NetSignatureAlgorithm::RsaPkcs1Sha256 };
    };

    struct TestPlatformLoginInternal
    {
        static SigningKey makeKey( const utf8* pKeyID, NetSignatureAlgorithm algorithm )
        {
            SigningKey key;
            key._keyID     = pKeyID;
            key._algorithm = algorithm;
            (void)NetSecurity::getProvider().createSigningKeyPair( algorithm, key._privateKeyPem, key._publicKey );
            return key;
        }

        static string makeJwks( const vector<const SigningKey*>& listKey )
        {
            string jwks = "{\"keys\":[";
            for ( size_t index = 0; index < listKey.size(); ++index )
            {
                if ( index != 0 )
                    jwks += ",";
                jwks += JwksKeyCache::writeJwk( listKey[index]->_publicKey, listKey[index]->_keyID );
            }
            return jwks + "]}";
        }

        static string makeToken( const SigningKey& key, const utf8* pIssuer, const utf8* pAudience, int64 expiresAtSeconds, const utf8* pSubject, const utf8* pNonce )
        {
            string payload = string( "{\"iss\":\"" ) + pIssuer + "\",\"aud\":\"" + pAudience + "\",\"sub\":\"" + pSubject + "\",\"name\":\"Hero\",\"iat\":900,\"exp\":" +
                             to_string( expiresAtSeconds );
            if ( pNonce != nullptr )
                payload += string( ",\"nonce\":\"" ) + pNonce + "\"";
            payload += "}";
            string compact;
            (void)JwtUtil::makeSigned( NetSecurity::getProvider(), key._algorithm, key._keyID, payload, key._privateKeyPem, compact );
            return compact;
        }

        static vector<uint8> toBytes( string_view text ) { return vector<uint8>( reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() ); }

        static string findQuery( const string& url, const utf8* pName )
        {
            vector<HTTPHeader> listPair;
            const size_t       question = url.find( '?' );
            if ( question == string::npos || HTTPUtil::decodeForm( string_view( url ).substr( question + 1 ), listPair ) == false )
                return {};
            for ( const HTTPHeader& pair : listPair )
            {
                if ( pair._name == pName )
                    return pair._value;
            }
            return {};
        }
    };

    /** @brief 가짜 발급자(HTTPS) + 제공자 쪽 HTTP 클라이언트 + 루프백 망. */
    struct IssuerFixture
    {
        LoopbackStreamNetwork   _network;
        FakeIssuerHandler       _handler;
        unique_ptr<ITLSContext> _serverContext;
        unique_ptr<ITLSContext> _clientContext;
        HTTPServer              _server;
        HTTPClient              _client;
        int64                   _nowMs;

        IssuerFixture()
            : _network{ 11u }
            , _handler{}
            , _serverContext{}
            , _clientContext{}
            , _server{}
            , _client{}
            , _nowMs{ kNowMs }
        {
            INetSecurityProvider& provider = NetSecurity::getProvider();
            string                certificatePem;
            string                privateKeyPem;
            (void)provider.createSelfSignedCertificate( "localhost", 1, certificatePem, privateKeyPem ); // 실패면 PEM 이 비어 아래 TLS 준비가 실패로 드러난다
            TLSContextConfig serverTLS;
            serverTLS._role           = TLSRole::Server;
            serverTLS._certificatePem = certificatePem;
            serverTLS._privateKeyPem  = privateKeyPem;
            TLSContextConfig clientTLS;
            clientTLS._role       = TLSRole::Client;
            clientTLS._trustPem   = certificatePem;
            clientTLS._serverName = "localhost";
            string error;
            _serverContext = provider.createTLSContext( serverTLS, error );
            _clientContext = provider.createTLSContext( clientTLS, error );
            StreamTransportConfig transportSettings;
            transportSettings._ioThreadCount = 0;
            HTTPServerConfig serverSettings;
            serverSettings._pTLSContext = _serverContext.get();
            (void)_server.initialize( _network.createTransport(), transportSettings, serverSettings, &_handler );
            (void)_client.initialize( _network.createTransport(), transportSettings, HTTPClientConfig{} );
            _client.registerTLSContext( "localhost", _clientContext.get() );
        }

        string makeURL( const utf8* pPath ) const { return "https://localhost:" + to_string( static_cast<int32>( _server.getListenPort() ) ) + pPath; }

        PlatformLoginProviderConfig makeOidcSettings() const
        {
            PlatformLoginProviderConfig settings;
            settings._name    = "google";
            settings._kind    = PlatformLoginProviderKind::Oidc;
            settings._issuer  = "https://issuer.test";
            settings._jwksURL = makeURL( "/jwks" );
            settings._listClientID.push_back( "client-a" );
            settings._displayNamePath = "name";
            return settings;
        }

        /** @brief 제공자 표 하나를 끝까지 확인합니다(서버 · 제공자를 번갈아 돌린다). */
        PlatformLoginVerification verify( IPlatformLoginProvider& provider, string_view ticket )
        {
            const uint64   verificationID = provider.submitVerification( TestPlatformLoginInternal::toBytes( ticket ), _nowMs );
            const Deadline deadline       = Deadline::afterMilliseconds( 10000 ); // localhost 는 HTTP 클라이언트의 해석 스레드가 푼다 — 횟수가 아니라 시간으로 기다린다
            while ( deadline.isExpired() == false )
            {
                _server.update();
                provider.update( _nowMs );
                vector<PlatformLoginVerification> listVerification;
                (void)provider.pollVerifications( listVerification );
                for ( PlatformLoginVerification& verification : listVerification )
                {
                    if ( verification._verificationID == verificationID )
                        return std::move( verification );
                }
                _nowMs += 10;
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
            PlatformLoginVerification lost;
            lost._bUnavailable = SW_TRUE;
            return lost;
        }
    };
} // namespace

SW_TEST_CASE( PlatformLoginTest, OidcAcceptsValidTokenAndRejectsBadOnes )
{
    using Internal = TestPlatformLoginInternal;
    IssuerFixture    fixture;
    const SigningKey key   = Internal::makeKey( "k1", NetSignatureAlgorithm::RsaPkcs1Sha256 );
    const SigningKey other = Internal::makeKey( "k1", NetSignatureAlgorithm::RsaPkcs1Sha256 ); // 같은 kid, 다른 키(위조)
    fixture._handler._jwks = Internal::makeJwks( { &key } );
    OidcLoginProvider provider{ fixture.makeOidcSettings(), &NetSecurity::getProvider(), &fixture._client };

    const PlatformLoginVerification good = fixture.verify( provider, Internal::makeToken( key, "https://issuer.test", "client-a", 2000, "user-1", nullptr ) );
    SW_ASSERT_TRUE( good.isVerified() );
    SW_EXPECT_EQUAL( string( "user-1" ), good._subject );
    SW_EXPECT_EQUAL( string( "Hero" ), good._displayName );
    SW_EXPECT_EQUAL( 1, fixture._handler._jwksHitCount );

    SW_EXPECT_TRUE( fixture.verify( provider, Internal::makeToken( other, "https://issuer.test", "client-a", 2000, "user-1", nullptr ) )._bRejected == SW_TRUE );
    SW_EXPECT_TRUE( fixture.verify( provider, Internal::makeToken( key, "https://issuer.test", "client-b", 2000, "user-1", nullptr ) )._bRejected == SW_TRUE ); // aud
    SW_EXPECT_TRUE( fixture.verify( provider, Internal::makeToken( key, "https://evil.test", "client-a", 2000, "user-1", nullptr ) )._bRejected == SW_TRUE );   // iss
    SW_EXPECT_TRUE( fixture.verify( provider, Internal::makeToken( key, "https://issuer.test", "client-a", 900, "user-1", nullptr ) )._bRejected == SW_TRUE );  // exp(+60 초 허용 넘음)
    // alg none — 서명 없는 토큰은 머리에서 거절
    const string noneHeader = Base64Util::encodeURL( reinterpret_cast<const uint8*>( "{\"alg\":\"none\"}" ), 14 );
    const string body       = Base64Util::encodeURL( reinterpret_cast<const uint8*>( "{\"sub\":\"x\"}" ), 11 );
    SW_EXPECT_TRUE( fixture.verify( provider, noneHeader + "." + body + ".AA" )._bRejected == SW_TRUE );
    SW_EXPECT_TRUE( fixture.verify( provider, "not-a-token" )._bRejected == SW_TRUE );

    PlatformLoginProviderConfig nonceSettings = fixture.makeOidcSettings();
    nonceSettings._bRequireNonce              = SW_TRUE;
    OidcLoginProvider nonceProvider{ nonceSettings, &NetSecurity::getProvider(), &fixture._client };
    const string      withNonce = Internal::makeToken( key, "https://issuer.test", "client-a", 2000, "user-2", "n-abc" );
    SW_EXPECT_TRUE( fixture.verify( nonceProvider, withNonce )._bRejected == SW_TRUE );            // 클라이언트가 nonce 를 싣지 않았다
    SW_EXPECT_TRUE( fixture.verify( nonceProvider, withNonce + "|n-zzz" )._bRejected == SW_TRUE ); // 다른 nonce
    SW_EXPECT_TRUE( fixture.verify( nonceProvider, withNonce + "|n-abc" ).isVerified() );
}

SW_TEST_CASE( PlatformLoginTest, OidcRefetchesOnKeyRotationAndKeepsWorkingFromCacheWhenJwksIsDown )
{
    using Internal = TestPlatformLoginInternal;
    IssuerFixture    fixture;
    const SigningKey first  = Internal::makeKey( "k1", NetSignatureAlgorithm::RsaPkcs1Sha256 );
    const SigningKey second = Internal::makeKey( "k2", NetSignatureAlgorithm::EcdsaP256Sha256 );
    fixture._handler._jwks  = Internal::makeJwks( { &first } );
    OidcLoginProvider provider{ fixture.makeOidcSettings(), &NetSecurity::getProvider(), &fixture._client };
    SW_ASSERT_TRUE( fixture.verify( provider, Internal::makeToken( first, "https://issuer.test", "client-a", 5000, "u", nullptr ) ).isVerified() );
    SW_EXPECT_EQUAL( 1, provider.getJwksFetchCount() );

    // 키 회전 — 발급자가 k2(ES256)로 서명하기 시작했다. 모르는 kid 가 다시 받기를 부른다(최소 간격 30 초 뒤).
    fixture._handler._jwks = Internal::makeJwks( { &first, &second } );
    fixture._nowMs += 31000;
    SW_ASSERT_TRUE( fixture.verify( provider, Internal::makeToken( second, "https://issuer.test", "client-a", 5000, "u", nullptr ) ).isVerified() );
    SW_EXPECT_EQUAL( 2, provider.getJwksFetchCount() );
    SW_EXPECT_EQUAL( 2, provider.getKeyCache().getKeyCount() );

    // JWKS 서버가 죽었다 — 캐시에 있는 키로는 계속 확인하고, 모르는 kid 는 제공자 없음(거절이 아니다 — 다시 시도).
    fixture._handler._bDown = SW_TRUE;
    fixture._nowMs += 31000;
    SW_EXPECT_TRUE( fixture.verify( provider, Internal::makeToken( second, "https://issuer.test", "client-a", 5000, "u", nullptr ) ).isVerified() );
    const SigningKey                unknown = Internal::makeKey( "k3", NetSignatureAlgorithm::RsaPkcs1Sha256 );
    const PlatformLoginVerification down    = fixture.verify( provider, Internal::makeToken( unknown, "https://issuer.test", "client-a", 5000, "u", nullptr ) );
    SW_EXPECT_TRUE( down._bUnavailable == SW_TRUE );
    SW_EXPECT_TRUE( down._bRejected == SW_FALSE );

    // 서버가 살아났고 그래도 k3 가 없다 — 이번엔 거절. 최소 간격 안의 두 번째 모르는 kid 는 다시 받지 않고 거절(도배 막기).
    fixture._handler._bDown = SW_FALSE;
    fixture._nowMs += 31000;
    const int32 fetchBefore = provider.getJwksFetchCount();
    SW_EXPECT_TRUE( fixture.verify( provider, Internal::makeToken( unknown, "https://issuer.test", "client-a", 5000, "u", nullptr ) )._bRejected == SW_TRUE );
    SW_EXPECT_EQUAL( fetchBefore + 1, provider.getJwksFetchCount() );
    SW_EXPECT_TRUE( fixture.verify( provider, Internal::makeToken( unknown, "https://issuer.test", "client-a", 5000, "u", nullptr ) )._bRejected == SW_TRUE );
    SW_EXPECT_EQUAL( fetchBefore + 1, provider.getJwksFetchCount() );
}

SW_TEST_CASE( PlatformLoginTest, ProfileAPIProviderReadsTheSubjectPath )
{
    IssuerFixture               fixture;
    PlatformLoginProviderConfig settings;
    settings._name            = "naver";
    settings._kind            = PlatformLoginProviderKind::AccessTokenProfile;
    settings._profileURL      = fixture.makeURL( "/profile" );
    settings._subjectPath     = "response.id";
    settings._displayNamePath = "response.nickname";
    SW_ASSERT_TRUE( PlatformLoginProviderFactory::isValidSettings( settings ) );
    ProfileAPILoginProvider         provider{ settings, &fixture._client };
    const PlatformLoginVerification good = fixture.verify( provider, "good" );
    SW_ASSERT_TRUE( good.isVerified() );
    SW_EXPECT_EQUAL( string( "n-1" ), good._subject );
    SW_EXPECT_EQUAL( string( "Nick" ), good._displayName );
    SW_EXPECT_TRUE( fixture.verify( provider, "bad" )._bRejected == SW_TRUE ); // 401
    fixture._handler._bDown = SW_TRUE;
    SW_EXPECT_TRUE( fixture.verify( provider, "good" )._bUnavailable == SW_TRUE ); // 503
}

/**
 * @brief [PlatformLoginTest] 티켓 확인 API 제공자는 키 · 티켓을 쿼리로 보내 결과 경로가 성공 값일 때만 주체를 받는다 — 다른 결과 · 403 · 쿼리에 못 싣는 티켓은 거절, 503 은 쓸 수 없음
 * @details 게임 플랫폼 사용자 인증 티켓(OnlineKits A5)을 제품 코드 없이 데이터로 정의한다. 키는 환경 변수가 원본이고 테스트는 직접 넘긴다.
 */
SW_TEST_CASE( PlatformLoginTest, TicketQueryProviderChecksTheResultPath )
{
    IssuerFixture               fixture;
    PlatformLoginProviderConfig settings;
    settings._name                 = "steam";
    settings._kind                 = PlatformLoginProviderKind::TicketQuery;
    settings._profileURL           = fixture.makeURL( "/ticket?appid=480" );
    settings._ticketParam          = "ticket";
    settings._keyParam             = "key";
    settings._keySecretEnvironment = "SW_TEST_TICKET_KEY";
    settings._resultPath           = "response.params.result";
    settings._resultValue          = "OK";
    settings._subjectPath          = "response.params.steamid";
    SW_ASSERT_TRUE( PlatformLoginProviderFactory::isValidSettings( settings ) );
    TicketQueryLoginProvider provider{ settings, "secret-key", &fixture._client };
    SW_EXPECT_TRUE( provider.makeRequestURL( "abc123" ).find( "/ticket?appid=480&key=secret-key&ticket=abc123" ) != string::npos );

    const PlatformLoginVerification good = fixture.verify( provider, "abc123" );
    SW_ASSERT_TRUE( good.isVerified() );
    SW_EXPECT_EQUAL( string( "76561198000000001" ), good._subject );
    SW_EXPECT_TRUE( fixture.verify( provider, "ffff00" )._bRejected == SW_TRUE );    // 200 이지만 결과가 OK 가 아니다
    SW_EXPECT_TRUE( fixture.verify( provider, "abc&key=x" )._bRejected == SW_TRUE ); // 쿼리에 못 싣는 티켓 — 묻지 않고 거절
    TicketQueryLoginProvider wrongKey{ settings, "other-key", &fixture._client };
    SW_EXPECT_TRUE( fixture.verify( wrongKey, "abc123" )._bRejected == SW_TRUE ); // 403
    fixture._handler._bDown = SW_TRUE;
    SW_EXPECT_TRUE( fixture.verify( provider, "abc123" )._bUnavailable == SW_TRUE ); // 503

    // 데이터로 읽힌다. 필수 칸이 빠지면 오류다.
    vector<PlatformLoginProviderConfig> listSettings;
    string                              error;
    SW_ASSERT_TRUE( PlatformLoginProviderFactory::readSettings( "{\"providers\":[{\"name\":\"steam\",\"kind\":\"ticketQuery\",\"profileUrl\":\"https://x/a?appid=1\","
                                                                "\"ticketParam\":\"ticket\",\"keyParam\":\"key\",\"keySecretEnvironment\":\"K\",\"resultPath\":\"r\","
                                                                "\"resultValue\":\"OK\",\"subjectPath\":\"s\"}]}",
                                                                listSettings, error ) );
    SW_EXPECT_TRUE( listSettings[0]._kind == PlatformLoginProviderKind::TicketQuery );
    SW_EXPECT_FALSE( PlatformLoginProviderFactory::readSettings( "{\"providers\":[{\"name\":\"steam\",\"kind\":\"ticketQuery\",\"profileUrl\":\"https://x/a\"}]}", listSettings,
                                                                 error ) );
    // 키 환경 변수가 없으면 만들지 않는다(서버 기동 실패 — 키 없는 확인으로 대신 뜨지 않는다).
    HTTPClient unused;
    settings._keySecretEnvironment = "SW_TEST_TICKET_KEY_THAT_IS_NOT_SET";
    SW_EXPECT_TRUE( PlatformLoginProviderFactory::create( settings, nullptr, &unused ) == nullptr );
}

SW_TEST_CASE( PlatformLoginTest, ProviderSettingsComeFromData )
{
    const utf8*                         pJSON = "{\"providers\":["
                                                "{\"name\":\"google\",\"kind\":\"oidc\",\"issuer\":\"https://accounts.google.com\",\"jwksUrl\":\"https://www.googleapis.com/oauth2/v3/certs\","
                                                "\"clientIds\":[\"a\",\"b\"],\"requireNonce\":true},"
                                                "{\"name\":\"kakao\",\"kind\":\"oidc\",\"issuer\":\"https://kauth.kakao.com\",\"jwksUrl\":\"https://kauth.kakao.com/.well-known/jwks.json\","
                                                "\"clientIds\":[\"k\"],\"displayNamePath\":\"nickname\"},"
                                                "{\"name\":\"naver\",\"kind\":\"profile\",\"profileUrl\":\"https://openapi.naver.com/v1/nid/me\",\"subjectPath\":\"response.id\"}]}";
    vector<PlatformLoginProviderConfig> listSettings;
    string                              error;
    SW_ASSERT_TRUE( PlatformLoginProviderFactory::readSettings( pJSON, listSettings, error ) );
    SW_ASSERT_EQUAL( size_t( 3 ), listSettings.size() );
    SW_EXPECT_EQUAL( size_t( 2 ), listSettings[0]._listClientID.size() );
    SW_EXPECT_TRUE( listSettings[0]._bRequireNonce == SW_TRUE );
    SW_EXPECT_EQUAL( string( "sub" ), listSettings[1]._subjectPath );
    SW_EXPECT_TRUE( listSettings[2]._kind == PlatformLoginProviderKind::AccessTokenProfile );
    HTTPClient unused;
    SW_EXPECT_TRUE( PlatformLoginProviderFactory::create( listSettings[2], nullptr, &unused ) != nullptr );
    SW_EXPECT_TRUE( PlatformLoginProviderFactory::create( listSettings[0], nullptr, &unused ) == nullptr ); // OIDC 는 서명 제공자가 있어야

    SW_EXPECT_FALSE( PlatformLoginProviderFactory::readSettings( "{\"providers\":[{\"name\":\"x\",\"kind\":\"oidc\",\"isuer\":\"typo\"}]}", listSettings, error ) );
    SW_EXPECT_TRUE( error.find( "isuer" ) != string::npos ); // 철자가 틀린 키는 오류
    SW_EXPECT_FALSE( PlatformLoginProviderFactory::readSettings( "{\"providers\":[{\"name\":\"Bad Name\",\"kind\":\"profile\",\"profileUrl\":\"u\"}]}", listSettings, error ) );
    SW_EXPECT_FALSE( PlatformLoginProviderFactory::readSettings( "{\"providers\":[{\"name\":\"x\",\"kind\":\"oidc\"}]}", listSettings, error ) ); // 필수 칸
}

SW_TEST_CASE( PlatformLoginTest, PcLoopbackPkceFlowGetsAnIDTokenWithNonce )
{
    using Internal = TestPlatformLoginInternal;
    IssuerFixture           fixture;
    RecordingBrowser        browser;
    LoopbackPkceLoginClient client;
    PkceLoginProviderConfig settings;
    settings._provider         = "google";
    settings._authorizationURL = fixture.makeURL( "/authorize?prompt=select_account" );
    settings._tokenURL         = fixture.makeURL( "/token" );
    settings._clientID         = "client-a";
    StreamTransportConfig transportSettings;
    transportSettings._ioThreadCount = 0;
    SW_ASSERT_TRUE( client.initialize( fixture._network.createTransport(), fixture._network.createTransport(), transportSettings, &NetSecurity::getProvider(),
                                       &browser, { settings } ) );
    client.getHTTPClient().registerTLSContext( "localhost", fixture._clientContext.get() );

    const SigningKey key       = Internal::makeKey( "k1", NetSignatureAlgorithm::EcdsaP256Sha256 );
    const uint64     requestID = client.beginLogin( "google", kNowMs );
    SW_ASSERT_FALSE( browser._lastURL.empty() );
    SW_EXPECT_EQUAL( string( "select_account" ), Internal::findQuery( browser._lastURL, "prompt" ) ); // 설정의 쿼리를 지킨다
    SW_EXPECT_EQUAL( string( "S256" ), Internal::findQuery( browser._lastURL, "code_challenge_method" ) );
    const string state       = Internal::findQuery( browser._lastURL, "state" );
    const string nonce       = Internal::findQuery( browser._lastURL, "nonce" );
    const string redirectUri = Internal::findQuery( browser._lastURL, "redirect_uri" );
    SW_EXPECT_TRUE( StringUtil::startsWith( redirectUri, "http://127.0.0.1:" ) );
    fixture._handler._expectedChallenge = Internal::findQuery( browser._lastURL, "code_challenge" );
    fixture._handler._idToken           = Internal::makeToken( key, "https://issuer.test", "client-a", 2000, "pc-user", nonce.c_str() );

    // "브라우저" — 위조 state 로 한 번(무시), 진짜 state 로 한 번 리다이렉트를 부른다.
    HTTPClient&       browserClient = fixture._client;
    HTTPClientRequest forged;
    forged._url = redirectUri + "?code=" + fixture._handler._expectedCode + "&state=forged";
    (void)browserClient.submitRequest( forged, kNowMs );
    HTTPClientRequest redirect;
    redirect._url = redirectUri + "?code=" + fixture._handler._expectedCode + "&state=" + HTTPUtil::encodePercent( state );
    (void)browserClient.submitRequest( redirect, kNowMs );
    vector<PlatformLoginClientResult> listResult;
    vector<HTTPClientResponse>        listPage;
    for ( const Deadline deadline = Deadline::afterMilliseconds( 10000 ); listResult.empty() && deadline.isExpired() == false; )
    {
        fixture._server.update();
        browserClient.update( kNowMs );
        (void)browserClient.pollResponses( listPage );
        client.update( kNowMs );
        (void)client.pollResults( listResult );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
    SW_ASSERT_EQUAL( size_t( 1 ), listResult.size() );
    SW_EXPECT_EQUAL( requestID, listResult[0]._requestID );
    SW_ASSERT_TRUE( listResult[0]._bSucceeded == SW_TRUE );
    SW_EXPECT_EQUAL( fixture._handler._idToken + "|" + nonce, string( reinterpret_cast<const utf8*>( listResult[0]._ticket.data() ), listResult[0]._ticket.size() ) );
    SW_EXPECT_EQUAL( 1, fixture._handler._tokenHitCount ); // 위조 state 는 토큰 교환까지 가지 않았다
    SW_ASSERT_EQUAL( size_t( 2 ), listPage.size() );
    int32 rejectedPageCount = 0;
    for ( const HTTPClientResponse& page : listPage )
    {
        rejectedPageCount += page._statusCode == 400 ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 1, rejectedPageCount );

    // 사용자가 거절했다 — 취소로 끝난다.
    (void)client.beginLogin( "google", kNowMs );
    const string      secondState = Internal::findQuery( browser._lastURL, "state" );
    HTTPClientRequest denied;
    denied._url = redirectUri + "?error=access_denied&state=" + HTTPUtil::encodePercent( secondState );
    (void)browserClient.submitRequest( denied, kNowMs );
    listResult.clear();
    for ( const Deadline deadline = Deadline::afterMilliseconds( 10000 ); listResult.empty() && deadline.isExpired() == false; )
    {
        fixture._server.update();
        browserClient.update( kNowMs );
        (void)browserClient.pollResponses( listPage );
        client.update( kNowMs );
        (void)client.pollResults( listResult );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
    SW_ASSERT_EQUAL( size_t( 1 ), listResult.size() );
    SW_EXPECT_TRUE( listResult[0]._bCancelled == SW_TRUE );
    SW_EXPECT_TRUE( listResult[0]._bSucceeded == SW_FALSE );

    // 모르는 제공자 · 시한
    listResult.clear();
    (void)client.beginLogin( "apple", kNowMs );
    (void)client.beginLogin( "google", kNowMs );
    client.update( kNowMs + settings._timeoutMs );
    (void)client.pollResults( listResult );
    SW_ASSERT_EQUAL( size_t( 2 ), listResult.size() );
    SW_EXPECT_TRUE( listResult[0]._bSucceeded == SW_FALSE );
    SW_EXPECT_TRUE( listResult[1]._bCancelled == SW_TRUE );
}

SW_TEST_CASE( PlatformLoginTest, LoginServiceCreatesAnAccountFromAVerifiedIDToken )
{
    using Internal = TestPlatformLoginInternal;
    IssuerFixture    fixture;
    const SigningKey key                        = Internal::makeKey( "k1", NetSignatureAlgorithm::RsaPkcs1Sha256 );
    fixture._handler._jwks                      = Internal::makeJwks( { &key } );
    unique_ptr<IPlatformLoginProvider> provider = PlatformLoginProviderFactory::create( fixture.makeOidcSettings(), &NetSecurity::getProvider(), &fixture._client );
    SW_ASSERT_TRUE( provider != nullptr );

    MemoryServiceDatabase  database;
    MemoryServiceStore     store{ &database };
    NetSecurityLoginCrypto crypto{ &NetSecurity::getProvider() };
    LoginServiceConfig     settings;
    settings._passwordHashParams._memoryKiB = 64; // 이 테스트는 비밀번호를 쓰지 않는다 — 없는 계정 해시를 가볍게
    const uint8  arrMasterKey[LoginTicketAuthority::kMasterKeySize]{};
    LoginService service;
    service.initialize( &store, &crypto, settings, arrMasterKey );
    SW_ASSERT_TRUE( service.registerPlatformProvider( provider.get() ) );

    const string token = Internal::makeToken( key, "https://issuer.test", "client-a", 2000, "google-sub-9", nullptr );
    service.platformLogin( "google", Internal::toBytes( token ), AccountClientInfo{}, 1, fixture._nowMs, 42 );
    vector<LoginCompletion> listCompletion;
    for ( const Deadline deadline = Deadline::afterMilliseconds( 10000 ); listCompletion.empty() && deadline.isExpired() == false; )
    {
        fixture._server.update();
        service.update( fixture._nowMs );
        (void)store.pollCompletions();
        service.drainCompletions( listCompletion );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
    SW_ASSERT_EQUAL( size_t( 1 ), listCompletion.size() );
    SW_EXPECT_EQUAL( uint64( 42 ), listCompletion[0]._requestTag );
    SW_ASSERT_TRUE( listCompletion[0]._result == LoginResult::Ok );
    SW_EXPECT_TRUE( listCompletion[0]._grant._bCreated == SW_TRUE );
    SW_EXPECT_TRUE( StringUtil::startsWith( listCompletion[0]._grant._identity._displayName, "Hero-" ) );

    store.shutdown();
    (void)store.pollCompletions();
    service.shutdown();
}

SW_TEST_CASE( PlatformLoginTest, ProviderSetNeedsATrustFileForHTTPSProviders )
{
    PlatformLoginProviderConfig plain;
    plain._name        = "naver";
    plain._kind        = PlatformLoginProviderKind::AccessTokenProfile;
    plain._profileURL  = "http://127.0.0.1:9/v1/nid/me";
    plain._subjectPath = "response.id";
    PlatformLoginProviderSet set;
    string                   error;
    SW_ASSERT_TRUE( set.initialize( { plain }, "", error ) );
    SW_ASSERT_EQUAL( 1, set.getProviderCount() );
    SW_EXPECT_EQUAL( string( "naver" ), string( set.getProvider( 0 )->getName() ) );

    PlatformLoginProviderConfig secure = plain;
    secure._name                       = "kakao";
    secure._profileURL                 = "https://kapi.kakao.com/v2/user/me";
    SW_EXPECT_FALSE( set.initialize( { plain, secure }, "", error ) ); // 신뢰 파일 없이 개발용 인증서로 바꾸지 않는다
    SW_EXPECT_TRUE( error.find( "platformTrustFile" ) != string::npos );
    SW_EXPECT_EQUAL( 0, set.getProviderCount() ); // 실패하면 만든 것을 모두 버린다
    set.shutdown();
}

SW_TEST_CASE( PlatformLoginTest, SystemBrowserOpensOnlyPlainWebAddresses )
{
    SW_EXPECT_TRUE( SystemExternalBrowser::isOpenableURL( "https://accounts.example.com/authorize?client_id=a&redirect_uri=http%3A%2F%2F127.0.0.1%3A5%2Fcb" ) );
    SW_EXPECT_TRUE( SystemExternalBrowser::isOpenableURL( "HTTP://127.0.0.1:8080/" ) );
    SW_EXPECT_FALSE( SystemExternalBrowser::isOpenableURL( "file:///etc/passwd" ) );
    SW_EXPECT_FALSE( SystemExternalBrowser::isOpenableURL( "javascript:alert(1)" ) );
    SW_EXPECT_FALSE( SystemExternalBrowser::isOpenableURL( "https://a.example/'; rm -rf ~; '" ) ); // 따옴표와 공백은 셸이 해석한다
    SW_EXPECT_FALSE( SystemExternalBrowser::isOpenableURL( "https://a.example/$HOME" ) );
    SW_EXPECT_FALSE( SystemExternalBrowser::isOpenableURL( "https://a.example/\"x" ) );
    SystemExternalBrowser browser;
    SW_EXPECT_FALSE( browser.openURL( "file:///etc/passwd" ) ); // 띄우기 전에 거절한다
}
