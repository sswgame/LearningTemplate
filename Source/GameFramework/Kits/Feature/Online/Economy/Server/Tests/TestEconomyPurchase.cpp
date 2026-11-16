// EconomyPurchaseTest — 구매 저장 규칙(메모리 저장소): 가상 화폐 가격은 재원(무상 → 유상) 순서로 나눠 내고, 같은 멱등 키로 다시 사면 원장이 한 번만 움직이며,
// 계정당 한도 · 잔액 부족 · 판매 기간 밖은 아무것도 바꾸지 않는다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Catalog/CurrencyCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Catalog/OfferCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyStoreLogic.h"
#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

using namespace sw;

namespace
{
    constexpr uint64 kBuyer = 42;
    constexpr int64  kNowMs = 1700000000000ll;

    const utf8* const kCurrencyXML = R"(<CurrencyCatalog>
  <Currency id="cur.gold"/>
  <Currency id="cur.gem_free"/>
  <Currency id="cur.gem_paid" paid="true"/>
  <Currency id="cur.gem"><Funding asset="cur.gem_free"/><Funding asset="cur.gem_paid"/></Currency>
</CurrencyCatalog>)";

    const utf8* const kOfferXML = R"(<OfferCatalog>
  <Offer id="starter_pack" limit="1">
    <Price currency="cur.gem" amount="100"/>
    <Grant asset="item.sword" amount="1"/>
    <Grant asset="cur.gold" amount="500"/>
  </Offer>
  <Offer id="castle">
    <Price currency="cur.gem" amount="100000"/>
    <Grant asset="item.castle" amount="1"/>
  </Offer>
  <Offer id="expired" endMs="1000">
    <Price currency="cur.gold" amount="1"/>
    <Grant asset="item.hat" amount="1"/>
  </Offer>
</OfferCatalog>)";

    /** @brief 계정에 발행합니다. 분개 키는 자산 · 양으로 만든다(같은 키면 원장이 재생으로 막는다). */
    void grant( MemoryServiceDatabase& database, string_view assetID, int64 amount )
    {
        LedgerTransferRequest request;
        request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeAccount( kBuyer ), string( assetID ), amount } );
        request._journalKey = "test/grant/" + string( assetID ) + "/" + to_string( amount );
        request._reason     = "test.grant";
        LedgerTransferResult result;
        SW_EXPECT_TRUE( Ledger::executeTransfer( database, request, result ) == LedgerResult::Ok );
    }

    int64 getBalanceOf( MemoryServiceDatabase& database, string_view assetID )
    {
        LedgerBalance balance;
        SW_EXPECT_TRUE( Ledger::readBalance( database, LedgerHolder::makeAccount( kBuyer ), assetID, balance ) == ServiceStoreResult::Ok );
        return balance._amount;
    }

    EconomyServiceResult buy( MemoryServiceDatabase& database, const CurrencyCatalog& currencies, const OfferCatalog& offers, string_view offerID, uint64 idempotencyLow )
    {
        EconomyPurchaseInput input;
        input._offerID    = string( offerID );
        input._journalKey = LedgerJournalKey::makeFromIdempotency( LedgerJournalKey::makeAccountScope( kBuyer ), 7, idempotencyLow );
        input._accountID  = kBuyer;
        input._nowMs      = kNowMs;
        EconomyPurchaseReply reply;
        return EconomyStoreLogic::purchase( database, currencies, offers, input, reply );
    }

    struct Fixture
    {
        MemoryServiceDatabase database;
        CurrencyCatalog       currencies;
        OfferCatalog          offers;

        Fixture()
        {
            SW_EXPECT_TRUE( currencies.loadFromXMLText( kCurrencyXML, "test" ) );
            SW_EXPECT_TRUE( offers.loadFromXMLText( kOfferXML, "test" ) );
            grant( database, "cur.gem_free", 30 );
            grant( database, "cur.gem_paid", 100 );
        }
    };
} // namespace

