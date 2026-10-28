// APNs 푸시 제공자(OnlineKits G4 · H2-5) — 가짜 HTTP/2 서버로: 요청 모양(`POST /3/device/<토큰>`, `apns-topic` · `apns-push-type`, 소문자 `authorization: bearer <JWT>`)과 제공자 토큰(ES256 서명 · `kid` · `iss`)이
// 키로 서명됐는지, 40 분 안에는 토큰을 재사용하고 지나면 새로 서명하는지, 상태 대응(410 무효, 429 늦추기 + Retry-After, 5xx 다시, 403 이면 토큰을 버려 새로 서명, 그 밖의 4xx 거절),
// 조용한 알림 · 문구 JSON, 16 진이 아닌 기기 토큰은 보내지 않음, 설정 파일 · 환경 변수와 쓸 수 없는 키.
#include "pch.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/String/Base64Util.h"
#include "Core/Time/Clock.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "EngineTest/GameFramework/Online/FakeHTTP2Fixture.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Apns/ApnsPushProvider.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushProviderSet.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using test::FakeHTTP2Fixture;
using test::SeenRequest;

namespace
{
    constexpr const utf8* kDeviceToken = "00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff";

    /** @brief 가짜 HTTP/2 서버 + 새 서명 키로 만든 APNs 제공자입니다. */
    struct ApnsFixture
    {
        FakeHTTP2Fixture _fixture;
        NetPublicKey     _publicKey{};
        string           _privateKeyPem{};
        ApnsPushProvider _provider;

        ApnsFixture()
            : _fixture{ "h2", "h2" }
            , _provider{ makeSettings( _fixture, _publicKey, _privateKeyPem ), &NetSecurity::getProvider(), &_fixture._client }
        {
        }

        static ApnsPushSettings makeSettings( const FakeHTTP2Fixture& fixture, NetPublicKey& outPublicKey, string& outPrivateKeyPem )
        {
            (void)NetSecurity::getProvider().createSigningKeyPair( NetSignatureAlgorithm::EcdsaP256Sha256, outPrivateKeyPem, outPublicKey ); // 실패면 키가 비어 서명이 실패로 드러난다
            ApnsPushSettings settings;
            settings._privateKeyPem = outPrivateKeyPem;
            settings._keyID         = "KEY123";
            settings._teamID        = "TEAM456";
            settings._topic         = "com.example.app";
            settings._baseURL       = fixture.makeBaseURL();
            return settings;
        }

        /** @brief 결과가 @p count 개 모일 때까지 제공자와 서버를 번갈아 돌립니다. */
        vector<PushDeliveryResult> drain( int32 count, int32 maxRounds = 4000 )
        {
            vector<PushDeliveryResult> listResult;
            for ( int32 round = 0; round < maxRounds && static_cast<int32>( listResult.size() ) < count; ++round )
            {
                _provider.update( _fixture._nowMs );
                (void)_provider.pollResults( listResult );
                _fixture.pump();
            }
            return listResult;
        }

        PushDeliveryStatus sendOne( uint64 deliveryID, const PushNotificationMessage& message, int64& outRetryAfterMs )
        {
            _provider.send( deliveryID, kDeviceToken, "ko", message, _fixture._nowMs );
            const vector<PushDeliveryResult> listResult = drain( 1 );
            if ( listResult.size() != 1 )
                return PushDeliveryStatus::Rejected;
            outRetryAfterMs = listResult[0]._retryAfterMs;
            return listResult[0]._status;
        }
    };

    PushNotificationMessage makeAlert()
    {
        PushNotificationMessage message;
        message._titleKey       = "push.mail.title";
        message._bodyKey        = "push.mail.body";
        message._deepLink       = "mailbox";
        message._arrArgument[0] = "3";
        message._badgeCount     = 5;
        return message;
    }

