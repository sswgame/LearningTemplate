// SteamReceiptValidatorTest — 스팀 영수증 검증(바깥 응답 해석 — 신뢰 경계): 주문 번호 · 상태 표 · 시각 읽기, 쿼리에 싣지 못하는 영수증, 응답의 위조 · 잘림 · 범위 밖 값, 일시 실패와 영구 실패의 구분,
// 환불 보고의 거름 · 중복 · 조회 구간, 종료. 실제 HTTP 클라이언트가 루프백 망의 가짜 스팀(`HTTPServer`)과 말한다 — 바깥 서비스는 없다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Network/Transport/StreamTypes.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Steam/SteamReceiptValidator.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPClient.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPServer.h"

using namespace sw;

namespace
{
    /** @brief 2023-11-14T22:13:20 UTC 입니다. */
    constexpr int64       kNowMs         = 1700000000000ll;
    constexpr int64       kPollMs        = 3600000;
    constexpr int64       kLookbackMs    = 7200000;
    constexpr uint64      kBuyer         = 42;
    constexpr uint64      kTicket        = 9;
    constexpr const utf8* kOrder         = "1234";
    constexpr const utf8* kEmptyReport   = R"({"response":{"result":"OK","params":{"orders":[]}}})";
    constexpr const utf8* kFailureReport = R"({"response":{"result":"Failure","error":{"errorcode":1,"errordesc":"busy"}}})";

    /** @brief 가짜 스팀 + 실제 HTTP 클라이언트 + 검증기입니다. 선언 순서가 수명이다 — 망이 전송들보다, 클라이언트가 검증기보다 오래 산다. */
    struct SteamFixture
    {
        LoopbackStreamNetwork             _network{ 7u };
        ManualClock                       _clock{ MonotonicMilliseconds{ 0 } };
        HTTPServer                        _server{};
        HTTPClient                        _client{ _clock };
        unique_ptr<SteamReceiptValidator> _pValidator{};
        string                            _queryBody{};
        string                            _reportBody{ kEmptyReport };
        vector<string>                    _listQueryOrder{};
        vector<string>                    _listQueryPath{};
        vector<string>                    _listReportTime{};
        int32                             _queryStatus{ HTTPConstant::kStatusOk };
        int32                             _reportStatus{ HTTPConstant::kStatusOk };
        int64                             _nowMs{ kNowMs };

        /** @brief @p pBaseURL 이 있으면 그 주소로 묻습니다(없으면 가짜 스팀). */
        explicit SteamFixture( bool bSandbox = false, const utf8* pBaseURL = nullptr, ReceiptBuyerBinding binding = ReceiptBuyerBinding::None )
        {
            StreamTransportConfig transportSettings;
            transportSettings._ioThreadCount = 0;
            SW_EXPECT_TRUE( _server.initialize( _network.createTransport(), transportSettings, HTTPServerConfig{}, HTTPRequestFunction::create<&SteamFixture::onRequest>( this ) ) );
            SW_EXPECT_TRUE( _client.initialize( _network.createTransport(), transportSettings, HTTPClientConfig{} ) );
            SteamReceiptSettings settings;
            settings._appID                = "480";
            settings._key                  = "KEY";
            settings._baseURL              = pBaseURL != nullptr ? string( pBaseURL ) : "http://127.0.0.1:" + to_string( static_cast<int32>( _server.getListenPort() ) );
            settings._reportPollIntervalMs = kPollMs;
            settings._reportLookbackMs     = kLookbackMs;
            settings._bSandbox             = bSandbox ? SW_TRUE : SW_FALSE;
            settings._buyerRule._binding   = binding;
            if ( binding == ReceiptBuyerBinding::LinkedLogin )
                settings._buyerRule._loginProvider = "steam";
            _pValidator = make_unique<SteamReceiptValidator>( settings, &_client );
            _pValidator->restoreRefundCursor( 0 ); // 서비스가 저장소에서 읽어 넘기는 자리다 — 기준이 없는 첫 기동
        }

        void onRequest( const HTTPServerRequest& request, HTTPServerResponse& outResponse )
        {
            const bool    bReport   = request._path.find( "GetReport" ) != string::npos;
            const string& body      = bReport ? _reportBody : _queryBody;
            outResponse._statusCode = bReport ? _reportStatus : _queryStatus;
            outResponse._bodyBytes.assign( body.begin(), body.end() );
            if ( bReport )
            {
                const HTTPHeader* pTime = request.findQuery( "time" );
                _listReportTime.push_back( pTime != nullptr ? pTime->_value : string() );
                return;
            }
            const HTTPHeader* pOrder = request.findQuery( "orderid" );
            _listQueryOrder.push_back( pOrder != nullptr ? pOrder->_value : string() );
            _listQueryPath.push_back( request._path );
        }

