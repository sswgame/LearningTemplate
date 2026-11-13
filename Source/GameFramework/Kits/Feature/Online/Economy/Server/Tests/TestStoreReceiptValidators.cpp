// StoreReceiptValidatorTest · GoogleReceiptValidatorTest · AppleReceiptValidatorTest — 스토어 검증의 공통 틀(응답 분류 · 요청 대조 · 조회 구간 · 설정의 구매자 규칙)과 그 위의 구글 · 애플 파싱.
// 실제 HTTP 클라이언트가 루프백 망의 가짜 스토어(`HTTPServer`)와 말한다 — 바깥 서비스는 없다. 서명은 가짜 제공자가 답한다(규칙만 본다). 스팀은 `TestSteamReceiptValidator.cpp`. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Encoding/Base64Util.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Network/Transport/StreamTypes.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Apple/AppleReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Google/GoogleReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptProviderSet.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/StoreReceiptValidator.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPClient.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPServer.h"
#include "GameFramework/Runtime/Online/Security/Tests/FakeNetSecurityProvider.h"

using namespace sw;

namespace
{
    constexpr int64       kNowMs      = 1700000000000ll;
    constexpr int64       kPollMs     = 3600000;
    constexpr int64       kLookbackMs = 7200000;
    constexpr uint64      kBuyer      = 42;
    constexpr uint64      kTicket     = 9;
    constexpr const utf8* kBundle     = "com.x.game";

    /** @brief 망 · 시계 · 가짜 서버 · 실제 HTTP 클라이언트의 묶음입니다. 선언 순서가 수명이다 — 망이 전송들보다 오래 산다. */
    struct LoopbackHTTP
    {
        LoopbackStreamNetwork _network{ 7u };
        ManualClock           _clock{ MonotonicMilliseconds{ 0 } };
        HTTPServer            _server{};
        HTTPClient            _client{ _clock };
    };

    struct StoreTestUtil
    {
        /** @brief 본문 @p payload 의 JWS 를 만듭니다. 사슬과 서명은 가짜 제공자가 답합니다. */
        static string makeJws( string_view payload )
        {
            const string_view header = R"({"alg":"ES256","x5c":["AAAA"]})";
            return Base64Util::encodeURL( reinterpret_cast<const uint8*>( header.data() ), header.size() ) + "." +
                   Base64Util::encodeURL( reinterpret_cast<const uint8*>( payload.data() ), payload.size() ) + "." + Base64Util::encodeURL( reinterpret_cast<const uint8*>( "s" ), 1 );
        }

        static ReceiptValidationRequest makeRequest( string_view storeName, string_view payload )
        {
            ReceiptValidationRequest request;
            request._storeName = string( storeName );
            request._payload   = string( payload );
            request._accountID = kBuyer;
            request._ticket    = kTicket;
            return request;
        }

        static void startServer( LoopbackHTTP& http, HTTPRequestFunction onRequest )
        {
            StreamTransportConfig transportSettings;
            transportSettings._ioThreadCount = 0;
            SW_EXPECT_TRUE( http._server.initialize( http._network.createTransport(), transportSettings, HTTPServerConfig{}, onRequest ) );
            SW_EXPECT_TRUE( http._client.initialize( http._network.createTransport(), transportSettings, HTTPClientConfig{} ) );
        }

        static string makeBaseURL( LoopbackHTTP& http )
        {
            return "http://127.0.0.1:" + to_string( static_cast<int32>( http._server.getListenPort() ) );
        }

        /** @brief 요청 하나를 맡기고 왕복을 돌린 뒤 결과 하나를 받습니다. */
        template <typename TRig>
        static ReceiptValidationResult validate( TRig& rig, string_view storeName, string_view payload )
        {
            rig._pValidator->submitValidation( makeRequest( storeName, payload ), UnixMilliseconds{ rig._nowMs } );
            rig.pump();
            vector<ReceiptValidationResult> listResult;
            rig._pValidator->pollCompletions( listResult );
            SW_EXPECT_EQUAL( size_t( 1 ), listResult.size() );
            return listResult.empty() ? ReceiptValidationResult{} : listResult.front();
        }
    };

    /** @brief 가짜 구글입니다. */
    struct GoogleFixture
    {
        LoopbackHTTP                       _http{};
        test::FakeNetSecurityProvider      _security{};
        unique_ptr<GoogleReceiptValidator> _pValidator{};
        string                             _productBody{};
        vector<string>                     _listVoidedBody{}; ///< 쪽마다의 답. 순서대로 쓰고 다 쓰면 빈 답이다
        vector<string>                     _listVoidedStart{};
        vector<string>                     _listVoidedToken{};
        int32                              _productStatus{ HTTPConstant::kStatusOk };
        int32                              _voidedStatus{ HTTPConstant::kStatusOk };
        int32                              _tokenRequestCount{ 0 };
        int32                              _acknowledgeCount{ 0 };
        int32                              _acknowledgeStatus{ HTTPConstant::kStatusOk };
        int64                              _nowMs{ kNowMs };

        /** @param bRestoreCursor false 면 서비스가 기준 시각을 아직 넘기지 않은 제공자로 선다. */
        explicit GoogleFixture( bool bRestoreCursor = true )
        {
            StoreTestUtil::startServer( _http, HTTPRequestFunction::create<&GoogleFixture::onRequest>( this ) );
            GoogleReceiptSettings settings;
            settings._credentials._clientEmail   = "a@b";
            settings._credentials._privateKeyPem = "key";
            settings._credentials._tokenURL      = StoreTestUtil::makeBaseURL( _http ) + "/token";
            settings._packageName                = kBundle;
            settings._baseURL                    = StoreTestUtil::makeBaseURL( _http );
            settings._voidedPollIntervalMs       = kPollMs;
            settings._voidedLookbackMs           = kLookbackMs;
            _pValidator                          = make_unique<GoogleReceiptValidator>( std::move( settings ), &_security, &_http._client );
            if ( bRestoreCursor )
                _pValidator->restoreRefundCursor( 0 ); // 서비스가 저장소에서 읽어 넘기는 자리다 — 기준이 없는 첫 기동
        }

        void onRequest( const HTTPServerRequest& request, HTTPServerResponse& outResponse )
        {
            string body;
            if ( request._path == "/token" )
            {
                ++_tokenRequestCount;
                body = R"({"access_token":"T","expires_in":3600})";
            }
            else if ( request._path.find( "voidedpurchases" ) != string::npos )
            {
                const HTTPHeader* pStart = request.findQuery( "startTime" );
                const HTTPHeader* pToken = request.findQuery( "token" );
                _listVoidedStart.push_back( pStart != nullptr ? pStart->_value : string() );
                _listVoidedToken.push_back( pToken != nullptr ? pToken->_value : string() );
                if ( _listVoidedBody.empty() == false )
                {
                    body = _listVoidedBody.front();
                    _listVoidedBody.erase( _listVoidedBody.begin() );
                }
                else
                {
                    body = "{}";
                }
                outResponse._statusCode = _voidedStatus;
            }
            else if ( request._path.find( ":acknowledge" ) != string::npos )
            {
                ++_acknowledgeCount;
                outResponse._statusCode = _acknowledgeStatus; // 구글 문서: 성공하면 몸이 비어 있다
            }
            else
            {
                body                    = _productBody;
                outResponse._statusCode = _productStatus;
            }
            outResponse._bodyBytes.assign( body.begin(), body.end() );
        }

