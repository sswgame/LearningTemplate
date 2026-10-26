// 애플 영수증 제공자(OnlineKits H3) — 가짜 앱 스토어 서버(거래 조회 · 알림 기록)와 테스트용 인증서 사슬로: 서버가 만든 ES256 JWT 인증, 서명된 거래(JWS)를 `x5c` 사슬이 루트까지 이어지고
// 리프 키로 서명이 맞는지 확인(다른 루트 · 애플 표지 없음 · 만료 · 변조는 거절), 번들 id · 거래 id 불일치 거절, 환불(revocationDate) · 샌드박스 표시, 경로에 못 싣는 영수증은 묻지 않고 거절,
// 알림 기록의 REFUND 를 쪽 넘김까지 읽어 한 번씩 알림. 보안 제공자의 사슬 확인(`verifyCertificateChain`)도 여기서 직접 본다.
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/String/Base64Util.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "EngineTest/GameFramework/Online/FakeHTTPSFixture.h"

#include "GameFramework/Base/Online/Security/JSONWebToken.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Apple/AppleReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptProviderSet.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr int64 kPurchaseMs = 1700000000000ll;
    constexpr int64 kRefundMs   = 1700000500000ll;

    /** @brief 인증서 사슬 하나(리프, 중간, 루트)와 리프 서명 키입니다. */
    struct TestPKI
    {
        vector<sw::string> _listChainPem{};
        sw::string         _leafKeyPem{};
        sw::string         _rootPem{};

        TestPKI( const utf8* pLeafOid, const utf8* pIssuerOid )
        {
            const bool bMade = NetSecurity::getProvider().createTestCertificateChain( 30, pLeafOid, pIssuerOid, _listChainPem, _leafKeyPem );
            SW_EXPECT_TRUE( bMade );
            if ( bMade )
                _rootPem = _listChainPem[2];
        }

        /** @brief PEM 인증서 한 장을 DER 바이트로 풉니다. */
        static vector<uint8> pemToDer( const sw::string& pem )
        {
            sw::string body;
            for ( const utf8 character : pem )
            {
                if ( character != '\r' && character != '\n' )
                    body.push_back( character );
            }
            const sw::string header = "-----BEGIN CERTIFICATE-----";
            const sw::string footer = "-----END CERTIFICATE-----";
            const size_t     begin  = body.find( header );
            const size_t     end    = body.find( footer );
            vector<uint8>    listDer;
            const bool       bFramed = begin != sw::string::npos && end != sw::string::npos;
            if ( bFramed )
                SW_EXPECT_TRUE( Base64Util::decode( body.substr( begin + header.size(), end - begin - header.size() ), listDer ) );
            return listDer;
        }

        vector<vector<uint8>> makeChainDer() const
        {
            vector<vector<uint8>> listDer;
            for ( const sw::string& pem : _listChainPem )
            {
                listDer.push_back( pemToDer( pem ) );
            }
            return listDer;
        }

        /** @brief 본문 JSON 을 리프 키로 서명한 JWS(머리에 `x5c` 사슬)를 만듭니다. */
        sw::string makeJWS( const sw::string& payloadJSON ) const
        {
            sw::string x5c = "[";
            for ( size_t index = 0; index < _listChainPem.size(); ++index )
            {
                const vector<uint8> listDer = pemToDer( _listChainPem[index] );
                x5c += ( index == 0 ? "\"" : ",\"" ) + Base64Util::encode( listDer.data(), listDer.size() ) + "\"";
            }
            x5c += "]";
            const sw::string header       = "{\"alg\":\"ES256\",\"x5c\":" + x5c + "}";
            sw::string       signingInput = Base64Util::encodeURL( reinterpret_cast<const uint8*>( header.data() ), header.size() );
            signingInput += ".";
            signingInput += Base64Util::encodeURL( reinterpret_cast<const uint8*>( payloadJSON.data() ), payloadJSON.size() );
            vector<uint8> listSignature;
            SW_EXPECT_TRUE( NetSecurity::getProvider().signData( NetSignatureAlgorithm::EcdsaP256Sha256, _leafKeyPem, reinterpret_cast<const uint8*>( signingInput.data() ),
                                                                 static_cast<int32>( signingInput.size() ), listSignature ) );
            return signingInput + "." + Base64Util::encodeURL( listSignature.data(), listSignature.size() );
        }
    };

    sw::string makeTransactionPayload( const utf8* pTransactionID, const utf8* pBundleID, const utf8* pEnvironment, bool bRevoked )
    {
        sw::string payload = sw::string( "{\"transactionId\":\"" ) + pTransactionID + "\",\"bundleId\":\"" + pBundleID + "\",\"productId\":\"gems100\",\"purchaseDate\":" +
                             to_string( kPurchaseMs ) + ",\"environment\":\"" + pEnvironment + "\"";
        if ( bRevoked )
            payload += ",\"revocationDate\":" + to_string( kRefundMs );
        return payload + "}";
    }

    /** @brief 가짜 앱 스토어 — 인증 JWT 를 확인하고 거래 조회와 알림 기록(쪽 둘)에 답한다. */
    class FakeAppStoreHandler final : public IHTTPRequestHandler
    {
    public:
        const TestPKI* _pPKI{ nullptr };        ///< 애플 루트로 믿는 사슬
        const TestPKI* _pForeignPKI{ nullptr }; ///< 다른 루트의 사슬(위조)
        NetPublicKey   _authPublicKey{};
        int32          _transactionHits{ 0 };
        int32          _historyHits{ 0 };
        int32          _authFailuresLeft{ 0 }; ///< 거래 조회에 401 을 줄 횟수
        uint8          _bAuthValid{ SW_FALSE };
        uint8          _bHistoryBodyValid{ SW_FALSE };

        void onHTTPRequest( const HTTPServerRequest& request, HTTPServerResponse& outResponse ) override
        {
            checkAuthorization( request.findHeader( "authorization" ) );
            if ( _bAuthValid == SW_FALSE )
            {
                outResponse._statusCode = 401;
                return;
            }
            const sw::string transactionPrefix = "/inApps/v1/transactions/";
            if ( sw::StringUtil::startsWith( request._path, transactionPrefix ) )
            {
                ++_transactionHits;
                if ( _authFailuresLeft > 0 )
                {
                    --_authFailuresLeft;
                    outResponse._statusCode = 401;
                    return;
                }
                respondTransaction( request._path.substr( transactionPrefix.size() ), outResponse );
                return;
            }
            if ( request._path == "/inApps/v1/notification/history" && request._method == HTTPMethod::Post )
            {
                ++_historyHits;
                respondHistory( request, outResponse );
                return;
            }
            outResponse._statusCode = 404;
        }

    private:
        void checkAuthorization( const HTTPHeader* pAuthorization )
        {
            _bAuthValid = SW_FALSE;
            if ( pAuthorization == nullptr || pAuthorization->_value.rfind( "Bearer ", 0 ) != 0 )
                return;
            JSONWebToken token;
            sw::string   issuer;
            sw::string   audience;
            sw::string   bundle;
            const bool   bTokenOk = token.parse( string_view( pAuthorization->_value ).substr( 7 ) ) && token.getAlgorithm() == NetSignatureAlgorithm::EcdsaP256Sha256 &&
                                  token.getKeyID() == "KEY1" && token.verifySignature( NetSecurity::getProvider(), _authPublicKey );
            const bool bClaimsOk = bTokenOk && token.findText( "iss", issuer ) && issuer == "ISSUER1" && token.findText( "aud", audience ) &&
                                   audience == AppleReceiptSettings::kAudience && token.findText( "bid", bundle ) && bundle == "com.example.game";
            _bAuthValid = bClaimsOk ? SW_TRUE : SW_FALSE;
        }

        void respondTransaction( const sw::string& id, HTTPServerResponse& outResponse ) const
        {
            sw::string jws;
            if ( id == "3001" )
                jws = _pPKI->makeJWS( makeTransactionPayload( "3001", "com.example.game", "Production", false ) );
            else if ( id == "3002" )
                jws = _pPKI->makeJWS( makeTransactionPayload( "3002", "com.example.game", "Production", true ) );
            else if ( id == "3003" )
                jws = _pPKI->makeJWS( makeTransactionPayload( "3003", "com.other.game", "Production", false ) );
            else if ( id == "3004" )
                jws = _pForeignPKI->makeJWS( makeTransactionPayload( "3004", "com.example.game", "Production", false ) );
            else if ( id == "3005" )
                jws = _pPKI->makeJWS( makeTransactionPayload( "3005", "com.example.game", "Sandbox", false ) );
            else if ( id == "3006" )
                jws = _pPKI->makeJWS( makeTransactionPayload( "9999", "com.example.game", "Production", false ) ); // 다른 거래의 서명본
            else
            {
                outResponse._statusCode = 404;
                return;
            }
            test::setResponseText( outResponse, 200, "{\"signedTransactionInfo\":\"" + jws + "\"}" );
        }

        sw::string makeNotification( const utf8* pType, const utf8* pTransactionID ) const
        {
            const sw::string transaction = _pPKI->makeJWS( makeTransactionPayload( pTransactionID, "com.example.game", "Production", true ) );
            return _pPKI->makeJWS( sw::string( "{\"notificationType\":\"" ) + pType + "\",\"data\":{\"bundleId\":\"com.example.game\",\"environment\":\"Production\"," +
                                   "\"signedTransactionInfo\":\"" + transaction + "\"}}" );
        }

        void respondHistory( const HTTPServerRequest& request, HTTPServerResponse& outResponse )
        {
            JSONDocument body;
            _bHistoryBodyValid       = body.tryParse( request.getBodyText() ) && body.getRoot().get( "notificationType", false ).asString() == "REFUND" &&
                                         body.getRoot().has( "startDate", false ) && body.getRoot().has( "endDate", false )
                                         ? SW_TRUE
                                         : SW_FALSE;
            const HTTPHeader* pToken = request.findQuery( "paginationToken" );
            if ( pToken == nullptr )
                test::setResponseText( outResponse, 200,
                                       "{\"notificationHistory\":[{\"signedPayload\":\"" + makeNotification( "REFUND", "3002" ) + "\"},{\"signedPayload\":\"" +
                                           makeNotification( "DID_RENEW", "3010" ) + "\"}],\"hasMore\":true,\"paginationToken\":\"page-2\"}" );
            else if ( pToken->_value == "page-2" )
                test::setResponseText( outResponse, 200,
                                       "{\"notificationHistory\":[{\"signedPayload\":\"" + makeNotification( "REFUND", "3007" ) + "\"}],\"hasMore\":false}" );
            else
                outResponse._statusCode = 400;
        }
    };

    struct AppleFixture
    {
        TestPKI                _pki;
        TestPKI                _foreignPki;
        FakeAppStoreHandler    _handler;
        test::FakeHTTPSFixture _https;
        AppleReceiptValidator  _validator;

        static AppleReceiptSettings makeSettings( FakeAppStoreHandler& handler, const TestPKI& pki, const test::FakeHTTPSFixture& https, bool bMarkers )
        {
            sw::string   authKeyPem;
            NetPublicKey authPublicKey;
            SW_EXPECT_TRUE( NetSecurity::getProvider().createSigningKeyPair( NetSignatureAlgorithm::EcdsaP256Sha256, authKeyPem, authPublicKey ) );
            handler._authPublicKey = authPublicKey;
            AppleReceiptSettings settings;
            settings._issuerID              = "ISSUER1";
            settings._keyID                 = "KEY1";
            settings._privateKeyPem         = authKeyPem;
            settings._bundleID              = "com.example.game";
            settings._baseURL               = "https://localhost:" + to_string( static_cast<int32>( https._server.getListenPort() ) );
            settings._trustedRootsPem       = pki._rootPem;
            settings._historyPollIntervalMs = 1;
            settings._bRequireAppleMarkers  = bMarkers ? SW_TRUE : SW_FALSE;
            return settings;
        }

        AppleFixture()
            : _pki{ AppleReceiptSettings::kLeafOid, AppleReceiptSettings::kIntermediateOid }
            , _foreignPki{ AppleReceiptSettings::kLeafOid, AppleReceiptSettings::kIntermediateOid }
            , _handler{}
            , _https{ &_handler }
            , _validator{ makeSettings( _handler, _pki, _https, true ), &NetSecurity::getProvider(), &_https._client }
        {
            _handler._pPKI        = &_pki;
            _handler._pForeignPKI = &_foreignPki;
        }

        vector<ReceiptValidationResult> collect( size_t count )
        {
            vector<ReceiptValidationResult> listResult;
            const Deadline                  deadline = Deadline::afterMilliseconds( 10000 );
            while ( listResult.size() < count && deadline.isExpired() == false )
            {
                _https.pump();
                _validator.pollCompletions( listResult );
            }
            return listResult;
        }

        static const ReceiptValidationResult* findByTicket( const vector<ReceiptValidationResult>& listResult, uint64 ticket )
        {
            for ( const ReceiptValidationResult& result : listResult )
            {
                if ( result._ticket == ticket )
                    return &result;
            }
            return nullptr;
        }

        vector<ReceiptRefundNotice> collectRefunds( size_t count, int64 waitMilliseconds )
        {
            vector<ReceiptRefundNotice>     listNotice;
            vector<ReceiptValidationResult> listIgnored;
            const Deadline                  deadline = Deadline::afterMilliseconds( waitMilliseconds );
            while ( listNotice.size() < count && deadline.isExpired() == false )
            {
                _https.pump();
                _validator.pollCompletions( listIgnored );
                _validator.pollRefunds( listNotice );
            }
            return listNotice;
        }
    };

    ReceiptValidationRequest makeRequest( const utf8* pPayload, uint64 ticket )
    {
        ReceiptValidationRequest request;
        request._storeName = "apple";
        request._payload   = pPayload;
        request._accountID = 42;
        request._ticket    = ticket;
        return request;
    }
} // namespace

