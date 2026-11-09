// ReceiptProviderEndToEndTest — 설정이 고른 실제 영수증 제공자를 로컬 가짜 끝점까지 잇는다. 제공자 파일 → 환경 변수 비밀 → 실제 HTTP 클라이언트 · TLS(자체 서명 인증서를 신뢰 파일로) · OpenSSL 서명 → 가짜 스토어 → 결과.
// 키는 케이스마다 새로 만들고 실제 계정은 쓰지 않는다. 응답 해석의 세부는 `TestSteamReceiptValidator.cpp` · `TestStoreReceiptValidators.cpp` 가 본다. 경제 키트 조립은 옵션으로 제공자를 세워 환불 조회가 끝점에 닿는지 본다.
// 지급(서비스 → 원장)은 `TestEconomyReceipts.cpp` 가 본다(nogpu).
#include "pch.h"

#include "Core/Encoding/Base64Util.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/StreamTypes.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"
#include "Core/Time/WallClock.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처가 엔진 서비스(`AssetPaths`)를 직접 쓴다
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/ServerConfig.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Apple/AppleReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptProviderSet.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPServer.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Provider/OnlineProviderFile.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Server/Tests/OnlineEndpointTestUtil.h"
#include "GameFramework/Runtime/Online/Server/Tests/OnlineRuntimeTestUtil.h"
#include "GameFramework/Runtime/Online/Service/Server/Tests/OnlineHostTestUtil.h"

using namespace sw;

namespace
{
    constexpr uint64      kBuyer         = 42;
    constexpr const utf8* kBundle        = "com.x.game";
    constexpr const utf8* kSteamKey      = "steamkey";
    constexpr const utf8* kSteamEnv      = "SW_TEST_RECEIPT_STEAM_KEY";
    constexpr const utf8* kGoogleEnv     = "SW_TEST_RECEIPT_GOOGLE_ACCOUNT";
    constexpr const utf8* kAppleEnv      = "SW_TEST_RECEIPT_APPLE_KEY";
    constexpr const utf8* kSteamOrder    = "1234";
    constexpr const utf8* kAppleOrder    = "5678";
    constexpr const utf8* kGooglePayload = "gem100|TOKEN";

    struct ReceiptEndpointTestUtil
    {
        static int64 getNowMs() { return WallClock::nowUnixMilliseconds().count(); }

        /** @brief PEM 의 머리 · 꼬리 줄을 뗀 본문(표준 Base64 글)입니다. JWS 의 `x5c` 원소가 이 모양입니다. */
        static string extractPemBody( string_view pem )
        {
            string body;
            size_t position = 0;
            while ( position < pem.size() )
            {
                const size_t lineEnd = pem.find( '\n', position );
                const size_t end     = lineEnd == string_view::npos ? pem.size() : lineEnd;
                string_view  line    = pem.substr( position, end - position );
                position             = end + 1;
                if ( line.empty() == false && line.back() == '\r' )
                    line.remove_suffix( 1 );
                if ( StringUtil::startsWith( line, "-----" ) == false )
                    body.append( line );
            }
            return body;
        }

        static string encodeURL( string_view text ) { return Base64Util::encodeURL( reinterpret_cast<const uint8*>( text.data() ), text.size() ); }
    };

    /**
     * @brief 가짜 스토어(스팀 · 구글 · 애플을 한 HTTPS 서버로)입니다. `update` 를 부른 스레드에서 답합니다.
     * @details 인증서는 케이스마다 만든 자체 서명이고 그 PEM 이 제공자의 신뢰 파일입니다 - 운영의 CA 번들과 같은 길입니다.
     */
    struct FakeStoreEndpoint
    {
        unique_ptr<ITLSContext> _tlsContext{};
        HTTPServer              _server{};
        string                  _trustFile{};
        string                  _appleTransaction{};
        vector<string>          _listSteamKey{};
        vector<string>          _listAppleAuthorization{};
        string                  _googleAuthorization{};
        int32                   _tokenRequestCount{ 0 };
        int32                   _acknowledgeCount{ 0 };
        int32                   _voidedRequestCount{ 0 };

