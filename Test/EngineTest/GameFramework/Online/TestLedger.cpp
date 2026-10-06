// 원장 — 복식 이동 · 음수 금지 · 다리 전부 또는 없음 · 분개 키 멱등 · 응답 유실 · 상한 · 동시 이동 · 내역 · 붙이기 · 규칙 위반 · 환불 회수 빚, 매번 보존 검사.
#include "pch.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Ledger/LedgerAudit.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

#include "TestFramework/TestFramework.h"

#include <thread>

using namespace sw;

namespace
{
    constexpr uint64 kLedgerAlice = 0xA1;
    constexpr uint64 kLedgerBob   = 0xB2;

    LedgerPosting makePosting( const LedgerHolder& from, const LedgerHolder& to, const utf8* pAsset, int64 amount )
    {
        LedgerPosting posting;
        posting._from    = from;
        posting._to      = to;
        posting._assetId = pAsset;
        posting._amount  = amount;
        return posting;
    }

    LedgerTransferRequest makeRequest( const utf8* pKeyTail, const utf8* pReason, vector<LedgerPosting> listPosting, int64 timeMs = 1000 )
    {
        LedgerTransferRequest request;
        request._journalKey  = string( "test/" ) + pKeyTail;
        request._reason      = pReason;
        request._listPosting = std::move( listPosting );
        request._timeMs      = timeMs;
        return request;
    }

    LedgerResult grant( MemoryServiceDatabase& database, uint64 accountId, const utf8* pAsset, int64 amount, const utf8* pKeyTail )
    {
        LedgerTransferOutcome outcome;
        return Ledger::executeTransfer(
            database, makeRequest( pKeyTail, "test.grant", { makePosting( LedgerHolder::makeMint(), LedgerHolder::makeAccount( accountId ), pAsset, amount ) } ), outcome );
    }

    int64 readAmount( MemoryServiceDatabase& database, uint64 accountId, const utf8* pAsset )
    {
        LedgerBalance balance;
        if ( Ledger::readBalance( database, LedgerHolder::makeAccount( accountId ), pAsset, balance ) != ServiceStoreResult::Ok )
            return -1;
        return balance._amount;
    }

    bool isBalanced( MemoryServiceDatabase& database )
    {
        LedgerAuditReport report;
        return LedgerAudit::computeReport( database, report ) == ServiceStoreResult::Ok && report.isBalanced();
    }

    class CapPolicy final : public ILedgerPolicy
    {
    public:
        int64 getBalanceCap( string_view assetId ) const override { return assetId == "cur.gold" ? 100 : 0; }
    };

    struct ConcurrentContext
    {
        MemoryServiceDatabase* _pDatabase{ nullptr };
        int32                  _threadIndex{ 0 };
        int32                  _appliedCount{ 0 };
        int32                  _refusedCount{ 0 };
    };

    void runRandomTransfers( ConcurrentContext* pContext )
    {
        uint64 state = 0x9E3779B97F4A7C15ull * static_cast<uint64>( pContext->_threadIndex + 1 );
        for ( int32 transferIndex = 0; transferIndex < 200; ++transferIndex )
        {
            state                         = state * 6364136223846793005ull + 1442695040888963407ull;
            const uint64          fromId  = 1 + ( state >> 33 ) % 4;
            const uint64          toId    = 1 + ( fromId + ( state >> 40 ) % 3 ) % 4;
            const int64           amount  = 1 + static_cast<int64>( ( state >> 20 ) % 40 );
            const string          keyTail = "c" + ServiceKeyUtil::makeHex64( static_cast<uint64>( pContext->_threadIndex ) * 1000 + static_cast<uint64>( transferIndex ) );
            LedgerTransferOutcome outcome;
            const LedgerResult    result = Ledger::executeTransfer(
                *pContext->_pDatabase,
                makeRequest( keyTail.c_str(), "test.move", { makePosting( LedgerHolder::makeAccount( fromId ), LedgerHolder::makeAccount( toId ), "cur.gold", amount ) } ),
                outcome );
            if ( result == LedgerResult::Ok )
                ++pContext->_appliedCount;
            else
                ++pContext->_refusedCount; // 모자람 · 재시도 소진 — 어느 쪽이든 아무것도 쓰지 않았다
        }
    }
} // namespace