        /** @brief 요청 → 응답 한 왕복에 넉넉한 수만큼 돌립니다. 처음 부를 때는 보고서 조회도 나간다. */
        void pump()
        {
            for ( int32 round = 0; round < 8; ++round )
            {
                _pValidator->update( UnixMilliseconds{ _nowMs } );
                _server.update();
            }
        }

        void submit( string_view payload )
        {
            ReceiptValidationRequest request;
            request._storeName = "steam";
            request._payload   = string( payload );
            request._accountID = kBuyer;
            request._ticket    = kTicket;
            _pValidator->submitValidation( request, UnixMilliseconds{ _nowMs } );
        }

        /** @brief 영수증 하나를 맡기고 결과 하나를 받습니다. */
        ReceiptValidationResult validate( string_view payload = kOrder )
        {
            submit( payload );
            pump();
            vector<ReceiptValidationResult> listResult;
            _pValidator->pollCompletions( listResult );
            SW_EXPECT_EQUAL( size_t( 1 ), listResult.size() );
            return listResult.empty() ? ReceiptValidationResult{} : listResult.front();
        }

        vector<ReceiptRefundNotification> takeRefunds()
        {
            vector<ReceiptRefundNotification> listNotice;
            _pValidator->pollRefunds( listNotice );
            return listNotice;
        }
    };

    /** @brief 응답 본문을 만듭니다. */
    struct SteamBodyTestUtil
    {
        /** @brief 주문 @p pOrderID · 상태 @p pStatus 의 정상 QueryTxn 응답입니다. */
        static string makeOrder( const utf8* pOrderID, const utf8* pStatus )
        {
            return string( R"({"response":{"result":"OK","params":{"orderid":")" ) + pOrderID + R"(","status":")" + pStatus +
                   R"(","time":"2023-11-14T22:13:20Z","items":[{"itemid":"100","qty":1},{"itemid":"200","qty":1}]}}})";
        }

        /** @brief 상한만큼 찬 환불 보고입니다. @p pExtraField 를 줄마다 덧붙입니다. */
        static string makeFullReport( const utf8* pExtraField )
        {
            string body = R"({"response":{"result":"OK","params":{"orders":[)";
            for ( int32 index = 0; index < SteamReceiptValidator::kReportMaxResults; ++index )
            {
                body += index == 0 ? "" : ",";
                body += R"({"orderid":")" + to_string( index + 1 ) + R"(","status":"Refunded")" + pExtraField + "}";
            }
            body += "]}}}";
            return body;
        }
    };
} // namespace

/** @brief [SteamReceiptValidatorTest] 주문 번호는 1~20 자리 십진 숫자뿐이다 — 쿼리 인자를 끼워 넣는 글자 · 부호 · 공백은 거절한다 */
SW_TEST_CASE( SteamReceiptValidatorTest, OrderIDIsOneToTwentyDecimalDigits )
{
    SW_EXPECT_TRUE( SteamReceiptValidator::isOrderID( "0" ) );
    SW_EXPECT_TRUE( SteamReceiptValidator::isOrderID( "12345678901234567890" ) );
    SW_EXPECT_FALSE( SteamReceiptValidator::isOrderID( "" ) );
    SW_EXPECT_FALSE( SteamReceiptValidator::isOrderID( "123456789012345678901" ) );
    SW_EXPECT_FALSE( SteamReceiptValidator::isOrderID( "12a" ) );
    SW_EXPECT_FALSE( SteamReceiptValidator::isOrderID( "-1" ) );
    SW_EXPECT_FALSE( SteamReceiptValidator::isOrderID( "+1" ) );
    SW_EXPECT_FALSE( SteamReceiptValidator::isOrderID( " 1" ) );
    SW_EXPECT_FALSE( SteamReceiptValidator::isOrderID( "1&orderid=2" ) );
    SW_EXPECT_FALSE( SteamReceiptValidator::isOrderID( string_view( "1\0", 2 ) ) );
}

/** @brief [SteamReceiptValidatorTest] 상태 표: 성공만 Valid, 환불류는 Refunded, 마무리 전은 Retry, 그 밖과 대소문자가 다른 값 · 빈 값은 Invalid 다 */
SW_TEST_CASE( SteamReceiptValidatorTest, StatusTable )
{
    SW_EXPECT_TRUE( SteamReceiptValidator::mapOrderStatus( "Succeeded" ) == ReceiptStatus::Valid );
    for ( const utf8* pStatus : { "Refunded", "PartialRefund", "Chargedback", "RefundedSuspected", "PartialRefundSuspected", "RefundedFraudulent" } )
    {
        SW_EXPECT_TRUE( SteamReceiptValidator::isRefundStatus( pStatus ) );
        SW_EXPECT_TRUE( SteamReceiptValidator::mapOrderStatus( pStatus ) == ReceiptStatus::Refunded );
    }
    SW_EXPECT_TRUE( SteamReceiptValidator::mapOrderStatus( "Init" ) == ReceiptStatus::Retry );
    SW_EXPECT_TRUE( SteamReceiptValidator::mapOrderStatus( "Approved" ) == ReceiptStatus::Retry );
    for ( const utf8* pStatus : { "Failed", "succeeded", "", "Succeeded " } )
    {
        SW_EXPECT_FALSE( SteamReceiptValidator::isRefundStatus( pStatus ) );
        SW_EXPECT_TRUE( SteamReceiptValidator::mapOrderStatus( pStatus ) == ReceiptStatus::Invalid );
    }
}

