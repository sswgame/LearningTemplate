// 영수증 지급 — 가짜 제공자는 다음 poll 에 결과, 유효한 영수증은 한 번 지급, 다른 계정의 같은 영수증은 AlreadyRedeemed, 무효 · 환불 · 대기 · 모르는 상품 · 샌드박스,
// 등록부는 같은 스토어 · 규칙 밖 이름을 거절한다.
#include "pch.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Online/Economy/Catalog/CurrencyCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Catalog/OfferCatalog.h"
#include "GameFramework/Kits/Feature/Online/Server/Economy/EconomyStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/Server/Economy/Receipt/Provider/Fake/FakeReceiptValidator.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct RedeemFixture
    {
        MemoryServiceDatabase    _database;
        CurrencyCatalog          _currencies;
        OfferCatalog             _offers;
        FakeReceiptValidator     _fake;
        ReceiptValidatorRegistry _registry;

        RedeemFixture()
            : _database{}
            , _currencies{}
            , _offers{}
            , _fake{}
            , _registry{}
        {
            // 시험 준비 — 깨진 항목은 카탈로그가 경고로 남기고, 상품 · 통화가 없으면 아래 단언이 실패한다
            (void)_currencies.loadFromXmlText( R"(<CurrencyCatalog><Currency id="cur.gem_paid" paid="true"/></CurrencyCatalog>)", "currency" );
            // 시험 준비 — 깨진 항목은 카탈로그가 경고로 남기고, 상품 · 통화가 없으면 아래 단언이 실패한다
            (void)_offers.loadFromXmlText( R"(<OfferCatalog><Offer id="gem_100"><Product store="fake" id="gem100"/><Grant asset="cur.gem_paid" amount="100"/></Offer></OfferCatalog>)",
                                           "offer" );
            (void)_registry.registerValidator( &_fake );
        }

        ReceiptValidationResult validate( const utf8* pPayload, uint64 accountId )
        {
            [[maybe_unused]] const uint64   ticket = _registry.submitValidation( "fake", pPayload, accountId );
            vector<ReceiptValidationResult> listResult;
            _registry.pollCompletions( listResult );
            SW_ASSERT( listResult.size() == 1 && listResult[0]._ticket == ticket );
            return listResult[0];
        }

        EconomyResult redeem( const utf8* pPayload, uint64 accountId, bool bAcceptSandbox = false )
        {
            EconomyRedeemInput input;
            input._receipt        = validate( pPayload, accountId );
            input._accountId      = accountId;
            input._nowMs          = 1000;
            input._bAcceptSandbox = bAcceptSandbox ? SW_TRUE : SW_FALSE;
            EconomyReply reply;
            return EconomyStoreLogic::redeemReceipt( _database, _currencies, _offers, input, reply );
        }

        int64 readPaid( uint64 accountId )
        {
            LedgerBalance balance;
            // 실패면 balance 가 0 으로 남아 호출한 단언이 틀린 값으로 잡는다
            (void)Ledger::readBalance( _database, LedgerHolder::makeAccount( accountId ), "cur.gem_paid", balance );
            return balance._amount;
        }
    };

    /** @brief 이름이 규칙 밖인 제공자(등록 거절 확인). */
    class BadNameReceiptValidator final : public IReceiptValidator
    {
    public:
        const utf8* getStoreName() const override { return "Bad.Store"; }
        void        submitValidation( const ReceiptValidationRequest& request ) override { (void)request; }
        void        pollCompletions( vector<ReceiptValidationResult>& outListResult ) override { (void)outListResult; }
    };
} // namespace