        void pump()
        {
            for ( int32 round = 0; round < 40; ++round )
            {
                _pValidator->update( UnixMilliseconds{ _nowMs } );
                _http._server.update();
            }
        }

        ReceiptValidationResult validate( string_view payload = "gem100|TOKEN" ) { return StoreTestUtil::validate( *this, "google", payload ); }

        /** @brief 지급 알림 하나를 맡기고 왕복을 돌린 뒤 결과를 받습니다. */
        ReceiptDeliveryStatus confirmDelivery( string_view deliveryKey = "gem100|TOKEN" )
        {
            ReceiptDeliveryTask task;
            task._storeName   = "google";
            task._recordKey   = "rcpt.google/rec";
            task._deliveryKey = string( deliveryKey );
            _pValidator->confirmDelivery( task, UnixMilliseconds{ _nowMs } );
            pump();
            vector<ReceiptDeliveryResult> listServiceResult;
            _pValidator->pollDeliveryResults( listServiceResult );
            SW_EXPECT_EQUAL( size_t( 1 ), listServiceResult.size() );
            if ( listServiceResult.empty() )
                return ReceiptDeliveryStatus::Retry;
            SW_EXPECT_EQUAL( string( "google" ), listServiceResult[0]._storeName );
            SW_EXPECT_EQUAL( string( "rcpt.google/rec" ), listServiceResult[0]._recordKey );
            return listServiceResult[0]._status;
        }

        vector<ReceiptRefundNotification> takeRefunds()
        {
            vector<ReceiptRefundNotification> listNotice;
            _pValidator->pollRefunds( listNotice );
            return listNotice;
        }
    };

    /** @brief 가짜 애플입니다. */
    struct AppleFixture
    {
        LoopbackHTTP                      _http{};
        test::FakeNetSecurityProvider     _security{};
        unique_ptr<AppleReceiptValidator> _pValidator{};
        string                            _transactionBody{};
        vector<string>                    _listHistoryBody{}; ///< 쪽마다의 답. 순서대로 쓰고 다 쓰면 빈 답이다
        vector<int64>                     _listHistoryStart{};
        vector<string>                    _listHistoryToken{};
        vector<string>                    _listTransactionPath{};
        int32                             _transactionStatus{ HTTPConstant::kStatusOk };
        int32                             _historyStatus{ HTTPConstant::kStatusOk };
        int64                             _nowMs{ kNowMs };

        explicit AppleFixture( bool bSandboxServer = false )
        {
            StoreTestUtil::startServer( _http, HTTPRequestFunction::create<&AppleFixture::onRequest>( this ) );
            AppleReceiptSettings settings;
            settings._issuerID              = "issuer";
            settings._keyID                 = "key";
            settings._privateKeyPem         = "pem";
            settings._bundleID              = kBundle;
            settings._baseURL               = StoreTestUtil::makeBaseURL( _http );
            settings._trustedRootsPem       = "roots";
            settings._historyPollIntervalMs = kPollMs;
            settings._historyLookbackMs     = kLookbackMs;
            settings._bRequireAppleMarkers  = SW_FALSE;
            settings._bSandboxServer        = bSandboxServer ? SW_TRUE : SW_FALSE;
            _pValidator                     = make_unique<AppleReceiptValidator>( std::move( settings ), &_security, &_http._client );
            _pValidator->restoreRefundCursor( 0 ); // 서비스가 저장소에서 읽어 넘기는 자리다 — 기준이 없는 첫 기동
        }

        void onRequest( const HTTPServerRequest& request, HTTPServerResponse& outResponse )
        {
            string body;
            if ( request._path == "/inApps/v1/notifications/history" ) // 애플 문서의 주소 그대로다 — 전에는 단수(`notification`)로 보내 모든 조회가 404 였다
            {
                JSONDocument document;
                int64        startMs = 0;
                SW_EXPECT_TRUE( document.tryParse( request.getBodyText() ) );
                SW_EXPECT_TRUE( document.getRoot().readOptionalInto( "startDate", startMs ) );
                _listHistoryStart.push_back( startMs );
                const HTTPHeader* pToken = request.findQuery( "paginationToken" );
                _listHistoryToken.push_back( pToken != nullptr ? pToken->_value : string() );
                if ( _listHistoryBody.empty() == false )
                {
                    body = _listHistoryBody.front();
                    _listHistoryBody.erase( _listHistoryBody.begin() );
                }
                else
                {
                    body = R"({"hasMore":false,"notificationHistory":[]})";
                }
                outResponse._statusCode = _historyStatus;
            }
            else
            {
                _listTransactionPath.push_back( request._path );
                body                    = _transactionBody;
                outResponse._statusCode = _transactionStatus;
            }
            outResponse._bodyBytes.assign( body.begin(), body.end() );
        }

        void pump()
        {
            for ( int32 round = 0; round < 40; ++round )
            {
                _pValidator->update( UnixMilliseconds{ _nowMs } );
                _http._server.update();
            }
        }

        ReceiptValidationResult validate( string_view payload = "1234" ) { return StoreTestUtil::validate( *this, "apple", payload ); }

        vector<ReceiptRefundNotification> takeRefunds()
        {
            vector<ReceiptRefundNotification> listNotice;
            _pValidator->pollRefunds( listNotice );
            return listNotice;
        }

        /** @brief 서명한 거래 본문을 담은 답을 정합니다. */
        void setTransaction( string_view payload ) { _transactionBody = R"({"signedTransactionInfo":")" + StoreTestUtil::makeJws( payload ) + R"("})"; }
    };

    struct AppleBodyTestUtil
    {
        /** @brief 거래 @p pTransactionID 의 환불 알림 항목(JSON)을 만듭니다. */
        static string makeRefundItem( const utf8* pTransactionID, const utf8* pEnvironment = "Production", const utf8* pNotificationBundle = kBundle, const utf8* pTransactionBundle = kBundle )
        {
            const string transaction  = StoreTestUtil::makeJws( string( R"({"transactionId":")" ) + pTransactionID + R"(","bundleId":")" + pTransactionBundle + R"(","revocationDate":1700000000000})" );
            const string notification = StoreTestUtil::makeJws( string( R"({"notificationType":"REFUND","data":{"bundleId":")" ) + pNotificationBundle + R"(","environment":")" + pEnvironment +
                                                                R"(","signedTransactionInfo":")" + transaction + R"("}})" );
            return R"({"signedPayload":")" + notification + R"("})";
        }