    /** @brief 서명 확인에 쓰는 JWT 의 세 조각입니다. */
    bool splitToken( const string& authorization, string& outSigningInput, string& outHeaderJSON, string& outClaimsJSON, vector<uint8>& outListSignature )
    {
        const string prefix = "bearer ";
        if ( authorization.compare( 0, prefix.size(), prefix ) != 0 )
            return false;
        const string token  = authorization.substr( prefix.size() );
        const size_t first  = token.find( '.' );
        const size_t second = first == string::npos ? string::npos : token.find( '.', first + 1 );
        if ( second == string::npos )
            return false;
        outSigningInput = token.substr( 0, second );
        vector<uint8> listHeader;
        vector<uint8> listClaim;
        const bool    bHeaderDecoded    = Base64Util::decodeURL( token.substr( 0, first ), listHeader );
        const bool    bClaimDecoded     = Base64Util::decodeURL( token.substr( first + 1, second - first - 1 ), listClaim );
        const bool    bSignatureDecoded = Base64Util::decodeURL( token.substr( second + 1 ), outListSignature );
        const bool    bAllDecoded       = bHeaderDecoded && bClaimDecoded && bSignatureDecoded;
        if ( bAllDecoded == false )
            return false;
        outHeaderJSON = string( reinterpret_cast<const utf8*>( listHeader.data() ), listHeader.size() );
        outClaimsJSON = string( reinterpret_cast<const utf8*>( listClaim.data() ), listClaim.size() );
        return true;
    }
} // namespace

/**
 * @brief [ApnsPushProviderTest] 요청은 `POST /3/device/<토큰>` + 소문자 머리이고 제공자 토큰은 ES256 으로 서명된 JWT(`kid` · `iss` · `iat`)이며 본문에 문구 키 · 인자 · 배지 · 사용자 키가 실린다
 */
SW_TEST_CASE( ApnsPushProviderTest, SendsAlertWithSignedProviderTokenAndKeyedPayload )
{
    ApnsFixture fixture;
    SW_ASSERT_TRUE( fixture._fixture._bReady );
    int64 retryAfterMs = 0;
    SW_EXPECT_TRUE( fixture.sendOne( 1, makeAlert(), retryAfterMs ) == PushDeliveryStatus::Delivered );
    SW_ASSERT_EQUAL( size_t( 1 ), fixture._fixture._server._listSeen.size() );
    const SeenRequest& seen = fixture._fixture._server._listSeen[0];
    SW_EXPECT_TRUE( seen.valueOf( ":method" ) == "POST" );
    string expectedPath = "/3/device/";
    expectedPath += kDeviceToken;
    SW_EXPECT_TRUE( seen.valueOf( ":path" ) == expectedPath );
    SW_EXPECT_TRUE( seen.valueOf( "apns-topic" ) == "com.example.app" );
    SW_EXPECT_TRUE( seen.valueOf( "apns-push-type" ) == "alert" );
    SW_EXPECT_TRUE( seen.find( "apns-priority" ) == nullptr );

    string        signingInput;
    string        headerJSON;
    string        claimsJSON;
    vector<uint8> signature;
    SW_ASSERT_TRUE( splitToken( seen.valueOf( "authorization" ), signingInput, headerJSON, claimsJSON, signature ) );
    SW_EXPECT_TRUE( NetSecurity::getProvider().verifySignature( fixture._publicKey, reinterpret_cast<const uint8*>( signingInput.data() ), static_cast<int32>( signingInput.size() ),
                                                                signature.data(), static_cast<int32>( signature.size() ) ) );
    JSONDocument header;
    SW_ASSERT_TRUE( header.tryParse( headerJSON ) );
    SW_EXPECT_TRUE( header.getRoot().get( "alg" ).asString() == "ES256" && header.getRoot().get( "kid" ).asString() == "KEY123" );
    JSONDocument claims;
    SW_ASSERT_TRUE( claims.tryParse( claimsJSON ) );
    SW_EXPECT_TRUE( claims.getRoot().get( "iss" ).asString() == "TEAM456" );
    SW_EXPECT_TRUE( claims.getRoot().get( "iat" ).isNumber() );

    JSONDocument payload;
    SW_ASSERT_TRUE( payload.tryParse( seen.getBodyText() ) );
    const JSONValue alert = payload.getRoot().get( "aps" ).get( "alert" );
    SW_EXPECT_TRUE( alert.get( "title-loc-key" ).asString() == "push.mail.title" && alert.get( "loc-key" ).asString() == "push.mail.body" );
    SW_EXPECT_EQUAL( size_t( 1 ), alert.get( "loc-args" ).size() );
    SW_EXPECT_TRUE( payload.getRoot().get( "deep_link" ).asString() == "mailbox" );
    SW_EXPECT_TRUE( payload.getRoot().get( "arg0" ).asString() == "3" );
}

