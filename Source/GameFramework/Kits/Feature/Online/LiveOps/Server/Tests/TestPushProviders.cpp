// FcmPushProviderTest · ApnsPushProviderTest · PushProviderSetTest — 실제 푸시 제공자의 요청 모양과 응답 대응, 설정 읽기. FCM 은 실제 HTTP 클라이언트가 루프백 망의 가짜 서버와 말한다 — 바깥 서비스는 없다.
// APNs 는 HTTP/2 전용이라 가짜 서버가 없어 요청 만들기와 응답 분류를 정적 함수로 본다. 결과에 따른 재시도 · 기기 지우기는 PushDispatcherTest 가 본다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Network/Transport/StreamTypes.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Apns/ApnsPushProvider.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Fcm/FcmPushProvider.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushProviderSet.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPClient.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPServer.h"
#include "GameFramework/Runtime/Online/Security/Tests/FakeNetSecurityProvider.h"

using namespace sw;

namespace
{
    constexpr int64  kNowMs      = 1700000000000ll;
    constexpr uint64 kDeliveryID = 77;

    struct PushTestUtil
    {
        static PushNotificationMessage makeAlert()
        {
            PushNotificationMessage message;
            message._titleKey       = "title.key";
            message._bodyKey        = "body.key";
            message._deepLink       = "mailbox";
            message._arrArgument[0] = "first";
            message._badgeCount     = 3;
            return message;
        }

        static string readText( JSONDocument& document, const utf8* pPath )
        {
            string text;
            (void)document.getRoot().findPathText( pPath, text );
            return text;
        }
    };

    /** @brief 가짜 FCM(토큰 주소와 보내기 주소)과 제공자의 묶음입니다. 선언 순서가 수명이다 — 망이 전송들보다 오래 산다. */
    struct FcmRig
    {
        LoopbackStreamNetwork         _network{ 7u };
        ManualClock                   _clock{ MonotonicMilliseconds{ 0 } };
        HTTPServer                    _server{};
        HTTPClient                    _client{ _clock };
        test::FakeNetSecurityProvider _security{};
        unique_ptr<FcmPushProvider>   _pProvider{};
        vector<string>                _listSendPath{};
        vector<string>                _listAuthorization{};
        vector<string>                _listSendBody{};
        string                        _sendBody{};
        string                        _retryAfter{};
        int32                         _sendStatus{ HTTPConstant::kStatusOk };
        int32                         _tokenStatus{ HTTPConstant::kStatusOk };
        int32                         _tokenRequestCount{ 0 };

        FcmRig()
        {
            StreamTransportConfig transportSettings;
            transportSettings._ioThreadCount = 0;
            SW_EXPECT_TRUE( _server.initialize( _network.createTransport(), transportSettings, HTTPServerConfig{}, HTTPRequestFunction::create<&FcmRig::onRequest>( this ) ) );
            SW_EXPECT_TRUE( _client.initialize( _network.createTransport(), transportSettings, HTTPClientConfig{} ) );
            const string    baseURL = "http://127.0.0.1:" + to_string( static_cast<int32>( _server.getListenPort() ) );
            FcmPushSettings settings;
            settings._credentials._projectID     = "proj";
            settings._credentials._clientEmail   = "a@b";
            settings._credentials._privateKeyPem = "key";
            settings._credentials._tokenURL      = baseURL + "/token";
            settings._sendBaseURL                = baseURL;
            _pProvider                           = make_unique<FcmPushProvider>( std::move( settings ), &_security, &_client );
        }

        void onRequest( const HTTPServerRequest& request, HTTPServerResponse& outResponse )
        {
            string body;
            if ( request._path == "/token" )
            {
                ++_tokenRequestCount;
                body                    = R"({"access_token":"T","expires_in":3600})";
                outResponse._statusCode = _tokenStatus;
            }
            else
            {
                _listSendPath.push_back( request._path );
                const HTTPHeader* pAuthorization = request.findHeader( "Authorization" );
                _listAuthorization.push_back( pAuthorization != nullptr ? pAuthorization->_value : string() );
                _listSendBody.emplace_back( request.getBodyText() );
                body                    = _sendBody;
                outResponse._statusCode = _sendStatus;
                if ( _retryAfter.empty() == false )
                    outResponse._listHeader.push_back( HTTPHeader{ "Retry-After", _retryAfter } );
            }
            outResponse._bodyBytes.assign( body.begin(), body.end() );
        }

        void pump()
        {
            for ( int32 round = 0; round < 40; ++round )
            {
                _pProvider->update( UnixMilliseconds{ kNowMs } );
                _server.update();
            }
        }

