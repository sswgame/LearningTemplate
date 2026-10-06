// 경제 구매 — 가격은 소각, 지급은 발행에서, 무상 재원 먼저, 모자람 · 기간 · 결제 상품 거절, 한도는 재시도 · 동시 구매에도, 응답 유실 재시도는 지난 결과,
// 재원에 빚(환불 회수)이 있으면 그 가상 화폐로 사지 못한다.
#include "pch.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Ledger/LedgerAudit.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Online/Economy/CurrencyCatalog.h"
#include "GameFramework/Kits/Online/Economy/OfferCatalog.h"
#include "GameFramework/Kits/Online/Server/Economy/EconomyStoreLogic.h"

#include "TestFramework/TestFramework.h"

#include <thread>

using namespace sw;

namespace
{
    struct PurchaseFixture
    {
        static constexpr uint64 kBuyer = 0x77;

        MemoryServiceDatabase _database;
        CurrencyCatalog       _currencies;
        OfferCatalog          _offers;

        PurchaseFixture()
            : _database{}
            , _currencies{}
            , _offers{}
        {
            (void)_currencies.loadFromXmlText( R"(<CurrencyCatalog><Currency id="cur.gold"/><Currency id="cur.gem_free"/><Currency id="cur.gem_paid" paid="true"/>
                <Currency id="cur.gem"><Funding asset="cur.gem_free"/><Funding asset="cur.gem_paid"/></Currency></CurrencyCatalog>)",
                                               "currency" );
            (void)_offers.loadFromXmlText( R"(<OfferCatalog>
                <Offer id="potion" maxCount="10"><Price currency="cur.gold" amount="20"/><Grant asset="item.potion" amount="1"/></Offer>
                <Offer id="elixir"><Price currency="cur.gem" amount="20"/><Grant asset="item.elixir" amount="1"/></Offer>
                <Offer id="starter" limit="1"><Price currency="cur.gem" amount="100"/><Grant asset="item.sword" amount="1"/><Grant asset="cur.gold" amount="500"/></Offer>
                <Offer id="event" startMs="1000" endMs="2000"><Price currency="cur.gold" amount="1"/><Grant asset="item.badge" amount="1"/></Offer>
                <Offer id="gem_100"><Product store="fake" id="gem100"/><Grant asset="cur.gem_paid" amount="100"/></Offer>
            </OfferCatalog>)",
                                           "offer" );
        }

        void grant( const utf8* pAsset, int64 amount, const utf8* pKey )
        {
            LedgerTransferRequest request;
            request._journalKey = string( "test/" ) + pKey;
            request._reason     = "test.grant";
            request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeAccount( kBuyer ), pAsset, amount } );
            LedgerTransferOutcome outcome;
            (void)Ledger::executeTransfer( _database, request, outcome );
        }

        /** @brief 환불 회수 — 계정 → 소각, 빚 허용. */
        void revokeWithDebt( const utf8* pAsset, int64 amount, const utf8* pKey )
        {
            LedgerTransferRequest request;
            request._journalKey = string( "test/" ) + pKey;
            request._reason     = "refund.revoke";
            request._bAllowDebt = SW_TRUE;
            request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeAccount( kBuyer ), LedgerHolder::makeSink(), pAsset, amount } );
            LedgerTransferOutcome outcome;
            (void)Ledger::executeTransfer( _database, request, outcome );
        }

        int64 readAmount( const utf8* pAsset )
        {
            LedgerBalance balance;
            (void)Ledger::readBalance( _database, LedgerHolder::makeAccount( kBuyer ), pAsset, balance );
            return balance._amount;
        }

        EconomyResult buy( const utf8* pOffer, int32 count, uint64 keyLow, int64 nowMs = 1500, EconomyReply* pOutReply = nullptr )
        {
            EconomyPurchaseInput input;
            input._offerId    = pOffer;
            input._count      = count;
            input._accountId  = kBuyer;
            input._nowMs      = nowMs;
            input._journalKey = LedgerJournalKey::makeFromIdempotency( LedgerJournalKey::makeAccountScope( kBuyer ), 1, keyLow );
            EconomyReply        reply;
            const EconomyResult result = EconomyStoreLogic::purchase( _database, _currencies, _offers, input, reply );
            if ( pOutReply != nullptr )
                *pOutReply = reply;
            return result;
        }

        bool isBalanced()
        {
            LedgerAuditReport report;
            return LedgerAudit::computeReport( _database, report ) == ServiceStoreResult::Ok && report.isBalanced();
        }
    };

    struct TestEconomyPurchaseInternal
    {
        static void buyStarter( PurchaseFixture* pFixture, uint64 keyLow, EconomyResult* pOutResult ) { *pOutResult = pFixture->buy( "starter", 1, keyLow ); }
    };
} // namespace

