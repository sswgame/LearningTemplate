// 애플 로그인(OnlineKits A4) — 가짜 HTTPS 서버(JWKS · 토큰 · 철회)로: ID 토큰 확인은 공통 OIDC 구현이 하고, 인가 코드가 있으면 서버가 만든 client secret JWT(ES256, kid · iss · sub · aud)로
// 코드를 교환해 refresh token 을 연동 비밀로 돌려준다 · 코드가 없거나 틀려도 로그인은 성공(비밀만 없다) · 틀린 ID 토큰은 교환하지 않는다 · 철회(200 · 이미 무효 400 invalid_grant · 5xx 다시).
#include "pch.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Serialization/JSON/JSONWriter.h"

#include "EngineTest/GameFramework/Online/FakeHTTPSFixture.h"

#include "GameFramework/Base/Online/Security/JSONWebToken.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderConfig.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/Provider/Apple/AppleLoginProvider.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr int64 kAppleNowMs = 1000000; ///< 테스트 벽시계(초로 1000)

    /** @brief 가짜 애플 — `/jwks`, `/token`(client secret 확인 뒤 코드 교환), `/revoke`. */
    class FakeAppleHandler final : public IHTTPRequestHandler
    {
    public:
        NetPublicKey _secretPublicKey{}; ///< client secret 서명 키(ES256)의 공개 키
        sw::string   _jwks{};
        int32        _tokenHits{ 0 };
        int32        _revokeHits{ 0 };
        uint8        _bClientSecretValid{ SW_FALSE };

        void onHTTPRequest( const HTTPServerRequest& request, HTTPServerResponse& outResponse ) override
        {
            if ( request._path == "/jwks" )
            {
                test::setResponseText( outResponse, 200, _jwks );
                return;
            }
            vector<HTTPHeader> listForm;
            if ( request._method != HTTPMethod::Post || HTTPUtil::decodeForm( request.getBodyText(), listForm ) == false )
            {
                outResponse._statusCode = 400;
                return;
            }
            const auto find = [&listForm]( const utf8* pName ) -> sw::string
            {
                for ( const HTTPHeader& pair : listForm )
                {
                    if ( pair._name == pName )
                        return pair._value;
                }
                return {};
            };
            _bClientSecretValid = checkClientSecret( find( "client_secret" ), find( "client_id" ) ) ? SW_TRUE : SW_FALSE;
            if ( _bClientSecretValid == SW_FALSE )
            {
                test::setResponseText( outResponse, 400, "{\"error\":\"invalid_client\"}" );
                return;
            }
            if ( request._path == "/token" )
            {
                ++_tokenHits;
                if ( find( "grant_type" ) == "authorization_code" && find( "code" ) == "good-code" )
                    test::setResponseText( outResponse, 200, "{\"access_token\":\"a\",\"refresh_token\":\"r-1\",\"token_type\":\"Bearer\"}" );
                else
                    test::setResponseText( outResponse, 400, "{\"error\":\"invalid_grant\"}" );
                return;
            }
            if ( request._path == "/revoke" )
            {
                ++_revokeHits;
                const sw::string token = find( "token" );
                if ( find( "token_type_hint" ) != "refresh_token" )
                    outResponse._statusCode = 400;
                else if ( token == "r-1" )
                    outResponse._statusCode = 200;
                else if ( token == "gone" )
                    test::setResponseText( outResponse, 400, "{\"error\":\"invalid_grant\"}" );
                else
                    outResponse._statusCode = 503;
                return;
            }
            outResponse._statusCode = 404;
        }

    private:
        bool checkClientSecret( const sw::string& secret, const sw::string& clientID ) const
        {
            JSONWebToken token;
            sw::string   issuer;
            sw::string   subject;
            sw::string   audience;
            int64        expiresAt = 0;
            return token.parse( secret ) && token.getAlgorithm() == NetSignatureAlgorithm::EcdsaP256Sha256 && token.getKeyID() == "KEY1" &&
                   token.verifySignature( NetSecurity::getProvider(), _secretPublicKey ) && token.findText( "iss", issuer ) && issuer == "TEAM1" &&
                   token.findText( "sub", subject ) && subject == clientID && clientID == "com.example.app" && token.findText( "aud", audience ) &&
                   audience == AppleLoginProvider::kAudience && token.findInteger( "exp", expiresAt ) && expiresAt > 1000;
        }
    };

    struct AppleFixture
    {
        FakeAppleHandler                       _handler;
        test::FakeHTTPSFixture                 _https;
        sw::unique_ptr<HTTPClient>             _appleClient;
        NetPublicKey                           _idPublicKey{};
        sw::string                             _idPrivateKeyPem{};
        sw::unique_ptr<IPlatformLoginProvider> _provider;
        int64                                  _nowMs{ kAppleNowMs };

        AppleFixture()
            : _handler{}
            , _https{ &_handler }
            , _appleClient{ _https.createClient() }
            , _provider{}
        {
            INetSecurityProvider& security = NetSecurity::getProvider();
            sw::string            signingKeyPem;
            (void)security.createSigningKeyPair( NetSignatureAlgorithm::EcdsaP256Sha256, signingKeyPem, _handler._secretPublicKey ); // 실패면 키가 비어 아래 서명 · 확인이 실패로 드러난다
            (void)security.createSigningKeyPair( NetSignatureAlgorithm::RsaPkcs1Sha256, _idPrivateKeyPem, _idPublicKey );            // 실패면 키가 비어 아래 ID 토큰 확인이 실패로 드러난다
            _handler._jwks = "{\"keys\":[" + JwksKeyCache::writeJwk( _idPublicKey, "id-key" ) + "]}";
            PlatformLoginProviderConfig settings;
            settings._name         = "apple";
            settings._kind         = PlatformLoginProviderKind::AppleSignIn;
            settings._issuer       = "https://issuer.test";
            settings._jwksURL      = _https.makeURL( "/jwks" );
            settings._tokenURL     = _https.makeURL( "/token" );
            settings._revokeURL    = _https.makeURL( "/revoke" );
            settings._teamID       = "TEAM1";
            settings._signingKeyID = "KEY1";
            settings._subjectPath  = "sub";
            settings._listClientID.push_back( "com.example.app" );
            _provider = sw::make_unique<AppleLoginProvider>( settings, std::move( signingKeyPem ), &security, &_https._client, _appleClient.get() );
        }

        sw::string makeIDToken( const utf8* pSubject, const utf8* pAudience = "com.example.app" ) const
        {
            sw::JSONWriter writer;
            writer.beginObject();
            writer.key( "iss" );
            writer.valueString( "https://issuer.test" );
            writer.key( "aud" );
            writer.valueString( pAudience );
            writer.key( "sub" );
            writer.valueString( pSubject );
            writer.key( "iat" );
            writer.valueInt( 900 );
            writer.key( "exp" );
            writer.valueInt( 2000 );
            writer.endObject();
            const std::optional<sw::JSONText> payload = writer.finish();
            sw::string                        compact;
            if ( payload.has_value() )
                (void)JSONWebToken::makeSigned( NetSecurity::getProvider(), NetSignatureAlgorithm::RsaPkcs1Sha256, "id-key", *payload, _idPrivateKeyPem, compact );
            return compact;
        }

        PlatformLoginVerification verify( const sw::string& ticket )
        {
            const uint64   verificationID = _provider->submitVerification( vector<uint8>( reinterpret_cast<const uint8*>( ticket.data() ), reinterpret_cast<const uint8*>( ticket.data() ) + ticket.size() ), _nowMs );
            const Deadline deadline       = Deadline::afterMilliseconds( 10000 );
            while ( deadline.isExpired() == false )
            {
                _https.pump();
                _provider->update( _nowMs );
                vector<PlatformLoginVerification> listVerification;
                (void)_provider->pollVerifications( listVerification );
                for ( PlatformLoginVerification& verification : listVerification )
                {
                    if ( verification._verificationID == verificationID )
                        return std::move( verification );
                }
                _nowMs += 10;
            }
            PlatformLoginVerification lost;
            lost._bUnavailable = SW_TRUE;
            return lost;
        }

        /** @brief 철회 하나를 끝까지 기다려 결과를 반환합니다. */
        PlatformLoginRevocation revoke( string_view secret )
        {
            const uint64   revocationID = _provider->submitRevocation( secret, _nowMs );
            const Deadline deadline     = Deadline::afterMilliseconds( 10000 );
            while ( deadline.isExpired() == false )
            {
                _https.pump();
                _provider->update( _nowMs );
                vector<PlatformLoginRevocation> listRevocation;
                (void)_provider->pollRevocations( listRevocation );
                for ( const PlatformLoginRevocation& revocation : listRevocation )
                {
                    if ( revocation._revocationID == revocationID )
                        return revocation;
                }
                _nowMs += 10;
            }
            PlatformLoginRevocation lost;
            lost._bUnavailable = SW_TRUE;
            return lost;
        }
    };
} // namespace