/** @brief [SteamReceiptValidatorTest] 시각 읽기: `Z` 는 있어도 없어도 되고 쓰기와 왕복한다. 윤일 · 윤초 자리는 받는다 */
SW_TEST_CASE( SteamReceiptValidatorTest, ParseTimeAcceptsTheFormat )
{
    int64 unixMs = 0;
    SW_EXPECT_TRUE( SteamReceiptValidator::parseTime( "2023-11-14T22:13:20Z", unixMs ) );
    SW_EXPECT_EQUAL( kNowMs, unixMs );
    SW_EXPECT_TRUE( SteamReceiptValidator::parseTime( "2023-11-14T22:13:20", unixMs ) );
    SW_EXPECT_EQUAL( kNowMs, unixMs );
    SW_EXPECT_TRUE( SteamReceiptValidator::parseTime( "1970-01-01T00:00:00", unixMs ) );
    SW_EXPECT_EQUAL( int64( 0 ), unixMs );
    SW_EXPECT_TRUE( SteamReceiptValidator::parseTime( "2024-02-29T00:00:00", unixMs ) );
    SW_EXPECT_TRUE( SteamReceiptValidator::parseTime( "2023-12-31T23:59:60", unixMs ) );

    SW_EXPECT_TRUE( SteamReceiptValidator::parseTime( SteamReceiptValidator::formatTime( kNowMs ), unixMs ) );
    SW_EXPECT_EQUAL( kNowMs, unixMs );
}

/** @brief [SteamReceiptValidatorTest] 시각 읽기 거절: 모양 · 범위 밖 · 그 달에 없는 날 · 부호와 공백이 든 칸(`-1` 시가 범위 검사를 지나가던 결함). 틀리면 출력을 건드리지 않는다 */
SW_TEST_CASE( SteamReceiptValidatorTest, ParseTimeRejectsBadValues )
{
    constexpr int64 kUntouched = 77;
    for ( const utf8* pText : {
              "",
              "Z",
              "2023-11-14",
              "2023-11-14T22:13:20ZZ",
              "2023-11-14 22:13:20",
              "2023/11/14T22:13:20",
              "2023-11-14T22:13:2",
              "2023-00-14T22:13:20",
              "2023-13-14T22:13:20",
              "2023-11-00T22:13:20",
              "2023-11-32T22:13:20",
              "2023-02-31T00:00:00",
              "2023-02-29T00:00:00",
              "2023-04-31T00:00:00",
              "2023-11-14T24:13:20",
              "2023-11-14T22:60:20",
              "2023-11-14T22:13:61",
              "2023-11-14T-1:13:20",
              "2023-11-14T22:-1:20",
              "2023-11-14T22:13:-1",
              "2023-11-14T+1:13:20",
              "2023-11- 4T22:13:20",
              "-023-11-14T22:13:20",
              "2023-1a-14T22:13:20",
              "99999-11-14T22:13:20",
          } )
    {
        int64 unixMs = kUntouched;
        SW_EXPECT_FALSE_MSG( SteamReceiptValidator::parseTime( pText, unixMs ), pText );
        SW_EXPECT_EQUAL( kUntouched, unixMs );
    }
}

/** @brief [SteamReceiptValidatorTest] 시각 쓰기: 초 단위 UTC 이고 1970 이전(보고서 구간이 뒤로 갈 때)도 날짜가 무너지지 않는다 */
SW_TEST_CASE( SteamReceiptValidatorTest, FormatTime )
{
    SW_EXPECT_EQUAL( string( "2023-11-14T22:13:20" ), SteamReceiptValidator::formatTime( kNowMs ) );
    SW_EXPECT_EQUAL( string( "1970-01-01T00:00:00" ), SteamReceiptValidator::formatTime( 0 ) );
    SW_EXPECT_EQUAL( string( "1970-01-01T00:00:00" ), SteamReceiptValidator::formatTime( 999 ) );
    SW_EXPECT_EQUAL( string( "1969-12-31T23:59:59" ), SteamReceiptValidator::formatTime( -1000 ) );
    SW_EXPECT_EQUAL( string( "2024-02-29T00:00:00" ), SteamReceiptValidator::formatTime( 1709164800000ll ) );
}