SW_TEST_CASE( EconomyPurchaseTest, BuyChargesPriceAndGrantsFromMint )
{
    PurchaseFixture fixture;
    fixture.grant( "cur.gold", 100, "g" );
    EconomyReply reply;
    SW_EXPECT_TRUE( fixture.buy( "potion", 3, 1, 1500, &reply ) == EconomyResult::Ok );
    SW_EXPECT_EQUAL( fixture.readAmount( "cur.gold" ), int64( 40 ) );
    SW_EXPECT_EQUAL( fixture.readAmount( "item.potion" ), int64( 3 ) );
    SW_EXPECT_EQUAL( reply._listBalance.size(), size_t( 2 ) ); // 바뀐 잔액 — 금 · 물약
    SW_EXPECT_TRUE( fixture.isBalanced() );
}

SW_TEST_CASE( EconomyPurchaseTest, FreeFundingIsSpentBeforePaid )
{
    PurchaseFixture fixture;
    fixture.grant( "cur.gem_free", 30, "f" );
    fixture.grant( "cur.gem_paid", 200, "p" );
    SW_EXPECT_TRUE( fixture.buy( "starter", 1, 1 ) == EconomyResult::Ok );
    SW_EXPECT_EQUAL( fixture.readAmount( "cur.gem_free" ), int64( 0 ) );
    SW_EXPECT_EQUAL( fixture.readAmount( "cur.gem_paid" ), int64( 130 ) );
    SW_EXPECT_EQUAL( fixture.readAmount( "item.sword" ), int64( 1 ) );
    SW_EXPECT_EQUAL( fixture.readAmount( "cur.gold" ), int64( 500 ) );
    SW_EXPECT_TRUE( fixture.isBalanced() );
}

SW_TEST_CASE( EconomyPurchaseTest, InsufficientFundsWritesNothing )
{
    PurchaseFixture fixture;
    fixture.grant( "cur.gem_free", 30, "f" );
    const uint64 before = fixture._database.computeContentHash();
    SW_EXPECT_TRUE( fixture.buy( "starter", 1, 1 ) == EconomyResult::InsufficientFunds );
    SW_EXPECT_TRUE( fixture.buy( "potion", 1, 2 ) == EconomyResult::InsufficientFunds );
    SW_EXPECT_EQUAL( fixture._database.computeContentHash(), before );
}

SW_TEST_CASE( EconomyPurchaseTest, LimitHoldsAndRetryOfTheLimitBuyIsReplayed )
{
    PurchaseFixture fixture;
    fixture.grant( "cur.gem_paid", 1000, "p" );
    SW_EXPECT_TRUE( fixture.buy( "starter", 1, 7 ) == EconomyResult::Ok );
    EconomyReply retry;
    SW_EXPECT_TRUE( fixture.buy( "starter", 1, 7, 1500, &retry ) == EconomyResult::Ok ); // 같은 키 재시도 — 한도에 걸리지 않고 지난 결과
    SW_EXPECT_TRUE( retry._bReplayed == SW_TRUE );
    SW_EXPECT_TRUE( fixture.buy( "starter", 1, 8 ) == EconomyResult::LimitReached ); // 새 구매
    SW_EXPECT_EQUAL( fixture.readAmount( "item.sword" ), int64( 1 ) );
    SW_EXPECT_EQUAL( fixture.readAmount( "cur.gem_paid" ), int64( 900 ) );
}

