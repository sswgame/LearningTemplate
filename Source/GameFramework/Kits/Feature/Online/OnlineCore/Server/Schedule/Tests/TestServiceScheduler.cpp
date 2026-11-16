// ServiceSchedulerTest — 예약 작업: 회차 계산, 회차마다 서버 하나만 실행, 임대가 지나면 이어받기, 지나친 회차, 저장소 실패 뒤 다시 하기. ServiceTTLTest — 보존 기간 쓸기와 그 예약 작업(메모리 저장소, nogpu).
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Online/Schedule/Server/ServiceScheduler.h"
#include "GameFramework/Runtime/Online/Schedule/Server/ServiceTTLMonitor.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceTTLRule.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

using namespace sw;

namespace
{
    constexpr int64 kNowMs    = 1700000000000ll; // 2023-11-14 22:13:20 UTC, 화요일
    constexpr int64 kMinuteMs = 60000;
    constexpr int64 kDayMs    = 24 * 60 * kMinuteMs;
    constexpr int64 kDayStart = ( kNowMs / kDayMs ) * kDayMs;

    struct RunSink
    {
        vector<ScheduledRun> _listRun{};

        void onRun( const ScheduledRun& run ) { _listRun.push_back( run ); }
    };

    ScheduleDef makeDaily( int32 minuteOfDay = 0 )
    {
        ScheduleDef definition;
        definition._jobID       = "daily.reward";
        definition._kind        = ScheduleKind::Daily;
        definition._minuteOfDay = minuteOfDay;
        return definition;
    }

    struct SchedulerFixture
    {
        MemoryServiceDatabase _database;
        MemoryServiceStore    _store;
        ServiceScheduler      _first;
        ServiceScheduler      _second;
        RunSink               _firstSink;
        RunSink               _secondSink;

        explicit SchedulerFixture( const ScheduleDef& definition = makeDaily() )
            : _database{}
            , _store{ &_database }
            , _first{}
            , _second{}
            , _firstSink{}
            , _secondSink{}
        {
            _first.initialize( &_store, 1 );
            _second.initialize( &_store, 2 );
            SW_EXPECT_TRUE( _first.registerJob( definition, ScheduledJobFunction::create<&RunSink::onRun>( &_firstSink ) ) );
            SW_EXPECT_TRUE( _second.registerJob( definition, ScheduledJobFunction::create<&RunSink::onRun>( &_secondSink ) ) );
        }

        void poll()
        {
            while ( _store.pollCompletions() > 0 )
            {
            }
        }

        void tickBoth( int64 nowMs )
        {
            _first.update( UnixMilliseconds{ nowMs } );
            _second.update( UnixMilliseconds{ nowMs } );
            poll();
        }
    };

    bool readLeadingTime( const ServiceRecord& record, int64& outTimeMs )
    {
        if ( record._bytes.size() < sizeof( int64 ) )
            return false;
        Memory::copy( &outTimeMs, record._bytes.data(), sizeof( int64 ) );
        return true;
    }

    struct TTLFixture
    {
        MemoryServiceDatabase _database;
        hashed_string         _table{ "ttl_test" };
        hashed_string         _ttlIndexTable{ "ttl_test_index" }; ///< TTL 색인 규칙용이다

        void put( string_view key, int64 timeMs )
        {
            vector<uint8> bytes( sizeof( int64 ) );
            Memory::copy( bytes.data(), &timeMs, sizeof( int64 ) );
            ServiceTransaction transaction;
            transaction.put( _table, key, bytes, ServiceRecord::kAbsentVersion );
            SW_REQUIRE_TRUE( _database.commit( transaction ) == ServiceStoreResult::Ok );
        }

        ServiceTTLRule makeRule( int64 expireAfterMs = 1000 ) const
        {
            ServiceTTLRule rule;
            rule._table             = _table;
            rule._expireAfterMs     = expireAfterMs;
            rule._pfnReadTime       = &readLeadingTime;
            rule._maxScanPerSweep   = 100;
            rule._maxDeletePerSweep = 100;
            return rule;
        }