        static string makeHistory( const string& items, const utf8* pNextToken = nullptr )
        {
            const string more = pNextToken != nullptr ? string( R"("hasMore":true,"paginationToken":")" ) + pNextToken + R"(",)" : string( R"("hasMore":false,)" );
            return "{" + more + R"("notificationHistory":[)" + items + "]}";
        }

        static constexpr const utf8* kGoodTransaction = R"({"transactionId":"1234","bundleId":"com.x.game","environment":"Production","productId":"gem100","purchaseDate":1700000000000,"appAccountToken":"tok"})";
    };
} // namespace

/** @brief [StoreReceiptValidatorTest] 응답 분류: 전송 실패 · 429 · 5xx 는 닿지 못함, 401 · 403 은 자격 거절, 그 밖의 4xx 는 거절, 2xx 는 답이다 */
SW_TEST_CASE( StoreReceiptValidatorTest, ClassifyResponse )
{
    HTTPClientResponse response;
    response._bTransportFailed = SW_TRUE;
    SW_EXPECT_TRUE( StoreReceiptValidator::classifyResponse( response ) == ReceiptResponseKind::Unreachable );
    response._bTransportFailed = SW_FALSE;
    for ( int32 status : { 429, 500, 503 } )
    {
        response._statusCode = status;
        SW_EXPECT_TRUE( StoreReceiptValidator::classifyResponse( response ) == ReceiptResponseKind::Unreachable );
    }
    for ( int32 status : { 401, 403 } )
    {
        response._statusCode = status;
        SW_EXPECT_TRUE( StoreReceiptValidator::classifyResponse( response ) == ReceiptResponseKind::CredentialRejected );
    }
    for ( int32 status : { 400, 404, 410 } )
    {
        response._statusCode = status;
        SW_EXPECT_TRUE( StoreReceiptValidator::classifyResponse( response ) == ReceiptResponseKind::Refused );
    }
    response._statusCode = 200;
    SW_EXPECT_TRUE( StoreReceiptValidator::classifyResponse( response ) == ReceiptResponseKind::Answered );
}

/** @brief [StoreReceiptValidatorTest] 요청 대조: 물은 거래 · 키 · 상품과 답이 다르면 Invalid, 거래 id 나 상품이 없으면 Invalid 이고, 답이 상품을 말하지 않으면 물은 상품을 쓴다. 정해지지 않은 답은 대조하지 않는다 */
SW_TEST_CASE( StoreReceiptValidatorTest, ApplyOrderFactsComparesWithTheAsk )
{
    const auto apply = []( const ReceiptAsk& ask, const ReceiptOrderFacts& facts )
    {
        ReceiptValidationResult result;
        result._transactionID = ask._transactionID;
        result._productID     = ask._productID;
        StoreReceiptValidator::applyOrderFacts( ask, facts, result );
        return result;
    };

    ReceiptAsk askOrder;
    askOrder._transactionID = "1";
    ReceiptOrderFacts facts;
    facts._status        = ReceiptStatus::Valid;
    facts._transactionID = "1";
    facts._productID     = "p";
    facts._storeBuyerID  = "b";
    SW_EXPECT_TRUE( apply( askOrder, facts )._status == ReceiptStatus::Valid );
    SW_EXPECT_EQUAL( string( "b" ), apply( askOrder, facts )._storeBuyerID );

    ReceiptOrderFacts otherOrder = facts;
    otherOrder._transactionID    = "2";
    SW_EXPECT_TRUE( apply( askOrder, otherOrder )._status == ReceiptStatus::Invalid );
    SW_EXPECT_EQUAL( string( "1" ), apply( askOrder, otherOrder )._transactionID ); // 물은 거래로 남는다

    ReceiptOrderFacts otherRefund = otherOrder;
    otherRefund._status           = ReceiptStatus::Refunded;
    SW_EXPECT_TRUE( apply( askOrder, otherRefund )._status == ReceiptStatus::Invalid );

    ReceiptOrderFacts noOrder = facts;
    noOrder._transactionID.clear();
    SW_EXPECT_TRUE( apply( askOrder, noOrder )._status == ReceiptStatus::Invalid );

    ReceiptOrderFacts noProduct = facts;
    noProduct._productID.clear();
    SW_EXPECT_TRUE( apply( askOrder, noProduct )._status == ReceiptStatus::Invalid );
    ReceiptOrderFacts refundedNoProduct = noProduct;
    refundedNoProduct._status           = ReceiptStatus::Refunded;
    SW_EXPECT_TRUE( apply( askOrder, refundedNoProduct )._status == ReceiptStatus::Refunded ); // 환불은 지급할 것을 고르지 않는다

    ReceiptAsk askProduct;
    askProduct._productID = "p";
    askProduct._askedKey  = "k";
    ReceiptOrderFacts silent;
    silent._status        = ReceiptStatus::Valid;
    silent._transactionID = "9";
    SW_EXPECT_TRUE( apply( askProduct, silent )._status == ReceiptStatus::Valid );
    SW_EXPECT_EQUAL( string( "p" ), apply( askProduct, silent )._productID );
    SW_EXPECT_EQUAL( string( "9" ), apply( askProduct, silent )._transactionID );
    ReceiptOrderFacts otherProduct = silent;
    otherProduct._productID        = "q";
    SW_EXPECT_TRUE( apply( askProduct, otherProduct )._status == ReceiptStatus::Invalid );
    ReceiptOrderFacts otherKey = silent;
    otherKey._answeredKey      = "x";
    SW_EXPECT_TRUE( apply( askProduct, otherKey )._status == ReceiptStatus::Invalid );

    ReceiptOrderFacts pending;
    pending._status = ReceiptStatus::Retry;
    SW_EXPECT_TRUE( apply( askProduct, pending )._status == ReceiptStatus::Retry );
}

/** @brief [StoreReceiptValidatorTest] 조회 구간: 첫 조회가 기준이 되어 실패해도 시작점이 밀리지 않고, 끝까지 읽어야 옮겨지며, 잘리면 그 시각에서 바로 잇고 끝난 뒤에 처음 시작 시각으로 옮겨진다 */
SW_TEST_CASE( StoreReceiptValidatorTest, PollWindowMovesOnlyAfterAFullRead )
{
    ReceiptPollWindow window( 100, 10, 0 );
    SW_EXPECT_TRUE( window.tryBegin( 1000 ) );
    SW_EXPECT_FALSE( window.tryBegin( 5000 ) ); // 진행 중
    SW_EXPECT_EQUAL( int64( 990 ), window.getFromMs() );
    SW_EXPECT_EQUAL( int64( 1000 ), window.getToMs() );
    window.finishFailed();
    SW_EXPECT_FALSE( window.tryBegin( 1099 ) ); // 주기 전
    SW_EXPECT_TRUE( window.tryBegin( 1100 ) );
    SW_EXPECT_EQUAL( int64( 990 ), window.getFromMs() ); // 첫 조회가 실패해도 그대로다
    window.finishRead();

    SW_EXPECT_TRUE( window.tryBegin( 1200 ) );
    SW_EXPECT_EQUAL( int64( 1090 ), window.getFromMs() );
    window.continueFrom( 1150 );
    SW_EXPECT_TRUE( window.tryBegin( 1201 ) ); // 잘린 조회는 주기를 기다리지 않는다
    SW_EXPECT_EQUAL( int64( 1150 ), window.getFromMs() );
    SW_EXPECT_EQUAL( int64( 1200 ), window.getToMs() );
    window.finishRead();
    SW_EXPECT_TRUE( window.tryBegin( 1301 ) );
    SW_EXPECT_EQUAL( int64( 1190 ), window.getFromMs() );

    // 끝까지 갔지만 읽지 못한 항목이 있으면 이어 읽던 자리가 아니라 구간의 처음부터다.
    window.continueFrom( 1250 );
    SW_EXPECT_TRUE( window.tryBegin( 1302 ) );
    window.finishIncomplete();
    SW_EXPECT_TRUE( window.tryBegin( 1402 ) );
    SW_EXPECT_EQUAL( int64( 1190 ), window.getFromMs() );
}

