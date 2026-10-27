// 스토어 영수증 제공자(OnlineKits H3) — 가짜 HTTPS 서버로: 스팀(QueryTxn 상태 대응 · 상품 id · 거래 id · 키 오류는 다시 시도 · 쿼리에 못 싣는 영수증은 묻지 않고 거절 ·
// GetReport 로 환불 알림 한 번) 과 구글(서비스 계정 JWT 로 받은 토큰 · purchases.products.get 상태 대응 · 테스트 구매 · 구매 확인 · voidedpurchases 로 환불 알림 한 번),
// 설정 파일을 엄격하게 읽고 환경 변수에 자격이 없으면 기동이 멈춘다.
#include "pch.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Time/Clock.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "EngineTest/GameFramework/Online/FakeHTTPSFixture.h"

#include "GameFramework/Base/Online/HTTP/ServiceAccountTokenSource.h"
#include "GameFramework/Base/Online/Security/JSONWebToken.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Google/GoogleReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Steam/SteamReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptProviderSet.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 서버 루프가 넘기는 시각(유닉스 밀리초) 대신 쓰는 고정 시각입니다. 제공자는 시계를 읽지 않으므로 이 값만 흐른다. */
    constexpr int64 kTestNowMs = 1792576800000ll; // 2026-10-21T10:00:00 UTC

    ReceiptValidationRequest makeRequest( sw::string_view storeName, sw::string_view payload, uint64 ticket )
    {
        ReceiptValidationRequest request;
        request._storeName = sw::string( storeName );
        request._payload   = sw::string( payload );
        request._accountID = 42;
        request._ticket    = ticket;
        return request;
    }

    /** @brief 검증 결과 @p count 개가 모일 때까지 서버와 제공자를 번갈아 돌립니다. */
    vector<ReceiptValidationResult> collectResults( test::FakeHTTPSFixture& https, IReceiptValidator& validator, int64 nowMs, size_t count )
    {
        vector<ReceiptValidationResult> listResult;
        const Deadline                  deadline = Deadline::afterMilliseconds( 10000 );
        while ( listResult.size() < count && deadline.isExpired() == false )
        {
            https.pump();
            validator.update( UnixMilliseconds{ nowMs } );
            validator.pollCompletions( listResult );
        }
        return listResult;
    }

    const ReceiptValidationResult* findByTicket( const vector<ReceiptValidationResult>& listResult, uint64 ticket )
    {
        for ( const ReceiptValidationResult& result : listResult )
        {
            if ( result._ticket == ticket )
                return &result;
        }
        return nullptr;
    }

    /** @brief 환불 알림이 @p count 개 모일 때까지 돕니다(검증 결과도 함께 거둔다 — 같은 제공자가 두 가지를 모두 낸다). */
    vector<ReceiptRefundNotice> collectRefunds( test::FakeHTTPSFixture& https, IReceiptValidator& validator, int64 nowMs, size_t count, int64 waitMilliseconds )
    {
        vector<ReceiptRefundNotice>     listNotice;
        vector<ReceiptValidationResult> listIgnored;
        const Deadline                  deadline = Deadline::afterMilliseconds( waitMilliseconds );
        while ( listNotice.size() < count && deadline.isExpired() == false )
        {
            https.pump();
            validator.update( UnixMilliseconds{ nowMs } );
            validator.pollCompletions( listIgnored );
            validator.pollRefunds( listNotice );
        }
        return listNotice;
    }

    /** @brief 가짜 스팀 — QueryTxn(키 · 앱 · 주문)과 GetReport. */
    class FakeSteamHandler final : public IHTTPRequestHandler
    {
    public:
        int32 _queryHits{ 0 };
        int32 _reportHits{ 0 };
        uint8 _bSandboxInterface{ SW_FALSE };

        void onHTTPRequest( const HTTPServerRequest& request, HTTPServerResponse& outResponse ) override
        {
            const HTTPHeader* pKey   = request.findQuery( "key" );
            const HTTPHeader* pApp   = request.findQuery( "appid" );
            const bool        bKeyOk = pKey != nullptr && pKey->_value == "publisher-key";
            const bool        bAppOk = pApp != nullptr && pApp->_value == "480";
            if ( bKeyOk == false || bAppOk == false )
            {
                outResponse._statusCode = 403;
                return;
            }
            _bSandboxInterface = request._path.find( "ISteamMicroTxnSandbox" ) != sw::string::npos ? SW_TRUE : SW_FALSE;
            if ( request._path.find( "/QueryTxn/v3/" ) != sw::string::npos )
            {
                ++_queryHits;
                const HTTPHeader* pOrder = request.findQuery( "orderid" );
                test::setResponseText( outResponse, 200, makeOrder( pOrder == nullptr ? sw::string() : pOrder->_value ) );
                return;
            }
            if ( request._path.find( "/GetReport/v4/" ) != sw::string::npos )
            {
                ++_reportHits;
                test::setResponseText( outResponse, 200,
                                       "{\"response\":{\"result\":\"OK\",\"params\":{\"count\":2,\"orders\":["
                                       "{\"orderid\":\"1001\",\"status\":\"Succeeded\",\"time\":\"2026-10-21T10:00:00\"},"
                                       "{\"orderid\":\"1002\",\"status\":\"Refunded\",\"time\":\"2026-10-21T11:30:00\"}]}}}" );
                return;
            }
            outResponse._statusCode = 404;
        }

    private:
        static sw::string makeOrder( const sw::string& orderID )
        {
            const auto order = [&orderID]( const utf8* pStatus )
            {
                return sw::string( "{\"response\":{\"result\":\"OK\",\"params\":{\"orderid\":\"" ) + orderID + "\",\"transid\":\"9\",\"steamid\":\"7656\",\"status\":\"" + pStatus +
                       "\",\"currency\":\"USD\",\"time\":\"2026-10-21T10:00:00\",\"items\":[{\"itemid\":7,\"qty\":1,\"amount\":100}]}}}";
            };
            if ( orderID == "1001" )
                return order( "Succeeded" );
            if ( orderID == "1002" )
                return order( "Refunded" );
            if ( orderID == "1003" )
                return order( "Init" );
            if ( orderID == "1004" )
                return order( "Failed" );
            return "{\"response\":{\"result\":\"Failure\",\"error\":{\"errorcode\":3,\"errordesc\":\"Order not found\"}}}";
        }
    };

    struct SteamFixture
    {
        FakeSteamHandler       _handler;
        test::FakeHTTPSFixture _https;
        SteamReceiptValidator  _validator;

        static SteamReceiptSettings makeSettings( const test::FakeHTTPSFixture& https, const utf8* pKey, bool bSandbox )
        {
            SteamReceiptSettings settings;
            settings._appID    = "480";
            settings._key      = pKey;
            settings._baseURL  = "https://localhost:" + to_string( static_cast<int32>( https._server.getListenPort() ) );
            settings._bSandbox = bSandbox ? SW_TRUE : SW_FALSE;
            return settings;
        }

        explicit SteamFixture( const utf8* pKey = "publisher-key", bool bSandbox = false )
            : _handler{}
            , _https{ &_handler }
            , _validator{ makeSettings( _https, pKey, bSandbox ), &_https._client }
        {
        }
    };
    /** @brief 가짜 구글 — `/token`(서비스 계정 JWT 확인), 상품 조회, 구매 확인(acknowledge), 무효 구매 목록. */
    class FakeGoogleHandler final : public IHTTPRequestHandler
    {
    public:
        NetPublicKey _publicKey{};
        sw::string   _tokenURL{};
        int32        _tokenHits{ 0 };
        int32        _productHits{ 0 };
        int32        _acknowledgeHits{ 0 };
        int32        _voidedHits{ 0 };
        uint8        _bAssertionValid{ SW_FALSE };

        void onHTTPRequest( const HTTPServerRequest& request, HTTPServerResponse& outResponse ) override
        {
            if ( request._path == "/token" )
            {
                ++_tokenHits;
                checkAssertion( request.getBodyText() );
                test::setResponseText( outResponse, 200, "{\"access_token\":\"gtok-1\",\"expires_in\":3600}" );
                return;
            }
            const HTTPHeader* pAuthorization = request.findHeader( "authorization" );
            if ( pAuthorization == nullptr || pAuthorization->_value != "Bearer gtok-1" )
            {
                outResponse._statusCode = 401;
                return;
            }
            const sw::string prefix = "/androidpublisher/v3/applications/com.example.game/purchases/";
            if ( request._path.rfind( prefix + "voidedpurchases", 0 ) == 0 )
            {
                ++_voidedHits;
                test::setResponseText( outResponse, 200, "{\"voidedPurchases\":[{\"orderId\":\"GPA.2\",\"voidedTimeMillis\":\"1700000000000\"}]}" );
                return;
            }
            if ( request._path.size() > 12 && request._path.compare( request._path.size() - 12, 12, ":acknowledge" ) == 0 )
            {
                ++_acknowledgeHits;
                test::setResponseText( outResponse, 200, "{}" );
                return;
            }
            if ( request._path.rfind( prefix + "products/gems100/tokens/", 0 ) == 0 )
            {
                ++_productHits;
                const sw::string token = request._path.substr( ( prefix + "products/gems100/tokens/" ).size() );
                test::setResponseText( outResponse, 200, makePurchase( token ) );
                return;
            }
            outResponse._statusCode = 404;
        }

    private:
        static sw::string makePurchase( const sw::string& token )
        {
            const auto purchase = [&token]( int32 state, int32 type, int32 acknowledgement )
            {
                return "{\"kind\":\"androidpublisher#productPurchase\",\"purchaseTimeMillis\":\"1700000000000\",\"purchaseState\":" + to_string( state ) +
                       ",\"consumptionState\":0,\"orderId\":\"GPA." + sw::string( token ) + "\",\"purchaseType\":" + to_string( type ) + ",\"acknowledgementState\":" +
                       to_string( acknowledgement ) + "}";
            };
            if ( token == "tokenOK" )
                return sw::string( purchase( 0, 9, 0 ).c_str() );
            if ( token == "tokenAcked" )
                return sw::string( purchase( 0, 9, 1 ).c_str() );
            if ( token == "tokenCanceled" )
                return sw::string( purchase( 1, 9, 1 ).c_str() );
            if ( token == "tokenPending" )
                return sw::string( purchase( 2, 9, 0 ).c_str() );
            if ( token == "tokenTest" )
                return sw::string( purchase( 0, 0, 1 ).c_str() );
            return "{}";
        }

        void checkAssertion( sw::string_view body )
        {
            vector<HTTPHeader> listForm;
            sw::string         assertion;
            if ( HTTPUtil::decodeForm( body, listForm ) )
            {
                for ( const HTTPHeader& pair : listForm )
                {
                    if ( pair._name == "assertion" )
                        assertion = pair._value;
                }
            }
            JwtUnverified                    unverified;
            const bool                       bParsed = unverified.parse( assertion );
            const std::optional<JwtVerified> token =
                bParsed ? std::move( unverified ).verify( NetSecurity::getProvider(), _publicKey, JwtClaimRule::makeSignatureOnly() ) : std::nullopt;
            sw::string issuer;
            sw::string scope;
            _bAssertionValid =
                token.has_value() && token->findText( "iss", issuer ) && issuer == "svc@proj.iam" && token->findText( "scope", scope ) && scope == GoogleReceiptSettings::kScope;
        }
    };

    struct GoogleFixture
    {
        FakeGoogleHandler      _handler;
        test::FakeHTTPSFixture _https;
        GoogleReceiptValidator _validator;

        static GoogleReceiptSettings makeSettings( FakeGoogleHandler& handler, const test::FakeHTTPSFixture& https )
        {
            sw::string   privateKeyPem;
            NetPublicKey publicKey;
            (void)NetSecurity::getProvider().createSigningKeyPair( NetSignatureAlgorithm::RsaPkcs1Sha256, privateKeyPem, publicKey );
            handler._publicKey              = publicKey;
            handler._tokenURL               = https.makeURL( "/token" );
            const sw::string serviceAccount = "{\"project_id\":\"proj\",\"client_email\":\"svc@proj.iam\",\"private_key_id\":\"k1\",\"private_key\":\"" +
                                              JSONDocument::escapeString( privateKeyPem ) + "\",\"token_uri\":\"" + handler._tokenURL + "\"}";
            GoogleReceiptSettings settings;
            sw::string            error;
            const bool            bRead = ServiceAccountCredentials::read( serviceAccount, settings._credentials, error );
            SW_EXPECT_TRUE( bRead );
            settings._packageName = "com.example.game";
            settings._baseURL     = "https://localhost:" + to_string( static_cast<int32>( https._server.getListenPort() ) );
            return settings;
        }

        GoogleFixture()
            : _handler{}
            , _https{ &_handler }
            , _validator{ makeSettings( _handler, _https ), &NetSecurity::getProvider(), &_https._client }
        {
        }
    };
} // namespace