/**
 * @brief [ApnsPushProviderTest] 제공자 토큰은 40 분 안에는 재사용하고(APNs 는 20 분보다 자주 갱신하면 거절) 지나면 새로 서명한다 — 연결도 하나로 이어 쓴다
 */
SW_TEST_CASE( ApnsPushProviderTest, ReusesTheProviderTokenWithinTheRefreshInterval )
{
    ApnsFixture fixture;
    SW_ASSERT_TRUE( fixture._fixture._bReady );
    int64 retryAfterMs = 0;
    SW_EXPECT_TRUE( fixture.sendOne( 1, makeAlert(), retryAfterMs ) == PushDeliveryStatus::Delivered );
    fixture._fixture._nowMs += ApnsPushProvider::kTokenRefreshMs - 60000;
    SW_EXPECT_TRUE( fixture.sendOne( 2, makeAlert(), retryAfterMs ) == PushDeliveryStatus::Delivered );
    fixture._fixture._nowMs += 120000; // 간격을 넘었다
    SW_EXPECT_TRUE( fixture.sendOne( 3, makeAlert(), retryAfterMs ) == PushDeliveryStatus::Delivered );
    SW_ASSERT_EQUAL( size_t( 3 ), fixture._fixture._server._listSeen.size() );
    const vector<SeenRequest>& listSeen = fixture._fixture._server._listSeen;
    SW_EXPECT_TRUE( listSeen[0].valueOf( "authorization" ) == listSeen[1].valueOf( "authorization" ) );
    SW_EXPECT_TRUE( listSeen[1].valueOf( "authorization" ) != listSeen[2].valueOf( "authorization" ) );
    SW_EXPECT_EQUAL( int32( 1 ), fixture._fixture._server._acceptedCount );
}

/**
 * @brief [ApnsPushProviderTest] 상태 대응 — 410 무효 · 429 늦추기(Retry-After 초) · 503 다시 · 403 거절이며 토큰을 버려 다음 배달이 새로 서명 · 400 거절
 */
SW_TEST_CASE( ApnsPushProviderTest, MapsStatusCodesAndDropsTheTokenOnForbidden )
{
    SW_EXPECT_TRUE( ApnsPushProvider::mapStatus( 200, false ) == PushDeliveryStatus::Delivered );
    SW_EXPECT_TRUE( ApnsPushProvider::mapStatus( 410, false ) == PushDeliveryStatus::InvalidToken );
    SW_EXPECT_TRUE( ApnsPushProvider::mapStatus( 429, false ) == PushDeliveryStatus::RateLimited );
    SW_EXPECT_TRUE( ApnsPushProvider::mapStatus( 503, false ) == PushDeliveryStatus::Transient );
    SW_EXPECT_TRUE( ApnsPushProvider::mapStatus( 403, false ) == PushDeliveryStatus::Rejected );
    SW_EXPECT_TRUE( ApnsPushProvider::mapStatus( 400, false ) == PushDeliveryStatus::Rejected );
    SW_EXPECT_TRUE( ApnsPushProvider::mapStatus( 0, true ) == PushDeliveryStatus::Transient );

    ApnsFixture fixture;
    SW_ASSERT_TRUE( fixture._fixture._bReady );
    int64 retryAfterMs                   = 0;
    fixture._fixture._server._statusCode = 410;
    SW_EXPECT_TRUE( fixture.sendOne( 1, makeAlert(), retryAfterMs ) == PushDeliveryStatus::InvalidToken );
    fixture._fixture._server._statusCode = 429;
    fixture._fixture._server._listExtraResponseHeader.push_back( HpackHeader{ "retry-after", "30" } );
    SW_EXPECT_TRUE( fixture.sendOne( 2, makeAlert(), retryAfterMs ) == PushDeliveryStatus::RateLimited );
    SW_EXPECT_EQUAL( int64( 30000 ), retryAfterMs );
    fixture._fixture._server._listExtraResponseHeader.clear();
    fixture._fixture._server._statusCode = 503;
    SW_EXPECT_TRUE( fixture.sendOne( 3, makeAlert(), retryAfterMs ) == PushDeliveryStatus::Transient );
    fixture._fixture._server._statusCode = 403;
    SW_EXPECT_TRUE( fixture.sendOne( 4, makeAlert(), retryAfterMs ) == PushDeliveryStatus::Rejected );
    fixture._fixture._server._statusCode = 200;
    SW_EXPECT_TRUE( fixture.sendOne( 5, makeAlert(), retryAfterMs ) == PushDeliveryStatus::Delivered );
    SW_ASSERT_EQUAL( size_t( 5 ), fixture._fixture._server._listSeen.size() );
    const vector<SeenRequest>& listSeen = fixture._fixture._server._listSeen;
    SW_EXPECT_TRUE( listSeen[2].valueOf( "authorization" ) == listSeen[3].valueOf( "authorization" ) ); // 403 전까지는 같은 토큰
    SW_EXPECT_TRUE( listSeen[3].valueOf( "authorization" ) != listSeen[4].valueOf( "authorization" ) ); // 403 뒤에는 새로 서명했다
}