SW_TEST_CASE( LedgerTest, TransferMovesBothSidesAndKeepsTheSum )
{
    MemoryServiceDatabase database;
    SW_ASSERT_TRUE( grant( database, kLedgerAlice, "cur.gold", 100, "g1" ) == LedgerResult::Ok );
    LedgerTransferOutcome outcome;
    const LedgerResult    result = Ledger::executeTransfer(
        database, makeRequest( "t1", "test.give", { makePosting( LedgerHolder::makeAccount( kLedgerAlice ), LedgerHolder::makeAccount( kLedgerBob ), "cur.gold", 30 ) } ), outcome );
    SW_EXPECT_TRUE( result == LedgerResult::Ok );
    SW_EXPECT_EQUAL( readAmount( database, kLedgerAlice, "cur.gold" ), int64( 70 ) );
    SW_EXPECT_EQUAL( readAmount( database, kLedgerBob, "cur.gold" ), int64( 30 ) );
    SW_EXPECT_EQUAL( outcome._listHolderBalance.size(), size_t( 2 ) );
    LedgerAuditReport report;
    SW_ASSERT_TRUE( LedgerAudit::computeReport( database, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( report.isBalanced() );
    SW_ASSERT_TRUE( report.findAsset( "cur.gold" ) != nullptr );
    SW_EXPECT_EQUAL( report.findAsset( "cur.gold" )->_held, int64( 100 ) );
}

SW_TEST_CASE( LedgerTest, NegativeBalanceIsRefusedAndNothingIsWritten )
{
    MemoryServiceDatabase database;
    SW_ASSERT_TRUE( grant( database, kLedgerAlice, "cur.gold", 10, "g1" ) == LedgerResult::Ok );
    const uint64          before = database.computeContentHash();
    LedgerTransferOutcome outcome;
    const LedgerResult    result = Ledger::executeTransfer(
        database, makeRequest( "t1", "test.give", { makePosting( LedgerHolder::makeAccount( kLedgerAlice ), LedgerHolder::makeAccount( kLedgerBob ), "cur.gold", 11 ) } ), outcome );
    SW_EXPECT_TRUE( result == LedgerResult::InsufficientFunds );
    SW_EXPECT_EQUAL( outcome._failedPostingIndex, 0 );
    SW_EXPECT_EQUAL( database.computeContentHash(), before );
    SW_EXPECT_TRUE( isBalanced( database ) );
}

SW_TEST_CASE( LedgerTest, MultiPostingIsAllOrNothing )
{
    MemoryServiceDatabase database;
    SW_ASSERT_TRUE( grant( database, kLedgerAlice, "item.sword", 1, "g1" ) == LedgerResult::Ok );
    SW_ASSERT_TRUE( grant( database, kLedgerBob, "cur.gold", 40, "g2" ) == LedgerResult::Ok );
    const uint64          before = database.computeContentHash();
    LedgerTransferOutcome outcome;
    // 칼 → 밥, 금 50 → 앨리스(밥은 40 뿐) — 칼 다리도 적용되면 안 된다
    const LedgerResult result = Ledger::executeTransfer( database,
                                                         makeRequest( "trade1", "trade.settle",
                                                                      { makePosting( LedgerHolder::makeAccount( kLedgerAlice ), LedgerHolder::makeAccount( kLedgerBob ), "item.sword", 1 ),
                                                                        makePosting( LedgerHolder::makeAccount( kLedgerBob ), LedgerHolder::makeAccount( kLedgerAlice ), "cur.gold", 50 ) } ),
                                                         outcome );
    SW_EXPECT_TRUE( result == LedgerResult::InsufficientFunds );
    SW_EXPECT_EQUAL( outcome._failedPostingIndex, 1 );
    SW_EXPECT_EQUAL( database.computeContentHash(), before );
}

SW_TEST_CASE( LedgerTest, SameJournalKeyReplaysInsteadOfApplyingTwice )
{
    MemoryServiceDatabase database;
    SW_ASSERT_TRUE( grant( database, kLedgerAlice, "cur.gold", 100, "g1" ) == LedgerResult::Ok );
    const LedgerTransferRequest request =
        makeRequest( "same", "test.give", { makePosting( LedgerHolder::makeAccount( kLedgerAlice ), LedgerHolder::makeAccount( kLedgerBob ), "cur.gold", 30 ) } );
    LedgerTransferOutcome first;
    LedgerTransferOutcome second;
    SW_ASSERT_TRUE( Ledger::executeTransfer( database, request, first ) == LedgerResult::Ok );
    SW_ASSERT_TRUE( Ledger::executeTransfer( database, request, second ) == LedgerResult::Ok );
    SW_EXPECT_TRUE( second._bReplayed == SW_TRUE );
    SW_EXPECT_EQUAL( readAmount( database, kLedgerBob, "cur.gold" ), int64( 30 ) ); // 한 번만
    SW_EXPECT_EQUAL( second._listHolderBalance.size(), first._listHolderBalance.size() );

    LedgerTransferRequest changed   = request;
    changed._listPosting[0]._amount = 31;
    LedgerTransferOutcome third;
    SW_EXPECT_TRUE( Ledger::executeTransfer( database, changed, third ) == LedgerResult::JournalKeyReused );
    SW_EXPECT_TRUE( isBalanced( database ) );
}

SW_TEST_CASE( LedgerTest, LostCommitReplyIsResolvedAndRejectedCommitCanBeRetried )
{
    MemoryServiceDatabase database;
    SW_ASSERT_TRUE( grant( database, kLedgerAlice, "cur.gold", 100, "g1" ) == LedgerResult::Ok );
    const LedgerTransferRequest lost =
        makeRequest( "lost", "test.give", { makePosting( LedgerHolder::makeAccount( kLedgerAlice ), LedgerHolder::makeAccount( kLedgerBob ), "cur.gold", 10 ) } );
    database.armFault( ServiceStoreFault::LoseCommitReply );
    LedgerTransferOutcome outcome;
    SW_EXPECT_TRUE( Ledger::executeTransfer( database, lost, outcome ) == LedgerResult::Ok ); // 커밋은 됐고 분개가 그것을 말한다
    SW_EXPECT_TRUE( outcome._bReplayed == SW_TRUE );
    SW_EXPECT_EQUAL( readAmount( database, kLedgerBob, "cur.gold" ), int64( 10 ) );

    const LedgerTransferRequest rejected =
        makeRequest( "rejected", "test.give", { makePosting( LedgerHolder::makeAccount( kLedgerAlice ), LedgerHolder::makeAccount( kLedgerBob ), "cur.gold", 5 ) } );
    database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( Ledger::executeTransfer( database, rejected, outcome ) == LedgerResult::Unavailable );
    SW_EXPECT_EQUAL( readAmount( database, kLedgerBob, "cur.gold" ), int64( 10 ) );
    SW_EXPECT_TRUE( Ledger::executeTransfer( database, rejected, outcome ) == LedgerResult::Ok ); // 같은 키로 다시
    SW_EXPECT_EQUAL( readAmount( database, kLedgerBob, "cur.gold" ), int64( 15 ) );

    database.armFault( ServiceStoreFault::RejectRead );
    SW_EXPECT_TRUE( Ledger::executeTransfer( database, rejected, outcome ) == LedgerResult::Unavailable );
    SW_EXPECT_TRUE( isBalanced( database ) );
}

SW_TEST_CASE( LedgerTest, CapIsCheckedOnlyWhenTheBalanceGrows )
{
    MemoryServiceDatabase database;
    CapPolicy             policy;
    LedgerTransferRequest overCap =
        makeRequest( "cap1", "test.grant", { makePosting( LedgerHolder::makeMint(), LedgerHolder::makeAccount( kLedgerAlice ), "cur.gold", 101 ) } );
    overCap._pPolicy = &policy;
    LedgerTransferOutcome outcome;
    SW_EXPECT_TRUE( Ledger::executeTransfer( database, overCap, outcome ) == LedgerResult::CapExceeded );
    SW_EXPECT_EQUAL( outcome._failedPostingIndex, 0 );

    SW_ASSERT_TRUE( grant( database, kLedgerAlice, "cur.gold", 150, "nocap" ) == LedgerResult::Ok ); // 정책 없는 지급(상한이 내려간 뒤의 옛 잔액 흉내)
    LedgerTransferRequest spend = makeRequest( "spend", "test.spend", { makePosting( LedgerHolder::makeAccount( kLedgerAlice ), LedgerHolder::makeSink(), "cur.gold", 20 ) } );
    spend._pPolicy              = &policy;
    SW_EXPECT_TRUE( Ledger::executeTransfer( database, spend, outcome ) == LedgerResult::Ok ); // 줄어드는 쪽은 상한을 보지 않는다
    SW_EXPECT_EQUAL( readAmount( database, kLedgerAlice, "cur.gold" ), int64( 130 ) );
}

SW_TEST_CASE( LedgerTest, ZeroBalanceRecordIsErasedAndCanComeBack )
{
    MemoryServiceDatabase database;
    SW_ASSERT_TRUE( grant( database, kLedgerAlice, "cur.gold", 5, "g1" ) == LedgerResult::Ok );
    LedgerTransferOutcome outcome;
    SW_ASSERT_TRUE( Ledger::executeTransfer( database,
                                             makeRequest( "all", "test.spend", { makePosting( LedgerHolder::makeAccount( kLedgerAlice ), LedgerHolder::makeSink(), "cur.gold", 5 ) } ),
                                             outcome ) == LedgerResult::Ok );
    SW_EXPECT_EQUAL( database.countRecords( Ledger::getBalanceTable() ), 0 );
    SW_ASSERT_TRUE( grant( database, kLedgerAlice, "cur.gold", 7, "g2" ) == LedgerResult::Ok );
    SW_EXPECT_EQUAL( readAmount( database, kLedgerAlice, "cur.gold" ), int64( 7 ) );
    LedgerAuditReport report;
    SW_ASSERT_TRUE( LedgerAudit::computeReport( database, report ) == ServiceStoreResult::Ok );
    SW_ASSERT_TRUE( report.findAsset( "cur.gold" ) != nullptr );
    SW_EXPECT_EQUAL( report.findAsset( "cur.gold" )->_issued, int64( 12 ) );
    SW_EXPECT_EQUAL( report.findAsset( "cur.gold" )->_burned, int64( 5 ) );
    SW_EXPECT_TRUE( report.isBalanced() );
}

SW_TEST_CASE( LedgerTest, ConcurrentTransfersKeepTheSumAndNeverGoNegative )
{
    MemoryServiceDatabase database;
    for ( uint64 accountId = 1; accountId <= 4; ++accountId )
    {
        const string keyTail = "seed" + ServiceKeyUtil::makeHex64( accountId );
        SW_ASSERT_TRUE( grant( database, accountId, "cur.gold", 100, keyTail.c_str() ) == LedgerResult::Ok );
    }
    ConcurrentContext arrContext[4];
    std::thread       arrThread[4];
    for ( int32 threadIndex = 0; threadIndex < 4; ++threadIndex )
    {
        arrContext[threadIndex]._pDatabase   = &database;
        arrContext[threadIndex]._threadIndex = threadIndex;
        arrThread[threadIndex]               = std::thread( &runRandomTransfers, &arrContext[threadIndex] );
    }
    int32 appliedCount = 0;
    for ( int32 threadIndex = 0; threadIndex < 4; ++threadIndex )
    {
        arrThread[threadIndex].join();
        appliedCount += arrContext[threadIndex]._appliedCount;
    }
    SW_EXPECT_TRUE( appliedCount > 0 );
    int64 total = 0;
    for ( uint64 accountId = 1; accountId <= 4; ++accountId )
    {
        const int64 amount = readAmount( database, accountId, "cur.gold" );
        SW_EXPECT_TRUE( amount >= 0 );
        total += amount;
    }
    SW_EXPECT_EQUAL( total, int64( 400 ) );
    LedgerAuditReport report;
    SW_ASSERT_TRUE( LedgerAudit::computeReport( database, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( report.isBalanced() );
    SW_EXPECT_EQUAL( report._journalCount, 4 + appliedCount ); // 적용한 것만 분개가 있다
}

SW_TEST_CASE( LedgerTest, HistoryListsNewestFirstWithCursor )
{
    MemoryServiceDatabase database;
    for ( int32 index = 0; index < 5; ++index )
    {
        LedgerTransferOutcome outcome;
        const string          keyTail = "h" + ServiceKeyUtil::makeHex64( static_cast<uint64>( index ) );
        SW_ASSERT_TRUE(
            Ledger::executeTransfer(
                database,
                makeRequest( keyTail.c_str(), "test.grant", { makePosting( LedgerHolder::makeMint(), LedgerHolder::makeAccount( kLedgerAlice ), "cur.gold", index + 1 ) }, 1000 + index ),
                outcome ) == LedgerResult::Ok );
    }
    vector<LedgerJournalEntry> listEntry;
    string                     cursor;
    SW_ASSERT_TRUE( Ledger::listHistory( database, LedgerHolder::makeAccount( kLedgerAlice ), "", 3, listEntry, cursor ) == ServiceStoreResult::Ok );
    SW_ASSERT_EQUAL( listEntry.size(), size_t( 3 ) );
    SW_EXPECT_EQUAL( listEntry[0]._timeMs, int64( 1004 ) );
    SW_EXPECT_EQUAL( listEntry[0]._listPosting[0]._amount, int64( 5 ) );
    SW_EXPECT_FALSE( cursor.empty() );
    listEntry.clear();
    string nextCursor;
    SW_ASSERT_TRUE( Ledger::listHistory( database, LedgerHolder::makeAccount( kLedgerAlice ), cursor, 3, listEntry, nextCursor ) == ServiceStoreResult::Ok );
    SW_ASSERT_EQUAL( listEntry.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( listEntry[1]._timeMs, int64( 1000 ) );
    SW_EXPECT_TRUE( nextCursor.empty() );
}

SW_TEST_CASE( LedgerTest, StagedTransferJoinsTheCallersTransaction )
{
    MemoryServiceDatabase database;
    SW_ASSERT_TRUE( grant( database, kLedgerAlice, "cur.gold", 50, "g1" ) == LedgerResult::Ok );
    const hashed_string tradeTable{ "test_trade" };
    ServiceTransaction  seed;
    seed.put( tradeTable, "t1", vector<uint8>{ 1 } );
    SW_ASSERT_TRUE( database.commit( seed ) == ServiceStoreResult::Ok );
    ServiceRecord trade;
    SW_ASSERT_TRUE( database.readRecord( tradeTable, "t1", trade ) == ServiceStoreResult::Ok );

    const LedgerTransferRequest request =
        makeRequest( "trade.t1", "trade.settle", { makePosting( LedgerHolder::makeAccount( kLedgerAlice ), LedgerHolder::makeAccount( kLedgerBob ), "cur.gold", 20 ) } );
    ServiceTransaction stale;
    stale.put( tradeTable, "t1", vector<uint8>{ 2 }, trade._version );
    LedgerTransferOutcome outcome;
    SW_ASSERT_TRUE( Ledger::stageTransfer( database, request, stale, outcome ) == LedgerResult::Ok );
    ServiceTransaction bump; // 그새 거래 레코드가 바뀌었다
    bump.put( tradeTable, "t1", vector<uint8>{ 3 } );
    SW_ASSERT_TRUE( database.commit( bump ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( database.commit( stale ) == ServiceStoreResult::Conflict );
    SW_EXPECT_TRUE( Ledger::resolveConflict( database, request, outcome ) == LedgerResult::Conflict ); // 분개 없음 — 적용 안 됨
    SW_EXPECT_EQUAL( readAmount( database, kLedgerBob, "cur.gold" ), int64( 0 ) );

    SW_ASSERT_TRUE( database.readRecord( tradeTable, "t1", trade ) == ServiceStoreResult::Ok );
    ServiceTransaction fresh;
    fresh.put( tradeTable, "t1", vector<uint8>{ 4 }, trade._version );
    SW_ASSERT_TRUE( Ledger::stageTransfer( database, request, fresh, outcome ) == LedgerResult::Ok );
    SW_EXPECT_TRUE( database.commit( fresh ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( readAmount( database, kLedgerBob, "cur.gold" ), int64( 20 ) );
    SW_EXPECT_TRUE( isBalanced( database ) );
}

SW_TEST_CASE( LedgerTest, MalformedRequestsAreRejectedWithoutWrites )
{
    MemoryServiceDatabase database;
    SW_ASSERT_TRUE( grant( database, kLedgerAlice, "cur.gold", 50, "g1" ) == LedgerResult::Ok );
    const uint64          before = database.computeContentHash();
    LedgerTransferOutcome outcome;
    const LedgerHolder    alice = LedgerHolder::makeAccount( kLedgerAlice );
    SW_EXPECT_TRUE( Ledger::executeTransfer( database, makeRequest( "z", "test.x", { makePosting( alice, LedgerHolder::makeAccount( kLedgerBob ), "cur.gold", 0 ) } ), outcome ) ==
                    LedgerResult::Invalid );
    SW_EXPECT_TRUE( Ledger::executeTransfer( database, makeRequest( "s", "test.x", { makePosting( alice, alice, "cur.gold", 1 ) } ), outcome ) == LedgerResult::Invalid );
    SW_EXPECT_TRUE( Ledger::executeTransfer( database, makeRequest( "m", "test.x", { makePosting( LedgerHolder::makeMint(), LedgerHolder::makeSink(), "cur.gold", 1 ) } ),
                                             outcome ) == LedgerResult::Invalid );
    SW_EXPECT_TRUE( Ledger::executeTransfer( database, makeRequest( "u", "test.x", { makePosting( alice, LedgerHolder::makeAccount( kLedgerBob ), "Cur Gold", 1 ) } ), outcome ) ==
                    LedgerResult::Invalid );
    SW_EXPECT_TRUE( Ledger::executeTransfer( database, makeRequest( "r", "Bad Reason", { makePosting( alice, LedgerHolder::makeAccount( kLedgerBob ), "cur.gold", 1 ) } ), outcome ) ==
                    LedgerResult::Invalid );
    SW_EXPECT_TRUE( Ledger::executeTransfer( database, makeRequest( "e", "test.x", {} ), outcome ) == LedgerResult::Invalid );
    vector<LedgerPosting> listTooMany;
    for ( int32 index = 0; index <= LedgerConstant::kMaxPostingCount; ++index )
        listTooMany.push_back( makePosting( alice, LedgerHolder::makeAccount( kLedgerBob ), "cur.gold", 1 ) );
    SW_EXPECT_TRUE( Ledger::executeTransfer( database, makeRequest( "n", "test.x", listTooMany ), outcome ) == LedgerResult::Invalid );
    SW_EXPECT_EQUAL( database.computeContentHash(), before );
}

SW_TEST_CASE( LedgerTest, EscrowHoldsAndReleases )
{
    MemoryServiceDatabase database;
    SW_ASSERT_TRUE( grant( database, kLedgerAlice, "item.gem", 3, "g1" ) == LedgerResult::Ok );
    const LedgerHolder    escrow = LedgerHolder::makeEscrow( "auction", "0000000000000007" );
    LedgerTransferOutcome outcome;
    SW_ASSERT_TRUE( Ledger::executeTransfer( database, makeRequest( "lock", "auction.list", { makePosting( LedgerHolder::makeAccount( kLedgerAlice ), escrow, "item.gem", 3 ) } ),
                                             outcome ) == LedgerResult::Ok );
    vector<LedgerBalance> listBalance;
    SW_ASSERT_TRUE( Ledger::listBalances( database, escrow, listBalance ) == ServiceStoreResult::Ok );
    SW_ASSERT_EQUAL( listBalance.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( listBalance[0]._amount, int64( 3 ) );
    SW_EXPECT_TRUE( Ledger::executeTransfer( database, makeRequest( "over", "auction.settle", { makePosting( escrow, LedgerHolder::makeAccount( kLedgerBob ), "item.gem", 4 ) } ),
                                             outcome ) == LedgerResult::InsufficientFunds ); // 맡김도 음수 금지
    SW_ASSERT_TRUE( Ledger::executeTransfer( database, makeRequest( "rel", "auction.settle", { makePosting( escrow, LedgerHolder::makeAccount( kLedgerBob ), "item.gem", 3 ) } ),
                                             outcome ) == LedgerResult::Ok );
    SW_EXPECT_EQUAL( readAmount( database, kLedgerBob, "item.gem" ), int64( 3 ) );
    SW_EXPECT_TRUE( isBalanced( database ) );
}

SW_TEST_CASE( LedgerTest, RefundClawbackMayCreateDebtThatBlocksSpendingUntilRepaid )
{
    MemoryServiceDatabase database;
    SW_ASSERT_TRUE( grant( database, kLedgerAlice, "cur.gem", 100, "buy" ) == LedgerResult::Ok );
    LedgerTransferOutcome outcome;
    SW_ASSERT_TRUE( Ledger::executeTransfer( database,
                                             makeRequest( "use", "shop.buy", { makePosting( LedgerHolder::makeAccount( kLedgerAlice ), LedgerHolder::makeSink(), "cur.gem", 80 ) } ),
                                             outcome ) == LedgerResult::Ok );

    // 빚 허용 없는 회수는 모자람이다.
    LedgerTransferRequest clawback =
        makeRequest( "refund", "refund.revoke", { makePosting( LedgerHolder::makeAccount( kLedgerAlice ), LedgerHolder::makeSink(), "cur.gem", 100 ) } );
    SW_EXPECT_TRUE( Ledger::executeTransfer( database, clawback, outcome ) == LedgerResult::InsufficientFunds );
    clawback._journalKey = "test/refund2";
    clawback._bAllowDebt = SW_TRUE;
    SW_ASSERT_TRUE( Ledger::executeTransfer( database, clawback, outcome ) == LedgerResult::Ok );
    SW_EXPECT_EQUAL( readAmount( database, kLedgerAlice, "cur.gem" ), int64( -80 ) );

    // 빚이 있는 동안 그 재화는 쓰지 못한다(1 도).
    SW_EXPECT_TRUE( Ledger::executeTransfer( database,
                                             makeRequest( "use2", "shop.buy", { makePosting( LedgerHolder::makeAccount( kLedgerAlice ), LedgerHolder::makeSink(), "cur.gem", 1 ) } ),
                                             outcome ) == LedgerResult::InsufficientFunds );
    SW_EXPECT_TRUE( Ledger::executeTransfer( database,
                                             makeRequest( "give", "test.give", { makePosting( LedgerHolder::makeAccount( kLedgerAlice ), LedgerHolder::makeAccount( kLedgerBob ), "cur.gem", 1 ) } ),
                                             outcome ) == LedgerResult::InsufficientFunds );

    // 빚은 맡김에는 없다.
    LedgerTransferRequest escrowDebt =
        makeRequest( "esc", "test.x", { makePosting( LedgerHolder::makeEscrow( "mail", "m1" ), LedgerHolder::makeAccount( kLedgerBob ), "cur.gem", 1 ) } );
    escrowDebt._bAllowDebt = SW_TRUE;
    SW_EXPECT_TRUE( Ledger::executeTransfer( database, escrowDebt, outcome ) == LedgerResult::InsufficientFunds );

    // 받는 이동이 빚을 먼저 갚고, 다 갚은 뒤에야 쓸 수 있다.
    SW_ASSERT_TRUE( grant( database, kLedgerAlice, "cur.gem", 50, "reward1" ) == LedgerResult::Ok );
    SW_EXPECT_EQUAL( readAmount( database, kLedgerAlice, "cur.gem" ), int64( -30 ) );
    SW_ASSERT_TRUE( grant( database, kLedgerAlice, "cur.gem", 40, "reward2" ) == LedgerResult::Ok );
    SW_EXPECT_EQUAL( readAmount( database, kLedgerAlice, "cur.gem" ), int64( 10 ) );
    SW_EXPECT_TRUE( Ledger::executeTransfer( database,
                                             makeRequest( "use3", "shop.buy", { makePosting( LedgerHolder::makeAccount( kLedgerAlice ), LedgerHolder::makeSink(), "cur.gem", 10 ) } ),
                                             outcome ) == LedgerResult::Ok );

    LedgerAuditReport report;
    SW_ASSERT_TRUE( LedgerAudit::computeReport( database, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( report.isBalanced() );
    SW_EXPECT_EQUAL( report._debtBalanceCount, 0 );
}

SW_TEST_CASE( LedgerTest, AuditCountsDebtWithoutCallingItAViolation )
{
    MemoryServiceDatabase database;
    LedgerTransferRequest clawback =
        makeRequest( "refund", "refund.revoke", { makePosting( LedgerHolder::makeAccount( kLedgerAlice ), LedgerHolder::makeSink(), "cur.gem", 5 ) } );
    clawback._bAllowDebt = SW_TRUE;
    LedgerTransferOutcome outcome;
    SW_ASSERT_TRUE( Ledger::executeTransfer( database, clawback, outcome ) == LedgerResult::Ok );
    LedgerAuditReport report;
    SW_ASSERT_TRUE( LedgerAudit::computeReport( database, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( report._debtBalanceCount, 1 );
    SW_EXPECT_EQUAL( report._negativeEscrowCount, 0 );
    SW_EXPECT_TRUE( report.isBalanced() ); // 보유 −5 = 발행 0 − 소각 5
}