SW_TEST_CASE( SteamReceiptTest, QueryTxnStatusesBecomeReceiptStatuses )
{
    SteamFixture fixture;
    fixture._validator.submitValidation( makeRequest( "steam", "1001", 1 ), UnixMilliseconds{ kTestNowMs } );
    fixture._validator.submitValidation( makeRequest( "steam", "1002", 2 ), UnixMilliseconds{ kTestNowMs } );
    fixture._validator.submitValidation( makeRequest( "steam", "1003", 3 ), UnixMilliseconds{ kTestNowMs } );
    fixture._validator.submitValidation( makeRequest( "steam", "1004", 4 ), UnixMilliseconds{ kTestNowMs } );
    fixture._validator.submitValidation( makeRequest( "steam", "9999", 5 ), UnixMilliseconds{ kTestNowMs } );
    const vector<ReceiptValidationResult> listResult = collectResults( fixture._https, fixture._validator, kTestNowMs, 5 );
    SW_ASSERT_EQUAL( listResult.size(), size_t( 5 ) );
    const ReceiptValidationResult* pValid = findByTicket( listResult, 1 );
    SW_ASSERT_TRUE( pValid != nullptr );
    SW_EXPECT_TRUE( pValid->_status == ReceiptStatus::Valid );
    SW_EXPECT_EQUAL( pValid->_transactionID, sw::string( "1001" ) );
    SW_EXPECT_EQUAL( pValid->_productID, sw::string( "7" ) );
    SW_EXPECT_EQUAL( pValid->_accountID, uint64( 42 ) );
    SW_EXPECT_TRUE( pValid->_bSandbox == SW_FALSE );
    SW_EXPECT_EQUAL( pValid->_purchaseTimeMs, int64( 1792576800000ll ) ); // 2026-10-21T10:00:00 UTC
    SW_EXPECT_TRUE( findByTicket( listResult, 2 )->_status == ReceiptStatus::Refunded );
    SW_EXPECT_TRUE( findByTicket( listResult, 3 )->_status == ReceiptStatus::Retry );   // 구매자가 아직 마무리하지 않았다
    SW_EXPECT_TRUE( findByTicket( listResult, 4 )->_status == ReceiptStatus::Invalid ); // Failed
    SW_EXPECT_TRUE( findByTicket( listResult, 5 )->_status == ReceiptStatus::Invalid ); // 모르는 주문
    SW_EXPECT_EQUAL( fixture._handler._queryHits, 5 );
}