/** @brief [EconomyPurchaseTest] 가상 화폐 가격은 무상 재원부터 쓰고 모자란 만큼 유상에서 낸다 — 지급은 모두 들어온다 */
SW_TEST_CASE( EconomyPurchaseTest, VirtualPriceSpendsFreeFundsFirst )
{
    Fixture fixture;
    SW_REQUIRE_TRUE( buy( fixture.database, fixture.currencies, fixture.offers, "starter_pack", 1 ) == EconomyResult::Ok );
    SW_EXPECT_EQUAL( getBalanceOf( fixture.database, "cur.gem_free" ), int64( 0 ) );
    SW_EXPECT_EQUAL( getBalanceOf( fixture.database, "cur.gem_paid" ), int64( 30 ) );
    SW_EXPECT_EQUAL( getBalanceOf( fixture.database, "item.sword" ), int64( 1 ) );
    SW_EXPECT_EQUAL( getBalanceOf( fixture.database, "cur.gold" ), int64( 500 ) );
}

/** @brief [EconomyPurchaseTest] 같은 멱등 키로 다시 보내면(응답을 잃은 재시도) 원장은 한 번만 움직이고, 다른 키의 두 번째 구매는 계정당 한도에 걸린다 */
SW_TEST_CASE( EconomyPurchaseTest, RetryIsIdempotentAndLimitHolds )
{
    Fixture fixture;
    SW_REQUIRE_TRUE( buy( fixture.database, fixture.currencies, fixture.offers, "starter_pack", 1 ) == EconomyResult::Ok );
    const uint64 hashAfterFirst = fixture.database.computeContentHash();
    SW_EXPECT_TRUE( buy( fixture.database, fixture.currencies, fixture.offers, "starter_pack", 1 ) == EconomyResult::Ok ); // 지난 결과를 돌려준다
    SW_EXPECT_EQUAL( fixture.database.computeContentHash(), hashAfterFirst );
    grant( fixture.database, "cur.gem_paid", 1000 ); // 잔액 부족이 아니라 한도에 걸리는지 본다
    const uint64 hashWithFunds = fixture.database.computeContentHash();
    SW_EXPECT_TRUE( buy( fixture.database, fixture.currencies, fixture.offers, "starter_pack", 2 ) == EconomyResult::LimitReached );
    SW_EXPECT_EQUAL( fixture.database.computeContentHash(), hashWithFunds );
}

/** @brief [EconomyPurchaseTest] 잔액이 모자라거나 판매 기간 밖이거나 모르는 상품이면 아무것도 바꾸지 않는다 */
SW_TEST_CASE( EconomyPurchaseTest, RefusalsChangeNothing )
{
    Fixture      fixture;
    const uint64 hashBefore = fixture.database.computeContentHash();
    SW_EXPECT_TRUE( buy( fixture.database, fixture.currencies, fixture.offers, "castle", 3 ) == EconomyResult::InsufficientFunds );
    SW_EXPECT_TRUE( buy( fixture.database, fixture.currencies, fixture.offers, "expired", 4 ) == EconomyResult::NotOnSale );
    SW_EXPECT_TRUE( buy( fixture.database, fixture.currencies, fixture.offers, "no_such_offer", 5 ) == EconomyResult::UnknownOffer );
    SW_EXPECT_EQUAL( fixture.database.computeContentHash(), hashBefore );
}

/** @brief [EconomyPurchaseTest] 계정 삭제 → 경제 키트에 그 계정의 구매 수 레코드 0(`econ_purchase`). 잔액은 탈퇴 정리가 원장에서 소각한다(감사 P0-7) */
SW_TEST_CASE( EconomyPurchaseTest, AccountDeletionErasesPurchaseCounts )
{
    Fixture fixture;
    SW_REQUIRE_TRUE( buy( fixture.database, fixture.currencies, fixture.offers, "starter_pack", 1 ) == EconomyResult::Ok );
    SW_REQUIRE_EQUAL( 1, fixture.database.countRecords( EconomyStoreLogic::getPurchaseCountTable() ) );
    SW_REQUIRE_TRUE( EconomyStoreLogic::eraseAccount( fixture.database, kBuyer, 0 ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 0, fixture.database.countRecords( EconomyStoreLogic::getPurchaseCountTable() ) );
}