/** @brief [StoreReceiptValidatorTest] 조회 구간: 시작점은 스토어가 받는 가장 오랜 시각으로 잘린다 — 그보다 앞이면 조회가 영원히 400 이다 */
SW_TEST_CASE( StoreReceiptValidatorTest, PollWindowClampsToTheStoreLimit )
{
    ReceiptPollWindow window( 100, 10, 500 );
    SW_EXPECT_TRUE( window.tryBegin( 1000 ) );
    window.finishFailed();
    SW_EXPECT_TRUE( window.tryBegin( 9000 ) );
    SW_EXPECT_EQUAL( int64( 8500 ), window.getFromMs() );
}

/** @brief [StoreReceiptValidatorTest] 조회 구간의 영속: 넣은 기준에서 잇고, 스토어 한계에 잘린 길이를 알며, 잘린 조회를 잇는 동안에는 다시 세지 않는다 */
SW_TEST_CASE( StoreReceiptValidatorTest, PollWindowRestoresAndKnowsWhatIsLost )
{
    ReceiptPollWindow window( 100, 10, 500 );
    window.restore( 800 );
    SW_EXPECT_TRUE( window.tryBegin( 1000 ) );
    SW_EXPECT_EQUAL( int64( 790 ), window.getFromMs() ); // 재시작 전의 기준에서 잇는다
    SW_EXPECT_EQUAL( int64( 0 ), window.getLostMs() );
    window.finishRead();
    SW_EXPECT_EQUAL( int64( 1000 ), window.getLastReadMs() );

    SW_EXPECT_TRUE( window.tryBegin( 2000 ) );
    SW_EXPECT_EQUAL( int64( 1500 ), window.getFromMs() );
    SW_EXPECT_EQUAL( int64( 510 ), window.getLostMs() ); // 990 ~ 1500 은 스토어에 물을 수 없다
    window.continueFrom( 1700 );
    SW_EXPECT_TRUE( window.tryBegin( 2001 ) );
    SW_EXPECT_EQUAL( int64( 0 ), window.getLostMs() );

    ReceiptPollWindow noLimit( 100, 10, 0 );
    noLimit.restore( 1 );
    SW_EXPECT_TRUE( noLimit.tryBegin( 1000000 ) );
    SW_EXPECT_EQUAL( int64( 0 ), noLimit.getLostMs() );
}

/** @brief [GoogleReceiptValidatorTest] 재시작: 저장소의 기준을 받기 전에는 조회하지 않고, 받으면 그 기준에서 읽는다. 끝까지 읽어야 옮겨진 기준을 한 번 내준다 */
SW_TEST_CASE( GoogleReceiptValidatorTest, RefundQueryWaitsForTheStoredCursor )
{
    GoogleFixture rig( false );
    rig.pump();
    SW_EXPECT_TRUE( rig._listVoidedStart.empty() );

    int64 cursorMs = 0;
    SW_EXPECT_FALSE( rig._pValidator->takeRefundCursor( cursorMs ) );
    const int64 storedMs = kNowMs - kPollMs * 5; // 다섯 주기 동안 꺼져 있었다 — 겹침(두 주기)보다 길다
    rig._pValidator->restoreRefundCursor( storedMs );
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._listVoidedStart.size() );
    SW_EXPECT_EQUAL( to_string( storedMs - kLookbackMs ), rig._listVoidedStart[0] );

    SW_EXPECT_TRUE( rig._pValidator->takeRefundCursor( cursorMs ) );
    SW_EXPECT_EQUAL( kNowMs, cursorMs );
    SW_EXPECT_FALSE( rig._pValidator->takeRefundCursor( cursorMs ) );
}

/** @brief [GoogleReceiptValidatorTest] 멈춘 동안 스토어 한계(구글 30 일)를 넘겼으면 잘린 구간을 경고하고, 한계 안에서 조회는 이어진다 */
SW_TEST_CASE( GoogleReceiptValidatorTest, GapBeyondTheStoreLimitWarns )
{
    SW_EXPECT_LOG( "StoreReceiptValidator.RefundGapBeyondStoreLimit" );
    constexpr int64 kDayMs = 24ll * 60 * 60 * 1000;
    GoogleFixture   rig( false );
    rig._pValidator->restoreRefundCursor( kNowMs - 40 * kDayMs );
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._listVoidedStart.size() );
    SW_EXPECT_EQUAL( to_string( kNowMs - 29 * kDayMs ), rig._listVoidedStart[0] );
}

/** @brief [StoreReceiptValidatorTest] 설정의 구매자 규칙: 필수이고, 그 스토어가 줄 수 없는 묶기 · 모르는 값은 오류이며, `none` 은 적어야 받는다 */
SW_TEST_CASE( StoreReceiptValidatorTest, ProviderEntryRequiresABuyerBinding )
{
    const auto read = []( string_view text, ReceiptProviderConfig& outConfig )
    {
        JSONDocument document;
        SW_EXPECT_TRUE( document.tryParse( text ) );
        return ReceiptProviderSet::readEntry( document.getRoot(), outConfig ).has_value();
    };
    {
        ReceiptProviderConfig config;
        SW_EXPECT_TRUE( read( R"({"kind":"steam","appId":"480","keySecretEnvironment":"K","buyerBinding":"linkedLogin","buyerLoginProvider":"steam"})", config ) );
        SW_EXPECT_TRUE( config._buyerRule._binding == ReceiptBuyerBinding::LinkedLogin );
        SW_EXPECT_EQUAL( string( "steam" ), config._buyerRule._loginProvider );
    }
    {
        ReceiptProviderConfig config;
        SW_EXPECT_TRUE( read( R"({"kind":"google","packageName":"p","serviceAccountEnvironment":"K","buyerBinding":"accountToken"})", config ) );
        SW_EXPECT_TRUE( config._buyerRule._binding == ReceiptBuyerBinding::AccountToken );
    }
    {
        ReceiptProviderConfig config;
        SW_EXPECT_TRUE( read( R"({"kind":"google","packageName":"p","serviceAccountEnvironment":"K","buyerBinding":"none"})", config ) );
        SW_EXPECT_TRUE( config._buyerRule._binding == ReceiptBuyerBinding::None );
    }
    for ( const utf8* pText : {
              R"({"kind":"steam","appId":"480","keySecretEnvironment":"K"})",
              R"({"kind":"steam","appId":"480","keySecretEnvironment":"K","buyerBinding":"linkedLogin"})",
              R"({"kind":"steam","appId":"480","keySecretEnvironment":"K","buyerBinding":"accountToken"})",
              R"({"kind":"steam","appId":"480","keySecretEnvironment":"K","buyerBinding":"none","buyerLoginProvider":"steam"})",
              R"({"kind":"google","packageName":"p","serviceAccountEnvironment":"K"})",
              R"({"kind":"google","packageName":"p","serviceAccountEnvironment":"K","buyerBinding":"linkedLogin","buyerLoginProvider":"google"})",
              R"({"kind":"google","packageName":"p","serviceAccountEnvironment":"K","buyerBinding":"weird"})",
          } )
    {
        ReceiptProviderConfig config;
        SW_EXPECT_FALSE_MSG( read( pText, config ), pText );
    }
}