SW_TEST_CASE( SteamReceiptTest, ReceiptsThatCannotGoIntoAQueryAreRefusedWithoutAskingSteam )
{
    SteamFixture fixture;
    fixture._validator.submitValidation( makeRequest( "steam", "1001&key=other", 1 ), UnixMilliseconds{ kTestNowMs } );
    fixture._validator.submitValidation( makeRequest( "steam", "", 2 ), UnixMilliseconds{ kTestNowMs } );
    fixture._validator.submitValidation( makeRequest( "steam", "123456789012345678901", 3 ), UnixMilliseconds{ kTestNowMs } ); // 21 자리
    const vector<ReceiptValidationResult> listResult = collectResults( fixture._https, fixture._validator, kTestNowMs, 3 );
    SW_ASSERT_EQUAL( listResult.size(), size_t( 3 ) );
    for ( const ReceiptValidationResult& result : listResult )
    {
        SW_EXPECT_TRUE( result._status == ReceiptStatus::Invalid );
    }
    SW_EXPECT_EQUAL( fixture._handler._queryHits, 0 );
}

SW_TEST_CASE( SteamReceiptTest, AWrongPublisherKeyIsRetriedNotBlamedOnThePlayer )
{
    SteamFixture fixture( "wrong-key" );
    {
        SW_TEST_DEFENSIVE_SCOPE( "the provider logs that steam rejected the key" );
        fixture._validator.submitValidation( makeRequest( "steam", "1001", 1 ), UnixMilliseconds{ kTestNowMs } );
        const vector<ReceiptValidationResult> listResult = collectResults( fixture._https, fixture._validator, kTestNowMs, 1 );
        SW_ASSERT_EQUAL( listResult.size(), size_t( 1 ) );
        SW_EXPECT_TRUE( listResult[0]._status == ReceiptStatus::Retry );
    }
}