SW_TEST_CASE( AppleReceiptTest, SignedTransactionsAreVerifiedAgainstTheAppleRootAndMapped )
{
    AppleFixture fixture;
    fixture._validator.submitValidation( makeRequest( "3001", 1 ) );
    fixture._validator.submitValidation( makeRequest( "3002", 2 ) );
    fixture._validator.submitValidation( makeRequest( "3005", 3 ) );
    fixture._validator.submitValidation( makeRequest( "4040", 4 ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "the provider logs the transactions that do not verify" );
        fixture._validator.submitValidation( makeRequest( "3003", 5 ) );
        fixture._validator.submitValidation( makeRequest( "3004", 6 ) );
        fixture._validator.submitValidation( makeRequest( "3006", 7 ) );
        const vector<ReceiptValidationResult> listResult = fixture.collect( 7 );
        SW_ASSERT_EQUAL( listResult.size(), size_t( 7 ) );
        SW_EXPECT_TRUE( fixture._handler._bAuthValid == SW_TRUE ); // ES256 · kid · iss · aud · bid
        const ReceiptValidationResult* pValid = AppleFixture::findByTicket( listResult, 1 );
        SW_ASSERT_TRUE( pValid != nullptr );
        SW_EXPECT_TRUE( pValid->_status == ReceiptStatus::Valid );
        SW_EXPECT_EQUAL( pValid->_transactionID, sw::string( "3001" ) );
        SW_EXPECT_EQUAL( pValid->_productID, sw::string( "gems100" ) );
        SW_EXPECT_EQUAL( pValid->_purchaseTimeMs, kPurchaseMs );
        SW_EXPECT_TRUE( pValid->_bSandbox == SW_FALSE );
        SW_EXPECT_EQUAL( pValid->_accountID, uint64( 42 ) );
        SW_EXPECT_TRUE( AppleFixture::findByTicket( listResult, 2 )->_status == ReceiptStatus::Refunded ); // revocationDate
        SW_EXPECT_TRUE( AppleFixture::findByTicket( listResult, 3 )->_status == ReceiptStatus::Valid );
        SW_EXPECT_TRUE( AppleFixture::findByTicket( listResult, 3 )->_bSandbox == SW_TRUE );
        SW_EXPECT_TRUE( AppleFixture::findByTicket( listResult, 4 )->_status == ReceiptStatus::Invalid ); // 모르는 거래(404)
        SW_EXPECT_TRUE( AppleFixture::findByTicket( listResult, 5 )->_status == ReceiptStatus::Invalid ); // 다른 앱의 거래
        SW_EXPECT_TRUE( AppleFixture::findByTicket( listResult, 6 )->_status == ReceiptStatus::Invalid ); // 애플 루트로 이어지지 않는 사슬
        SW_EXPECT_TRUE( AppleFixture::findByTicket( listResult, 7 )->_status == ReceiptStatus::Invalid ); // 요청한 거래가 아닌 서명본
    }
}