        void start()
        {
            INetSecurityProvider& security = game::getSubsystem<INetSecurityProvider>();
            TLSContextConfig      settings;
            SW_REQUIRE_TRUE( security.createSelfSignedCertificate( "localhost", 1, settings._certificatePem, settings._privateKeyPem ) );
            _trustFile                                = test::writeScratchText( "store-trust.pem", settings._certificatePem );
            expected<unique_ptr<ITLSContext>> context = security.createTLSContext( settings );
            SW_REQUIRE_TRUE_MSG( context.has_value(), context.has_value() ? "" : string( context.error().detail() ).c_str() );
            _tlsContext = std::move( context ).value();
            StreamTransportConfig transportSettings;
            transportSettings._ioThreadCount = 0;
            HTTPServerConfig serverSettings;
            serverSettings._pTLSContext = _tlsContext.get();
            SW_REQUIRE_TRUE( _server.initialize( StreamTransportFactory::createPlatformTransport(), transportSettings, serverSettings, HTTPRequestFunction::create<&FakeStoreEndpoint::onRequest>( this ) ) );
        }

        string makeBaseURL() { return "https://localhost:" + to_string( static_cast<int32>( _server.getListenPort() ) ); }

        void onRequest( const HTTPServerRequest& request, HTTPServerResponse& outResponse )
        {
            string body;
            if ( request._path == "/token" )
            {
                ++_tokenRequestCount;
                SW_EXPECT_TRUE( request.getBodyText().find( "assertion=" ) != string_view::npos );
                body = R"({"access_token":"google-token","expires_in":3600})";
            }
            else if ( request._path.find( "QueryTxn" ) != string::npos )
            {
                const HTTPHeader* const pKey = request.findQuery( "key" );
                _listSteamKey.push_back( pKey != nullptr ? pKey->_value : string() );
                body = string( R"({"response":{"result":"OK","params":{"orderid":")" ) + kSteamOrder + R"(","status":"Succeeded","time":"2023-11-14T22:13:20Z","items":[{"itemid":"100","qty":1}]}}})";
            }
            else if ( request._path.find( "/inApps/v1/transactions/" ) != string::npos )
            {
                const HTTPHeader* const pAuthorization = request.findHeader( "Authorization" );
                _listAppleAuthorization.push_back( pAuthorization != nullptr ? pAuthorization->_value : string() );
                body = R"({"signedTransactionInfo":")" + _appleTransaction + R"("})";
            }
            else if ( request._path.find( "voidedpurchases" ) != string::npos )
            {
                ++_voidedRequestCount;
                body = "{}";
            }
            else if ( request._path.find( ":acknowledge" ) != string::npos )
            {
                ++_acknowledgeCount;
            }
            else
            {
                const HTTPHeader* const pAuthorization = request.findHeader( "Authorization" );
                _googleAuthorization                   = pAuthorization != nullptr ? pAuthorization->_value : string();
                body                                   = R"({"orderId":"GPA.1","purchaseState":0,"acknowledgementState":0,"purchaseTimeMillis":"1700000000000","obfuscatedExternalAccountId":"tok"})";
            }
            outResponse._bodyBytes.assign( body.begin(), body.end() );
        }
    };

    /** @brief 스토어마다 새 키와 비밀 환경 변수, 제공자 파일입니다. 키는 끝나면 사라집니다. */
    struct ReceiptCredentials
    {
        string                                      _appleRootFile{};
        string                                      _appleLeafKey{};
        vector<string>                              _listChainPem{};
        string                                      _googleAccountJSON{};
        string                                      _appleSigningKey{};
        unique_ptr<test::ScopedEnvironmentVariable> _steamEnv{};
        unique_ptr<test::ScopedEnvironmentVariable> _googleEnv{};
        unique_ptr<test::ScopedEnvironmentVariable> _appleEnv{};

        void make()
        {
            INetSecurityProvider& security = game::getSubsystem<INetSecurityProvider>();
            string                googleKey;
            NetPublicKey          publicKey;
            SW_REQUIRE_TRUE( security.createSigningKeyPair( NetSignatureAlgorithm::RsaPkcs1SHA256, googleKey, publicKey ) );
            _googleAccountJSON = R"({"project_id":"proj","client_email":"a@b","private_key":")" + test::escapeJSONText( googleKey ) + R"("})";
            SW_REQUIRE_TRUE( security.createSigningKeyPair( NetSignatureAlgorithm::EcdsaP256Sha256, _appleSigningKey, publicKey ) );
            SW_REQUIRE_TRUE( security.createTestCertificateChain( 1, AppleReceiptSettings::kLeafOid, AppleReceiptSettings::kIntermediateOid, _listChainPem, _appleLeafKey ) );
            SW_REQUIRE_EQUAL( size_t( 3 ), _listChainPem.size() );
            _appleRootFile = test::writeScratchText( "apple-root.pem", _listChainPem[2] );
            _steamEnv      = make_unique<test::ScopedEnvironmentVariable>( kSteamEnv, kSteamKey );
            _googleEnv     = make_unique<test::ScopedEnvironmentVariable>( kGoogleEnv, _googleAccountJSON );
            _appleEnv      = make_unique<test::ScopedEnvironmentVariable>( kAppleEnv, _appleSigningKey );
        }