/** @brief [SteamReceiptValidatorTest] 주소: 설정이 샌드박스면 샌드박스 인터페이스를 부른다 */
SW_TEST_CASE( SteamReceiptValidatorTest, URLsPickTheInterface )
{
    HTTPClient*          pNoClient = nullptr; // 주소 만들기는 클라이언트를 쓰지 않는다
    SteamReceiptSettings settings;
    settings._appID   = "480";
    settings._key     = "KEY";
    settings._baseURL = "http://steam";
    {
        const SteamReceiptValidator validator( settings, pNoClient );
        SW_EXPECT_EQUAL( string( "http://steam/ISteamMicroTxn/QueryTxn/v3/?key=KEY&appid=480&orderid=1234" ), validator.makeQueryURL( kOrder ) );
        SW_EXPECT_EQUAL( string( "http://steam/ISteamMicroTxn/GetReport/v4/?key=KEY&appid=480&type=STANDARD&time=2023-11-14T22:13:20&maxresults=1000" ), validator.makeReportURL( kNowMs ) );
    }
    settings._bSandbox = SW_TRUE;
    const SteamReceiptValidator sandbox( settings, pNoClient );
    SW_EXPECT_EQUAL( string( "http://steam/ISteamMicroTxnSandbox/QueryTxn/v3/?key=KEY&appid=480&orderid=1234" ), sandbox.makeQueryURL( kOrder ) );
}

/** @brief [SteamReceiptValidatorTest] 쿼리에 싣지 못하는 영수증은 스팀에 묻지 않고 Invalid 로 끝난다(다른 인자를 끼워 넣는 길) */
SW_TEST_CASE( SteamReceiptValidatorTest, MalformedReceiptNeverReachesSteam )
{
    SteamFixture rig;
    for ( const utf8* pPayload : { "", "abc", "1&orderid=2", "12345678901234567890123", "1 " } )
    {
        const ReceiptValidationResult result = rig.validate( pPayload );
        SW_EXPECT_TRUE( result._status == ReceiptStatus::Invalid );
        SW_EXPECT_EQUAL( kTicket, result._ticket );
        SW_EXPECT_EQUAL( kBuyer, result._accountID );
        SW_EXPECT_TRUE( result._transactionID.empty() );
    }
    SW_EXPECT_TRUE( rig._listQueryOrder.empty() );
}

/** @brief [SteamReceiptValidatorTest] 정상 주문: 거래 id 는 주문 번호, 상품은 첫 항목, 구매 시각은 응답의 것이고 표가 요청과 같다 */
SW_TEST_CASE( SteamReceiptValidatorTest, SucceededOrderIsValid )
{
    SteamFixture rig;
    rig._queryBody                       = SteamBodyTestUtil::makeOrder( kOrder, "Succeeded" );
    const ReceiptValidationResult result = rig.validate();
    SW_EXPECT_TRUE( result._status == ReceiptStatus::Valid );
    SW_EXPECT_EQUAL( string( "steam" ), result._storeName );
    SW_EXPECT_EQUAL( string( kOrder ), result._transactionID );
    SW_EXPECT_EQUAL( string( "100" ), result._productID );
    SW_EXPECT_EQUAL( kNowMs, result._purchaseTimeMs );
    SW_EXPECT_EQUAL( kTicket, result._ticket );
    SW_EXPECT_EQUAL( kBuyer, result._accountID );
    SW_EXPECT_TRUE( result._bSandbox == SW_FALSE );
    SW_EXPECT_TRUE( result._failureText.empty() );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._listQueryOrder.size() );
    SW_EXPECT_EQUAL( string( kOrder ), rig._listQueryOrder[0] );
    SW_EXPECT_EQUAL( string( "/ISteamMicroTxn/QueryTxn/v3/" ), rig._listQueryPath[0] );
}

/** @brief [SteamReceiptValidatorTest] 샌드박스 설정의 결과는 샌드박스 결제로 표시된다(서비스가 운영 서버에서 거절하는 근거) */
SW_TEST_CASE( SteamReceiptValidatorTest, SandboxConfigMarksTheResult )
{
    SteamFixture rig( true );
    rig._queryBody                       = SteamBodyTestUtil::makeOrder( kOrder, "Succeeded" );
    const ReceiptValidationResult result = rig.validate();
    SW_EXPECT_TRUE( result._status == ReceiptStatus::Valid );
    SW_EXPECT_TRUE( result._bSandbox == SW_TRUE );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._listQueryPath.size() );
    SW_EXPECT_EQUAL( string( "/ISteamMicroTxnSandbox/QueryTxn/v3/" ), rig._listQueryPath[0] );
}