/** @brief [GoogleReceiptValidatorTest] 정상 구매: 거래 id 는 주문 번호, 상품은 물은 것, 구매자는 답의 난독화 계정 id 다. 구매 확인은 검증에서 보내지 않고 알림 키와 기한(구매 + 3 일)만 싣는다 — 전에는 지급 전에 보내 지급이 지면 환불도 재시도도 없었다 */
SW_TEST_CASE( GoogleReceiptValidatorTest, PurchasedIsValidAndWaitsForTheGrant )
{
    GoogleFixture rig;
    rig._productBody                     = R"({"orderId":"GPA.1","purchaseState":0,"acknowledgementState":0,"purchaseTimeMillis":"1700000000000","obfuscatedExternalAccountId":"tok"})";
    const ReceiptValidationResult result = rig.validate();
    SW_EXPECT_TRUE( result._status == ReceiptStatus::Valid );
    SW_EXPECT_EQUAL( string( "google" ), result._storeName );
    SW_EXPECT_EQUAL( string( "GPA.1" ), result._transactionID );
    SW_EXPECT_EQUAL( string( "gem100" ), result._productID );
    SW_EXPECT_EQUAL( string( "tok" ), result._storeBuyerID );
    SW_EXPECT_EQUAL( kNowMs, result._purchaseTimeMs );
    SW_EXPECT_EQUAL( kTicket, result._ticket );
    SW_EXPECT_EQUAL( kBuyer, result._accountID );
    SW_EXPECT_EQUAL( string( "gem100|TOKEN" ), result._deliveryKey );
    SW_EXPECT_EQUAL( kNowMs + GoogleReceiptSettings::kAcknowledgeWindowMs, result._deliveryDeadlineMs );
    rig.pump();
    SW_EXPECT_EQUAL( 0, rig._acknowledgeCount );

    SW_EXPECT_TRUE( rig.confirmDelivery() == ReceiptDeliveryStatus::Confirmed );
    SW_EXPECT_EQUAL( 1, rig._acknowledgeCount );
}

/** @brief [GoogleReceiptValidatorTest] 이미 확인된 구매는 알릴 것이 없다. 취소 · 보류 답도 알림 키를 싣지 않는다 */
SW_TEST_CASE( GoogleReceiptValidatorTest, OnlyUnacknowledgedPurchasesNeedConfirmation )
{
    GoogleFixture rig;
    rig._productBody = R"({"orderId":"GPA.1","purchaseState":0,"acknowledgementState":1})";
    SW_EXPECT_TRUE( rig.validate()._deliveryKey.empty() );
    rig._productBody = R"({"orderId":"GPA.1","purchaseState":1,"acknowledgementState":0})";
    SW_EXPECT_TRUE( rig.validate()._deliveryKey.empty() );
    rig._productBody = R"({"orderId":"GPA.1","purchaseState":2,"acknowledgementState":0})";
    SW_EXPECT_TRUE( rig.validate()._deliveryKey.empty() );
    rig._productBody                     = R"({"orderId":"GPA.1","purchaseState":0,"acknowledgementState":0})";
    const ReceiptValidationResult result = rig.validate();
    SW_EXPECT_FALSE( result._deliveryKey.empty() );
    SW_EXPECT_EQUAL( kNowMs + GoogleReceiptSettings::kAcknowledgeWindowMs, result._deliveryDeadlineMs ); // 구매 시각이 없으면 지금부터 잰다
}

/** @brief [GoogleReceiptValidatorTest] 구매 확인의 답: 5xx · 429 · 자격 거절은 Retry(토큰을 버린다), 그 밖의 4xx 는 Refused 다. 읽을 수 없는 키는 묻지 않고 Refused 다 */
SW_TEST_CASE( GoogleReceiptValidatorTest, ConfirmDeliveryStatusDecidesRetry )
{
    SW_EXPECT_LOG( "GoogleReceiptValidator.GoogleRejectedAccessToken" );
    SW_EXPECT_LOG( "StoreReceiptValidator.DeliveryRefused" );
    SW_EXPECT_LOG( "StoreReceiptValidator.DeliveryKeyUnreadable" );
    GoogleFixture rig;
    for ( int32 status : { 500, 503, 429 } )
    {
        rig._acknowledgeStatus = status;
        SW_EXPECT_TRUE( rig.confirmDelivery() == ReceiptDeliveryStatus::Retry );
    }
    const int32 tokensBefore = rig._tokenRequestCount;
    rig._acknowledgeStatus   = HTTPConstant::kStatusForbidden;
    SW_EXPECT_TRUE( rig.confirmDelivery() == ReceiptDeliveryStatus::Retry );
    rig._acknowledgeStatus = HTTPConstant::kStatusOk;
    SW_EXPECT_TRUE( rig.confirmDelivery() == ReceiptDeliveryStatus::Confirmed );
    SW_EXPECT_EQUAL( tokensBefore + 1, rig._tokenRequestCount );
    for ( int32 status : { 400, 404, 410 } )
    {
        rig._acknowledgeStatus = status;
        SW_EXPECT_TRUE( rig.confirmDelivery() == ReceiptDeliveryStatus::Refused );
    }
    const int32 asked = rig._acknowledgeCount;
    SW_EXPECT_TRUE( rig.confirmDelivery( "no-bar" ) == ReceiptDeliveryStatus::Refused );
    SW_EXPECT_EQUAL( asked, rig._acknowledgeCount );
}