        /** @brief 애플이 주는 것과 같은 서명 거래(JWS)를 실제 키와 사슬로 만듭니다. */
        string makeAppleTransaction() const
        {
            string header = R"({"alg":"ES256","x5c":[")" + ReceiptEndpointTestUtil::extractPemBody( _listChainPem[0] ) + R"(",")" + ReceiptEndpointTestUtil::extractPemBody( _listChainPem[1] ) + R"(",")" +
                            ReceiptEndpointTestUtil::extractPemBody( _listChainPem[2] ) + R"("]})";
            StringBuilder<constant::buffer::kMax256> payloadBuilder;
            payloadBuilder.appendFormat( R"({"transactionId":"%#","productId":"gem100","bundleId":"%#","environment":"Production","appAccountToken":"tok","purchaseDate":1700000000000})", kAppleOrder, kBundle );
            string        payload{ payloadBuilder.view() };
            const string  signingInput = ReceiptEndpointTestUtil::encodeURL( header ) + "." + ReceiptEndpointTestUtil::encodeURL( payload );
            vector<uint8> signature;
            SW_EXPECT_TRUE( game::getSubsystem<INetSecurityProvider>().signData( NetSignatureAlgorithm::EcdsaP256Sha256, _appleLeafKey, reinterpret_cast<const uint8*>( signingInput.data() ),
                                                                                 static_cast<int32>( signingInput.size() ), signature ) );
            return signingInput + "." + Base64Util::encodeURL( signature.data(), signature.size() );
        }

        string writeProvidersFile( const string& baseURL ) const
        {
            StringBuilder<constant::buffer::kMax256> textBuilder;
            textBuilder.appendFormat( R"({"providers":[
                {"kind":"steam","appId":"480","keySecretEnvironment":"%#","baseUrl":"%#","buyerBinding":"linkedLogin","buyerLoginProvider":"steam"},
                {"kind":"google","packageName":"%#","serviceAccountEnvironment":"%#","baseUrl":"%#","tokenUrl":"%#/token","buyerBinding":"accountToken"},
                {"kind":"apple","issuerId":"issuer","keyId":"key","bundleId":"%#","signingKeySecretEnvironment":"%#","rootCertificateFile":"%#","baseUrl":"%#","buyerBinding":"accountToken"}
            ]})",
                                      kSteamEnv, baseURL, kBundle, kGoogleEnv, baseURL, baseURL, kBundle, kAppleEnv, _appleRootFile, baseURL );
            const string text{ textBuilder.view() };
            return test::writeScratchText( "receipts.json", text );
        }
    };

    /** @brief 제공자 파일 → 제공자 묶음 → 레지스트리 → 가짜 스토어입니다. 선언 순서가 수명이다 - 레지스트리가 묶음보다, 묶음이 끝점보다 먼저 내려간다. */
    struct ReceiptEndToEndTest
    {
        FakeStoreEndpoint        _endpoint{};
        ReceiptCredentials       _secrets{};
        SystemMonotonicClock     _clock{};
        ReceiptProviderSet       _providers{};
        ReceiptValidatorRegistry _registry{};

        ReceiptEndToEndTest()
        {
            _endpoint.start();
            _secrets.make();
            _endpoint._appleTransaction = _secrets.makeAppleTransaction();
        }

        ~ReceiptEndToEndTest()
        {
            _registry.shutdown();
            _providers.shutdown();
        }

        /** @brief 운영의 경제 키트 조립과 같은 순서로 파일에서 제공자를 세웁니다. */
        [[nodiscard]] expected<void> assemble()
        {
            const optional<AbsolutePath>  providersFile = AbsolutePath::tryMake( _secrets.writeProvidersFile( _endpoint.makeBaseURL() ) );
            const optional<AbsolutePath>  trustFile     = AbsolutePath::tryMake( _endpoint._trustFile );
            vector<ReceiptProviderConfig> listConfig;
            if ( expected<void> read = OnlineProviderFile::read<ReceiptProviderConfig>( providersFile, trustFile, "receiptProvidersFile", "receiptTrustFile", &ReceiptProviderSet::readEntry, listConfig );
                 read.has_value() == false )
                return read;
            if ( expected<void> providers = _providers.initialize( listConfig, OnlineProviderFile::getTrustText( trustFile ), _clock ); providers.has_value() == false )
                return providers;
            for ( int32 index = 0; index < _providers.getProviderCount(); ++index )
            {
                SW_EXPECT_TRUE( _registry.registerValidator( _providers.getProvider( index ) ) );
            }
            return {};
        }

        void pump()
        {
            _registry.update( UnixMilliseconds{ ReceiptEndpointTestUtil::getNowMs() } );
            _endpoint._server.update();
        }

        ReceiptValidationResult validate( string_view storeName, string_view payload )
        {
            const uint64 ticket = _registry.submitValidation( storeName, payload, kBuyer, UnixMilliseconds{ ReceiptEndpointTestUtil::getNowMs() } );
            SW_EXPECT_TRUE( ticket != 0 );
            vector<ReceiptValidationResult> listResult;
            const bool                      bDone = test::pumpUntil( [this]()
            { pump(); }, [this, &listResult]()
            {
                _registry.pollCompletions( listResult );
                return listResult.empty() == false;
            } );
            SW_EXPECT_TRUE( bDone );
            return listResult.empty() ? ReceiptValidationResult{} : listResult.front();
        }
    };
} // namespace

