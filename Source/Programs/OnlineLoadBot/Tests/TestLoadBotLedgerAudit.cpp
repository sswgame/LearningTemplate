// LoadBotLedgerAuditTest — 부하 봇 끝의 원장 감사(감사 O-12): 봇이 저장소 앞에 맡기는 감사가 균형인 원장은 통과시키고, 원장 밖에서 바뀐 원장(발행 없이 남은 보유)은 잡는다.
// 저장소 앞의 커밋 · 충돌 수(Conflict 재시도율)가 일의 입구에서 세어지는지, 스냅샷 읽기 감사가 도중의 쓰기를 보지 않는지도 본다. 메모리 저장소(nogpu), `UnitTests.OnlineKits` 가 돌린다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/LedgerAuditReport.h"
#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordReader.h"
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

    /**
     * @brief 감사가 잔액 표를 처음 훑기 바로 앞에 원본 데이터에 발행 하나(금 50)를 끼워 넣는 연결입니다 — 분개는 이미 읽었고 잔액은 아직 읽지 않은 틈이다.
     * @details 메모리 저장소는 일 하나를 한 번에 돌려 그 틈이 저절로 나지 않는다. PostgreSQL 에서 다른 서버의 예약 작업이 끼는 자리를 이 연결이 만든다.
     */
    class AuditInterleavingConnection final : public IServiceStoreConnection
    {
    public:
        AuditInterleavingConnection( IServiceStoreConnection& inner, MemoryServiceDatabase& database, string_view journalKey )
            : _inner{ inner }
            , _database{ database }
            , _journalKey{ journalKey }
        {
        }

        ServiceStoreResult readRecord( const hashed_string& table, string_view key, ServiceRecord& outRecord ) override { return _inner.readRecord( table, key, outRecord ); }
        ServiceStoreResult listRecords( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending, vector<ServiceRecord>& outListRecord ) override
        {
            if ( _bInterleaved == false && table == Ledger::getBalanceTable() )
            {
                _bInterleaved = true;
                LedgerTransferResult moved;
                SW_EXPECT_TRUE( Ledger::executeTransfer( _database, makeGrant( 50, _journalKey ), moved ) == LedgerResult::Ok );
            }
            return ServiceRecordReader::readPage( _inner, table, keyPrefix, cursorKey, maxCount, bDescending, outListRecord )._result;
        }
        ServiceStoreResult commit( const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo = nullptr ) override { return _inner.commit( transaction, pOutInfo ); }

    private:
        IServiceStoreConnection& _inner;
        MemoryServiceDatabase&   _database;
        string_view              _journalKey;            ///< 끼울 발행의 분개 키 — 판마다 달라야 멱등 재생으로 묻히지 않는다
        bool                     _bInterleaved{ false }; ///< NOLINT(sw-bool-flag): 테스트 연결 하나의 한 번 깃발이다
    };

    /** @brief @p kind 의 일로 끼어드는 연결 위에서 감사를 돌려 결과를 돌려줍니다(메모리 저장소는 맡길 때 돌고 `pollCompletions` 가 완료를 부른다). */
    ServiceStoreRead<LedgerAuditReport> runInterleavedAudit( MemoryServiceStore& store, MemoryServiceDatabase& database, ServiceStoreJobKind kind, string_view journalKey )
    {
        ServiceStoreTracker tracker;
        tracker.bindStore( &store );
        ServiceStoreRead<LedgerAuditReport> audit;
        const auto                          run = [&database, journalKey]( IServiceStoreConnection& connection )
        {
            AuditInterleavingConnection         interleaving( connection, database, journalKey );
            ServiceStoreRead<LedgerAuditReport> read;
            read._result = LedgerAuditReport::computeReport( interleaving, read._value );
            return read;
        };
        const auto complete = [&audit]( ServiceStoreRead<LedgerAuditReport>& read )
        { audit = std::move( read ); };
        if ( kind == ServiceStoreJobKind::SnapshotRead )
            tracker.submitSnapshotRead( run, complete );
        else
            tracker.submit( run, complete );
        (void)store.pollCompletions();
        return audit;
    }
} // namespace

/**
 * @brief [LoadBotLedgerAuditTest] 스냅샷 읽기 감사는 도중에 끼는 쓰기를 보지 않고 시작 시점 기준으로 균형이다. 같은 틈을 스냅샷 없이 읽으면 분개(전)와 잔액(후)이 어긋나 거짓 불균형이다
 * @details 변이: 메모리 저장소가 스냅샷 일에도 원본 연결을 넘기면 첫 판정(균형 · 보유 100)이 진다.
 */
SW_TEST_CASE( LoadBotLedgerAuditTest, SnapshotAuditReadsOnePointInTime )
{
    MemoryServiceDatabase database;
    MemoryServiceStore    store( &database );
    LedgerTransferResult  result;
    SW_REQUIRE_TRUE( Ledger::executeTransfer( database, makeGrant( 100, "test/grant.before" ), result ) == LedgerResult::Ok );

    const ServiceStoreRead<LedgerAuditReport> snapshot = runInterleavedAudit( store, database, ServiceStoreJobKind::SnapshotRead, "test/grant.during.a" );
    SW_REQUIRE_TRUE( snapshot.isOk() );
    SW_EXPECT_TRUE( snapshot._value.isBalanced() );
    const LedgerAssetAudit* pGold = snapshot._value.findAsset( kGold );
    SW_REQUIRE_TRUE( pGold != nullptr );
    SW_EXPECT_EQUAL( int64( 100 ), pGold->_issued ); // 시작 시점 — 끼어든 발행 50 은 보이지 않는다
    SW_EXPECT_EQUAL( int64( 100 ), pGold->_held );

    // 끼어든 쓰기는 원본에 남았다 — 다음 스냅샷 감사가 본다
    const expected<LedgerAuditReport> after = LoadBotLocalServer::runLedgerAudit( store );
    SW_REQUIRE_TRUE( after.has_value() );
    SW_EXPECT_TRUE( after->isBalanced() );
    SW_REQUIRE_TRUE( after->findAsset( kGold ) != nullptr );
    SW_EXPECT_EQUAL( int64( 150 ), after->findAsset( kGold )->_issued );

    // 스냅샷 없이 같은 틈을 읽으면: 분개는 끼기 전(150), 잔액은 낀 뒤(200) — 쪽마다 시점이 다른 읽기의 거짓 불균형
    const ServiceStoreRead<LedgerAuditReport> mixed = runInterleavedAudit( store, database, ServiceStoreJobKind::ReadWrite, "test/grant.during.b" );
    SW_REQUIRE_TRUE( mixed.isOk() );
    SW_EXPECT_FALSE( mixed._value.isBalanced() );
}

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