/** @brief [GoogleReceiptValidatorTest] 2xx 인데 JSON 이 아니면 Retry 다 — 전에는 Invalid 로 답해 결제한 사람의 영수증을 버렸다. 구매 확인도 보내지 않는다 */
SW_TEST_CASE( GoogleReceiptValidatorTest, BodyThatIsNotJSONIsRetry )
{
    GoogleFixture rig;
    for ( const utf8* pBody : { "", "<html>bad gateway</html>", R"({"orderId":"GPA)" } )
    {
        rig._productBody                     = pBody;
        const ReceiptValidationResult result = rig.validate();
        SW_EXPECT_TRUE_MSG( result._status == ReceiptStatus::Retry, pBody );
        SW_EXPECT_EQUAL( string( "gem100" ), result._productID );
    }
    SW_EXPECT_EQUAL( 0, rig._acknowledgeCount );
}

/** @brief [GoogleReceiptValidatorTest] 상태 표와 대조: 취소 Refunded, 보류 Retry, 다른 상품 · 다른 토큰의 답 · 주문 번호 없음 Invalid. 라이선스 테스트는 샌드박스다 */
SW_TEST_CASE( GoogleReceiptValidatorTest, StateAndEchoDecideTheResult )
{
    GoogleFixture rig;
    rig._productBody = R"({"orderId":"GPA.1","purchaseState":1})";
    SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Refunded );
    rig._productBody = R"({"purchaseState":2})";
    SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Retry );
    rig._productBody = R"({"orderId":"GPA.1","purchaseState":0,"purchaseType":0})";
    SW_EXPECT_TRUE( rig.validate()._bSandbox == SW_TRUE );
    for ( const utf8* pBody : {
              R"({"orderId":"GPA.1","purchaseState":0,"productId":"other"})",
              R"({"orderId":"GPA.1","purchaseState":0,"purchaseToken":"OTHER"})",
              R"({"purchaseState":0})",
              R"({"orderId":"GPA.1","purchaseState":9})",
              R"({"orderId":"GPA.1"})",
              R"({})",
          } )
    {
        rig._productBody                     = pBody;
        const ReceiptValidationResult result = rig.validate();
        SW_EXPECT_TRUE_MSG( result._status == ReceiptStatus::Invalid, pBody );
        SW_EXPECT_FALSE( result._failureText.empty() );
    }
    SW_EXPECT_EQUAL( 0, rig._acknowledgeCount ); // 확인 상태를 말하지 않은 답은 확인하지 않는다
}

/** @brief [GoogleReceiptValidatorTest] 상태 코드: 5xx · 429 Retry, 400 · 404 · 410 Invalid, 401 · 403 은 토큰을 버리고 Retry 이며 다음 요청이 새 토큰을 받는다 */
SW_TEST_CASE( GoogleReceiptValidatorTest, StatusSplitsRetryFromInvalidAndDropsTheToken )
{
    SW_EXPECT_LOG( "GoogleReceiptValidator.GoogleRejectedAccessToken" );
    GoogleFixture rig;
    rig._productBody = R"({"orderId":"GPA.1","purchaseState":0})";
    for ( int32 status : { 500, 429 } )
    {
        rig._productStatus = status;
        SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Retry );
    }
    for ( int32 status : { 400, 404, 410 } )
    {
        rig._productStatus = status;
        SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Invalid );
    }
    const int32 tokensBefore = rig._tokenRequestCount;
    rig._productStatus       = HTTPConstant::kStatusForbidden;
    SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Retry );
    rig._productStatus = HTTPConstant::kStatusOk;
    SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Valid );
    SW_EXPECT_EQUAL( tokensBefore + 1, rig._tokenRequestCount );
}

/** @brief [GoogleReceiptValidatorTest] 영수증 모양이 틀리면 구글에 묻지 않고 Invalid 다 */
SW_TEST_CASE( GoogleReceiptValidatorTest, MalformedReceiptNeverReachesGoogle )
{
    GoogleFixture rig;
    rig.pump();
    for ( const utf8* pPayload : { "", "gem100", "gem100|", "|TOKEN", "gem/100|TOKEN", "gem100|TO KEN" } )
    {
        const ReceiptValidationResult result = rig.validate( pPayload );
        SW_EXPECT_TRUE_MSG( result._status == ReceiptStatus::Invalid, pPayload );
        SW_EXPECT_EQUAL( kBuyer, result._accountID );
    }
    SW_EXPECT_EQUAL( 0, rig._acknowledgeCount );
}

/** @brief [GoogleReceiptValidatorTest] 무효 구매 목록: 쪽 토큰을 따라 끝까지 읽어 한 번씩 알리고, 끝난 뒤에야 구간이 옮겨진다. 주문 번호 없는 줄은 버린다 */
SW_TEST_CASE( GoogleReceiptValidatorTest, VoidedFollowsPagesThenMovesTheWindow )
{
    GoogleFixture rig;
    rig._listVoidedBody.push_back( R"({"tokenPagination":{"nextPageToken":"P2"},"voidedPurchases":[{"orderId":"A","voidedTimeMillis":"1700000000000"},{"voidedTimeMillis":"1"}]})" );
    rig._listVoidedBody.push_back( R"({"voidedPurchases":[{"orderId":"B"},{"orderId":"A"}]})" );
    rig.pump();
    const vector<ReceiptRefundNotification> listNotice = rig.takeRefunds();
    SW_REQUIRE_EQUAL( size_t( 2 ), listNotice.size() );
    SW_EXPECT_EQUAL( string( "A" ), listNotice[0]._transactionID );
    SW_EXPECT_EQUAL( kNowMs, listNotice[0]._refundedMs );
    SW_EXPECT_EQUAL( string( "B" ), listNotice[1]._transactionID );
    SW_EXPECT_EQUAL( kNowMs, listNotice[1]._refundedMs ); // 시각이 없으면 지금이다
    SW_REQUIRE_EQUAL( size_t( 2 ), rig._listVoidedToken.size() );
    SW_EXPECT_TRUE( rig._listVoidedToken[0].empty() );
    SW_EXPECT_EQUAL( string( "P2" ), rig._listVoidedToken[1] );

    const int64 firstStartMs = kNowMs;
    rig._nowMs += kPollMs;
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 3 ), rig._listVoidedStart.size() );
    SW_EXPECT_EQUAL( to_string( firstStartMs - kLookbackMs ), rig._listVoidedStart[2] );
}

/** @brief [GoogleReceiptValidatorTest] 무효 구매 목록을 읽지 못하면(5xx · JSON 아님) 구간을 옮기지 않는다 */
SW_TEST_CASE( GoogleReceiptValidatorTest, FailedVoidedKeepsTheWindow )
{
    SW_EXPECT_LOG( "StoreReceiptValidator.RefundQueryFailed" );
    GoogleFixture rig;
    rig.pump();
    rig._voidedStatus = 500;
    rig._nowMs += kPollMs;
    rig.pump();
    rig._voidedStatus = HTTPConstant::kStatusOk;
    rig._listVoidedBody.push_back( "not json" );
    rig._nowMs += kPollMs;
    rig.pump();
    rig._nowMs += kPollMs;
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 4 ), rig._listVoidedStart.size() );
    for ( const string& start : rig._listVoidedStart )
    {
        SW_EXPECT_EQUAL( to_string( kNowMs - kLookbackMs ), start );
    }
}