/** @brief [ReceiptEndToEndTest] 스팀: 파일이 고른 제공자가 환경 변수의 키로 TLS 끝점에 묻고 주문이 지급 가능으로 돌아온다 */
SW_TEST_CASE_F( ReceiptEndToEndTest, SteamReachesTheEndpoint )
{
    {
        const expected<void> assembled = assemble();
        SW_REQUIRE_TRUE_MSG( assembled.has_value(), assembled.has_value() ? string_view{} : assembled.error().message() );
    }
    SW_EXPECT_EQUAL( 3, _providers.getProviderCount() );
    const ReceiptValidationResult result = validate( "steam", kSteamOrder );
    SW_EXPECT_TRUE( result._status == ReceiptStatus::Valid );
    SW_EXPECT_EQUAL( string( kSteamOrder ), result._transactionID );
    SW_REQUIRE_EQUAL( size_t( 1 ), _endpoint._listSteamKey.size() );
    SW_EXPECT_EQUAL( string( kSteamKey ), _endpoint._listSteamKey[0] );
}

/** @brief [ReceiptEndToEndTest] 구글: 환경 변수의 서비스 계정을 OpenSSL 로 서명해 토큰을 받고, 구매를 확인한 뒤 지급 알림까지 끝점에 닿는다 */
SW_TEST_CASE_F( ReceiptEndToEndTest, GoogleReachesTheEndpoint )
{
    {
        const expected<void> assembled = assemble();
        SW_REQUIRE_TRUE_MSG( assembled.has_value(), assembled.has_value() ? string_view{} : assembled.error().message() );
    }
    const ReceiptValidationResult result = validate( "google", kGooglePayload );
    SW_EXPECT_TRUE( result._status == ReceiptStatus::Valid );
    SW_EXPECT_EQUAL( string( "GPA.1" ), result._transactionID );
    SW_EXPECT_EQUAL( string( "tok" ), result._storeBuyerID );
    SW_EXPECT_EQUAL( 1, _endpoint._tokenRequestCount );
    SW_EXPECT_EQUAL( string( "Bearer google-token" ), _endpoint._googleAuthorization );

    ReceiptDeliveryTask task;
    task._storeName   = "google";
    task._recordKey   = "rcpt.google/rec";
    task._deliveryKey = result._deliveryKey;
    SW_REQUIRE_TRUE( _registry.confirmDelivery( task, UnixMilliseconds{ ReceiptEndpointTestUtil::getNowMs() } ) );
    vector<ReceiptDeliveryResult> listOutcome;
    SW_REQUIRE_TRUE( test::pumpUntil( [this]()
    { pump(); }, [this, &listOutcome]()
    {
        _registry.pollDeliveryOutcomes( listOutcome );
        return listOutcome.empty() == false;
    } ) );
    SW_EXPECT_TRUE( listOutcome[0]._status == ReceiptDeliveryStatus::Confirmed );
    SW_EXPECT_EQUAL( 1, _endpoint._acknowledgeCount );
}

