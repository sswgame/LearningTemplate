// LoadBotLedgerAuditTest — 부하 봇 끝의 원장 감사(감사 O-12): 봇이 저장소 앞에 맡기는 감사가 균형인 원장은 통과시키고, 원장 밖에서 바뀐 원장(발행 없이 남은 보유)은 잡는다.
// 저장소 앞의 커밋 · 충돌 수(Conflict 재시도율)가 일의 입구에서 세어지는지도 본다. 메모리 저장소(nogpu), `UnitTests.OnlineKits` 가 돌린다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Ledger/LedgerAuditReport.h"
#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStoreTracker.h"

#include "Programs/OnlineLoadBot/LoadBotLocalServer.h"

using namespace sw;

namespace
{
    constexpr uint64      kAccountID = 101;
    constexpr const utf8* kGold      = "cur.gold";

    /** @brief 발행 → 계정 @p amount 금입니다. */
    LedgerTransferRequest makeGrant( int64 amount, string_view journalKey )
    {
        LedgerTransferRequest request;
        request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeAccount( kAccountID ), string( kGold ), amount } );
        request._journalKey = string( journalKey );
        request._reason     = "test.grant";
        request._timeMs     = 1700000000000ll;
        return request;
    }
} // namespace

/**
 * @brief [LoadBotLedgerAuditTest] 균형인 원장은 감사를 통과하고, 분개를 잃어(원장 밖 쓰기) 발행 없이 보유만 남은 원장은 불균형으로 잡힌다 — 봇은 이것으로 종료 코드 5 를 낸다
 * @details 변이: 감사를 저장소에 맡기지 않고 건너뛰거나 `isBalanced` 대신 늘 참을 넣으면 두 번째 판정이 진다.
 */
SW_TEST_CASE( LoadBotLedgerAuditTest, AuditCatchesAnImbalance )
{
    MemoryServiceDatabase database;
    MemoryServiceStore    store( &database );
    LedgerTransferResult  result;
    SW_REQUIRE_TRUE( Ledger::executeTransfer( database, makeGrant( 100, "test/grant.a" ), result ) == LedgerResult::Ok );

    const expected<LedgerAuditReport> balanced = LoadBotLocalServer::runLedgerAudit( store );
    SW_REQUIRE_TRUE( balanced.has_value() );
    SW_EXPECT_TRUE( balanced->isBalanced() );
    const LoadBotServerStats balancedStats = LoadBotLocalServer::makeServerStats( store.getCommitStats(), *balanced );
    SW_EXPECT_TRUE( balancedStats._bRead == SW_TRUE );
    SW_EXPECT_TRUE( balancedStats._bBalanced == SW_TRUE );
    SW_EXPECT_EQUAL( 0, balancedStats._mismatchedAssetCount );

    // 원장 밖에서 분개를 지운다 — 발행 합에서 100 이 빠지고 계정 잔액 100 은 남는다(재화가 생긴 것과 같다)
    vector<ServiceRecord> listJournal;
    SW_REQUIRE_TRUE( database.listRecords( Ledger::getJournalTable(), "", "", 10, false, listJournal ) == ServiceStoreResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), listJournal.size() );
    ServiceTransaction lostJournal;
    lostJournal.erase( Ledger::getJournalTable(), listJournal[0]._key, listJournal[0]._version );
    SW_REQUIRE_TRUE( database.commit( lostJournal ) == ServiceStoreResult::Ok );

    const expected<LedgerAuditReport> broken = LoadBotLocalServer::runLedgerAudit( store );
    SW_REQUIRE_TRUE( broken.has_value() );
    SW_EXPECT_FALSE( broken->isBalanced() );
    const LoadBotServerStats brokenStats = LoadBotLocalServer::makeServerStats( store.getCommitStats(), *broken );
    SW_EXPECT_TRUE( brokenStats._bBalanced == SW_FALSE );
    SW_EXPECT_EQUAL( 1, brokenStats._mismatchedAssetCount );
}

/**
 * @brief [LoadBotLedgerAuditTest] 저장소 앞은 일의 커밋과 그 가운데 버전 충돌을 센다 — 봇 보고서의 Conflict 재시도율(충돌 / 커밋)이 이 수다
 * @details "없어야 한다" 쓰기를 같은 키에 두 번 맡기면 두 번째가 Conflict 다. 변이: 읽기 집합 포장이 세지 않거나 충돌을 커밋으로만 세면 진다.
 */
SW_TEST_CASE( LoadBotLedgerAuditTest, StoreCountsCommitsAndConflicts )
{
    static const hashed_string kTable{ "loadbot_test" };
    MemoryServiceDatabase      database;
    MemoryServiceStore         store( &database );
    ServiceStoreTracker        tracker;
    tracker.bindStore( &store );
    ServiceStoreResult arrResult[2] = { ServiceStoreResult::Unavailable, ServiceStoreResult::Unavailable };
    for ( ServiceStoreResult& slot : arrResult )
    {
        tracker.submit( []( IServiceStoreConnection& connection )
        {
            ServiceTransaction transaction;
            transaction.put( kTable, "key", vector<uint8>{ 1 }, ServiceRecord::kAbsentVersion );
            return connection.commit( transaction );
        }, [&slot]( ServiceStoreResult committed )
        { slot = committed; } );
    }
    (void)store.pollCompletions();
    SW_EXPECT_TRUE( arrResult[0] == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( arrResult[1] == ServiceStoreResult::Conflict );
    SW_EXPECT_EQUAL( int64( 2 ), store.getCommitStats()._commitCount.load() );
    SW_EXPECT_EQUAL( int64( 1 ), store.getCommitStats()._conflictCount.load() );
}
