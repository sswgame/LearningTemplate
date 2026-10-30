#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Ledger/LedgerAudit.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"

// LedgerTest — 복식 원장의 돈 이동: 발행 · 계정 사이 이동, 잔액 부족 · 상한, 분개 키 멱등, 저장소 실패(커밋 거절 · 응답 유실)에서도 정확히 한 번.
// 매 케이스 끝에 감사(자산마다 보유 = 발행 - 소각)를 본다. 저장소는 메모리 구현이다(nogpu).

namespace
{
    constexpr uint64      kAlice = 101;
    constexpr uint64      kBob   = 202;
    constexpr const utf8* kGold  = "gold";

    sw::LedgerTransferRequest makeMove( const sw::LedgerHolder& from, const sw::LedgerHolder& to, int64 amount, sw::string_view journalKey )
    {
        sw::LedgerTransferRequest request;
        request._listPosting.push_back( sw::LedgerPosting{ from, to, sw::string( kGold ), amount } );
        request._journalKey = sw::string( journalKey );
        request._reason     = "test.move";
        request._timeMs     = 1700000000000ll;
        return request;
    }

    int64 readGold( sw::MemoryServiceDatabase& database, uint64 accountID )
    {
        sw::LedgerBalance balance;
        SW_EXPECT_TRUE( sw::Ledger::readBalance( database, sw::LedgerHolder::makeAccount( accountID ), kGold, balance ) == sw::ServiceStoreResult::Ok );
        return balance._amount;
    }

    void expectAuditBalanced( sw::MemoryServiceDatabase& database )
    {
        sw::LedgerAuditReport report;
        SW_EXPECT_TRUE( sw::LedgerAudit::computeReport( database, report ) == sw::ServiceStoreResult::Ok );
        SW_EXPECT_TRUE( report.isBalanced() );
        SW_EXPECT_EQUAL( 0, report._unreadableRecordCount );
    }

    sw::LedgerResult move( sw::MemoryServiceDatabase& database, const sw::LedgerTransferRequest& request, sw::LedgerTransferOutcome& outOutcome )
    {
        return sw::Ledger::executeTransfer( database, request, outOutcome );
    }

    /** @brief 금 상한 50 인 정책입니다. */
    class GoldCapPolicy final : public sw::ILedgerPolicy
    {
    public:
        int64 getBalanceCap( sw::string_view assetID ) const override { return assetID == kGold ? 50 : sw::LedgerConstant::kMaxBalance; }
    };
} // namespace

/**
 * @brief [LedgerTest] 발행 → 계정, 계정 → 계정 이동이 잔액에 맞게 남고, 모자라면 아무것도 바꾸지 않는다
 */
SW_TEST_CASE( LedgerTest, MovesAndRefusesOverdraft )
{
    sw::MemoryServiceDatabase database;
    sw::LedgerTransferOutcome outcome;
    SW_REQUIRE_TRUE( move( database, makeMove( sw::LedgerHolder::makeMint(), sw::LedgerHolder::makeAccount( kAlice ), 100, "test/grant.1" ), outcome ) == sw::LedgerResult::Ok );
    SW_REQUIRE_TRUE( move( database, makeMove( sw::LedgerHolder::makeAccount( kAlice ), sw::LedgerHolder::makeAccount( kBob ), 30, "test/pay.1" ), outcome ) ==
                     sw::LedgerResult::Ok );
    SW_EXPECT_EQUAL( int64( 70 ), readGold( database, kAlice ) );
    SW_EXPECT_EQUAL( int64( 30 ), readGold( database, kBob ) );

    const uint64 commitsBefore = database.getCommitCount();
    SW_EXPECT_TRUE( move( database, makeMove( sw::LedgerHolder::makeAccount( kBob ), sw::LedgerHolder::makeAccount( kAlice ), 31, "test/pay.2" ), outcome ) ==
                    sw::LedgerResult::InsufficientFunds );
    SW_EXPECT_EQUAL( 0, outcome._failedPostingIndex );
    SW_EXPECT_EQUAL( commitsBefore, database.getCommitCount() );
    SW_EXPECT_EQUAL( int64( 30 ), readGold( database, kBob ) );
    expectAuditBalanced( database );
}

/**
 * @brief [LedgerTest] 같은 분개 키를 다시 보내면 한 번만 적용되고(재생), 같은 키에 다른 내용이면 거절한다
 */