SW_TEST_CASE( AppleReceiptTest, MalformedReceiptsAreRefusedWithoutAskingApple )
{
    AppleFixture fixture;
    fixture._validator.submitValidation( makeRequest( "3001/../x", 1 ) );
    fixture._validator.submitValidation( makeRequest( "", 2 ) );
    fixture._validator.submitValidation( makeRequest( "123456789012345678901", 3 ) );
    const vector<ReceiptValidationResult> listResult = fixture.collect( 3 );
    SW_ASSERT_EQUAL( listResult.size(), size_t( 3 ) );
    for ( const ReceiptValidationResult& result : listResult )
    {
        SW_EXPECT_TRUE( result._status == ReceiptStatus::Invalid );
    }
    SW_EXPECT_EQUAL( fixture._handler._transactionHits, 0 );
}

SW_TEST_CASE( AppleReceiptTest, AnAuthorizationApplesRefusesIsRetriedAndTheTokenIsMadeAgain )
{
    AppleFixture fixture;
    fixture._handler._authFailuresLeft = 1;
    {
        SW_TEST_DEFENSIVE_SCOPE( "the provider logs that apple rejected its token" );
        fixture._validator.submitValidation( makeRequest( "3001", 1 ) );
        const vector<ReceiptValidationResult> listFirst = fixture.collect( 1 );
        SW_ASSERT_EQUAL( listFirst.size(), size_t( 1 ) );
        SW_EXPECT_TRUE( listFirst[0]._status == ReceiptStatus::Retry ); // 우리 설정 문제 — 플레이어 탓이 아니다
    }
    fixture._validator.submitValidation( makeRequest( "3001", 2 ) );
    const vector<ReceiptValidationResult> listSecond = fixture.collect( 1 );
    SW_ASSERT_EQUAL( listSecond.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listSecond[0]._status == ReceiptStatus::Valid );
}