        int32 count() const { return _database.countRecords( _table ); }

        void putIndex( int64 timeMs, string_view target )
        {
            ServiceTransaction transaction;
            transaction.put( _ttlIndexTable, ServiceTTLRule::makeTTLIndexKey( timeMs, target ), vector<uint8>{}, ServiceRecord::kAnyVersion );
            SW_REQUIRE_TRUE( _database.commit( transaction ) == ServiceStoreResult::Ok );
        }

        ServiceTTLRule makeIndexRule( int64 expireAfterMs = 1000 ) const
        {
            ServiceTTLRule rule = makeRule( expireAfterMs );
            rule._ttlIndexTable = _ttlIndexTable;
            return rule;
        }

        int32 countIndex() const { return _database.countRecords( _ttlIndexTable ); }
    };
} // namespace

/** @brief [ServiceSchedulerTest] 일일 회차: 오늘 시각이 지났으면 오늘, 아직이면 어제다 */
SW_TEST_CASE( ServiceSchedulerTest, DailyOccurrence )
{
    SW_EXPECT_EQUAL( kDayStart + 60 * kMinuteMs, ServiceScheduler::computeLatestOccurrence( makeDaily( 60 ), kNowMs ) );
    SW_EXPECT_EQUAL( kDayStart - kDayMs + 23 * 60 * kMinuteMs, ServiceScheduler::computeLatestOccurrence( makeDaily( 23 * 60 ), kNowMs ) );
    const int64 exactMs = kDayStart + 22 * 60 * kMinuteMs;
    SW_EXPECT_EQUAL( exactMs, ServiceScheduler::computeLatestOccurrence( makeDaily( 22 * 60 ), exactMs ) ); // 정각은 그 회차다
    SW_EXPECT_EQUAL( int64( -1 ), ServiceScheduler::computeLatestOccurrence( makeDaily(), -1 ) );
}

/** @brief [ServiceSchedulerTest] 주간 회차: 요일은 월요일 0 이고, 오늘이 그 요일이어도 시각 전이면 지난주다 */
SW_TEST_CASE( ServiceSchedulerTest, WeeklyOccurrence )
{
    ScheduleDef weekly;
    weekly._jobID = "weekly.reset";
    weekly._kind  = ScheduleKind::Weekly;

    weekly._dayOfWeek = 0; // 월요일 - 어제다
    SW_EXPECT_EQUAL( kDayStart - kDayMs, ServiceScheduler::computeLatestOccurrence( weekly, kNowMs ) );
    weekly._dayOfWeek = 1; // 화요일 - 오늘이다
    SW_EXPECT_EQUAL( kDayStart, ServiceScheduler::computeLatestOccurrence( weekly, kNowMs ) );
    weekly._minuteOfDay = 23 * 60; // 오늘 저녁 전이다
    SW_EXPECT_EQUAL( kDayStart - 7 * kDayMs + 23 * 60 * kMinuteMs, ServiceScheduler::computeLatestOccurrence( weekly, kNowMs ) );
    weekly._minuteOfDay = 0;
    weekly._dayOfWeek   = 2; // 수요일 - 지난주다
    SW_EXPECT_EQUAL( kDayStart - 6 * kDayMs, ServiceScheduler::computeLatestOccurrence( weekly, kNowMs ) );
}