/** @brief [SteamReceiptValidatorTest] 응답의 상태가 결과가 된다: 환불은 Refunded, 마무리 전은 Retry, 실패 · 모르는 값 · 상태 없음은 Invalid 다 */
SW_TEST_CASE( SteamReceiptValidatorTest, OrderStatusDecidesTheResult )
{
    SteamFixture rig;
    rig._queryBody = SteamBodyTestUtil::makeOrder( kOrder, "Refunded" );
    SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Refunded );
    rig._queryBody = SteamBodyTestUtil::makeOrder( kOrder, "Init" );
    SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Retry );
    rig._queryBody = SteamBodyTestUtil::makeOrder( kOrder, "Failed" );
    SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Invalid );
    rig._queryBody = SteamBodyTestUtil::makeOrder( kOrder, "SomethingNew" );
    SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Invalid );
    rig._queryBody = R"({"response":{"result":"OK","params":{"orderid":"1234","items":[{"itemid":"100"}]}}})";
    SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Invalid );
}

/** @brief [SteamReceiptValidatorTest] 위조 · 깨진 답은 지급하지 않는다: 다른 주문의 답, 주문 번호 없음, 항목 없음 · 비었음 · 모양이 틀림, 결과가 OK 아님, 뿌리 모양이 틀림 */
SW_TEST_CASE( SteamReceiptValidatorTest, ForgedAnswersGrantNothing )
{
    SteamFixture rig;
    for ( const utf8* pBody : {
              R"({"response":{"result":"OK","params":{"orderid":"9999","status":"Succeeded","items":[{"itemid":"100"}]}}})",
              R"({"response":{"result":"OK","params":{"status":"Succeeded","items":[{"itemid":"100"}]}}})",
              R"({"response":{"result":"OK","params":{"orderid":"1234","status":"Succeeded"}}})",
              R"({"response":{"result":"OK","params":{"orderid":"1234","status":"Succeeded","items":[]}}})",
              R"({"response":{"result":"OK","params":{"orderid":"1234","status":"Succeeded","items":{"itemid":"100"}}}})",
              R"({"response":{"result":"OK","params":{"orderid":"1234","status":"Succeeded","items":[{"qty":1}]}}})",
              R"({"response":{"result":"OK","params":{"orderid":"1234","status":"Succeeded","items":[7]}}})",
              R"({"response":{"result":"Failure","params":{"orderid":"1234","status":"Succeeded","items":[{"itemid":"100"}]}}})",
              R"({"response":{"params":{"orderid":"1234","status":"Succeeded","items":[{"itemid":"100"}]}}})",
              R"({"response":{"result":["OK"],"params":{"orderid":"1234","status":"Succeeded"}}})",
              R"({"response":7})",
              R"({"response":{"result":"OK","params":[]}})",
              R"({})",
              R"([])",
              R"(null)",
              R"(1234)",
          } )
    {
        rig._queryBody                       = pBody;
        const ReceiptValidationResult result = rig.validate();
        SW_EXPECT_TRUE_MSG( result._status == ReceiptStatus::Invalid, pBody );
        SW_EXPECT_FALSE( result._failureText.empty() );
    }
}

/** @brief [SteamReceiptValidatorTest] 2xx 인데 JSON 이 아니면(잘린 몸 · 프록시 화면) Retry 다 — Invalid 로 답하면 결제한 사람의 영수증이 버려진다 */
SW_TEST_CASE( SteamReceiptValidatorTest, TruncatedBodyIsRetry )
{
    SteamFixture rig;
    const string whole = SteamBodyTestUtil::makeOrder( kOrder, "Succeeded" );
    for ( size_t cut : { size_t( 1 ), whole.size() / 2, whole.size() - 1 } )
    {
        rig._queryBody = whole.substr( 0, cut );
        SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Retry );
    }
    rig._queryBody = "";
    SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Retry );
    rig._queryBody                       = "<html>bad gateway</html>";
    const ReceiptValidationResult result = rig.validate();
    SW_EXPECT_TRUE( result._status == ReceiptStatus::Retry );
    SW_EXPECT_EQUAL( string( kOrder ), result._transactionID );
}

/** @brief [SteamReceiptValidatorTest] 구매 시각이 틀린 주문은 시각만 비고 판정은 그대로다. 숫자로 온 주문 번호 · 상품 번호도 글로 읽는다 */
SW_TEST_CASE( SteamReceiptValidatorTest, LoosePropertiesStayReadable )
{
    SteamFixture rig;
    rig._queryBody                = R"({"response":{"result":"OK","params":{"orderid":"1234","status":"Succeeded","time":"2023-11-14T-1:13:20","items":[{"itemid":"100"}]}}})";
    ReceiptValidationResult first = rig.validate();
    SW_EXPECT_TRUE( first._status == ReceiptStatus::Valid );
    SW_EXPECT_EQUAL( int64( 0 ), first._purchaseTimeMs );

    rig._queryBody                 = R"({"response":{"result":"OK","params":{"orderid":1234,"status":"Succeeded","items":[{"itemid":100}]}}})";
    ReceiptValidationResult second = rig.validate();
    SW_EXPECT_TRUE( second._status == ReceiptStatus::Valid );
    SW_EXPECT_EQUAL( string( "100" ), second._productID );
}