SW_TEST_CASE( AppleLoginTest, AnAuthorizationCodeIsExchangedForARefreshTokenSignedWithTheClientSecret )
{
    AppleFixture                    fixture;
    const PlatformLoginVerification verification = fixture.verify( fixture.makeIDToken( "apple-user-1" ) + "|nonce-1|good-code" );
    SW_ASSERT_TRUE( verification.isVerified() );
    SW_EXPECT_EQUAL( verification._subject, sw::string( "apple-user-1" ) );
    SW_EXPECT_EQUAL( verification._linkSecret, sw::string( "r-1" ) );
    SW_EXPECT_TRUE( fixture._handler._bClientSecretValid == SW_TRUE ); // ES256 · kid · iss=팀 · sub=client id · aud=애플
    SW_EXPECT_EQUAL( fixture._handler._tokenHits, 1 );
}

SW_TEST_CASE( AppleLoginTest, LoginSucceedsWithoutASecretWhenTheCodeIsMissingOrRefused )
{
    AppleFixture                    fixture;
    const PlatformLoginVerification noCode = fixture.verify( fixture.makeIDToken( "apple-user-2" ) );
    SW_ASSERT_TRUE( noCode.isVerified() );
    SW_EXPECT_TRUE( noCode._linkSecret.empty() );
    SW_EXPECT_EQUAL( fixture._handler._tokenHits, 0 ); // 코드가 없으면 교환하지 않는다

    const PlatformLoginVerification noCodeWithNonce = fixture.verify( fixture.makeIDToken( "apple-user-2" ) + "|n" );
    SW_EXPECT_TRUE( noCodeWithNonce.isVerified() );
    SW_EXPECT_EQUAL( fixture._handler._tokenHits, 0 );

    {
        SW_TEST_DEFENSIVE_SCOPE( "the provider warns that the code could not be exchanged" );
        const PlatformLoginVerification badCode = fixture.verify( fixture.makeIDToken( "apple-user-3" ) + "||stale-code" );
        SW_ASSERT_TRUE( badCode.isVerified() ); // ID 토큰은 이미 확인했다 — 로그인은 된다
        SW_EXPECT_TRUE( badCode._linkSecret.empty() );
    }
    SW_EXPECT_EQUAL( fixture._handler._tokenHits, 1 );
}