/** @brief [ServiceSchedulerTest] 기간은 [시작, 끝) 안에서 시작 시각 하나이고, 간격은 UTC 0 기준 배수다 */
SW_TEST_CASE( ServiceSchedulerTest, WindowAndIntervalOccurrence )
{
    ScheduleDef window;
    window._jobID         = "window.event";
    window._kind          = ScheduleKind::Window;
    window._windowStartMs = kNowMs - 10;
    window._windowEndMs   = kNowMs + 10;
    SW_EXPECT_EQUAL( window._windowStartMs, ServiceScheduler::computeLatestOccurrence( window, kNowMs ) );
    SW_EXPECT_EQUAL( int64( -1 ), ServiceScheduler::computeLatestOccurrence( window, window._windowEndMs ) );
    SW_EXPECT_EQUAL( int64( -1 ), ServiceScheduler::computeLatestOccurrence( window, window._windowStartMs - 1 ) );

    ScheduleDef interval;
    interval._jobID      = "interval.sweep";
    interval._kind       = ScheduleKind::Interval;
    interval._intervalMs = 10 * kMinuteMs;
    SW_EXPECT_EQUAL( ( kNowMs / interval._intervalMs ) * interval._intervalMs, ServiceScheduler::computeLatestOccurrence( interval, kNowMs ) );
}

/** @brief [ServiceSchedulerTest] 일정 검사: 작업 id 글자 · 시각 범위 · 요일 · 기간 · 간격 상한. 틀리면 등록하지 않는다 */
SW_TEST_CASE( ServiceSchedulerTest, DefinitionRules )
{
    SW_EXPECT_LOG( "ServiceScheduler.IgnoredScheduledJobInvalid" );
    SW_EXPECT_TRUE( ServiceScheduler::isValidDefinition( makeDaily() ) );

    ScheduleDef bad = makeDaily();
    bad._jobID      = "Upper/Case";
    SW_EXPECT_FALSE( ServiceScheduler::isValidDefinition( bad ) );
    bad._jobID.clear();
    SW_EXPECT_FALSE( ServiceScheduler::isValidDefinition( bad ) );
    SW_EXPECT_FALSE( ServiceScheduler::isValidDefinition( makeDaily( 24 * 60 ) ) );
    SW_EXPECT_FALSE( ServiceScheduler::isValidDefinition( makeDaily( -1 ) ) );

    ScheduleDef weekly = makeDaily();
    weekly._kind       = ScheduleKind::Weekly;
    weekly._dayOfWeek  = 7;
    SW_EXPECT_FALSE( ServiceScheduler::isValidDefinition( weekly ) );

    ScheduleDef window    = makeDaily();
    window._kind          = ScheduleKind::Window;
    window._windowStartMs = 10;
    window._windowEndMs   = 10;
    SW_EXPECT_FALSE( ServiceScheduler::isValidDefinition( window ) );

    ScheduleDef interval = makeDaily();
    interval._kind       = ScheduleKind::Interval;
    interval._intervalMs = kMinuteMs - 1;
    SW_EXPECT_FALSE( ServiceScheduler::isValidDefinition( interval ) );

    ServiceScheduler scheduler;
    RunSink          sink;
    SW_EXPECT_FALSE( scheduler.registerJob( bad, ScheduledJobFunction::create<&RunSink::onRun>( &sink ) ) );
    SW_EXPECT_FALSE( scheduler.registerJob( makeDaily(), ScheduledJobFunction{} ) );
}

/** @brief [ServiceSchedulerTest] 회차마다 서버 하나만 돈다. 끝낸 회차는 다른 서버가 임대 뒤에도 다시 돌지 않고, 다음 회차에 다시 돈다 */
SW_TEST_CASE( ServiceSchedulerTest, ExactlyOneServerRunsAnOccurrence )
{
    SchedulerFixture rig;
    rig.tickBoth( kNowMs );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._firstSink._listRun.size() );
    SW_EXPECT_TRUE( rig._secondSink._listRun.empty() );
    const ScheduledRun run = rig._firstSink._listRun[0];
    SW_EXPECT_EQUAL( kDayStart, run._occurrenceMs.count() );

    rig._first.completeRun( run, true );
    rig.poll();
    SW_EXPECT_EQUAL( 0, rig._first.getPendingJobCount() );

    const int64 afterLease = kNowMs + ScheduleDef{}._leaseMs + 1;
    rig.tickBoth( afterLease );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._firstSink._listRun.size() );
    SW_EXPECT_TRUE( rig._secondSink._listRun.empty() );

    rig.tickBoth( kNowMs + kDayMs );
    SW_EXPECT_EQUAL( size_t( 2 ), rig._firstSink._listRun.size() + rig._secondSink._listRun.size() );
}