/** @brief [SteamReceiptValidatorTest] 상태 코드: 5xx · 429 는 Retry, 4xx 는 주문이 없다는 답이라 Invalid 다(몸이 성공처럼 보여도) */
SW_TEST_CASE( SteamReceiptValidatorTest, HTTPStatusSplitsRetryFromInvalid )
{
    SteamFixture rig;
    rig._queryBody = SteamBodyTestUtil::makeOrder( kOrder, "Succeeded" );
    for ( int32 status : { 500, 503, 429 } )
    {
        rig._queryStatus                     = status;
        const ReceiptValidationResult result = rig.validate();
        SW_EXPECT_TRUE( result._status == ReceiptStatus::Retry );
        SW_EXPECT_EQUAL( string( kOrder ), result._transactionID );
    }
    for ( int32 status : { 400, 404 } )
    {
        rig._queryStatus = status;
        SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Invalid );
    }
}

/** @brief [SteamReceiptValidatorTest] 401 · 403 은 우리 키 문제라 영수증을 버리지 않고 Retry 이며 오류 로그로 알린다 */
SW_TEST_CASE( SteamReceiptValidatorTest, RejectedKeyIsRetryAndLogged )
{
    SW_EXPECT_LOG( "SteamReceiptValidator.SteamRejectedPublisherKey" );
    SteamFixture rig;
    for ( int32 status : { HTTPConstant::kStatusUnauthorized, HTTPConstant::kStatusForbidden } )
    {
        rig._queryStatus = status;
        SW_EXPECT_TRUE( rig.validate()._status == ReceiptStatus::Retry );
    }
}

/** @brief [SteamReceiptValidatorTest] 전송 실패(틀린 주소)는 Retry 이고 전송의 이유를 싣는다. 보고서 실패는 경고다 */
SW_TEST_CASE( SteamReceiptValidatorTest, TransportFailureIsRetry )
{
    SW_EXPECT_LOG( "StoreReceiptValidator.RefundQueryFailed" );
    SteamFixture                  rig( false, "not a url" );
    const ReceiptValidationResult result = rig.validate();
    SW_EXPECT_TRUE( result._status == ReceiptStatus::Retry );
    SW_EXPECT_EQUAL( string( "malformed URL" ), result._failureText );
    SW_EXPECT_EQUAL( string( kOrder ), result._transactionID );
}

/** @brief [SteamReceiptValidatorTest] 환불 보고: 환불 상태만 알리고, 시각은 응답의 것(틀리면 지금)이다. 주문 번호가 아닌 줄 · 모양이 틀린 줄은 버린다. 겹쳐 읽어도 한 번만 알린다 */
SW_TEST_CASE( SteamReceiptValidatorTest, RefundReportFiltersAndDeduplicates )
{
    SteamFixture rig;
    rig._reportBody = R"({"response":{"result":"OK","params":{"orders":[
        {"orderid":"1","status":"Refunded","time":"2023-11-14T22:13:20Z"},
        {"orderid":"2","status":"Succeeded","time":"2023-11-14T22:13:20Z"},
        {"orderid":"3","status":"Chargedback","time":"not a time"},
        {"orderid":"","status":"Refunded"},
        {"status":"Refunded"},
        {"orderid":"4&x=1","status":"Refunded"},
        {"orderid":"123456789012345678901","status":"Refunded"},
        {"orderid":"1","status":"PartialRefund"},
        7,
        []
    ]}}})";
    rig.pump();
    const vector<ReceiptRefundNotification> listNotice = rig.takeRefunds();
    SW_REQUIRE_EQUAL( size_t( 2 ), listNotice.size() );
    SW_EXPECT_EQUAL( string( "steam" ), listNotice[0]._storeName );
    SW_EXPECT_EQUAL( string( "1" ), listNotice[0]._transactionID );
    SW_EXPECT_EQUAL( kNowMs, listNotice[0]._refundedMs );
    SW_EXPECT_EQUAL( string( "3" ), listNotice[1]._transactionID );
    SW_EXPECT_EQUAL( kNowMs, listNotice[1]._refundedMs );

    rig._nowMs += kPollMs;
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 2 ), rig._listReportTime.size() );
    SW_EXPECT_TRUE( rig.takeRefunds().empty() );
}