SW_TEST_CASE( LedgerTest, JournalKeyIsIdempotent )
{
    sw::MemoryServiceDatabase       database;
    sw::LedgerTransferOutcome       outcome;
    const sw::LedgerTransferRequest grant = makeMove( sw::LedgerHolder::makeMint(), sw::LedgerHolder::makeAccount( kAlice ), 40, "test/grant.once" );
    SW_REQUIRE_TRUE( move( database, grant, outcome ) == sw::LedgerResult::Ok );
    SW_EXPECT_FALSE( outcome._bReplayed );
    SW_REQUIRE_TRUE( move( database, grant, outcome ) == sw::LedgerResult::Ok );
    SW_EXPECT_TRUE( outcome._bReplayed );
    SW_EXPECT_EQUAL( int64( 40 ), readGold( database, kAlice ) );

    const sw::LedgerTransferRequest different = makeMove( sw::LedgerHolder::makeMint(), sw::LedgerHolder::makeAccount( kAlice ), 41, "test/grant.once" );
    SW_EXPECT_TRUE( move( database, different, outcome ) == sw::LedgerResult::JournalKeyReused );
    SW_EXPECT_EQUAL( int64( 40 ), readGold( database, kAlice ) );
    expectAuditBalanced( database );
}

/**
 * @brief [LedgerTest] 저장소가 커밋을 거절하면 적용되지 않고(Unavailable), 응답만 잃으면 분개로 적용을 알아챈다 — 같은 키로 다시 해도 돈은 한 번만 움직인다
 */
SW_TEST_CASE( LedgerTest, ExactlyOnceUnderStoreFaults )
{
    sw::MemoryServiceDatabase database;
    sw::LedgerTransferOutcome outcome;

    const sw::LedgerTransferRequest rejected = makeMove( sw::LedgerHolder::makeMint(), sw::LedgerHolder::makeAccount( kAlice ), 10, "test/grant.rejected" );
    database.armFault( sw::ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( move( database, rejected, outcome ) == sw::LedgerResult::Unavailable );
    SW_EXPECT_EQUAL( int64( 0 ), readGold( database, kAlice ) );
    SW_REQUIRE_TRUE( move( database, rejected, outcome ) == sw::LedgerResult::Ok ); // 같은 키로 다시
    SW_EXPECT_EQUAL( int64( 10 ), readGold( database, kAlice ) );

    const sw::LedgerTransferRequest lost = makeMove( sw::LedgerHolder::makeMint(), sw::LedgerHolder::makeAccount( kAlice ), 5, "test/grant.lostreply" );
    database.armFault( sw::ServiceStoreFault::LoseCommitReply );
    SW_EXPECT_TRUE( move( database, lost, outcome ) == sw::LedgerResult::Ok ); // 적용됐다 — 분개가 있다
    SW_EXPECT_TRUE( outcome._bReplayed );
    SW_REQUIRE_TRUE( move( database, lost, outcome ) == sw::LedgerResult::Ok );
    SW_EXPECT_EQUAL( int64( 15 ), readGold( database, kAlice ) );
    expectAuditBalanced( database );
}

/**
 * @brief [LedgerTest] 계정 잔액 상한을 넘는 이동은 거절하고, 잘못된 이동(같은 보유자끼리 · 0 이하 금액)은 Invalid 다
 */
SW_TEST_CASE( LedgerTest, CapAndInvalidRequests )
{
    sw::MemoryServiceDatabase database;
    sw::LedgerTransferOutcome outcome;
    const GoldCapPolicy       policy;
    sw::LedgerTransferRequest overCap = makeMove( sw::LedgerHolder::makeMint(), sw::LedgerHolder::makeAccount( kAlice ), 51, "test/grant.cap" );
    overCap._pPolicy                  = &policy;
    SW_EXPECT_TRUE( move( database, overCap, outcome ) == sw::LedgerResult::CapExceeded );
    SW_EXPECT_EQUAL( int64( 0 ), readGold( database, kAlice ) );

    SW_EXPECT_TRUE( move( database, makeMove( sw::LedgerHolder::makeAccount( kAlice ), sw::LedgerHolder::makeAccount( kAlice ), 1, "test/self" ), outcome ) ==
                    sw::LedgerResult::Invalid );
    SW_EXPECT_TRUE( move( database, makeMove( sw::LedgerHolder::makeMint(), sw::LedgerHolder::makeAccount( kAlice ), 0, "test/zero" ), outcome ) ==
                    sw::LedgerResult::Invalid );
    SW_EXPECT_TRUE( move( database, makeMove( sw::LedgerHolder::makeMint(), sw::LedgerHolder::makeAccount( kAlice ), -5, "test/negative" ), outcome ) ==
                    sw::LedgerResult::Invalid );
    expectAuditBalanced( database );
}
