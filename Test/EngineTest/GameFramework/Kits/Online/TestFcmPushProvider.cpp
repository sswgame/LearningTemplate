// FCM 푸시 제공자(OnlineKits G4) — 가짜 HTTPS 서버(OAuth 토큰 + messages:send)로: 서비스 계정 JWT 가 키로 서명됐는지 · 토큰은 한 번만 받아 캐시 · 기기와 토픽 발송 ·
// 상태 대응(404 무효, 429 늦추기, 5xx 다시, 400 거절, 401 이면 토큰을 버려 새로 받음) · 토큰 서버가 죽거나 키가 틀린 경우 · 문구 JSON · 설정 파일과 환경 변수.
#include "pch.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Time/Clock.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "EngineTest/GameFramework/Online/FakeHTTPSFixture.h"

#include "GameFramework/Base/Online/HTTP/ServiceAccountTokenSource.h"
#include "GameFramework/Base/Online/Security/JSONWebToken.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Fcm/FcmPushProvider.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushProviderSet.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 가짜 구글 — `/token` 은 서비스 계정 JWT 의 서명 · iss · scope · aud 를 확인하고 토큰을 주며, `/v1/projects/proj/messages:send` 는 본문의 기기 토큰으로 정한 응답을 준다. */
    class FakeFcmHandler final : public IHTTPRequestHandler
    {
    public:
        NetPublicKey _publicKey{};
        sw::string   _tokenURL{};
        sw::string   _grantedToken{ "tok-1" };
        sw::string   _lastSendBody{};
        sw::string   _lastAuthorization{};
        int32        _tokenHits{ 0 };
        int32        _sendHits{ 0 };
        int32        _tokenStatus{ 200 };
        int32        _authFailuresLeft{ 0 }; ///< 보내기에 401 을 줄 횟수. 401 을 주면 다음에 줄 토큰이 tok-2 로 바뀐다
        uint8        _bAssertionValid{ SW_FALSE };

        void onHTTPRequest( const HTTPServerRequest& request, HTTPServerResponse& outResponse ) override
        {
            if ( request._path == "/token" && request._method == HTTPMethod::Post )
            {
                ++_tokenHits;
                checkAssertion( request.getBodyText() );
                if ( _tokenStatus != 200 )
                {
                    test::setResponseText( outResponse, _tokenStatus, "{\"error\":\"invalid_grant\"}" );
                    return;
                }
                test::setResponseText( outResponse, 200, "{\"access_token\":\"" + _grantedToken + "\",\"expires_in\":3600,\"token_type\":\"Bearer\"}" );
                return;
            }
            if ( request._path == "/v1/projects/proj/messages:send" && request._method == HTTPMethod::Post )
            {
                ++_sendHits;
                _lastSendBody                    = sw::string( request.getBodyText() );
                const HTTPHeader* pAuthorization = request.findHeader( "authorization" );
                _lastAuthorization               = pAuthorization == nullptr ? sw::string() : pAuthorization->_value;
                if ( _authFailuresLeft > 0 )
                {
                    --_authFailuresLeft;
                    _grantedToken = "tok-2";
                    test::setResponseText( outResponse, 401, "{\"error\":{\"status\":\"UNAUTHENTICATED\"}}" );
                    return;
                }
                if ( _lastAuthorization != sw::string( "Bearer " ).append( _grantedToken ) )
                {
                    test::setResponseText( outResponse, 401, "{}" );
                    return;
                }
                respondByDevice( outResponse );
                return;
            }
            outResponse._statusCode = 404;
        }

    private:
        void checkAssertion( string_view body )
        {
            vector<HTTPHeader> listForm;
            sw::string         assertion;
            sw::string         grantType;
            if ( HTTPUtil::decodeForm( body, listForm ) )
            {
                for ( const HTTPHeader& pair : listForm )
                {
                    if ( pair._name == "assertion" )
                        assertion = pair._value;
                    if ( pair._name == "grant_type" )
                        grantType = pair._value;
                }
            }
            JSONWebToken token;
            sw::string   issuer;
            sw::string   scope;
            sw::string   audience;
            const bool   bParsed = token.parse( assertion );
            _bAssertionValid     = bParsed && grantType == "urn:ietf:params:oauth:grant-type:jwt-bearer" && token.getAlgorithm() == NetSignatureAlgorithm::RsaPkcs1Sha256 &&
                               token.verifySignature( NetSecurity::getProvider(), _publicKey ) && token.findText( "iss", issuer ) && issuer == "svc@proj.iam" &&
                               token.findText( "scope", scope ) && scope == "https://www.googleapis.com/auth/firebase.messaging" && token.findText( "aud", audience ) &&
                               audience == _tokenURL;
        }

        void respondByDevice( HTTPServerResponse& outResponse ) const
        {
            const auto has = [this]( const utf8* pNeedle )
            { return _lastSendBody.find( pNeedle ) != sw::string::npos; };
            if ( has( "\"token\":\"dead\"" ) )
                test::setResponseText( outResponse, 404, "{\"error\":{\"code\":404,\"status\":\"NOT_FOUND\",\"details\":[{\"errorCode\":\"UNREGISTERED\"}]}}" );
            else if ( has( "\"token\":\"slow\"" ) )
            {
                test::setResponseText( outResponse, 429, "{\"error\":{\"status\":\"RESOURCE_EXHAUSTED\"}}" );
                outResponse._listHeader.push_back( HTTPHeader{ "Retry-After", "7" } );
            }
            else if ( has( "\"token\":\"boom\"" ) )
                test::setResponseText( outResponse, 503, "{}" );
            else if ( has( "\"token\":\"bad\"" ) )
                test::setResponseText( outResponse, 400, "{\"error\":{\"status\":\"INVALID_ARGUMENT\"}}" );
            else
                test::setResponseText( outResponse, 200, "{\"name\":\"projects/proj/messages/1\"}" );
        }
    };

    PushNotificationMessage makeMessage()
    {
        PushNotificationMessage message;
        message._titleKey       = "push.mail.title";
        message._bodyKey        = "push.mail.body";
        message._deepLink       = "mailbox";
        message._arrArgument[0] = "Hero";
        message._badgeCount     = 3;
        return message;
    }

    /** @brief 가짜 구글 + 클라이언트 + 제공자 한 벌입니다. 서비스 계정 키는 이 테스트가 만든다. */
    struct FcmFixture
    {
        FakeFcmHandler         _handler;
        test::FakeHTTPSFixture _https;
        FcmPushProvider        _provider;

        static FcmPushSettings makeSettings( FakeFcmHandler& handler, const test::FakeHTTPSFixture& https )
        {
            sw::string   privateKeyPem;
            NetPublicKey publicKey;
            (void)NetSecurity::getProvider().createSigningKeyPair( NetSignatureAlgorithm::RsaPkcs1Sha256, privateKeyPem, publicKey );
            handler._publicKey              = publicKey;
            handler._tokenURL               = https.makeURL( "/token" );
            const sw::string serviceAccount = "{\"project_id\":\"proj\",\"client_email\":\"svc@proj.iam\",\"private_key_id\":\"k1\",\"private_key\":\"" +
                                              JSONDocument::escapeString( privateKeyPem ) + "\",\"token_uri\":\"" + handler._tokenURL + "\"}";
            FcmPushSettings settings;
            sw::string      error;
            const bool      bRead = ServiceAccountCredentials::read( serviceAccount, settings._credentials, error );
            SW_EXPECT_TRUE( bRead );
            settings._sendBaseURL = "https://localhost:" + to_string( static_cast<int32>( https._server.getListenPort() ) );
            return settings;
        }

        FcmFixture()
            : _handler{}
            , _https{ &_handler }
            , _provider{ makeSettings( _handler, _https ), &NetSecurity::getProvider(), &_https._client }
        {
        }

        /** @brief 결과 @p count 개가 모일 때까지 서버와 제공자를 번갈아 돌립니다. */
        vector<PushDeliveryResult> collect( size_t count )
        {
            vector<PushDeliveryResult> listResult;
            const Deadline             deadline = Deadline::afterMilliseconds( 10000 );
            while ( listResult.size() < count && deadline.isExpired() == false )
            {
                _https.pump();
                _provider.update( UnixMilliseconds{ _https._nowMs } );
                (void)_provider.pollResults( listResult );
            }
            return listResult;
        }
    };
} // namespace