/** @brief [SteamReceiptValidatorTest] 환불 보고의 뿌리 모양이 틀려도 멈추지 않고 알림이 없다 */
SW_TEST_CASE( SteamReceiptValidatorTest, RefundReportShapeIsChecked )
{
    SteamFixture rig;
    rig._reportBody = R"({"response":{"result":"OK","params":{"orders":{"orderid":"1","status":"Refunded"}}}})";
    rig.pump();
    SW_EXPECT_TRUE( rig.takeRefunds().empty() );
    rig._reportBody = R"({"response":{"result":"OK"}})";
    rig._nowMs += kPollMs;
    rig.pump();
    SW_EXPECT_TRUE( rig.takeRefunds().empty() );
    SW_EXPECT_EQUAL( size_t( 2 ), rig._listReportTime.size() );
}

/** @brief [SteamReceiptValidatorTest] 조회 구간: 성공한 조회만 시작점을 옮긴다. 5xx · 깨진 몸 · 결과가 OK 아닌 200 뒤에는 지난 성공부터 다시 읽어 환불을 놓치지 않는다 */
SW_TEST_CASE( SteamReceiptValidatorTest, FailedReportKeepsTheWindow )
{
    SW_EXPECT_LOG( "StoreReceiptValidator.RefundQueryFailed" );
    SW_EXPECT_LOG( "SteamReceiptValidator.SteamRefundReportNotOk" );
    SteamFixture rig;
    rig.pump(); // 첫 조회는 지금에서 겹침만큼 앞이다
    const int64 firstMs = kNowMs;
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._listReportTime.size() );
    SW_EXPECT_EQUAL( SteamReceiptValidator::formatTime( firstMs - kLookbackMs ), rig._listReportTime[0] );

    // 실패 셋 — 다음 조회의 시작점이 그대로다.
    rig._reportBody = kFailureReport;
    rig._nowMs += kPollMs;
    rig.pump();
    rig._reportBody = R"({"response":{"result":"OK")";
    rig._nowMs += kPollMs;
    rig.pump();
    rig._reportBody   = kEmptyReport;
    rig._reportStatus = 500;
    rig._nowMs += kPollMs;
    rig.pump();
    rig._reportStatus = HTTPConstant::kStatusOk;
    rig._nowMs += kPollMs;
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 5 ), rig._listReportTime.size() );
    for ( size_t index = 1; index < rig._listReportTime.size(); ++index )
    {
        SW_EXPECT_EQUAL( SteamReceiptValidator::formatTime( firstMs - kLookbackMs ), rig._listReportTime[index] );
    }

    // 다섯째의 성공이 시작점을 옮긴다.
    const int64 succeededMs = rig._nowMs;
    rig._nowMs += kPollMs;
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 6 ), rig._listReportTime.size() );
    SW_EXPECT_EQUAL( SteamReceiptValidator::formatTime( succeededMs - kLookbackMs ), rig._listReportTime[5] );
}

/** @brief [SteamReceiptValidatorTest] 조회 주기: 주기 전에는 묻지 않고, 앞 요청이 끝나기 전에는 주기가 지나도 겹쳐 보내지 않는다 */
SW_TEST_CASE( SteamReceiptValidatorTest, ReportPollIntervalAndNoOverlap )
{
    SteamFixture rig;
    rig.pump();
    rig._nowMs += kPollMs - 1;
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 1 ), rig._listReportTime.size() );

    // 답을 돌리지 않고(서버 멈춤) 주기를 두 번 넘긴다 — 날아가는 요청은 하나다.
    rig._nowMs += 1;
    rig._pValidator->update( UnixMilliseconds{ rig._nowMs } );
    rig._nowMs += kPollMs;
    rig._pValidator->update( UnixMilliseconds{ rig._nowMs } );
    SW_EXPECT_EQUAL( 1, rig._client.getPendingCount() );
}

/** @brief [SteamReceiptValidatorTest] 보고서가 상한까지 찼는데 이을 시각이 없으면 구간을 옮기지 않고 경고한다 — 옮기면 잘린 뒤쪽의 환불을 영영 놓친다 */
SW_TEST_CASE( SteamReceiptValidatorTest, FullReportWithoutTimesKeepsTheWindow )
{
    SW_EXPECT_LOG( "StoreReceiptValidator.RefundQueryTruncated" );
    SteamFixture rig;
    rig._reportBody = SteamBodyTestUtil::makeFullReport( "" );
    rig.pump();
    SW_EXPECT_EQUAL( static_cast<size_t>( SteamReceiptValidator::kReportMaxResults ), rig.takeRefunds().size() );

    rig._nowMs += kPollMs;
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 2 ), rig._listReportTime.size() );
    SW_EXPECT_EQUAL( rig._listReportTime[0], rig._listReportTime[1] );
}