/** @brief [ServiceSchedulerTest] 실행하던 서버가 끝을 적지 못하면 임대가 지난 뒤 다른 서버가 이어받는다. 옛 서버의 끝 기록은 이어받은 레코드를 덮지 않는다 */
SW_TEST_CASE( ServiceSchedulerTest, ExpiredLeaseIsTakenOver )
{
    SW_EXPECT_LOG( "ServiceScheduler.CouldNotRecordEnd" );
    SchedulerFixture rig;
    rig.tickBoth( kNowMs );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._firstSink._listRun.size() );

    rig._second.update( UnixMilliseconds{ kNowMs + 1000 } ); // 아직 임대 안이다
    rig.poll();
    SW_EXPECT_TRUE( rig._secondSink._listRun.empty() );

    const int64 afterLease = kNowMs + ScheduleDef{}._leaseMs;
    rig._second.update( UnixMilliseconds{ afterLease } );
    rig.poll();
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._secondSink._listRun.size() );
    const ScheduledRun taken = rig._secondSink._listRun[0];
    SW_EXPECT_EQUAL( kDayStart, taken._occurrenceMs.count() );

    rig._first.completeRun( rig._firstSink._listRun[0], true ); // 옛 서버가 뒤늦게 끝을 적는다
    rig.poll();
    rig._second.completeRun( taken, true );
    rig.poll();
}

/** @brief [ServiceSchedulerTest] 꺼져 있어 지나친 회차는 가장 최근 하나만 돈다 */
SW_TEST_CASE( ServiceSchedulerTest, MissedOccurrencesRunOnce )
{
    SchedulerFixture rig;
    rig._first.update( UnixMilliseconds{ kNowMs + 5 * kDayMs } );
    rig.poll();
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._firstSink._listRun.size() );
    SW_EXPECT_EQUAL( kDayStart + 5 * kDayMs, rig._firstSink._listRun[0]._occurrenceMs.count() );
    rig._first.update( UnixMilliseconds{ kNowMs + 5 * kDayMs + 1 } );
    rig.poll();
    SW_EXPECT_EQUAL( size_t( 1 ), rig._firstSink._listRun.size() );
}

/** @brief [ServiceSchedulerTest] 저장소가 실패하면 처리기를 부르지 않고, 재시도 시각 전에는 다시 묻지 않으며, 그 뒤에 돈다 */
SW_TEST_CASE( ServiceSchedulerTest, StoreFailureRetriesLater )
{
    SchedulerFixture rig;
    rig._database.armFault( ServiceStoreFault::RejectCommit );
    rig._first.update( UnixMilliseconds{ kNowMs } );
    rig.poll();
    SW_EXPECT_TRUE( rig._firstSink._listRun.empty() );

    rig._first.update( UnixMilliseconds{ kNowMs + 4999 } );
    SW_EXPECT_EQUAL( 0, rig._first.getPendingJobCount() );

    rig._first.update( UnixMilliseconds{ kNowMs + 5000 } );
    rig.poll();
    SW_EXPECT_EQUAL( size_t( 1 ), rig._firstSink._listRun.size() );
}

/** @brief [ServiceSchedulerTest] 실패로 끝낸 회차도 끝난 것이다 - 처리기가 멱등이어도 다른 서버가 다시 돌지 않고 다음 회차를 기다린다 */
SW_TEST_CASE( ServiceSchedulerTest, FailedRunIsFinished )
{
    SchedulerFixture rig;
    rig._first.update( UnixMilliseconds{ kNowMs } );
    rig.poll();
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._firstSink._listRun.size() );
    rig._first.completeRun( rig._firstSink._listRun[0], false );
    rig.poll();
    rig._second.update( UnixMilliseconds{ kNowMs + 1 } );
    rig.poll();
    SW_EXPECT_TRUE( rig._secondSink._listRun.empty() );
}