/** @brief [ReceiptEndToEndTest] 애플: 환경 변수의 키로 서명한 JWT 를 보내고, 답의 JWS 를 실제 사슬 · 표지 확인으로 설정의 루트 파일까지 잇는다 */
SW_TEST_CASE_F( ReceiptEndToEndTest, AppleReachesTheEndpointAndVerifiesTheChain )
{
    {
        const expected<void> assembled = assemble();
        SW_REQUIRE_TRUE_MSG( assembled.has_value(), assembled.has_value() ? string_view{} : assembled.error().message() );
    }
    const ReceiptValidationResult result = validate( "apple", kAppleOrder );
    SW_EXPECT_TRUE_MSG( result._status == ReceiptStatus::Valid, result._failureText.c_str() );
    SW_EXPECT_EQUAL( string( kAppleOrder ), result._transactionID );
    SW_EXPECT_EQUAL( string( "gem100" ), result._productID );
    SW_EXPECT_EQUAL( string( "tok" ), result._storeBuyerID );
    SW_REQUIRE_EQUAL( size_t( 1 ), _endpoint._listAppleAuthorization.size() );
    SW_EXPECT_TRUE( StringUtil::startsWith( _endpoint._listAppleAuthorization[0], "Bearer " ) );
}

/** @brief [ReceiptEndToEndTest] 비밀 환경 변수가 없으면 가짜로 대신 뜨지 않고 묶음 전체가 실패한다 */
SW_TEST_CASE_F( ReceiptEndToEndTest, MissingSecretStopsAssembly )
{
    SW_EXPECT_LOG( "Server.SecretNotSet" );
    _secrets._steamEnv.reset();
    const auto failed1 = assemble();
    SW_REQUIRE_FALSE( failed1.has_value() );
    const sw::string error1 = sw::string( failed1.error().message() ) + " " + sw::string( failed1.error().detail() );
    SW_EXPECT_TRUE( error1.find( kSteamEnv ) != string::npos );
    SW_EXPECT_EQUAL( 0, _providers.getProviderCount() );
}