SW_TEST_CASE( ReceiptRedeemTest, FakeValidatorCompletesOnTheNextPoll )
{
    RedeemFixture fixture;
    SW_EXPECT_EQUAL( fixture._registry.submitValidation( "nostore", "x", 1 ), uint64( 0 ) );
    const uint64 ticket = fixture._registry.submitValidation( "fake", "fake|gem100|tx1|ok", 1 );
    SW_EXPECT_EQUAL( fixture._fake.getPendingCount(), 1 );
    vector<ReceiptValidationResult> listResult;
    fixture._registry.pollCompletions( listResult );
    SW_ASSERT_EQUAL( listResult.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( listResult[0]._ticket, ticket );
    SW_EXPECT_TRUE( listResult[0]._status == ReceiptStatus::Valid );
    SW_EXPECT_EQUAL( listResult[0]._transactionId, string( "tx1" ) );
    SW_EXPECT_TRUE( fixture._registry.hasDevelopmentValidator() );
}

SW_TEST_CASE( ReceiptRedeemTest, RegistryRefusesDuplicateAndMalformedStoreNames )
{
    SW_TEST_DEFENSIVE_SCOPE( "duplicate and malformed store names are refused with an error log" );
    ReceiptValidatorRegistry registry;
    FakeReceiptValidator     first;
    FakeReceiptValidator     second;
    BadNameReceiptValidator  badName;
    SW_EXPECT_TRUE( registry.registerValidator( &first ) );
    SW_EXPECT_FALSE( registry.registerValidator( &second ) );
    SW_EXPECT_FALSE( registry.registerValidator( &badName ) );
}

SW_TEST_CASE( ReceiptRedeemTest, ValidReceiptGrantsOnce )
{
    RedeemFixture fixture;
    SW_EXPECT_TRUE( fixture.redeem( "fake|gem100|tx1|ok", 1 ) == EconomyResult::Ok );
    SW_EXPECT_TRUE( fixture.redeem( "fake|gem100|tx1|ok", 1 ) == EconomyResult::Ok ); // 같은 계정의 재시도 — 지난 결과
    SW_EXPECT_EQUAL( fixture.readPaid( 1 ), int64( 100 ) );
}

SW_TEST_CASE( ReceiptRedeemTest, SameReceiptOnAnotherAccountIsAlreadyRedeemed )
{
    RedeemFixture fixture;
    SW_ASSERT_TRUE( fixture.redeem( "fake|gem100|tx2|ok", 1 ) == EconomyResult::Ok );
    SW_EXPECT_TRUE( fixture.redeem( "fake|gem100|tx2|ok", 2 ) == EconomyResult::AlreadyRedeemed );
    SW_EXPECT_EQUAL( fixture.readPaid( 2 ), int64( 0 ) );
}

SW_TEST_CASE( ReceiptRedeemTest, InvalidRefundedPendingAndUnknownGrantNothing )
{
    RedeemFixture fixture;
    SW_EXPECT_TRUE( fixture.redeem( "fake|gem100|tx3|invalid", 1 ) == EconomyResult::ReceiptInvalid );
    SW_EXPECT_TRUE( fixture.redeem( "fake|gem100|tx4|refunded", 1 ) == EconomyResult::ReceiptRefunded );
    SW_EXPECT_TRUE( fixture.redeem( "fake|gem100|tx5|retry", 1 ) == EconomyResult::ReceiptPending );
    SW_EXPECT_TRUE( fixture.redeem( "fake|gem999|tx6|ok", 1 ) == EconomyResult::UnknownProduct );
    SW_EXPECT_TRUE( fixture.redeem( "garbage", 1 ) == EconomyResult::ReceiptInvalid );
    SW_EXPECT_EQUAL( fixture.readPaid( 1 ), int64( 0 ) );
    SW_EXPECT_TRUE( fixture.redeem( "fake|gem100|tx5|ok", 1 ) == EconomyResult::Ok ); // 대기였던 영수증을 다시
}

SW_TEST_CASE( ReceiptRedeemTest, SandboxNeedsPermission )
{
    RedeemFixture fixture;
    SW_EXPECT_TRUE( fixture.redeem( "fake|gem100|tx7|sandbox", 1 ) == EconomyResult::ReceiptInvalid );
    SW_EXPECT_TRUE( fixture.redeem( "fake|gem100|tx7|sandbox", 1, true ) == EconomyResult::Ok );
}