/** @brief [ServiceSchedulerTest] TTL: 회차마다 쌓이는 차지 레코드를 끝난 뒤 기간이 지나면 지운다. 레코드보다 긴 구간 작업은 등록을 거절한다(감사 O-9) */
SW_TEST_CASE( ServiceSchedulerTest, RunRecordsExpireAndLongWindowsAreRefused )
{
    SW_EXPECT_LOG( "ServiceScheduler.WindowOutlivesRunRecord" );
    SchedulerFixture rig;
    rig.tickBoth( kNowMs );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._firstSink._listRun.size() + rig._secondSink._listRun.size() );
    ServiceScheduler&  winner = rig._firstSink._listRun.empty() ? rig._second : rig._first;
    const ScheduledRun run    = rig._firstSink._listRun.empty() ? rig._secondSink._listRun[0] : rig._firstSink._listRun[0];
    winner.update( UnixMilliseconds{ kNowMs + 10 } );
    winner.completeRun( run, true );
    rig.poll();
    SW_REQUIRE_EQUAL( 1, rig._database.countRecords( ServiceScheduler::getTable() ) );

    ServiceTTLRule        rule = ServiceScheduler::makeRunTTLRule( 1000 );
    ServiceTTLSweepReport report;
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs + 500, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 1, rig._database.countRecords( ServiceScheduler::getTable() ) ); // 아직 기간 안이다
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs + 2000, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 0, rig._database.countRecords( ServiceScheduler::getTable() ) );
    SW_EXPECT_EQUAL( 0, rig._database.countRecords( hashed_string( "service_schedule_expiry" ) ) ); // 차지 줄 · 끝 줄 둘 다 지웠다

    ServiceScheduler guarded;
    guarded.initialize( &rig._store, 3 );
    guarded.setRunTTLMs( 1000 );
    ScheduleDef window;
    window._jobID         = "window.job";
    window._kind          = ScheduleKind::Window;
    window._windowStartMs = kNowMs;
    window._windowEndMs   = kNowMs + 1000;
    RunSink sink;
    SW_EXPECT_FALSE( guarded.registerJob( window, ScheduledJobFunction::create<&RunSink::onRun>( &sink ) ) );
    window._windowEndMs = kNowMs + 999;
    SW_EXPECT_TRUE( guarded.registerJob( window, ScheduledJobFunction::create<&RunSink::onRun>( &sink ) ) );
    guarded.shutdown();
}

/** @brief [ServiceTTLTest] 보존 기간이 지난 것만 지운다. 경계는 지난 것이고, 시각을 읽지 못하는 레코드는 남긴다 */
SW_TEST_CASE( ServiceTTLTest, DeletesOnlyExpired )
{
    TTLFixture rig;
    rig.put( "a.old", kNowMs - 2000 );
    rig.put( "b.edge", kNowMs - 1000 );
    rig.put( "c.new", kNowMs - 999 );
    ServiceTransaction unreadable;
    unreadable.put( rig._table, "d.bad", vector<uint8>{ 1 }, ServiceRecord::kAbsentVersion );
    SW_REQUIRE_TRUE( rig._database.commit( unreadable ) == ServiceStoreResult::Ok );

    ServiceTTLRule        rule = rig.makeRule();
    ServiceTTLSweepReport report;
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 2, report._deletedCount );
    SW_EXPECT_EQUAL( 4, report._scannedCount );
    SW_EXPECT_EQUAL( SW_TRUE, report._bWrapped );
    SW_EXPECT_EQUAL( 2, rig.count() );
    SW_EXPECT_TRUE( rule._cursor.empty() );
}

/** @brief [ServiceTTLTest] 상한에 닿으면 커서를 남겨 다음 회차가 이어서 지운다 */
SW_TEST_CASE( ServiceTTLTest, LimitsResumeFromCursor )
{
    TTLFixture rig;
    for ( const utf8* pKey : { "a", "b", "c", "d" } )
    {
        rig.put( pKey, kNowMs - 2000 );
    }
    ServiceTTLRule        rule = rig.makeRule();
    ServiceTTLSweepReport report;
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs, 100, 2, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 2, report._deletedCount );
    SW_EXPECT_EQUAL( 2, rig.count() );
    SW_EXPECT_EQUAL( string( "b" ), rule._cursor );

    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs, 1, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 1, report._scannedCount );
    SW_EXPECT_EQUAL( 1, rig.count() );
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 0, rig.count() );
}