        /** @brief 기기 하나에 보내고 왕복을 돌린 뒤 결과 하나를 받습니다. */
        PushDeliveryResult send()
        {
            _pProvider->send( kDeliveryID, "device-token", "ko-KR", PushTestUtil::makeAlert(), UnixMilliseconds{ kNowMs } );
            return take();
        }

        PushDeliveryResult take()
        {
            pump();
            vector<PushDeliveryResult> listResult;
            (void)_pProvider->pollResults( listResult );
            SW_EXPECT_EQUAL( size_t( 1 ), listResult.size() );
            if ( listResult.empty() )
                return PushDeliveryResult{};
            SW_EXPECT_EQUAL( kDeliveryID, listResult[0]._deliveryID );
            return listResult[0];
        }
    };
} // namespace

/** @brief [FcmPushProviderTest] 토큰을 받아 HTTP v1 주소로 Bearer 와 함께 보내고 토큰은 다시 받지 않는다. 몸에 기기 토큰 · 현지화 키 · 앱이 열 화면이 실린다 */
SW_TEST_CASE( FcmPushProviderTest, SendsTheV1RequestWithAnAccessToken )
{
    FcmRig rig;
    SW_EXPECT_TRUE( rig.send()._status == PushDeliveryStatus::Delivered );
    SW_EXPECT_TRUE( rig.send()._status == PushDeliveryStatus::Delivered );
    SW_EXPECT_EQUAL( 1, rig._tokenRequestCount );
    SW_REQUIRE_EQUAL( size_t( 2 ), rig._listSendPath.size() );
    SW_EXPECT_EQUAL( string( "/v1/projects/proj/messages:send" ), rig._listSendPath[0] );
    SW_EXPECT_EQUAL( string( "Bearer T" ), rig._listAuthorization[0] );

    JSONDocument document;
    SW_REQUIRE_TRUE( document.tryParse( rig._listSendBody[0] ) );
    SW_EXPECT_EQUAL( string( "device-token" ), PushTestUtil::readText( document, "message.token" ) );
    SW_EXPECT_EQUAL( string( "title.key" ), PushTestUtil::readText( document, "message.android.notification.title_loc_key" ) );
    SW_EXPECT_EQUAL( string( "body.key" ), PushTestUtil::readText( document, "message.apns.payload.aps.alert.loc-key" ) );
    SW_EXPECT_EQUAL( string( "mailbox" ), PushTestUtil::readText( document, "message.data.deep_link" ) );
    SW_EXPECT_EQUAL( string( "first" ), PushTestUtil::readText( document, "message.data.arg0" ) );
}

/** @brief [FcmPushProviderTest] 응답 표(FCM 오류 코드 문서): 404 · UNREGISTERED 는 기기를 지우고, 429 · 5xx · 전송 실패는 다시 보내며, 그 밖의 4xx 는 다시 보내지 않는다 */
SW_TEST_CASE( FcmPushProviderTest, StatusTable )
{
    SW_EXPECT_TRUE( FcmPushProvider::mapStatus( 200, false, "" ) == PushDeliveryStatus::Delivered );
    SW_EXPECT_TRUE( FcmPushProvider::mapStatus( 404, false, "" ) == PushDeliveryStatus::InvalidToken );
    SW_EXPECT_TRUE( FcmPushProvider::mapStatus( 400, false, R"({"error":{"details":[{"errorCode":"UNREGISTERED"}]}})" ) == PushDeliveryStatus::InvalidToken );
    SW_EXPECT_TRUE( FcmPushProvider::mapStatus( 400, false, R"({"error":{"status":"INVALID_ARGUMENT"}})" ) == PushDeliveryStatus::Rejected );
    SW_EXPECT_TRUE( FcmPushProvider::mapStatus( 429, false, "" ) == PushDeliveryStatus::RateLimited );
    SW_EXPECT_TRUE( FcmPushProvider::mapStatus( 500, false, "" ) == PushDeliveryStatus::Transient );
    SW_EXPECT_TRUE( FcmPushProvider::mapStatus( 503, false, "" ) == PushDeliveryStatus::Transient );
    SW_EXPECT_TRUE( FcmPushProvider::mapStatus( 0, true, "" ) == PushDeliveryStatus::Transient );
    SW_EXPECT_TRUE( FcmPushProvider::mapStatus( 401, false, "" ) == PushDeliveryStatus::Rejected );
    SW_EXPECT_TRUE( FcmPushProvider::mapStatus( 403, false, "" ) == PushDeliveryStatus::Rejected );
}