SW_TEST_CASE( AppleReceiptTest, TheNotificationHistoryNotifiesEachRefundOnceAcrossPages )
{
    AppleFixture                      fixture;
    const vector<ReceiptRefundNotice> listNotice = fixture.collectRefunds( 2, 10000 );
    SW_ASSERT_EQUAL( listNotice.size(), size_t( 2 ) ); // 첫 쪽의 REFUND(3002), 둘째 쪽의 REFUND(3007) — DID_RENEW 는 거른다
    SW_EXPECT_EQUAL( listNotice[0]._transactionID, sw::string( "3002" ) );
    SW_EXPECT_EQUAL( listNotice[1]._transactionID, sw::string( "3007" ) );
    SW_EXPECT_EQUAL( listNotice[0]._storeName, sw::string( "apple" ) );
    SW_EXPECT_EQUAL( listNotice[0]._refundedMs, kRefundMs );
    SW_EXPECT_TRUE( fixture._handler._bHistoryBodyValid == SW_TRUE );

    // 조회 주기(1 ms)가 지나 다시 읽어도 같은 거래를 다시 알리지 않는다.
    const int32                       historyHits = fixture._handler._historyHits;
    const vector<ReceiptRefundNotice> listAgain   = fixture.collectRefunds( 1, 300 );
    SW_EXPECT_TRUE( fixture._handler._historyHits > historyHits );
    SW_EXPECT_EQUAL( listAgain.size(), size_t( 0 ) );
}