/** @brief [ServiceTTLTest] 쓰기가 실패하면 그 결과를 돌려주고 커서가 지나가지 않아 다음 회차가 같은 레코드를 지운다 */
SW_TEST_CASE( ServiceTTLTest, FailedCommitDoesNotSkipRecords )
{
    TTLFixture rig;
    rig.put( "a", kNowMs - 2000 );
    ServiceTTLRule        rule = rig.makeRule();
    ServiceTTLSweepReport report;
    rig._database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs, 100, 100, report ) == ServiceStoreResult::Unavailable );
    SW_EXPECT_EQUAL( 1, rig.count() );

    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 1, report._deletedCount );
    SW_EXPECT_EQUAL( 0, rig.count() );
}

/** @brief [ServiceTTLTest] 시각 읽기나 기간이 없는 규칙은 거절한다 */
SW_TEST_CASE( ServiceTTLTest, InvalidRuleIsRefused )
{
    TTLFixture            rig;
    ServiceTTLRule        rule = rig.makeRule( 0 );
    ServiceTTLSweepReport report;
    SW_EXPECT_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs, 100, 100, report ) == ServiceStoreResult::Invalid );
    rule              = rig.makeRule();
    rule._pfnReadTime = nullptr;
    SW_EXPECT_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs, 100, 100, report ) == ServiceStoreResult::Invalid );
}

/** @brief [ServiceTTLTest] TTL 색인: 기간이 지난 줄까지만 읽고, 대상이 없거나 다시 써서 늦춰졌으면 색인 줄만 지운다 */
SW_TEST_CASE( ServiceTTLTest, IndexExpiresByRange )
{
    TTLFixture rig;
    rig.put( "a", kNowMs - 2000 );
    rig.putIndex( kNowMs - 2000, "a" );    // 지운다
    rig.putIndex( kNowMs - 1500, "gone" ); // 대상이 없다 - 색인 줄만 지운다
    rig.put( "b", kNowMs - 10 );
    rig.putIndex( kNowMs - 1200, "b" ); // 다시 써서 시각이 늦춰졌다 - 레코드는 남는다
    rig.putIndex( kNowMs - 10, "b" );
    rig.put( "c", kNowMs - 5000 );
    rig.putIndex( kNowMs - 500, "c" ); // 색인이 아직 기간 안이다 - 레코드가 오래됐어도 보지 않는다

    ServiceTTLRule        rule = rig.makeIndexRule();
    ServiceTTLSweepReport report;
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 1, report._deletedCount );
    SW_EXPECT_EQUAL( 3, report._scannedCount ); // 기간 안의 줄에서 멈춘다
    SW_EXPECT_EQUAL( 2, rig.count() );
    SW_EXPECT_EQUAL( 2, rig.countIndex() );
    SW_EXPECT_TRUE( rule._cursor.empty() );
}

/** @brief [ServiceTTLTest] TTL 색인의 접두어 묶음: 한 쪽보다 큰 묶음도 한 회차에 지우고, 기간 안의 레코드에서 멈춘 뒤 색인 줄을 지운다 */
SW_TEST_CASE( ServiceTTLTest, IndexGroupExpiresPrefix )
{
    TTLFixture rig;
    for ( int32 index = 0; index < 100; ++index )
    {
        rig.put( "ch/" + ServiceKeyUtil::makeHex64( static_cast<uint64>( index ) ), kNowMs - 5000 + index );
    }
    rig.put( "ch/" + ServiceKeyUtil::makeHex64( 1000 ), kNowMs - 10 ); // 기간 안이다
    rig.put( "other/a", kNowMs - 5000 );                               // 다른 묶음이다
    rig.putIndex( kNowMs - 2000, "ch/" );

    ServiceTTLRule        rule = rig.makeIndexRule();
    ServiceTTLSweepReport report;
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs, 100, 1000, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 100, report._deletedCount );
    SW_EXPECT_EQUAL( 2, rig.count() );
    SW_EXPECT_EQUAL( 0, rig.countIndex() );
}