/** @brief [FcmPushProviderTest] 기다릴 시간: `Retry-After` 를 따르고, 없는 429 는 문서의 최소 1 분이며, 5xx 는 발송기의 백오프에 맡긴다 */
SW_TEST_CASE( FcmPushProviderTest, RetryAfter )
{
    SW_EXPECT_EQUAL( int64( 7000 ), FcmPushProvider::computeRetryAfterMs( 503, "7" ) );
    SW_EXPECT_EQUAL( int64( 7000 ), FcmPushProvider::computeRetryAfterMs( 429, "7" ) );
    SW_EXPECT_EQUAL( int64( 60000 ), FcmPushProvider::computeRetryAfterMs( 429, "" ) );
    SW_EXPECT_EQUAL( int64( 60000 ), FcmPushProvider::computeRetryAfterMs( 429, "not a number" ) );
    SW_EXPECT_EQUAL( int64( 0 ), FcmPushProvider::computeRetryAfterMs( 503, "" ) );

    FcmRig rig;
    rig._sendStatus                 = 429;
    rig._retryAfter                 = "9";
    const PushDeliveryResult result = rig.send();
    SW_EXPECT_TRUE( result._status == PushDeliveryStatus::RateLimited );
    SW_EXPECT_EQUAL( int64( 9000 ), result._retryAfterMs );
}

/** @brief [FcmPushProviderTest] 왕복의 답이 결과가 된다: 몸의 UNREGISTERED 는 InvalidToken, 401 은 Rejected 이고 토큰을 버려 다음 배달이 새로 받는다 */
SW_TEST_CASE( FcmPushProviderTest, ResponsesBecomeResults )
{
    SW_EXPECT_LOG( "FcmPushProvider.FCMRejectedAccessToken" );
    FcmRig rig;
    rig._sendStatus = 400;
    rig._sendBody   = R"({"error":{"details":[{"errorCode":"UNREGISTERED"}]}})";
    SW_EXPECT_TRUE( rig.send()._status == PushDeliveryStatus::InvalidToken );
    rig._sendBody.clear();
    rig._sendStatus = 503;
    SW_EXPECT_TRUE( rig.send()._status == PushDeliveryStatus::Transient );

    rig._sendStatus = HTTPConstant::kStatusUnauthorized;
    SW_EXPECT_TRUE( rig.send()._status == PushDeliveryStatus::Rejected );
    SW_EXPECT_EQUAL( 1, rig._tokenRequestCount );
    rig._sendStatus = HTTPConstant::kStatusOk;
    SW_EXPECT_TRUE( rig.send()._status == PushDeliveryStatus::Delivered );
    SW_EXPECT_EQUAL( 2, rig._tokenRequestCount );
}

/** @brief [FcmPushProviderTest] 자격: 토큰 주소의 5xx 는 기다리던 배달이 Transient, 4xx 는 Rejected 이고 서명 실패도 Rejected 다. 토큰이 없으면 보내지 않는다 */
SW_TEST_CASE( FcmPushProviderTest, TokenFailuresEndTheWaitingDeliveries )
{
    SW_EXPECT_LOG( "ServiceAccountTokenSource.AccessTokenRequestService" );
    SW_EXPECT_LOG( "ServiceAccountTokenSource.CouldNotSignToken" );
    FcmRig rig;
    rig._tokenStatus = 500;
    SW_EXPECT_TRUE( rig.send()._status == PushDeliveryStatus::Transient );
    rig._tokenStatus = 400;
    SW_EXPECT_TRUE( rig.send()._status == PushDeliveryStatus::Rejected );
    rig._security._bSignOk = SW_FALSE;
    SW_EXPECT_TRUE( rig.send()._status == PushDeliveryStatus::Rejected );
    SW_EXPECT_TRUE( rig._listSendPath.empty() );
}

/** @brief [FcmPushProviderTest] 토픽: 규칙에 맞는 이름만 `message.topic` 으로 보내고 틀린 이름은 Rejected 다 */
SW_TEST_CASE( FcmPushProviderTest, Topics )
{
    SW_EXPECT_LOG( "FcmPushProvider.FCMTopicNotValid" );
    SW_EXPECT_TRUE( FcmPushProvider::isValidTopic( "news-KR_1.~%" ) );
    SW_EXPECT_FALSE( FcmPushProvider::isValidTopic( "" ) );
    SW_EXPECT_FALSE( FcmPushProvider::isValidTopic( "a b" ) );

    FcmRig rig;
    SW_EXPECT_TRUE( rig._pProvider->supportsTopics() );
    rig._pProvider->sendToTopic( kDeliveryID, "news", PushTestUtil::makeAlert(), UnixMilliseconds{ kNowMs } );
    SW_EXPECT_TRUE( rig.take()._status == PushDeliveryStatus::Delivered );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._listSendBody.size() );
    JSONDocument document;
    SW_REQUIRE_TRUE( document.tryParse( rig._listSendBody[0] ) );
    SW_EXPECT_EQUAL( string( "news" ), PushTestUtil::readText( document, "message.topic" ) );
    SW_EXPECT_TRUE( PushTestUtil::readText( document, "message.token" ).empty() );

    rig._pProvider->sendToTopic( kDeliveryID, "bad topic", PushTestUtil::makeAlert(), UnixMilliseconds{ kNowMs } );
    SW_EXPECT_TRUE( rig.take()._status == PushDeliveryStatus::Rejected );
}