/** @brief [SteamReceiptValidatorTest] 보고서가 상한까지 차면 읽은 가장 늦은 시각에서 바로 잇고, 잇는 조회가 끝나야 구간이 옮겨진다 */
SW_TEST_CASE( SteamReceiptValidatorTest, FullReportContinuesFromTheLastTime )
{
    SteamFixture rig;
    rig._reportBody = SteamBodyTestUtil::makeFullReport( R"(,"time":"2023-11-14T22:13:20Z")" );
    rig._pValidator->update( UnixMilliseconds{ rig._nowMs } );
    rig._server.update();
    for ( int32 round = 0; round < 16; ++round )
    {
        // 첫 답이 나간 뒤에 답을 바꿔 둔다 — 잇는 조회는 빈 보고를 받아 끝난다.
        if ( rig._listReportTime.size() == 1 )
            rig._reportBody = kEmptyReport;
        rig._pValidator->update( UnixMilliseconds{ rig._nowMs } );
        rig._server.update();
    }
    SW_REQUIRE_EQUAL( size_t( 2 ), rig._listReportTime.size() );
    SW_EXPECT_EQUAL( SteamReceiptValidator::formatTime( kNowMs - kLookbackMs ), rig._listReportTime[0] );
    SW_EXPECT_EQUAL( SteamReceiptValidator::formatTime( kNowMs ), rig._listReportTime[1] ); // 겹침 없이 그 시각부터

    rig._nowMs += kPollMs;
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 3 ), rig._listReportTime.size() );
    SW_EXPECT_EQUAL( SteamReceiptValidator::formatTime( kNowMs - kLookbackMs ), rig._listReportTime[2] ); // 구간의 기준은 첫 조회의 시작 시각이다
}

/** @brief [SteamReceiptValidatorTest] 회수하지 못한 환불을 잊으면 겹친 조회가 다시 알린다 — 겹침 거름이 회수할 기회를 없애지 않게 한다 */
SW_TEST_CASE( SteamReceiptValidatorTest, ForgottenRefundIsReportedAgain )
{
    SteamFixture rig;
    rig._reportBody = R"({"response":{"result":"OK","params":{"orders":[{"orderid":"1","status":"Refunded"}]}}})";
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 1 ), rig.takeRefunds().size() );
    rig._nowMs += kPollMs;
    rig.pump();
    SW_EXPECT_TRUE( rig.takeRefunds().empty() );

    rig._pValidator->removeRefund( "1" );
    rig._nowMs += kPollMs;
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 1 ), rig.takeRefunds().size() );
}

/** @brief [SteamReceiptValidatorTest] 답의 `steamid` 가 결과의 구매자가 되고 설정의 규칙이 결과에 실린다(대조는 지급 논리가 한다). 답에 없으면 비어 있다 */
SW_TEST_CASE( SteamReceiptValidatorTest, SteamIDBecomesTheBuyer )
{
    SteamFixture rig( false, nullptr, ReceiptBuyerBinding::LinkedLogin );
    SW_EXPECT_TRUE( rig._pValidator->getBuyerRule()._binding == ReceiptBuyerBinding::LinkedLogin );
    rig._queryBody                = R"({"response":{"result":"OK","params":{"orderid":"1234","steamid":"76561190000000001","status":"Succeeded","items":[{"itemid":"100"}]}}})";
    ReceiptValidationResult first = rig.validate();
    SW_EXPECT_TRUE( first._status == ReceiptStatus::Valid );
    SW_EXPECT_EQUAL( string( "76561190000000001" ), first._storeBuyerID );
    SW_EXPECT_TRUE( first._buyerRule._binding == ReceiptBuyerBinding::LinkedLogin );
    SW_EXPECT_EQUAL( string( "steam" ), first._buyerRule._loginProvider );

    rig._queryBody                 = SteamBodyTestUtil::makeOrder( kOrder, "Succeeded" );
    ReceiptValidationResult second = rig.validate();
    SW_EXPECT_TRUE( second._storeBuyerID.empty() );
}

/** @brief [SteamReceiptValidatorTest] 종료: 날아가던 검증은 한 번만 Retry 로 끝나고, 늦게 온 답은 결과를 더 내지 않으며, 새 보고서를 보내지 않는다 */
SW_TEST_CASE( SteamReceiptValidatorTest, ShutdownFinishesInFlightAsRetry )
{
    SteamFixture rig;
    rig._queryBody = SteamBodyTestUtil::makeOrder( kOrder, "Succeeded" );
    rig.pump();
    rig.submit( kOrder );
    rig._pValidator->shutdown();
    rig.pump();
    vector<ReceiptValidationResult> listResult;
    rig._pValidator->pollCompletions( listResult );
    SW_REQUIRE_EQUAL( size_t( 1 ), listResult.size() );
    SW_EXPECT_TRUE( listResult[0]._status == ReceiptStatus::Retry );
    SW_EXPECT_EQUAL( string( kOrder ), listResult[0]._transactionID );

    const size_t reportCount = rig._listReportTime.size();
    rig._nowMs += kPollMs * 2;
    rig.pump();
    SW_EXPECT_EQUAL( reportCount, rig._listReportTime.size() );
}