/**
 * @brief [ApnsPushProviderTest] 문구가 없으면 조용한 알림(`background` + 우선순위 5 + `content-available`)이고, 16 진이 아닌 기기 토큰은 보내지 않고 InvalidToken 이다
 */
SW_TEST_CASE( ApnsPushProviderTest, QuietPushAndDeviceTokenRules )
{
    PushNotificationMessage quiet;
    quiet._deepLink = "mailbox";
    SW_EXPECT_FALSE( ApnsPushProvider::hasAlert( quiet ) );
    JSONDocument quietPayload;
    SW_ASSERT_TRUE( quietPayload.tryParse( ApnsPushProvider::makePayloadJSON( quiet ) ) );
    SW_EXPECT_TRUE( quietPayload.getRoot().get( "aps" ).get( "content-available" ).isNumber() );
    SW_EXPECT_FALSE( quietPayload.getRoot().get( "aps" ).get( "alert" ).isObject() );
    SW_EXPECT_TRUE( ApnsPushProvider::isValidDeviceToken( kDeviceToken ) );
    SW_EXPECT_FALSE( ApnsPushProvider::isValidDeviceToken( "" ) );
    SW_EXPECT_FALSE( ApnsPushProvider::isValidDeviceToken( "../../x" ) );
    SW_EXPECT_FALSE( ApnsPushProvider::isValidDeviceToken( string( 201, 'a' ) ) );

    ApnsFixture fixture;
    SW_ASSERT_TRUE( fixture._fixture._bReady );
    int64 retryAfterMs = 0;
    SW_EXPECT_TRUE( fixture.sendOne( 1, quiet, retryAfterMs ) == PushDeliveryStatus::Delivered );
    SW_ASSERT_EQUAL( size_t( 1 ), fixture._fixture._server._listSeen.size() );
    SW_EXPECT_TRUE( fixture._fixture._server._listSeen[0].valueOf( "apns-push-type" ) == "background" );
    SW_EXPECT_TRUE( fixture._fixture._server._listSeen[0].valueOf( "apns-priority" ) == "5" );

    fixture._provider.send( 2, "not hex!", "ko", makeAlert(), fixture._fixture._nowMs );
    vector<PushDeliveryResult> listResult;
    SW_EXPECT_EQUAL( int32( 1 ), fixture._provider.pollResults( listResult ) );
    SW_ASSERT_EQUAL( size_t( 1 ), listResult.size() );
    SW_EXPECT_TRUE( listResult[0]._status == PushDeliveryStatus::InvalidToken );
    SW_EXPECT_EQUAL( size_t( 1 ), fixture._fixture._server._listSeen.size() ); // 요청이 나가지 않았다
}

/**
 * @brief [ApnsPushProviderTest] 서명할 수 없는 키는 그 배달을 Rejected 로 끝낸다(요청이 나가지 않는다)
 */