/** @brief [AppleReceiptValidatorTest] 정상 거래: 서명한 답의 값이 결과가 되고 구매자는 `appAccountToken` 이다. 운영 환경은 샌드박스가 아니다 */
SW_TEST_CASE( AppleReceiptValidatorTest, SignedTransactionIsValid )
{
    AppleFixture rig;
    rig.setTransaction( AppleBodyTestUtil::kGoodTransaction );
    const ReceiptValidationResult result = rig.validate();
    SW_EXPECT_TRUE( result._status == ReceiptStatus::Valid );
    SW_EXPECT_EQUAL( string( "apple" ), result._storeName );
    SW_EXPECT_EQUAL( string( "1234" ), result._transactionID );
    SW_EXPECT_EQUAL( string( "gem100" ), result._productID );
    SW_EXPECT_EQUAL( string( "tok" ), result._storeBuyerID );
    SW_EXPECT_EQUAL( kNowMs, result._purchaseTimeMs );
    SW_EXPECT_TRUE( result._bSandbox == SW_FALSE );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._listTransactionPath.size() );
    SW_EXPECT_EQUAL( string( "/inApps/v1/transactions/1234" ), rig._listTransactionPath[0] );
}

/** @brief [AppleReceiptValidatorTest] 읽지 못한 답은 영수증을 버리지 않는다: JSON 아님 · 서명 글 없음 · 사슬 확인 실패는 Retry 다(전에는 모두 Invalid 였다) */
SW_TEST_CASE( AppleReceiptValidatorTest, UnreadableAnswersAreRetry )
{
    SW_EXPECT_LOG( "AppleReceiptValidator.SignedTransactionAppleDoes" );
    AppleFixture rig;
    for ( const utf8* pBody : { "", "<html></html>", R"({"signedTransactionInfo")", "{}", R"({"signedTransactionInfo":7})", R"({"signedTransactionInfo":"a.b"})" } )
    {
        rig._transactionBody                 = pBody;
        const ReceiptValidationResult result = rig.validate();
        SW_EXPECT_TRUE_MSG( result._status == ReceiptStatus::Retry, pBody );
        SW_EXPECT_EQUAL( string( "1234" ), result._transactionID );
    }
    rig._security._bChainOk = SW_FALSE;
    rig.setTransaction( AppleBodyTestUtil::kGoodTransaction );
    SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Retry );
}

/** @brief [AppleReceiptValidatorTest] 서명한 답의 대조: 다른 거래 · 다른 번들 · 상품 없음 · 환경 없음은 Invalid, 취소된 거래는 Refunded 다 */
SW_TEST_CASE( AppleReceiptValidatorTest, SignedPropertiesAreCompared )
{
    AppleFixture rig;
    for ( const utf8* pPayload : {
              R"({"transactionId":"9999","bundleId":"com.x.game","environment":"Production","productId":"gem100"})",
              R"({"bundleId":"com.x.game","environment":"Production","productId":"gem100"})",
              R"({"transactionId":"1234","bundleId":"com.other","environment":"Production","productId":"gem100"})",
              R"({"transactionId":"1234","environment":"Production","productId":"gem100"})",
              R"({"transactionId":"1234","bundleId":"com.x.game","environment":"Production"})",
              R"({"transactionId":"1234","bundleId":"com.x.game","productId":"gem100"})",
          } )
    {
        rig.setTransaction( pPayload );
        const ReceiptValidationResult result = rig.validate();
        SW_EXPECT_TRUE_MSG( result._status == ReceiptStatus::Invalid, pPayload );
        SW_EXPECT_FALSE( result._failureText.empty() );
    }
    rig.setTransaction( R"({"transactionId":"1234","bundleId":"com.x.game","environment":"Production","revocationDate":1700000000000})" );
    SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Refunded );
}

/** @brief [AppleReceiptValidatorTest] 환경: 정확히 Production 인 것만 운영 결제다 — Sandbox 와 모르는 값(Xcode)은 샌드박스로 표시되어 서비스가 운영 서버에서 거절한다 */
SW_TEST_CASE( AppleReceiptValidatorTest, OnlyProductionIsNotSandbox )
{
    AppleFixture rig;
    for ( const utf8* pEnvironment : { "Sandbox", "Xcode", "production" } )
    {
        rig.setTransaction( string( R"({"transactionId":"1234","bundleId":"com.x.game","productId":"gem100","environment":")" ) + pEnvironment + R"("})" );
        const ReceiptValidationResult result = rig.validate();
        SW_EXPECT_TRUE( result._status == ReceiptStatus::Valid );
        SW_EXPECT_TRUE_MSG( result._bSandbox == SW_TRUE, pEnvironment );
    }
}

/** @brief [AppleReceiptValidatorTest] 상태 코드: 401 · 403 은 토큰 문제라 Retry 이고 오류 로그, 5xx · 429 Retry, 404 Invalid. 숫자가 아닌 영수증은 묻지 않는다 */
SW_TEST_CASE( AppleReceiptValidatorTest, StatusSplitsRetryFromInvalid )
{
    SW_EXPECT_LOG( "AppleReceiptValidator.AppleRejectedAppStore" );
    AppleFixture rig;
    rig.setTransaction( AppleBodyTestUtil::kGoodTransaction );
    for ( int32 status : { 401, 403, 500, 429 } )
    {
        rig._transactionStatus = status;
        SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Retry );
    }
    rig._transactionStatus = 404;
    SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Invalid );
    const size_t asked = rig._listTransactionPath.size();
    SW_EXPECT_TRUE( rig.validate( "1/../2" )._status == ReceiptStatus::Invalid );
    SW_EXPECT_EQUAL( asked, rig._listTransactionPath.size() );
}

/** @brief [AppleReceiptValidatorTest] 환불 기록: 우리 번들 · 환경의 환불만 알린다. 다른 번들의 알림 · 다른 번들의 거래 · 샌드박스 알림 · 숫자가 아닌 거래는 버린다 */
SW_TEST_CASE( AppleReceiptValidatorTest, RefundHistoryFilters )
{
    AppleFixture rig;
    using Body = AppleBodyTestUtil;
    rig._listHistoryBody.push_back( Body::makeHistory( Body::makeRefundItem( "1" ) + "," + Body::makeRefundItem( "2", "Sandbox" ) + "," + Body::makeRefundItem( "3", "Production", "com.other" ) + "," +
                                                       Body::makeRefundItem( "4", "Production", kBundle, "com.other" ) + "," + Body::makeRefundItem( "x5" ) + "," + Body::makeRefundItem( "1" ) ) );
    rig.pump();
    const vector<ReceiptRefundNotification> listNotice = rig.takeRefunds();
    SW_REQUIRE_EQUAL( size_t( 1 ), listNotice.size() );
    SW_EXPECT_EQUAL( string( "apple" ), listNotice[0]._storeName );
    SW_EXPECT_EQUAL( string( "1" ), listNotice[0]._transactionID );
    SW_EXPECT_EQUAL( kNowMs, listNotice[0]._refundedMs );
}

