// 경제 서비스(submitCall 입구, 메모리 저장소 — 결정적) — 응답은 저장소 완료에서만, 같은 키 구매는 한 번 차감, 로그인 없음 · 멱등 키 없음 거절과 지표,
// 영수증은 검증 → 지급.
#include "pch.h"

#include "Engine/Observability/MetricRegistry.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyService.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Fake/FakeReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Catalog/CurrencyCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Catalog/OfferCatalog.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct EconomyServiceReplyCapture
    {
        EconomyReply _reply{};
        int32        _count{ 0 };

        void onReply( const EconomyReply& reply )
        {
            _reply = reply;
            ++_count;
        }

        EconomyService::ReplyDelegate makeDelegate() { return EconomyService::ReplyDelegate::create<&EconomyServiceReplyCapture::onReply>( this ); }
    };

    struct EconomyServiceFixture
    {
        static constexpr AccountId kPlayer = 0x42;

        MemoryServiceDatabase    _database;
        MemoryServiceStore       _store;
        CurrencyCatalog          _currencies;
        OfferCatalog             _offers;
        FakeReceiptValidator     _fake;
        ReceiptValidatorRegistry _receipts;
        MetricRegistry           _metrics;
        EconomyService           _service;

        EconomyServiceFixture()
            : _database{}
            , _store{ &_database }
            , _currencies{}
            , _offers{}
            , _fake{}
            , _receipts{}
            , _metrics{}
            , _service{}
        {
            // 시험 준비 — 깨진 항목은 카탈로그가 경고로 남기고, 상품 · 통화가 없으면 아래 단언이 실패한다
            (void)_currencies.loadFromXmlText( R"(<CurrencyCatalog><Currency id="cur.gold"/><Currency id="cur.gem_paid" paid="true"/></CurrencyCatalog>)", "currency" );
            // 시험 준비 — 깨진 항목은 카탈로그가 경고로 남기고, 상품 · 통화가 없으면 아래 단언이 실패한다
            (void)_offers.loadFromXmlText( R"(<OfferCatalog><Offer id="potion" maxCount="5"><Price currency="cur.gold" amount="10"/><Grant asset="item.potion" amount="1"/></Offer>
                <Offer id="gem_100"><Product store="fake" id="gem100"/><Grant asset="cur.gem_paid" amount="100"/></Offer></OfferCatalog>)",
                                           "offer" );
#if !defined( SW_SHIPPING )
            // 가짜 검증기는 개발 전용이라 Shipping 의 경제 서비스는 그것이 든 등록부를 거절한다(ShippingRefusesDevelopmentValidator).
            (void)_receipts.registerValidator( &_fake );
#endif
            EconomyServiceSettings settings;
            settings._pCurrencyCatalog = &_currencies;
            settings._pOfferCatalog    = &_offers;
            settings._pReceiptRegistry = &_receipts;
            settings._pMetricRegistry  = &_metrics;
            SW_EXPECT_TRUE( _service.initialize( &_store, settings ) );
            LedgerTransferRequest seed;
            seed._journalKey = "test/gold";
            seed._reason     = "test.grant";
            seed._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeAccount( kPlayer ), "cur.gold", 100 } );
            LedgerTransferOutcome outcome;
            (void)Ledger::executeTransfer( _database, seed, outcome );
        }

        ~EconomyServiceFixture()
        {
            _service.shutdown();
            _store.shutdown();
            (void)_store.pollCompletions();
        }

        /** @brief 호스트가 하는 일 — 저장소 완료를 거둔 뒤 서비스 틱. */
        void tick( int64 nowMs )
        {
            (void)_store.pollCompletions();
            _service.tick( nowMs );
        }

        EconomyCall makeCall( uint16 method, AccountId accountId = kPlayer ) const
        {
            EconomyCall call;
            call._method    = method;
            call._accountId = accountId;
            call._nowMs     = 1000;
            return call;
        }
    };
} // namespace

