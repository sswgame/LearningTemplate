// 영수증 지급 — 가짜 제공자는 다음 poll 에 결과, 유효한 영수증은 한 번 지급, 다른 계정의 같은 영수증은 AlreadyRedeemed, 무효 · 환불 · 대기 · 모르는 상품 · 샌드박스,
// 레지스트리는 같은 스토어 · 규칙 밖 이름을 거절한다. 스토어 환불은 빚까지 한 번 회수, 지급 전 환불은 뒤의 지급을 막음, 청약철회는 기한 안 미사용분만.
#include "pch.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Fake/FakeReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Catalog/CurrencyCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Catalog/OfferCatalog.h"

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
            // 테스트 준비 — 깨진 항목은 카탈로그가 경고로 남기고, 상품 · 통화가 없으면 아래 단언이 실패한다
            (void)_currencies.loadFromXMLText( R"(<CurrencyCatalog><Currency id="cur.gem_paid" paid="true"/></CurrencyCatalog>)", "currency" );
            // 테스트 준비 — 깨진 항목은 카탈로그가 경고로 남기고, 상품 · 통화가 없으면 아래 단언이 실패한다
            (void)_offers.loadFromXMLText( R"(<OfferCatalog><Offer id="gem_100"><Product store="fake" id="gem100"/><Grant asset="cur.gem_paid" amount="100"/></Offer></OfferCatalog>)",
                                           "offer" );
            (void)_registry.registerValidator( &_fake );
        }

        ReceiptValidationResult validate( const utf8* pPayload, uint64 accountID )
        {
            [[maybe_unused]] const uint64   ticket = _registry.submitValidation( "fake", pPayload, accountID );
            vector<ReceiptValidationResult> listResult;
            _registry.pollCompletions( listResult );
            SW_ASSERT( listResult.size() == 1 && listResult[0]._ticket == ticket );
            return listResult[0];
        }

        EconomyResult redeem( const utf8* pPayload, uint64 accountID, bool bAcceptSandbox = false )
        {
            EconomyRedeemInput input;
            input._receipt        = validate( pPayload, accountID );
            input._accountID      = accountID;
            input._nowMs          = 1000;
            input._bAcceptSandbox = bAcceptSandbox ? SW_TRUE : SW_FALSE;
            EconomyReply reply;
            return EconomyStoreLogic::redeemReceipt( _database, _currencies, _offers, input, reply );
        }

        int64 readPaid( uint64 accountID )
        {
            LedgerBalance balance;
            // 실패면 balance 가 0 으로 남아 호출한 단언이 틀린 값으로 잡는다
            (void)Ledger::readBalance( _database, LedgerHolder::makeAccount( accountID ), "cur.gem_paid", balance );
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
        void        pollRefunds( vector<ReceiptRefundNotice>& outListNotice ) override { (void)outListNotice; }
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
    SW_EXPECT_EQUAL( listResult[0]._transactionID, string( "tx1" ) );
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

SW_TEST_CASE( ReceiptRedeemTest, StoreRefundRevokesTheGrantOnceEvenIntoDebt )
{
    RedeemFixture fixture;
    SW_ASSERT_TRUE( fixture.redeem( "fake|gem100|tx9|ok", 1 ) == EconomyResult::Ok );
    LedgerTransferRequest spend; // 일부를 써 버렸다
    spend._journalKey = "test/spend";
    spend._reason     = "test.spend";
    spend._listPosting.push_back( LedgerPosting{ LedgerHolder::makeAccount( 1 ), LedgerHolder::makeSink(), "cur.gem_paid", 70 } );
    LedgerTransferOutcome spent;
    SW_ASSERT_TRUE( Ledger::executeTransfer( fixture._database, spend, spent ) == LedgerResult::Ok );

    fixture._fake.pushRefund( "tx9", 5000 );
    vector<ReceiptRefundNotice> listNotice;
    fixture._registry.pollRefunds( listNotice );
    SW_ASSERT_EQUAL( listNotice.size(), size_t( 1 ) );
    vector<EconomyBalanceChange> listChange;
    SW_EXPECT_TRUE( EconomyStoreLogic::revokeRefund( fixture._database, fixture._currencies, listNotice[0], 5000, listChange ) == EconomyResult::Ok );
    SW_EXPECT_EQUAL( fixture.readPaid( 1 ), int64( -70 ) ); // 이미 쓴 70 은 빚으로 남는다
    SW_ASSERT_EQUAL( listChange.size(), size_t( 1 ) );      // 회수로 바뀐 잔액을 알림으로 보낼 수 있다
    SW_EXPECT_EQUAL( listChange[0]._accountID, AccountID( 1 ) );
    SW_EXPECT_EQUAL( listChange[0]._balance._amount, int64( -70 ) );
    SW_EXPECT_TRUE( EconomyStoreLogic::revokeRefund( fixture._database, fixture._currencies, listNotice[0], 5000, listChange ) == EconomyResult::AlreadyRefunded );
    SW_EXPECT_EQUAL( fixture.readPaid( 1 ), int64( -70 ) );

    ReceiptRefundNotice early{ "fake", "tx10", 6000 }; // 지급 전에 환불이 먼저 왔다
    SW_EXPECT_TRUE( EconomyStoreLogic::revokeRefund( fixture._database, fixture._currencies, early, 6000, listChange ) == EconomyResult::Ok );
    SW_EXPECT_TRUE( fixture.redeem( "fake|gem100|tx10|ok", 2 ) == EconomyResult::ReceiptRefunded );
    SW_EXPECT_EQUAL( fixture.readPaid( 2 ), int64( 0 ) );
}

SW_TEST_CASE( ReceiptRedeemTest, WithdrawalReturnsOnlyTheUnusedPartWithinTheWindow )
{
    RedeemFixture fixture;
    SW_ASSERT_TRUE( fixture.redeem( "fake|gem100|tx20|ok", 1 ) == EconomyResult::Ok ); // 지급 시각 1000
    LedgerTransferRequest spend;
    spend._journalKey = "test/spend20";
    spend._reason     = "test.spend";
    spend._listPosting.push_back( LedgerPosting{ LedgerHolder::makeAccount( 1 ), LedgerHolder::makeSink(), "cur.gem_paid", 30 } );
    LedgerTransferOutcome spent;
    SW_ASSERT_TRUE( Ledger::executeTransfer( fixture._database, spend, spent ) == LedgerResult::Ok );

    EconomyWithdrawInput input;
    input._storeName     = "fake";
    input._transactionID = "tx20";
    input._accountID     = 2;
    input._nowMs         = 2000;
    input._windowMs      = 7 * constant::kSecondsPerDay * constant::kMillisecondsPerSecond;
    EconomyReply reply;
    SW_EXPECT_TRUE( EconomyStoreLogic::withdrawPurchase( fixture._database, fixture._currencies, input, reply ) == EconomyResult::NotPurchaser ); // 다른 계정
    input._accountID = 1;
    input._nowMs     = 1000 + input._windowMs + 1;
    SW_EXPECT_TRUE( EconomyStoreLogic::withdrawPurchase( fixture._database, fixture._currencies, input, reply ) == EconomyResult::WithdrawalExpired );
    input._nowMs = 2000;
    SW_ASSERT_TRUE( EconomyStoreLogic::withdrawPurchase( fixture._database, fixture._currencies, input, reply ) == EconomyResult::Ok );
    SW_ASSERT_EQUAL( reply._listWithdrawn.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( reply._listWithdrawn[0]._amount, int64( 70 ) ); // 쓴 30 을 뺀 나머지
    SW_EXPECT_EQUAL( fixture.readPaid( 1 ), int64( 0 ) );
    SW_EXPECT_TRUE( EconomyStoreLogic::withdrawPurchase( fixture._database, fixture._currencies, input, reply ) == EconomyResult::AlreadyRefunded );
    vector<EconomyBalanceChange> listChange;
    ReceiptRefundNotice          refund{ "fake", "tx20", 3000 }; // 청약철회 뒤에 온 스토어 환불은 다시 거두지 않는다
    SW_EXPECT_TRUE( EconomyStoreLogic::revokeRefund( fixture._database, fixture._currencies, refund, 3000, listChange ) == EconomyResult::AlreadyRefunded );
    SW_EXPECT_EQUAL( fixture.readPaid( 1 ), int64( 0 ) );
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