SW_TEST_CASE( AppleLoginTest, ARejectedIDTokenNeverReachesTheTokenEndpoint )
{
    AppleFixture                    fixture;
    const PlatformLoginVerification wrongAudience = fixture.verify( fixture.makeIDToken( "apple-user-4", "com.other.app" ) + "|n|good-code" );
    SW_EXPECT_TRUE( wrongAudience._bRejected == SW_TRUE );
    SW_EXPECT_TRUE( wrongAudience._linkSecret.empty() );
    SW_EXPECT_TRUE( fixture.verify( "not-a-token||good-code" )._bRejected == SW_TRUE );
    SW_EXPECT_EQUAL( fixture._handler._tokenHits, 0 );
}

SW_TEST_CASE( AppleLoginTest, RevocationRevokesTheRefreshTokenAndRetriesWhenAppleIsDown )
{
    AppleFixture fixture;
    SW_EXPECT_TRUE( fixture._provider->supportsRevocation() );
    const PlatformLoginRevocation revoked = fixture.revoke( "r-1" );
    SW_EXPECT_TRUE( revoked._bUnavailable == SW_FALSE );
    SW_EXPECT_TRUE( fixture._handler._bClientSecretValid == SW_TRUE );
    const PlatformLoginRevocation alreadyGone = fixture.revoke( "gone" ); // 이미 무효인 토큰 — 더 철회할 것이 없다
    SW_EXPECT_TRUE( alreadyGone._bUnavailable == SW_FALSE );
    const PlatformLoginRevocation down = fixture.revoke( "boom" ); // 5xx — 대기열에 남아 다시 한다
    SW_EXPECT_TRUE( down._bUnavailable == SW_TRUE );
    SW_EXPECT_EQUAL( fixture._handler._revokeHits, 3 );
}