SW_TEST_CASE( EconomyPurchaseTest, WindowCountAndRealMoneyRulesRefuse )
{
    PurchaseFixture fixture;
    fixture.grant( "cur.gold", 100, "g" );
    SW_EXPECT_TRUE( fixture.buy( "event", 1, 1, 999 ) == EconomyResult::NotOnSale );
    SW_EXPECT_TRUE( fixture.buy( "event", 1, 2, 2000 ) == EconomyResult::NotOnSale );
    SW_EXPECT_TRUE( fixture.buy( "gem_100", 1, 3 ) == EconomyResult::NotPurchasable );
    SW_EXPECT_TRUE( fixture.buy( "potion", 11, 4 ) == EconomyResult::InvalidRequest );
    SW_EXPECT_TRUE( fixture.buy( "nothing", 1, 5 ) == EconomyResult::UnknownOffer );
    SW_EXPECT_EQUAL( fixture.readAmount( "cur.gold" ), int64( 100 ) );
}

SW_TEST_CASE( EconomyPurchaseTest, AfterSpendingTheRetryStillReplays )
{
    PurchaseFixture fixture;
    fixture.grant( "cur.gem_free", 20, "f" );
    SW_ASSERT_TRUE( fixture.buy( "elixir", 1, 9 ) == EconomyResult::Ok ); // 보석 0 이 됐다
    EconomyReply retry;
    SW_EXPECT_TRUE( fixture.buy( "elixir", 1, 9, 1500, &retry ) == EconomyResult::Ok ); // 응답을 잃은 재시도 — "모자람" 이 아니라 지난 결과
    SW_EXPECT_TRUE( retry._bReplayed == SW_TRUE );
    SW_EXPECT_EQUAL( fixture.readAmount( "item.elixir" ), int64( 1 ) );
}

SW_TEST_CASE( EconomyPurchaseTest, DebtOnAFundingAssetBlocksTheVirtualCurrency )
{
    PurchaseFixture fixture;
    fixture.grant( "cur.gem_free", 500, "f" );
    fixture.revokeWithDebt( "cur.gem_paid", 50, "r" ); // 환불 회수 — 유상 보석 −50
    SW_ASSERT_EQUAL( fixture.readAmount( "cur.gem_paid" ), int64( -50 ) );
    const uint64 before = fixture._database.computeContentHash();
    SW_EXPECT_TRUE( fixture.buy( "elixir", 1, 1 ) == EconomyResult::InsufficientFunds ); // 무상이 넉넉해도 빚을 먼저 갚는다
    SW_EXPECT_EQUAL( fixture._database.computeContentHash(), before );
    fixture.grant( "cur.gem_paid", 50, "repay" ); // 받는 이동이 빚을 먼저 갚는다
    SW_EXPECT_TRUE( fixture.buy( "elixir", 1, 2 ) == EconomyResult::Ok );
    SW_EXPECT_TRUE( fixture.isBalanced() );
}

SW_TEST_CASE( EconomyPurchaseTest, LimitHoldsUnderConcurrentPurchases )
{
    PurchaseFixture fixture;
    fixture.grant( "cur.gem_paid", 10000, "p" );
    EconomyResult arrResult[8]{};
    std::thread   arrThread[8];
    for ( int32 threadIndex = 0; threadIndex < 8; ++threadIndex )
        arrThread[threadIndex] = std::thread( &TestEconomyPurchaseInternal::buyStarter, &fixture, static_cast<uint64>( 100 + threadIndex ), &arrResult[threadIndex] );
    for ( std::thread& thread : arrThread )
        thread.join();
    int32 okCount = 0;
    for ( const EconomyResult result : arrResult )
    {
        SW_EXPECT_TRUE( result == EconomyResult::Ok || result == EconomyResult::LimitReached || result == EconomyResult::Busy );
        okCount += result == EconomyResult::Ok ? 1 : 0;
    }
    SW_EXPECT_EQUAL( okCount, 1 );
    SW_EXPECT_EQUAL( fixture.readAmount( "item.sword" ), int64( 1 ) );
    SW_EXPECT_TRUE( fixture.isBalanced() );
}