/** @brief [ApnsPushProviderTest] 응답 표(애플 문서): 410 만 기기를 지우고, 429 · 5xx · 전송 실패는 다시 보내며, 403 · 400 은 다시 보내지 않는다(`BadDeviceToken` 은 환경이 어긋난 토큰일 수 있어 지우지 않는다) */
SW_TEST_CASE( ApnsPushProviderTest, StatusTable )
{
    SW_EXPECT_TRUE( ApnsPushProvider::mapStatus( 200, false ) == PushDeliveryStatus::Delivered );
    SW_EXPECT_TRUE( ApnsPushProvider::mapStatus( 410, false ) == PushDeliveryStatus::InvalidToken );
    SW_EXPECT_TRUE( ApnsPushProvider::mapStatus( 429, false ) == PushDeliveryStatus::RateLimited );
    SW_EXPECT_TRUE( ApnsPushProvider::mapStatus( 500, false ) == PushDeliveryStatus::Transient );
    SW_EXPECT_TRUE( ApnsPushProvider::mapStatus( 503, false ) == PushDeliveryStatus::Transient );
    SW_EXPECT_TRUE( ApnsPushProvider::mapStatus( 0, true ) == PushDeliveryStatus::Transient );
    SW_EXPECT_TRUE( ApnsPushProvider::mapStatus( 400, false ) == PushDeliveryStatus::Rejected );
    SW_EXPECT_TRUE( ApnsPushProvider::mapStatus( 403, false ) == PushDeliveryStatus::Rejected );
    SW_EXPECT_TRUE( ApnsPushProvider::mapStatus( 413, false ) == PushDeliveryStatus::Rejected );
}

/** @brief [ApnsPushProviderTest] 기다릴 시간: `retry-after` 를 따르고, 없는 5xx 는 애플 문서의 15 분이며, 429 는 발송기의 백오프에 맡긴다 */
SW_TEST_CASE( ApnsPushProviderTest, RetryAfter )
{
    SW_EXPECT_EQUAL( int64( 5000 ), ApnsPushProvider::computeRetryAfterMs( 429, "5" ) );
    SW_EXPECT_EQUAL( int64( 900000 ), ApnsPushProvider::computeRetryAfterMs( 503, "" ) );
    SW_EXPECT_EQUAL( int64( 0 ), ApnsPushProvider::computeRetryAfterMs( 429, "" ) );
}

/** @brief [ApnsPushProviderTest] 본문: 문구가 있으면 현지화 키와 배지를, 없으면 조용한 알림(`content-available`)을 싣는다. 인자와 화면은 사용자 키다 */
SW_TEST_CASE( ApnsPushProviderTest, Payload )
{
    const PushNotificationMessage alert = PushTestUtil::makeAlert();
    SW_EXPECT_TRUE( ApnsPushProvider::hasAlert( alert ) );
    JSONDocument document;
    SW_REQUIRE_TRUE( document.tryParse( ApnsPushProvider::makePayloadJSON( alert ) ) );
    SW_EXPECT_EQUAL( string( "title.key" ), PushTestUtil::readText( document, "aps.alert.title-loc-key" ) );
    SW_EXPECT_EQUAL( string( "body.key" ), PushTestUtil::readText( document, "aps.alert.loc-key" ) );
    SW_EXPECT_EQUAL( string( "mailbox" ), PushTestUtil::readText( document, "deep_link" ) );
    SW_EXPECT_EQUAL( string( "first" ), PushTestUtil::readText( document, "arg0" ) );

    PushNotificationMessage silent;
    silent._deepLink = "event/x";
    SW_EXPECT_FALSE( ApnsPushProvider::hasAlert( silent ) );
    JSONDocument silentDocument;
    SW_REQUIRE_TRUE( silentDocument.tryParse( ApnsPushProvider::makePayloadJSON( silent ) ) );
    SW_EXPECT_TRUE( PushTestUtil::readText( silentDocument, "aps.alert.loc-key" ).empty() );
    int32 contentAvailable = 0;
    SW_EXPECT_TRUE( silentDocument.getRoot().get( "aps" ).readOptionalInto( "content-available", contentAvailable ) );
    SW_EXPECT_EQUAL( 1, contentAvailable );
}