SW_TEST_CASE( ApnsPushProviderTest, AnUnusableKeyRejectsTheDelivery )
{
    FakeHTTP2Fixture fixture( "h2", "h2" );
    SW_ASSERT_TRUE( fixture._bReady );
    ApnsPushSettings settings;
    settings._privateKeyPem = "not a pem";
    settings._keyID         = "K";
    settings._teamID        = "T";
    settings._topic         = "b";
    settings._baseURL       = fixture.makeBaseURL();
    ApnsPushProvider provider( std::move( settings ), &NetSecurity::getProvider(), &fixture._client );
    {
        SW_TEST_DEFENSIVE_SCOPE( "the signing failure is logged" );
        provider.send( 1, kDeviceToken, "ko", makeAlert(), fixture._nowMs );
    }
    vector<PushDeliveryResult> listResult;
    SW_ASSERT_EQUAL( int32( 1 ), provider.pollResults( listResult ) );
    SW_EXPECT_TRUE( listResult[0]._status == PushDeliveryStatus::Rejected );
    SW_EXPECT_TRUE( fixture._server._listSeen.empty() );
}

/**
 * @brief [ApnsPushProviderTest] 설정은 종류마다 키가 정해져 엄격하게 읽고(다른 종류의 키 · 빠진 필수 키는 오류), 인증 키 환경 변수가 없으면 제공자를 만들지 못한다
 */
SW_TEST_CASE( ApnsPushProviderTest, SettingsAreReadStrictlyAndTheKeyComesFromTheEnvironment )
{
    vector<PushProviderConfig> listSettings;
    string                     error;
    SW_EXPECT_TRUE( PushProviderSet::readSettings(
        "{\"providers\":[{\"kind\":\"apns\",\"keyEnvironment\":\"SW_TEST_APNS_KEY\",\"keyId\":\"K\",\"teamId\":\"T\",\"topic\":\"com.example.app\",\"sendUrl\":\"https://x\"}]}", listSettings, error ) );
    SW_ASSERT_EQUAL( size_t( 1 ), listSettings.size() );
    SW_EXPECT_TRUE( listSettings[0]._kind == "apns" && listSettings[0]._keyID == "K" && listSettings[0]._teamID == "T" && listSettings[0]._topic == "com.example.app" );
    SW_EXPECT_TRUE( listSettings[0]._keyEnvironment == "SW_TEST_APNS_KEY" && listSettings[0]._sendBaseURL == "https://x" );
    SW_EXPECT_FALSE( PushProviderSet::readSettings( "{\"providers\":[{\"kind\":\"apns\",\"keyEnvironment\":\"X\",\"keyId\":\"K\",\"teamId\":\"T\"}]}", listSettings, error ) );                            // topic 이 없다
    SW_EXPECT_FALSE( PushProviderSet::readSettings( "{\"providers\":[{\"kind\":\"apns\",\"serviceAccountEnvironment\":\"X\",\"keyId\":\"K\",\"teamId\":\"T\",\"topic\":\"b\"}]}", listSettings, error ) ); // fcm 의 키
    SW_EXPECT_FALSE( PushProviderSet::readSettings( "{\"providers\":[{\"kind\":\"fcm\",\"serviceAccountEnvironment\":\"X\",\"topic\":\"b\"}]}", listSettings, error ) );                                   // apns 의 키

    const SystemMonotonicClock clock;
    PushProviderSet            providerSet;
    vector<PushProviderConfig> listMissing;
    SW_ASSERT_TRUE( PushProviderSet::readSettings(
        "{\"providers\":[{\"kind\":\"apns\",\"keyEnvironment\":\"SW_TEST_APNS_KEY_THAT_IS_NOT_SET\",\"keyId\":\"K\",\"teamId\":\"T\",\"topic\":\"b\"}]}", listMissing, error ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "the missing environment variable is reported" );
        SW_EXPECT_FALSE( providerSet.initialize( listMissing, "", clock, error ) );
    }
    SW_EXPECT_TRUE( error.find( "SW_TEST_APNS_KEY_THAT_IS_NOT_SET" ) != string::npos );
    SW_EXPECT_EQUAL( providerSet.getProviderCount(), 0 );
}