/** @brief [AppleReceiptValidatorTest] 확인하지 못한 환불 알림이 있으면 구간을 옮기지 않아 다음 조회가 다시 읽는다 — 전에는 건너뛰고 구간을 옮겨 그 환불을 영영 놓쳤다 */
SW_TEST_CASE( AppleReceiptValidatorTest, UnverifiedNotificationKeepsTheWindow )
{
    SW_EXPECT_LOG( "AppleReceiptValidator.AppleNotificationDoesNot" );
    AppleFixture rig;
    using Body              = AppleBodyTestUtil;
    rig._security._bChainOk = SW_FALSE;
    rig._listHistoryBody.push_back( Body::makeHistory( Body::makeRefundItem( "1" ) ) );
    rig.pump();
    SW_EXPECT_TRUE( rig.takeRefunds().empty() );

    rig._security._bChainOk = SW_TRUE;
    rig._listHistoryBody.push_back( Body::makeHistory( Body::makeRefundItem( "1" ) ) );
    rig._nowMs += kPollMs;
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 1 ), rig.takeRefunds().size() );
    SW_REQUIRE_EQUAL( size_t( 2 ), rig._listHistoryStart.size() );
    SW_EXPECT_EQUAL( kNowMs - kLookbackMs, rig._listHistoryStart[1] ); // 그대로다

    const int64 readAtMs = rig._nowMs;
    rig._nowMs += kPollMs;
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 3 ), rig._listHistoryStart.size() );
    SW_EXPECT_EQUAL( readAtMs - kLookbackMs, rig._listHistoryStart[2] ); // 다 읽은 뒤에 옮겨진다
}

/** @brief [AppleReceiptValidatorTest] 환불 기록의 쪽은 끝까지 따라간다 — 전에는 열 쪽에서 멈추고도 구간을 옮겨 뒤쪽 환불을 놓쳤다 */
SW_TEST_CASE( AppleReceiptValidatorTest, RefundHistoryFollowsEveryPage )
{
    AppleFixture rig;
    using Body                 = AppleBodyTestUtil;
    constexpr int32 kPageCount = 12;
    for ( int32 page = 0; page < kPageCount; ++page )
    {
        const string nextToken = to_string( page + 1 );
        rig._listHistoryBody.push_back( Body::makeHistory( Body::makeRefundItem( to_string( page + 100 ).c_str() ), page + 1 < kPageCount ? nextToken.c_str() : nullptr ) );
    }
    for ( int32 round = 0; round < kPageCount; ++round )
    {
        rig.pump();
    }
    SW_EXPECT_EQUAL( static_cast<size_t>( kPageCount ), rig.takeRefunds().size() );
    SW_REQUIRE_EQUAL( static_cast<size_t>( kPageCount ), rig._listHistoryToken.size() );
    SW_EXPECT_EQUAL( string( "11" ), rig._listHistoryToken[11] );

    rig._nowMs += kPollMs;
    rig.pump();
    SW_REQUIRE_EQUAL( static_cast<size_t>( kPageCount + 1 ), rig._listHistoryStart.size() );
    SW_EXPECT_EQUAL( kNowMs - kLookbackMs, rig._listHistoryStart[kPageCount] ); // 끝까지 읽은 조회가 시작한 시각이 기준이다
}

/** @brief [AppleReceiptValidatorTest] 쪽이 더 있다는데 토큰이 없으면 구간을 옮기지 않는다 */
SW_TEST_CASE( AppleReceiptValidatorTest, MoreWithoutTokenKeepsTheWindow )
{
    AppleFixture rig;
    rig._listHistoryBody.push_back( R"({"hasMore":true,"notificationHistory":[]})" );
    rig.pump();
    rig._nowMs += kPollMs;
    rig.pump();
    rig._nowMs += kPollMs;
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 3 ), rig._listHistoryStart.size() );
    SW_EXPECT_EQUAL( kNowMs - kLookbackMs, rig._listHistoryStart[1] );
    SW_EXPECT_EQUAL( kNowMs + kPollMs - kLookbackMs, rig._listHistoryStart[2] );
}

/** @brief [AppleReceiptValidatorTest] 알림 이력의 시작점 한계는 환경을 따른다(애플 문서: 운영 180 일, 샌드박스 30 일, 코드는 하루 여유) — 샌드박스에 180 일을 쓰면 긴 중단 뒤 조회가 영원히 거절된다 */
SW_TEST_CASE( AppleReceiptValidatorTest, HistoryLimitFollowsTheEnvironment )
{
    SW_EXPECT_LOG( "StoreReceiptValidator.RefundQueryFailed" );
    SW_EXPECT_LOG( "StoreReceiptValidator.RefundGapBeyondStoreLimit" );
    constexpr int64      kDayMs = 24ll * 60 * 60 * 1000;
    AppleReceiptSettings settings;
    SW_EXPECT_EQUAL( 179 * kDayMs, settings.getHistoryMaxAgeMs() );
    settings._bSandboxServer = SW_TRUE;
    SW_EXPECT_EQUAL( 29 * kDayMs, settings.getHistoryMaxAgeMs() );

    AppleFixture rig( true );
    rig._historyStatus = 500;
    rig.pump();
    rig._nowMs += 40 * kDayMs; // 샌드박스 한계를 넘은 중단
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 2 ), rig._listHistoryStart.size() );
    SW_EXPECT_EQUAL( rig._nowMs - 29 * kDayMs, rig._listHistoryStart[1] );
}

/** @brief [AppleReceiptValidatorTest] 종료: 날아가던 검증은 한 번만 Retry 로 끝나고 새 조회를 보내지 않는다 */
SW_TEST_CASE( AppleReceiptValidatorTest, ShutdownFinishesInFlightAsRetry )
{
    AppleFixture rig;
    rig.setTransaction( AppleBodyTestUtil::kGoodTransaction );
    rig.pump();
    rig._pValidator->submitValidation( StoreTestUtil::makeRequest( "apple", "1234" ), UnixMilliseconds{ rig._nowMs } );
    rig._pValidator->shutdown();
    rig.pump();
    vector<ReceiptValidationResult> listResult;
    rig._pValidator->pollCompletions( listResult );
    SW_REQUIRE_EQUAL( size_t( 1 ), listResult.size() );
    SW_EXPECT_TRUE( listResult[0]._status == ReceiptStatus::Retry );
    SW_EXPECT_EQUAL( string( "1234" ), listResult[0]._transactionID );
    const size_t polled = rig._listHistoryStart.size();
    rig._nowMs += kPollMs * 2;
    rig.pump();
    SW_EXPECT_EQUAL( polled, rig._listHistoryStart.size() );
}