/** @brief [ServiceTTLTest] TTL 색인: 지우기 상한에 닿은 묶음은 색인 줄을 남겨 다음 회차가 이어서 지운다 */
SW_TEST_CASE( ServiceTTLTest, IndexGroupResumesAfterLimit )
{
    TTLFixture rig;
    for ( int32 index = 0; index < 100; ++index )
    {
        rig.put( "ch/" + ServiceKeyUtil::makeHex64( static_cast<uint64>( index ) ), kNowMs - 5000 );
    }
    rig.putIndex( kNowMs - 2000, "ch/" );
    ServiceTTLRule        rule = rig.makeIndexRule();
    ServiceTTLSweepReport report;
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs, 100, 10, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 63, report._deletedCount ); // 한 쪽(쓰기 상한 - 색인 줄)을 지운 뒤 상한에 닿았다
    SW_EXPECT_EQUAL( 1, rig.countIndex() );
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs, 100, 1000, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 0, rig.count() );
    SW_EXPECT_EQUAL( 0, rig.countIndex() );
}

/** @brief [ServiceTTLTest] 색인 백필: 색인 없는 기존 레코드를 한 번 훑어 줄을 넣고 지운다. 상한을 넘으면 진행을 남겨 이어 가고, 끝난 뒤에는 다시 훑지 않는다 */
SW_TEST_CASE( ServiceTTLTest, IndexBackfillsLegacyRecordsOnce )
{
    TTLFixture rig;
    rig.put( "a", kNowMs - 2000 );
    rig.put( "b", kNowMs - 1500 );
    rig.put( "c", kNowMs - 10 );
    ServiceTTLRule rule   = rig.makeIndexRule();
    rule._pBackfillSource = ServiceTTLRule::makeRecordBackfillSource<&readLeadingTime>();
    rule._backfillVersion = 1;

    ServiceTTLSweepReport report;
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs, 2, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 2, report._backfilledCount ); // 훑기 상한까지만 - 진행은 저장소에 남는다
    SW_EXPECT_EQUAL( 2, report._deletedCount );
    SW_EXPECT_EQUAL( 1, rig._database.countRecords( ServiceTTLRule::getBackfillTable() ) );
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 1, report._backfilledCount ); // 이어서 c 만
    SW_EXPECT_EQUAL( 0, report._deletedCount );
    SW_EXPECT_EQUAL( 1, rig.count() );
    SW_EXPECT_EQUAL( 1, rig.countIndex() );

    rig.put( "d", kNowMs - 5000 ); // 백필이 끝난 뒤 색인 없이 쓴 레코드는 다시 훑지 않는다 - 색인은 쓰는 쪽이 넣는다
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 0, report._backfilledCount );
    SW_EXPECT_EQUAL( 2, rig.count() );
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs + 2000, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 1, report._deletedCount ); // c 는 색인으로 지운다
    SW_EXPECT_EQUAL( 1, rig.count() );

    rule._backfillVersion = 2; // 쓰는 쪽의 줄 모양이 바뀌었다 - 새 판은 처음부터 다시 훑어 d 를 줍는다
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs + 2000, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 1, report._backfilledCount );
    SW_EXPECT_EQUAL( 1, report._deletedCount );
    SW_EXPECT_EQUAL( 0, rig.count() );
}