SW_TEST_CASE( AppleReceiptTest, MarkersAreRequiredUnlessTheSettingTurnsThemOff )
{
    TestPKI                plainPki( "", "" ); // 애플 표지가 없는 사슬
    FakeAppStoreHandler    handler;
    test::FakeHTTPSFixture https( &handler );
    AppleReceiptValidator  strict( AppleFixture::makeSettings( handler, plainPki, https, true ), &NetSecurity::getProvider(), &https._client );
    handler._pPKI        = &plainPki;
    handler._pForeignPKI = &plainPki;
    {
        SW_TEST_DEFENSIVE_SCOPE( "the strict provider logs the transaction without the Apple markers" );
        strict.submitValidation( makeRequest( "3001", 1 ) );
        vector<ReceiptValidationResult> listStrict;
        const Deadline                  deadline = Deadline::afterMilliseconds( 10000 );
        while ( listStrict.empty() && deadline.isExpired() == false )
        {
            https.pump();
            strict.pollCompletions( listStrict );
        }
        SW_ASSERT_EQUAL( listStrict.size(), size_t( 1 ) );
        SW_EXPECT_TRUE( listStrict[0]._status == ReceiptStatus::Invalid );
    }
    strict.shutdown();
    AppleReceiptValidator lenient( AppleFixture::makeSettings( handler, plainPki, https, false ), &NetSecurity::getProvider(), &https._client );
    lenient.submitValidation( makeRequest( "3001", 2 ) );
    vector<ReceiptValidationResult> listLenient;
    const Deadline                  deadline = Deadline::afterMilliseconds( 10000 );
    while ( listLenient.empty() && deadline.isExpired() == false )
    {
        https.pump();
        lenient.pollCompletions( listLenient );
    }
    SW_ASSERT_EQUAL( listLenient.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listLenient[0]._status == ReceiptStatus::Valid );
}