/** @brief [ApnsPushProviderTest] 경로에 넣을 수 없는 기기 토큰은 보내지 않고 InvalidToken 이고, 서명하지 못하면 오류 로그와 Rejected 다. 둘 다 HTTP 클라이언트에 닿지 않는다 */
SW_TEST_CASE( ApnsPushProviderTest, LocalFailuresNeverReachTheNetwork )
{
    SW_EXPECT_LOG( "ApnsPushProvider.CouldNotSignAPNs" );
    SW_EXPECT_TRUE( ApnsPushProvider::isValidDeviceToken( "00ffAA" ) );
    SW_EXPECT_FALSE( ApnsPushProvider::isValidDeviceToken( "" ) );
    SW_EXPECT_FALSE( ApnsPushProvider::isValidDeviceToken( "xyz" ) );
    SW_EXPECT_FALSE( ApnsPushProvider::isValidDeviceToken( "00/../ff" ) );
    SW_EXPECT_FALSE( ApnsPushProvider::isValidDeviceToken( string( 201, 'a' ) ) );

    test::FakeNetSecurityProvider security;
    security._bSignOk = SW_FALSE;
    ApnsPushSettings settings;
    settings._privateKeyPem = "pem";
    settings._keyID         = "key";
    settings._teamID        = "team";
    settings._topic         = "com.x.game";
    ApnsPushProvider provider( std::move( settings ), &security, nullptr ); // 닿으면 null 역참조로 드러난다
    SW_EXPECT_FALSE( provider.supportsTopics() );

    vector<PushDeliveryResult> listResult;
    provider.send( 1, "not hex", "ko-KR", PushTestUtil::makeAlert(), UnixMilliseconds{ kNowMs } );
    provider.send( 2, "00ff", "ko-KR", PushTestUtil::makeAlert(), UnixMilliseconds{ kNowMs } );
    SW_EXPECT_EQUAL( 2, provider.pollResults( listResult ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), listResult.size() );
    SW_EXPECT_TRUE( listResult[0]._status == PushDeliveryStatus::InvalidToken );
    SW_EXPECT_TRUE( listResult[1]._status == PushDeliveryStatus::Rejected );
}

/** @brief [PushProviderSetTest] 설정 읽기: 종류마다 필수 키가 있고, 모르는 종류 · 모르는 키 · 다른 종류의 키는 오류다. 자격은 환경 변수 이름만 받는다 */
SW_TEST_CASE( PushProviderSetTest, ReadEntry )
{
    const auto read = []( string_view text, PushProviderConfig& outConfig )
    {
        JSONDocument document;
        SW_EXPECT_TRUE( document.tryParse( text ) );
        string error;
        return PushProviderSet::readEntry( document.getRoot(), outConfig, error );
    };
    {
        PushProviderConfig config;
        SW_EXPECT_TRUE( read( R"({"kind":"fcm","serviceAccountEnvironment":"K","sendUrl":"http://x"})", config ) );
        SW_EXPECT_EQUAL( string( "K" ), config._serviceAccountEnvironment );
        SW_EXPECT_EQUAL( string( "http://x" ), config._sendBaseURL );
    }
    {
        PushProviderConfig config;
        SW_EXPECT_TRUE( read( R"({"kind":"apns","keyEnvironment":"K","keyId":"i","teamId":"t","topic":"b"})", config ) );
        SW_EXPECT_EQUAL( string( "b" ), config._topic );
    }
    for ( const utf8* pText : {
              R"({"kind":"fcm"})",
              R"({"kind":"apns","keyEnvironment":"K","keyId":"i","teamId":"t"})",
              R"({"kind":"apns","keyId":"i","teamId":"t","topic":"b"})",
              R"({"kind":"other"})",
              R"({"serviceAccountEnvironment":"K"})",
              R"({"kind":"fcm","serviceAccountEnvironment":"K","typo":"x"})",
              R"({"kind":"fcm","serviceAccountEnvironment":"K","topic":"b"})",
              R"({"kind":"apns","keyEnvironment":"K","keyId":"i","teamId":"t","topic":"b","tokenUrl":"x"})",
          } )
    {
        PushProviderConfig config;
        SW_EXPECT_FALSE_MSG( read( pText, config ), pText );
    }
}