SW_TEST_CASE( FcmPushProviderTest, TheTokenIsRequestedOnceSignedWithTheKeyAndCached )
{
    FcmFixture fixture;
    fixture._provider.send( 1, "device-a", "ko-KR", makeMessage(), UnixMilliseconds{ fixture._https._nowMs } );
    fixture._provider.send( 2, "device-b", "ko-KR", makeMessage(), UnixMilliseconds{ fixture._https._nowMs } ); // 토큰을 기다리는 중에 온 두 번째도 같은 토큰 요청을 쓴다
    const vector<PushDeliveryResult> listFirst = fixture.collect( 2 );
    SW_ASSERT_EQUAL( listFirst.size(), size_t( 2 ) );
    SW_EXPECT_TRUE( fixture._handler._bAssertionValid == SW_TRUE );
    SW_EXPECT_EQUAL( fixture._handler._tokenHits, 1 );
    SW_EXPECT_EQUAL( fixture._handler._sendHits, 2 );
    for ( const PushDeliveryResult& result : listFirst )
    {
        SW_EXPECT_TRUE( result._status == PushDeliveryStatus::Delivered );
    }
    SW_EXPECT_EQUAL( fixture._handler._lastAuthorization, sw::string( "Bearer tok-1" ) );

    fixture._provider.send( 3, "device-a", "", makeMessage(), UnixMilliseconds{ fixture._https._nowMs } );
    SW_ASSERT_EQUAL( fixture.collect( 1 ).size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( fixture._handler._tokenHits, 1 ); // 캐시된 토큰이다

    // 토큰은 만료 1 분 전에 다시 받는다.
    fixture._https._nowMs += 3600000 - 30000;
    fixture._provider.send( 4, "device-a", "", makeMessage(), UnixMilliseconds{ fixture._https._nowMs } );
    SW_ASSERT_EQUAL( fixture.collect( 1 ).size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( fixture._handler._tokenHits, 2 );
}

SW_TEST_CASE( FcmPushProviderTest, ATopicMessageGoesOutOnceToTheTopic )
{
    FcmFixture fixture;
    SW_EXPECT_TRUE( fixture._provider.supportsTopics() );
    fixture._provider.sendToTopic( 9, "liveops.kr", makeMessage(), UnixMilliseconds{ fixture._https._nowMs } );
    const vector<PushDeliveryResult> listResult = fixture.collect( 1 );
    SW_ASSERT_EQUAL( listResult.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listResult[0]._status == PushDeliveryStatus::Delivered );
    SW_EXPECT_EQUAL( listResult[0]._deliveryID, uint64( 9 ) );
    SW_EXPECT_TRUE( fixture._handler._lastSendBody.find( "\"topic\":\"liveops.kr\"" ) != sw::string::npos );
    SW_EXPECT_TRUE( fixture._handler._lastSendBody.find( "\"token\"" ) == sw::string::npos );

    // 토픽 이름 규칙 밖은 서버에 묻지 않고 거절한다.
    {
        SW_TEST_DEFENSIVE_SCOPE( "the invalid topic is logged" );
        fixture._provider.sendToTopic( 10, "bad topic!", makeMessage(), UnixMilliseconds{ fixture._https._nowMs } );
    }
    const vector<PushDeliveryResult> listBad = fixture.collect( 1 );
    SW_ASSERT_EQUAL( listBad.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listBad[0]._status == PushDeliveryStatus::Rejected );
    SW_EXPECT_EQUAL( fixture._handler._sendHits, 1 );
}

SW_TEST_CASE( FcmPushProviderTest, StatusCodesBecomeDeliveryStatuses )
{
    FcmFixture                    fixture;
    const PushNotificationMessage message = makeMessage();
    fixture._provider.send( 1, "dead", "", message, UnixMilliseconds{ fixture._https._nowMs } );
    fixture._provider.send( 2, "slow", "", message, UnixMilliseconds{ fixture._https._nowMs } );
    fixture._provider.send( 3, "boom", "", message, UnixMilliseconds{ fixture._https._nowMs } );
    fixture._provider.send( 4, "bad", "", message, UnixMilliseconds{ fixture._https._nowMs } );
    const vector<PushDeliveryResult> listResult = fixture.collect( 4 );
    SW_ASSERT_EQUAL( listResult.size(), size_t( 4 ) );
    for ( const PushDeliveryResult& result : listResult )
    {
        switch ( result._deliveryID )
        {
            case 1:
            {
                SW_EXPECT_TRUE( result._status == PushDeliveryStatus::InvalidToken );
                break;
            }
            case 2:
            {
                SW_EXPECT_TRUE( result._status == PushDeliveryStatus::RateLimited );
                SW_EXPECT_EQUAL( result._retryAfterMs, int64( 7000 ) );
                break;
            }
            case 3:
            {
                SW_EXPECT_TRUE( result._status == PushDeliveryStatus::Transient );
                break;
            }
            case 4:
            {
                SW_EXPECT_TRUE( result._status == PushDeliveryStatus::Rejected );
                break;
            }
            default:
            {
                SW_EXPECT_TRUE( false );
                break;
            }
        }
    }
    SW_EXPECT_TRUE( FcmPushProvider::mapStatus( 0, true, "" ) == PushDeliveryStatus::Transient ); // 전송 실패
    SW_EXPECT_TRUE( FcmPushProvider::mapStatus( 400, false, "{\"errorCode\":\"UNREGISTERED\"}" ) == PushDeliveryStatus::InvalidToken );
    SW_EXPECT_TRUE( FcmPushProvider::mapStatus( 403, false, "{}" ) == PushDeliveryStatus::Rejected );
}

SW_TEST_CASE( FcmPushProviderTest, ARejectedAccessTokenIsDroppedAndTheNextMessageGetsANewOne )
{
    FcmFixture fixture;
    fixture._handler._authFailuresLeft = 1;
    {
        SW_TEST_DEFENSIVE_SCOPE( "the provider warns that the access token was rejected" );
        fixture._provider.send( 1, "device-a", "", makeMessage(), UnixMilliseconds{ fixture._https._nowMs } );
        const vector<PushDeliveryResult> listFirst = fixture.collect( 1 );
        SW_ASSERT_EQUAL( listFirst.size(), size_t( 1 ) );
        SW_EXPECT_TRUE( listFirst[0]._status == PushDeliveryStatus::Rejected ); // 401 · 403
    }
    SW_EXPECT_EQUAL( fixture._handler._tokenHits, 1 );
    fixture._provider.send( 2, "device-a", "", makeMessage(), UnixMilliseconds{ fixture._https._nowMs } );
    const vector<PushDeliveryResult> listSecond = fixture.collect( 1 );
    SW_ASSERT_EQUAL( listSecond.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listSecond[0]._status == PushDeliveryStatus::Delivered );
    SW_EXPECT_EQUAL( fixture._handler._tokenHits, 2 ); // 버린 토큰 대신 새로 받았다
    SW_EXPECT_EQUAL( fixture._handler._lastAuthorization, sw::string( "Bearer tok-2" ) );
}

SW_TEST_CASE( FcmPushProviderTest, ATokenServerThatFailsMakesTheDeliveryRetryOrStop )
{
    FcmFixture fixture;
    fixture._handler._tokenStatus = 503;
    {
        SW_TEST_DEFENSIVE_SCOPE( "the provider warns that the access token request failed" );
        fixture._provider.send( 1, "device-a", "", makeMessage(), UnixMilliseconds{ fixture._https._nowMs } );
        const vector<PushDeliveryResult> listDown = fixture.collect( 1 );
        SW_ASSERT_EQUAL( listDown.size(), size_t( 1 ) );
        SW_EXPECT_TRUE( listDown[0]._status == PushDeliveryStatus::Transient ); // 곧 나아질 수 있다
        fixture._handler._tokenStatus = 400;                                    // invalid_grant — 키가 틀렸다
        fixture._provider.send( 2, "device-a", "", makeMessage(), UnixMilliseconds{ fixture._https._nowMs } );
        const vector<PushDeliveryResult> listKey = fixture.collect( 1 );
        SW_ASSERT_EQUAL( listKey.size(), size_t( 1 ) );
        SW_EXPECT_TRUE( listKey[0]._status == PushDeliveryStatus::Rejected );
    }
    SW_EXPECT_EQUAL( fixture._handler._sendHits, 0 );
}

SW_TEST_CASE( FcmPushProviderTest, TheMessageCarriesLocalizationKeysAndEscapesText )
{
    PushNotificationMessage message = makeMessage();
    message._arrArgument[1]         = "say \"hi\"\n";
    const sw::string json           = FcmPushProvider::makeMessageJSON( "tok", {}, message );
    JSONDocument     document;
    SW_ASSERT_TRUE( document.tryParse( json ) ); // 따옴표 · 줄바꿈이 이스케이프됐다
    const JSONValue body = document.getRoot().get( "message", false );
    SW_EXPECT_EQUAL( body.get( "token", false ).asString(), sw::string( "tok" ) );
    SW_EXPECT_EQUAL( body.get( "data", false ).get( "deep_link", false ).asString(), sw::string( "mailbox" ) );
    SW_EXPECT_EQUAL( body.get( "android", false ).get( "notification", false ).get( "title_loc_key", false ).asString(), sw::string( "push.mail.title" ) );
    SW_EXPECT_EQUAL( body.get( "android", false ).get( "notification", false ).get( "body_loc_args", false ).at( 1 ).asString(), sw::string( "say \"hi\"\n" ) );
    SW_EXPECT_EQUAL( body.get( "apns", false ).get( "payload", false ).get( "aps", false ).get( "alert", false ).get( "loc-key", false ).asString(), sw::string( "push.mail.body" ) );
    SW_EXPECT_EQUAL( body.get( "apns", false ).get( "payload", false ).get( "aps", false ).get( "badge", false ).asString(), sw::string( "3" ) );

    PushNotificationMessage silent;
    silent._deepLink            = "mailbox"; // 문구가 없으면 데이터 메시지만 보낸다
    const sw::string silentJSON = FcmPushProvider::makeMessageJSON( {}, "liveops.all", silent );
    SW_EXPECT_TRUE( silentJSON.find( "android" ) == sw::string::npos );
    SW_EXPECT_TRUE( silentJSON.find( "apns" ) == sw::string::npos );
    SW_EXPECT_TRUE( FcmPushProvider::isValidTopic( "liveops.us-west" ) );
    SW_EXPECT_FALSE( FcmPushProvider::isValidTopic( "liveops kr" ) );
    SW_EXPECT_FALSE( FcmPushProvider::isValidTopic( "" ) );
}

SW_TEST_CASE( PushProviderSetTest, SettingsAreReadStrictlyAndTheSecretComesFromTheEnvironment )
{
    vector<PushProviderConfig> listSettings;
    sw::string                 error;
    SW_EXPECT_TRUE( PushProviderSet::readSettings( "{\"providers\":[{\"kind\":\"fcm\",\"serviceAccountEnvironment\":\"SW_TEST_FCM_ACCOUNT\"}]}", listSettings, error ) );
    SW_ASSERT_EQUAL( listSettings.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( listSettings[0]._kind, sw::string( "fcm" ) );
    SW_EXPECT_FALSE( PushProviderSet::readSettings( "{\"providers\":[{\"kind\":\"fcm\",\"serviceAccountEnvironment\":\"X\",\"servicAccount\":\"typo\"}]}", listSettings, error ) ); // 철자 오류
    SW_EXPECT_FALSE( PushProviderSet::readSettings( "{\"providers\":[{\"kind\":\"apns\",\"serviceAccountEnvironment\":\"X\"}]}", listSettings, error ) );                           // 모르는 종류
    SW_EXPECT_FALSE( PushProviderSet::readSettings( "{\"providers\":[{\"kind\":\"fcm\"}]}", listSettings, error ) );                                                                // 자격 변수 이름이 없다
    SW_EXPECT_FALSE( PushProviderSet::readSettings( "[]", listSettings, error ) );

    // 환경 변수가 없으면 제공자를 만들지 못하고 기동이 멈춘다 — 가짜로 대신 뜨지 않는다.
    const SystemMonotonicClock clock;
    PushProviderSet            providerSet;
    vector<PushProviderConfig> listMissing;
    SW_ASSERT_TRUE( PushProviderSet::readSettings( "{\"providers\":[{\"kind\":\"fcm\",\"serviceAccountEnvironment\":\"SW_TEST_FCM_ACCOUNT_THAT_IS_NOT_SET\"}]}", listMissing, error ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "the missing environment variable is logged" );
        SW_EXPECT_FALSE( providerSet.initialize( listMissing, "", clock, error ) );
    }
    SW_EXPECT_TRUE( error.find( "SW_TEST_FCM_ACCOUNT_THAT_IS_NOT_SET" ) != sw::string::npos );
    SW_EXPECT_EQUAL( providerSet.getProviderCount(), 0 );
}