SW_TEST_CASE( NetSecurityCertificateChainTest, ChainsAreVerifiedAgainstTheRootsTimeAndMarkers )
{
    INetSecurityProvider& security = NetSecurity::getProvider();
    TestPKI               pki( "1.2.840.113635.100.6.11.1", "1.2.840.113635.100.6.2.1" );
    TestPKI               other( "1.2.840.113635.100.6.11.1", "1.2.840.113635.100.6.2.1" );
    const int64           nowSeconds = WallClock::nowUnixMilliseconds() / 1000;
    NetPublicKey          leafKey;
    SW_EXPECT_TRUE( security.verifyCertificateChain( pki.makeChainDer(), pki._rootPem, nowSeconds, "1.2.840.113635.100.6.11.1", "1.2.840.113635.100.6.2.1", leafKey ) );
    SW_EXPECT_TRUE( leafKey._algorithm == NetSignatureAlgorithm::EcdsaP256Sha256 );
    SW_EXPECT_EQUAL( leafKey._x.size(), size_t( 32 ) );
    SW_EXPECT_TRUE( security.verifyCertificateChain( pki.makeChainDer(), pki._rootPem, nowSeconds, "", "", leafKey ) ); // 표지를 요구하지 않을 때

    // 다른 루트 · 만료 뒤 · 표지 없음 · 변조 · 빈 사슬 · 루트가 없는 설정은 거절한다.
    SW_EXPECT_FALSE( security.verifyCertificateChain( pki.makeChainDer(), other._rootPem, nowSeconds, "", "", leafKey ) );
    SW_EXPECT_FALSE( security.verifyCertificateChain( pki.makeChainDer(), pki._rootPem, nowSeconds + 31 * 24 * 60 * 60, "", "", leafKey ) );
    SW_EXPECT_FALSE( security.verifyCertificateChain( pki.makeChainDer(), pki._rootPem, nowSeconds - 3600, "", "", leafKey ) ); // 발급 전
    SW_EXPECT_FALSE( security.verifyCertificateChain( pki.makeChainDer(), pki._rootPem, nowSeconds, "1.2.840.113635.100.6.11.99", "", leafKey ) );
    SW_EXPECT_FALSE( security.verifyCertificateChain( pki.makeChainDer(), pki._rootPem, nowSeconds, "", "1.2.840.113635.100.6.2.99", leafKey ) );
    vector<vector<uint8>> tampered = pki.makeChainDer();
    tampered[0][tampered[0].size() / 2] ^= 0x01;
    SW_EXPECT_FALSE( security.verifyCertificateChain( tampered, pki._rootPem, nowSeconds, "", "", leafKey ) );
    SW_EXPECT_FALSE( security.verifyCertificateChain( vector<vector<uint8>>{}, pki._rootPem, nowSeconds, "", "", leafKey ) );
    SW_EXPECT_FALSE( security.verifyCertificateChain( pki.makeChainDer(), "", nowSeconds, "", "", leafKey ) );
    vector<vector<uint8>> withoutIssuer = pki.makeChainDer();
    withoutIssuer.erase( withoutIssuer.begin() + 1 ); // 중간 인증서를 빼면 이어지지 않는다
    withoutIssuer.pop_back();
    SW_EXPECT_FALSE( security.verifyCertificateChain( withoutIssuer, pki._rootPem, nowSeconds, "", "", leafKey ) );
}