SW_TEST_CASE( AppleLoginTest, TicketsSplitIntoIDTokenNonceAndCode )
{
    sw::string idToken;
    sw::string nonce;
    sw::string code;
    AppleLoginProvider::splitTicket( "tok|n1|c1", idToken, nonce, code );
    SW_EXPECT_EQUAL( idToken, sw::string( "tok" ) );
    SW_EXPECT_EQUAL( nonce, sw::string( "n1" ) );
    SW_EXPECT_EQUAL( code, sw::string( "c1" ) );
    AppleLoginProvider::splitTicket( "tok||c1", idToken, nonce, code );
    SW_EXPECT_TRUE( nonce.empty() );
    SW_EXPECT_EQUAL( code, sw::string( "c1" ) );
    AppleLoginProvider::splitTicket( "tok|n1", idToken, nonce, code );
    SW_EXPECT_TRUE( code.empty() );
    AppleLoginProvider::splitTicket( "tok", idToken, nonce, code );
    SW_EXPECT_TRUE( nonce.empty() && code.empty() );
}

SW_TEST_CASE( AppleLoginTest, SettingsComeFromDataAndNeedTheKeyFields )
{
    vector<PlatformLoginProviderConfig> listSettings;
    sw::string                          error;
    SW_ASSERT_TRUE( PlatformLoginProviderFactory::readSettings(
        "{\"providers\":[{\"name\":\"apple\",\"kind\":\"appleSignIn\",\"issuer\":\"https://appleid.apple.com\",\"jwksUrl\":\"https://appleid.apple.com/auth/keys\","
        "\"clientIds\":[\"com.example.app\"],\"teamId\":\"TEAM1\",\"signingKeyId\":\"KEY1\",\"keySecretEnvironment\":\"SW_TEST_APPLE_KEY\"}]}",
        listSettings, error ) );
    SW_ASSERT_EQUAL( listSettings.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listSettings[0]._kind == PlatformLoginProviderKind::AppleSignIn );
    SW_EXPECT_EQUAL( listSettings[0]._subjectPath, sw::string( "sub" ) );
    SW_EXPECT_EQUAL( listSettings[0]._teamID, sw::string( "TEAM1" ) );
    SW_EXPECT_FALSE( PlatformLoginProviderFactory::readSettings(
        "{\"providers\":[{\"name\":\"apple\",\"kind\":\"appleSignIn\",\"issuer\":\"https://appleid.apple.com\",\"jwksUrl\":\"https://appleid.apple.com/auth/keys\","
        "\"clientIds\":[\"com.example.app\"],\"teamId\":\"TEAM1\"}]}",
        listSettings, error ) ); // 서명 키 id · 키 변수가 없다
}