SW_TEST_CASE( SteamReceiptTest, TheRefundReportNotifiesEachRefundedOrderOnce )
{
    SteamFixture                      fixture;
    const vector<ReceiptRefundNotice> listNotice = collectRefunds( fixture._https, fixture._validator, kTestNowMs, 1, 10000 );
    SW_ASSERT_EQUAL( listNotice.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( listNotice[0]._transactionID, sw::string( "1002" ) ); // 성공한 주문(1001)은 환불이 아니다
    SW_EXPECT_EQUAL( listNotice[0]._storeName, sw::string( "steam" ) );
    SW_EXPECT_EQUAL( listNotice[0]._refundedMs, int64( 1792582200000ll ) ); // 2026-10-21T11:30:00 UTC

    // 시각을 조회 주기만큼 밀면 두 번째 보고서가 온다 — 그래도 같은 주문을 다시 알리지 않는다.
    const int32                       reportHits = fixture._handler._reportHits;
    const vector<ReceiptRefundNotice> listAgain  = collectRefunds( fixture._https, fixture._validator, kTestNowMs + SteamReceiptSettings{}._reportPollIntervalMs, 1, 300 );
    SW_EXPECT_TRUE( fixture._handler._reportHits > reportHits );
    SW_EXPECT_EQUAL( listAgain.size(), size_t( 0 ) );
}

SW_TEST_CASE( SteamReceiptTest, TheSandboxInterfaceMarksResultsAsSandboxPurchases )
{
    SteamFixture fixture( "publisher-key", true );
    fixture._validator.submitValidation( makeRequest( "steam", "1001", 1 ), UnixMilliseconds{ kTestNowMs } );
    const vector<ReceiptValidationResult> listResult = collectResults( fixture._https, fixture._validator, kTestNowMs, 1 );
    SW_ASSERT_EQUAL( listResult.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listResult[0]._bSandbox == SW_TRUE );
    SW_EXPECT_TRUE( fixture._handler._bSandboxInterface == SW_TRUE );
    SW_EXPECT_TRUE( fixture._validator.makeQueryURL( "5" ).find( "ISteamMicroTxnSandbox/QueryTxn/v3/?key=publisher-key&appid=480&orderid=5" ) != sw::string::npos );
}

SW_TEST_CASE( SteamReceiptTest, TimesRoundTripAndOrderIDsAreChecked )
{
    int64 unixMs = 0;
    SW_ASSERT_TRUE( SteamReceiptValidator::parseTime( "2026-10-21T10:00:00", unixMs ) );
    SW_EXPECT_EQUAL( unixMs, int64( 1792576800000ll ) );
    SW_EXPECT_EQUAL( SteamReceiptValidator::formatTime( unixMs ), sw::string( "2026-10-21T10:00:00" ) );
    SW_EXPECT_EQUAL( SteamReceiptValidator::formatTime( 0 ), sw::string( "1970-01-01T00:00:00" ) );
    SW_EXPECT_EQUAL( SteamReceiptValidator::formatTime( 951782400000ll ), sw::string( "2000-02-29T00:00:00" ) ); // 윤일
    SW_EXPECT_TRUE( SteamReceiptValidator::parseTime( "2000-02-29T00:00:00Z", unixMs ) );
    SW_EXPECT_EQUAL( unixMs, int64( 951782400000ll ) );
    SW_EXPECT_FALSE( SteamReceiptValidator::parseTime( "2026-13-01T00:00:00", unixMs ) );
    SW_EXPECT_FALSE( SteamReceiptValidator::parseTime( "yesterday", unixMs ) );
    SW_EXPECT_TRUE( SteamReceiptValidator::isOrderID( "1234567890123456789" ) );
    SW_EXPECT_FALSE( SteamReceiptValidator::isOrderID( "12a" ) );
    SW_EXPECT_TRUE( SteamReceiptValidator::mapOrderStatus( "PartialRefund" ) == ReceiptStatus::Refunded );
    SW_EXPECT_TRUE( SteamReceiptValidator::mapOrderStatus( "Chargedback" ) == ReceiptStatus::Refunded );
    SW_EXPECT_TRUE( SteamReceiptValidator::mapOrderStatus( "Approved" ) == ReceiptStatus::Retry );
    SW_EXPECT_TRUE( SteamReceiptValidator::mapOrderStatus( "Whatever" ) == ReceiptStatus::Invalid );
}

SW_TEST_CASE( GoogleReceiptTest, PurchaseStatesBecomeReceiptStatusesAndValidPurchasesAreAcknowledged )
{
    GoogleFixture fixture;
    fixture._validator.submitValidation( makeRequest( "google", "gems100|tokenOK", 1 ), UnixMilliseconds{ kTestNowMs } );
    fixture._validator.submitValidation( makeRequest( "google", "gems100|tokenCanceled", 2 ), UnixMilliseconds{ kTestNowMs } );
    fixture._validator.submitValidation( makeRequest( "google", "gems100|tokenPending", 3 ), UnixMilliseconds{ kTestNowMs } );
    fixture._validator.submitValidation( makeRequest( "google", "gems100|tokenTest", 4 ), UnixMilliseconds{ kTestNowMs } );
    fixture._validator.submitValidation( makeRequest( "google", "gems100|tokenUnknown", 5 ), UnixMilliseconds{ kTestNowMs } );
    fixture._validator.submitValidation( makeRequest( "google", "gems100|tokenAcked", 6 ), UnixMilliseconds{ kTestNowMs } );
    const vector<ReceiptValidationResult> listResult = collectResults( fixture._https, fixture._validator, kTestNowMs, 6 );
    SW_ASSERT_EQUAL( listResult.size(), size_t( 6 ) );
    SW_EXPECT_TRUE( fixture._handler._bAssertionValid == SW_TRUE );
    SW_EXPECT_EQUAL( fixture._handler._tokenHits, 1 ); // 여섯 요청이 토큰 하나를 나눠 쓴다
    const ReceiptValidationResult* pValid = findByTicket( listResult, 1 );
    SW_ASSERT_TRUE( pValid != nullptr );
    SW_EXPECT_TRUE( pValid->_status == ReceiptStatus::Valid );
    SW_EXPECT_EQUAL( pValid->_transactionID, sw::string( "GPA.tokenOK" ) );
    SW_EXPECT_EQUAL( pValid->_productID, sw::string( "gems100" ) );
    SW_EXPECT_EQUAL( pValid->_purchaseTimeMs, int64( 1700000000000ll ) );
    SW_EXPECT_TRUE( pValid->_bSandbox == SW_FALSE );
    SW_EXPECT_TRUE( findByTicket( listResult, 2 )->_status == ReceiptStatus::Refunded );
    SW_EXPECT_TRUE( findByTicket( listResult, 3 )->_status == ReceiptStatus::Retry );
    SW_EXPECT_TRUE( findByTicket( listResult, 4 )->_status == ReceiptStatus::Valid );
    SW_EXPECT_TRUE( findByTicket( listResult, 4 )->_bSandbox == SW_TRUE ); // 라이선스 테스트 구매
    SW_EXPECT_TRUE( findByTicket( listResult, 5 )->_status == ReceiptStatus::Invalid );
    SW_EXPECT_TRUE( findByTicket( listResult, 6 )->_status == ReceiptStatus::Valid );

    // 구매 확인은 아직 확인하지 않은 유효한 구매(tokenOK)에만 한 번 간다.
    const Deadline deadline = Deadline::afterMilliseconds( 5000 );
    while ( fixture._handler._acknowledgeHits < 1 && deadline.isExpired() == false )
    {
        vector<ReceiptValidationResult> listIgnored;
        fixture._https.pump();
        fixture._validator.update( UnixMilliseconds{ kTestNowMs } );
        fixture._validator.pollCompletions( listIgnored );
    }
    SW_EXPECT_EQUAL( fixture._handler._acknowledgeHits, 1 );
}

SW_TEST_CASE( GoogleReceiptTest, MalformedReceiptsAreRefusedWithoutAskingGoogle )
{
    GoogleFixture fixture;
    fixture._validator.submitValidation( makeRequest( "google", "no-bar", 1 ), UnixMilliseconds{ kTestNowMs } );
    fixture._validator.submitValidation( makeRequest( "google", "gems100|tok/../x", 2 ), UnixMilliseconds{ kTestNowMs } );
    fixture._validator.submitValidation( makeRequest( "google", "|token", 3 ), UnixMilliseconds{ kTestNowMs } );
    const vector<ReceiptValidationResult> listResult = collectResults( fixture._https, fixture._validator, kTestNowMs, 3 );
    SW_ASSERT_EQUAL( listResult.size(), size_t( 3 ) );
    for ( const ReceiptValidationResult& result : listResult )
    {
        SW_EXPECT_TRUE( result._status == ReceiptStatus::Invalid );
    }
    SW_EXPECT_EQUAL( fixture._handler._tokenHits, 0 );
    SW_EXPECT_EQUAL( fixture._handler._productHits, 0 );
}

SW_TEST_CASE( GoogleReceiptTest, TheVoidedPurchaseListNotifiesEachRefundOnce )
{
    GoogleFixture                     fixture;
    const vector<ReceiptRefundNotice> listNotice = collectRefunds( fixture._https, fixture._validator, kTestNowMs, 1, 10000 );
    SW_ASSERT_EQUAL( listNotice.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( listNotice[0]._transactionID, sw::string( "GPA.2" ) );
    SW_EXPECT_EQUAL( listNotice[0]._storeName, sw::string( "google" ) );
    SW_EXPECT_EQUAL( listNotice[0]._refundedMs, int64( 1700000000000ll ) );
    // 시각을 조회 주기만큼 밀면 두 번째 목록이 온다 — 그래도 같은 주문을 다시 알리지 않는다.
    const int32                       voidedHits = fixture._handler._voidedHits;
    const vector<ReceiptRefundNotice> listAgain  = collectRefunds( fixture._https, fixture._validator, kTestNowMs + GoogleReceiptSettings{}._voidedPollIntervalMs, 1, 300 );
    SW_EXPECT_TRUE( fixture._handler._voidedHits > voidedHits );
    SW_EXPECT_EQUAL( listAgain.size(), size_t( 0 ) );
}

SW_TEST_CASE( GoogleReceiptTest, PayloadsAreSplitIntoProductAndToken )
{
    sw::string product;
    sw::string token;
    SW_EXPECT_TRUE( GoogleReceiptValidator::splitPayload( "gems.100|abc-DEF_1.2", product, token ) );
    SW_EXPECT_EQUAL( product, sw::string( "gems.100" ) );
    SW_EXPECT_EQUAL( token, sw::string( "abc-DEF_1.2" ) );
    SW_EXPECT_FALSE( GoogleReceiptValidator::splitPayload( "gems100", product, token ) );
    SW_EXPECT_FALSE( GoogleReceiptValidator::splitPayload( "gems100|a b", product, token ) );
    SW_EXPECT_TRUE( GoogleReceiptValidator::mapPurchaseState( 0 ) == ReceiptStatus::Valid );
    SW_EXPECT_TRUE( GoogleReceiptValidator::mapPurchaseState( 1 ) == ReceiptStatus::Refunded );
    SW_EXPECT_TRUE( GoogleReceiptValidator::mapPurchaseState( 2 ) == ReceiptStatus::Retry );
    SW_EXPECT_TRUE( GoogleReceiptValidator::mapPurchaseState( 7 ) == ReceiptStatus::Invalid );
}

SW_TEST_CASE( ReceiptProviderSetTest, SettingsAreReadStrictlyAndTheSecretComesFromTheEnvironment )
{
    vector<ReceiptProviderConfig> listSettings;
    sw::string                    error;
    SW_ASSERT_TRUE( ReceiptProviderSet::readSettings(
        "{\"providers\":[{\"kind\":\"steam\",\"appId\":\"480\",\"keySecretEnvironment\":\"SW_TEST_STEAM_KEY\",\"sandbox\":true,\"reportPollSeconds\":120},"
        "{\"kind\":\"google\",\"packageName\":\"com.example.game\",\"serviceAccountEnvironment\":\"SW_TEST_GOOGLE_ACCOUNT\"}]}",
        listSettings, error ) );
    SW_ASSERT_EQUAL( listSettings.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( listSettings[0]._pollSeconds, int64( 120 ) );
    SW_EXPECT_TRUE( listSettings[0]._bSandbox == SW_TRUE );
    SW_EXPECT_EQUAL( listSettings[1]._pollSeconds, int64( 3600 ) ); // 기본 주기
    SW_EXPECT_FALSE( ReceiptProviderSet::readSettings( "{\"providers\":[{\"kind\":\"steam\",\"appId\":\"480\",\"keySecretEnvironment\":\"X\",\"typo\":1}]}", listSettings, error ) );
    SW_EXPECT_FALSE( ReceiptProviderSet::readSettings( "{\"providers\":[{\"kind\":\"steam\",\"appId\":\"abc\",\"keySecretEnvironment\":\"X\"}]}", listSettings, error ) );
    SW_EXPECT_FALSE( ReceiptProviderSet::readSettings( "{\"providers\":[{\"kind\":\"steam\",\"appId\":\"480\",\"keySecretEnvironment\":\"X\",\"reportPollSeconds\":5}]}", listSettings, error ) );
    SW_EXPECT_FALSE( ReceiptProviderSet::readSettings( "{\"providers\":[{\"kind\":\"google\",\"packageName\":\"com.example.game\"}]}", listSettings, error ) );
    SW_EXPECT_FALSE( ReceiptProviderSet::readSettings( "{\"providers\":[{\"kind\":\"apple\"}]}", listSettings, error ) );

    // 환경 변수가 없으면 제공자를 만들지 못하고 기동이 멈춘다 — 가짜로 대신 뜨지 않는다.
    ReceiptProviderSet            providerSet;
    const SystemMonotonicClock    clock;
    vector<ReceiptProviderConfig> listMissing;
    SW_ASSERT_TRUE( ReceiptProviderSet::readSettings(
        "{\"providers\":[{\"kind\":\"steam\",\"appId\":\"480\",\"keySecretEnvironment\":\"SW_TEST_STEAM_KEY\"}]}", listMissing, error ) );
    bool bInitialized = true;
    {
        SW_TEST_DEFENSIVE_SCOPE( "the missing environment variable is logged" );
        bInitialized = providerSet.initialize( listMissing, "", clock, error );
    }
    SW_EXPECT_FALSE( bInitialized );
    SW_EXPECT_TRUE( error.find( "SW_TEST_STEAM_KEY" ) != sw::string::npos );
    SW_EXPECT_EQUAL( providerSet.getProviderCount(), 0 );
}