/** @brief [ServiceTTLTest] 색인 백필의 배포 창: 창 안에 끝난 바퀴는 처음부터 다시 돌아, 그사이 옛 판 서버가 색인 줄 없이 쓴 레코드를 창이 지난 뒤의 바퀴가 줍는다 */
SW_TEST_CASE( ServiceTTLTest, IndexBackfillRepeatsUntilRolloutWindowEnds )
{
    TTLFixture rig;
    rig.put( "a", kNowMs - 5000 );
    ServiceTTLRule rule     = rig.makeIndexRule();
    rule._pBackfillSource   = ServiceTTLRule::makeRecordBackfillSource<&readLeadingTime>();
    rule._backfillVersion   = 1;
    rule._backfillRolloutMs = 10000;

    ServiceTTLSweepReport report;
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 1, report._backfilledCount );
    SW_EXPECT_EQUAL( 0, rig.count() );
    rig.put( "b", kNowMs - 5000 ); // 옛 판 서버가 배포 창 안에서 색인 줄 없이 썼다
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs + 5000, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 1, report._backfilledCount ); // 창 안의 다음 바퀴가 줍는다
    SW_EXPECT_EQUAL( 0, rig.count() );
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs + 20000, 100, 100, report ) == ServiceStoreResult::Ok ); // 창이 지난 뒤 마지막 바퀴를 시작한다
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs + 30000, 100, 100, report ) == ServiceStoreResult::Ok ); // 마지막 바퀴가 끝나 판을 닫는다
    rig.put( "c", kNowMs - 5000 );
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, kNowMs + 40000, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 0, report._backfilledCount ); // 판이 끝났다 - 창 뒤의 색인 없는 쓰기는 없다(옛 판 서버가 모두 내려갔다)
    SW_EXPECT_EQUAL( 1, rig.count() );
}

/** @brief [ServiceTTLTest] 예약 작업: 회차를 차지하면 쓸고 끝을 적는다. 회차 시각을 기준으로 지운다. 규칙이 없으면 등록하지 않고, 틀린 규칙은 거절한다 */
SW_TEST_CASE( ServiceTTLTest, JobSweepsOnSchedule )
{
    SW_EXPECT_LOG( "ServiceTTLMonitor.ServiceTTLMonitorRuleTableNeeds" );
    TTLFixture         rig;
    MemoryServiceStore store( &rig._database );
    rig.put( "a", kDayStart - 2000 );
    rig.put( "b", kDayStart ); // 회차 시각 기준으로는 아직 남는다

    ServiceScheduler scheduler;
    scheduler.initialize( &store, 1 );
    constexpr int32 kMinuteOfDay = 0;

    ServiceTTLMonitor empty;
    SW_EXPECT_TRUE( empty.initialize( &store, &scheduler, {}, kMinuteOfDay ) );

    ServiceTTLMonitor badRule;
    SW_EXPECT_FALSE( badRule.initialize( &store, &scheduler, { rig.makeRule( 0 ) }, kMinuteOfDay ) );

    ServiceTTLRule unlimited     = rig.makeRule();
    unlimited._maxDeletePerSweep = 0; // 상한은 설정이 정한다 - 없으면 기동 오류다
    ServiceTTLMonitor noLimit;
    SW_EXPECT_FALSE( noLimit.initialize( &store, &scheduler, { unlimited }, kMinuteOfDay ) );

    ServiceTTLMonitor job;
    SW_REQUIRE_TRUE( job.initialize( &store, &scheduler, { rig.makeRule() }, kMinuteOfDay ) );
    scheduler.update( UnixMilliseconds{ kNowMs } );
    while ( store.pollCompletions() > 0 )
    {
    }
    SW_EXPECT_FALSE( job.isRunning() );
    SW_EXPECT_EQUAL( int64( 1 ), job.getLastDeletedCount() );
    SW_EXPECT_EQUAL( 1, rig.count() );
    SW_EXPECT_EQUAL( 0, scheduler.getPendingJobCount() );

    scheduler.update( UnixMilliseconds{ kNowMs + 1 } ); // 끝낸 회차는 다시 돌지 않는다
    while ( store.pollCompletions() > 0 )
    {
    }
    SW_EXPECT_EQUAL( 1, rig.count() );
    job.shutdown();
}