/** @brief [ReceiptEndToEndTest] 경제 키트 조립: 설정 옵션이 고른 제공자로 서버가 서고, 서비스가 넘긴 조회 구간으로 구글 환불 조회가 끝점에 닿는다. 신뢰 파일이 빠지면 서지 않는다 */
SW_TEST_CASE( ReceiptKitAssemblyTest, KitOptionsSelectTheProviders )
{
    test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "e2e.currencies.xml" ), R"(<CurrencyCatalog><Currency id="cur.gold"/></CurrencyCatalog>)" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "e2e.offers.xml" ), R"(<OfferCatalog><Offer id="pack"><Price currency="cur.gold" amount="1"/><Grant asset="item.hat" amount="1"/></Offer></OfferCatalog>)" ) );

    FakeStoreEndpoint endpoint;
    endpoint.start();
    INetSecurityProvider& security = game::getSubsystem<INetSecurityProvider>();
    string                googleKey;
    NetPublicKey          publicKey;
    SW_REQUIRE_TRUE( security.createSigningKeyPair( NetSignatureAlgorithm::RsaPkcs1SHA256, googleKey, publicKey ) );
    const test::ScopedEnvironmentVariable    googleEnv{ kGoogleEnv, R"({"project_id":"proj","client_email":"a@b","private_key":")" + test::escapeJSONText( googleKey ) + R"("})" };
    const string                             baseURL = endpoint.makeBaseURL();
    StringBuilder<constant::buffer::kMax256> textBuilder;
    textBuilder.appendFormat( R"({"providers":[{"kind":"google","packageName":"%#","serviceAccountEnvironment":"%#","baseUrl":"%#","tokenUrl":"%#/token","buyerBinding":"accountToken"}]})", kBundle, kGoogleEnv, baseURL, baseURL );
    const string providersFile =
        test::writeScratchText( "kit-receipts.json", string( textBuilder.view() ) );

    constexpr const utf8* kServerJSON    = R"({ "_listStore": [ { "_name": "local", "_driver": "memory" } ], "_listCache": [ { "_name": "local", "_driver": "memory" } ] })";
    const auto            makeOnlineJSON = [&providersFile]( string_view trustOption )
    {
        StringBuilder<constant::buffer::kMax256> resultBuilder;
        resultBuilder.appendFormat( R"({ "_storeName": "local", "_cacheName": "local", "_listKit": [ { "_name": "economy", "_settings": {
            "_currencyCatalog": "%#/e2e.currencies.xml",
            "_offerCatalog": "%#/e2e.offers.xml",
            "_receiptProvidersFile": "%#"%# } } ] })",
                                    test::ScopedScratchDomain::kDomain, test::ScopedScratchDomain::kDomain, providersFile, trustOption );
        return string( resultBuilder.view() );
    };

    {
        test::OnlineRuntimeFixture noTrust;
        const auto                 failed1 = noTrust.fromConfig( kServerJSON, makeOnlineJSON( "" ), ReceiptEndpointTestUtil::getNowMs() );
        SW_REQUIRE_FALSE( failed1.has_value() );
        const sw::string error1 = sw::string( failed1.error().message() ) + " " + sw::string( failed1.error().detail() );
        SW_EXPECT_TRUE( error1.find( "receiptTrustFile" ) != string::npos );
    }

    StringBuilder<constant::buffer::kMax256> trustOptionBuilder;
    trustOptionBuilder.appendFormat( R"(, "_receiptTrustFile": "%#")", endpoint._trustFile );
    const string               trustOption{ trustOptionBuilder.view() };
    test::OnlineRuntimeFixture rig;
    {
        const expected<void> started = rig.fromConfig( kServerJSON, makeOnlineJSON( trustOption ), ReceiptEndpointTestUtil::getNowMs() );
        SW_REQUIRE_TRUE_MSG( started.has_value(), started.has_value() ? string_view{} : started.error().message() );
    }
    SW_REQUIRE_TRUE( test::pumpUntil( [&rig, &endpoint]()
    {
        rig.updateAll( ReceiptEndpointTestUtil::getNowMs(), 1 );
        endpoint._server.update();
    }, [&endpoint]()
    { return endpoint._voidedRequestCount > 0; } ) );
    SW_EXPECT_EQUAL( 1, endpoint._tokenRequestCount );
}

/** @brief [ReceiptKitAssemblyTest] 저장소에 든 견본(`Shooter3D.external.*`)이 같은 읽기를 지난다 - 견본이 코드와 어긋나 낡지 않게 한다 */
SW_TEST_CASE( ReceiptKitAssemblyTest, ExternalSampleConfigReads )
{
    ServerConfig       serverConfig;
    OnlineServerConfig onlineConfig;
    SW_REQUIRE_TRUE( ConfigManager::readConfigFileInto( "Config/Server/Online/Shooter3D.external.json", serverConfig ) == ConfigReadResult::Loaded );
    SW_REQUIRE_TRUE( ConfigManager::readConfigFileInto( "Config/Server/Online/Shooter3D.external.online.json", onlineConfig ) == ConfigReadResult::Loaded );
    SW_REQUIRE_EQUAL( size_t( 1 ), serverConfig._listStore.size() );
    SW_EXPECT_EQUAL( string( "postgres" ), serverConfig._listStore[0]._driver );
    SW_REQUIRE_EQUAL( size_t( 1 ), serverConfig._listCache.size() );
    SW_EXPECT_EQUAL( string( "resp" ), serverConfig._listCache[0]._driver );
    SW_EXPECT_EQUAL( serverConfig._listStore[0]._name, onlineConfig._storeName );
    SW_EXPECT_EQUAL( serverConfig._listCache[0]._name, onlineConfig._cacheName );

    const optional<AbsolutePath> providersFile = AbsolutePath::tryResolve( "Config/Server/Online/Providers/Shooter3D.receipts.json", ProcessRoots::get()._project );
    SW_REQUIRE_TRUE( providersFile.has_value() );
    vector<ReceiptProviderConfig> listConfig;
    const expected<void>          read = OnlineProviderFile::read<ReceiptProviderConfig>( providersFile, sw::nullopt, "receiptProvidersFile", "receiptTrustFile", &ReceiptProviderSet::readEntry, listConfig );
    SW_REQUIRE_TRUE_MSG( read.has_value(), read.has_value() ? string_view{} : read.error().message() );
    SW_EXPECT_EQUAL( size_t( 3 ), listConfig.size() );
}