SW_TEST_CASE( EconomyServiceTest, RepliesArriveOnlyOnStoreCompletion )
{
    EconomyServiceFixture      fixture;
    EconomyServiceReplyCapture wallet;
    fixture._service.submitCall( fixture.makeCall( EconomyMethod::kGetWallet ), wallet.makeDelegate() );
    SW_EXPECT_EQUAL( wallet._count, 0 );
    fixture.tick( 1000 );
    SW_ASSERT_EQUAL( wallet._count, 1 );
    SW_EXPECT_TRUE( wallet._reply._result == EconomyResult::Ok );
    SW_ASSERT_EQUAL( wallet._reply._listBalance.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( wallet._reply._listBalance[0]._amount, int64( 100 ) );
}

SW_TEST_CASE( EconomyServiceTest, SameKeyPurchaseChargesOnce )
{
    EconomyServiceFixture fixture;
    EconomyCall           call = fixture.makeCall( EconomyMethod::kPurchase );
    call._purchase._offerId    = "potion";
    call._purchase._count      = 2;
    call._idempotencyKey       = NetIdempotencyKey{ 5, 6 };
    EconomyServiceReplyCapture first;
    EconomyServiceReplyCapture second;
    fixture._service.submitCall( call, first.makeDelegate() );
    fixture._service.submitCall( call, second.makeDelegate() );
    fixture.tick( 1000 );
    SW_EXPECT_TRUE( first._reply._result == EconomyResult::Ok );
    SW_EXPECT_TRUE( second._reply._result == EconomyResult::Ok );
    SW_EXPECT_TRUE( second._reply._bReplayed == SW_TRUE );
    LedgerBalance gold;
    SW_ASSERT_TRUE( Ledger::readBalance( fixture._database, LedgerHolder::makeAccount( EconomyServiceFixture::kPlayer ), "cur.gold", gold ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( gold._amount, int64( 80 ) );
}

SW_TEST_CASE( EconomyServiceTest, MissingSignInOrKeyIsRefusedAndCounted )
{
    EconomyServiceFixture      fixture;
    EconomyServiceReplyCapture unsignedCapture;
    EconomyServiceReplyCapture noKey;
    fixture._service.submitCall( fixture.makeCall( EconomyMethod::kGetWallet, kInvalidAccountId ), unsignedCapture.makeDelegate() );
    EconomyCall purchase        = fixture.makeCall( EconomyMethod::kPurchase );
    purchase._purchase._offerId = "potion";
    fixture._service.submitCall( purchase, noKey.makeDelegate() );
    SW_EXPECT_TRUE( unsignedCapture._reply._result == EconomyResult::NotSignedIn ); // 규칙 위반 거절은 그 자리에서
    SW_EXPECT_TRUE( noKey._reply._result == EconomyResult::InvalidRequest );
    string text;
    fixture._metrics.writePrometheusText( text );
    SW_EXPECT_TRUE( text.find( "service_requests_total{service=\"economy\",method=\"purchase\",result=\"invalid_request\"} 1\n" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "service_requests_total{service=\"economy\",method=\"get_wallet\",result=\"not_signed_in\"} 1\n" ) != string::npos );
}

SW_TEST_CASE( EconomyServiceTest, ReceiptGoesThroughTheValidatorThenGrants )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "the fake receipt validator is development-only and refused by a shipping economy service" );
#else
    EconomyServiceFixture fixture;
    EconomyCall           call = fixture.makeCall( EconomyMethod::kRedeemReceipt );
    call._redeem._storeName    = "fake";
    call._redeem._payload      = "fake|gem100|tx-svc|ok";
    EconomyServiceReplyCapture capture;
    fixture._service.submitCall( call, capture.makeDelegate() );
    SW_EXPECT_EQUAL( fixture._fake.getPendingCount(), 1 );
    fixture.tick( 1000 ); // 검증 결과를 거둬 지급 일을 맡긴다
    SW_EXPECT_EQUAL( capture._count, 0 );
    fixture.tick( 1001 ); // 지급 일의 완료
    SW_ASSERT_EQUAL( capture._count, 1 );
    SW_EXPECT_TRUE( capture._reply._result == EconomyResult::Ok );
    SW_EXPECT_EQUAL( capture._reply._productId, string( "gem100" ) );
    SW_ASSERT_EQUAL( capture._reply._listBalance.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( capture._reply._listBalance[0]._amount, int64( 100 ) );
#endif
}

/**
 * @brief [EconomyServiceTest] 개발 전용 검증기(가짜 스토어)가 든 등록부는 Shipping 에서 초기화를 거절하고, 개발 빌드에서는 받는다
 */
SW_TEST_CASE( EconomyServiceTest, ShippingRefusesDevelopmentValidator )
{
    CurrencyCatalog          currencies;
    OfferCatalog             offers;
    FakeReceiptValidator     fake;
    ReceiptValidatorRegistry receipts;
    MemoryServiceDatabase    database;
    MemoryServiceStore       store{ &database };
    EconomyService           service;
    SW_ASSERT_TRUE( receipts.registerValidator( &fake ) );
    EconomyServiceSettings settings;
    settings._pCurrencyCatalog = &currencies;
    settings._pOfferCatalog    = &offers;
    settings._pReceiptRegistry = &receipts;
#if defined( SW_SHIPPING )
    SW_TEST_DEFENSIVE_SCOPE( "a shipping economy service refuses a development-only receipt validator" );
    SW_EXPECT_FALSE( service.initialize( &store, settings ) );
#else
    SW_EXPECT_TRUE( service.initialize( &store, settings ) );
#endif
    service.shutdown();
    store.shutdown();
}