SW_TEST_CASE( AppleReceiptTest, TheProviderSetReadsAppleSettingsStrictly )
{
    vector<ReceiptProviderConfig> listSettings;
    sw::string                    error;
    SW_ASSERT_TRUE( ReceiptProviderSet::readSettings(
        "{\"providers\":[{\"kind\":\"apple\",\"issuerId\":\"I\",\"keyId\":\"K\",\"bundleId\":\"com.example.game\",\"signingKeySecretEnvironment\":\"SW_TEST_APPLE_KEY\","
        "\"rootCertificateFile\":\"Config/AppleRootCA-G3.pem\",\"sandbox\":true,\"historyPollSeconds\":600}]}",
        listSettings, error ) );
    SW_ASSERT_EQUAL( listSettings.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( listSettings[0]._bundleID, sw::string( "com.example.game" ) );
    SW_EXPECT_EQUAL( listSettings[0]._pollSeconds, int64( 600 ) );
    SW_EXPECT_TRUE( listSettings[0]._bSandbox == SW_TRUE );
    vector<ReceiptProviderConfig> listBad;
    SW_EXPECT_FALSE( ReceiptProviderSet::readSettings( "{\"providers\":[{\"kind\":\"apple\",\"issuerId\":\"I\",\"keyId\":\"K\"}]}", listBad, error ) ); // 번들 · 키 · 루트가 없다
    SW_EXPECT_TRUE( AppleReceiptValidator::isTransactionID( "2000000123456789" ) );
    SW_EXPECT_FALSE( AppleReceiptValidator::isTransactionID( "20a" ) );
}
