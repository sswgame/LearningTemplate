// ServiceSchedulerTest — 예약 작업: 회차 계산, 회차마다 서버 하나만 실행, 임대가 지나면 이어받기, 지나친 회차, 저장소 실패 뒤 다시 하기. ServiceRetentionTest — 보존 기간 쓸기와 그 예약 작업(메모리 저장소, nogpu).
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Online/Schedule/Server/ServiceRetentionJob.h"
#include "GameFramework/Runtime/Online/Schedule/Server/ServiceScheduler.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRetentionRule.h"

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

        void pump()
        {
            while ( _store.pollCompletions() > 0 )
            {
            }
        }

        void tickBoth( int64 nowMs )
        {
            _first.update( UnixMilliseconds{ nowMs } );
            _second.update( UnixMilliseconds{ nowMs } );
            pump();
        }
    };

    bool readLeadingTime( const ServiceRecord& record, int64& outTimeMs )
    {
        if ( record._bytes.size() < sizeof( int64 ) )
            return false;
        Memory::copy( &outTimeMs, record._bytes.data(), sizeof( int64 ) );
        return true;
    }

    struct RetentionFixture
    {
        MemoryServiceDatabase _database;
        hashed_string         _table{ "retention_test" };

        void put( string_view key, int64 timeMs )
        {
            vector<uint8> bytes( sizeof( int64 ) );
            Memory::copy( bytes.data(), &timeMs, sizeof( int64 ) );
            ServiceTransaction transaction;
            transaction.put( _table, key, bytes, ServiceRecord::kAbsentVersion );
            SW_REQUIRE_TRUE( _database.commit( transaction ) == ServiceStoreResult::Ok );
        }

        ServiceRetentionRule makeRule( int64 retentionMs = 1000 ) const
        {
            ServiceRetentionRule rule;
            rule._table       = _table;
            rule._retentionMs = retentionMs;
            rule._pfnReadTime = &readLeadingTime;
            return rule;
        }

        int32 count() const { return _database.countRecords( _table ); }
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
    rig.pump();
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
    rig.pump();
    SW_EXPECT_TRUE( rig._secondSink._listRun.empty() );

    const int64 afterLease = kNowMs + ScheduleDef{}._leaseMs;
    rig._second.update( UnixMilliseconds{ afterLease } );
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._secondSink._listRun.size() );
    const ScheduledRun taken = rig._secondSink._listRun[0];
    SW_EXPECT_EQUAL( kDayStart, taken._occurrenceMs.count() );

    rig._first.completeRun( rig._firstSink._listRun[0], true ); // 옛 서버가 뒤늦게 끝을 적는다
    rig.pump();
    rig._second.completeRun( taken, true );
    rig.pump();
}

/** @brief [ServiceSchedulerTest] 꺼져 있어 지나친 회차는 가장 최근 하나만 돈다 */
SW_TEST_CASE( ServiceSchedulerTest, MissedOccurrencesRunOnce )
{
    SchedulerFixture rig;
    rig._first.update( UnixMilliseconds{ kNowMs + 5 * kDayMs } );
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._firstSink._listRun.size() );
    SW_EXPECT_EQUAL( kDayStart + 5 * kDayMs, rig._firstSink._listRun[0]._occurrenceMs.count() );
    rig._first.update( UnixMilliseconds{ kNowMs + 5 * kDayMs + 1 } );
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 1 ), rig._firstSink._listRun.size() );
}

/** @brief [ServiceSchedulerTest] 저장소가 실패하면 처리기를 부르지 않고, 재시도 시각 전에는 다시 묻지 않으며, 그 뒤에 돈다 */
SW_TEST_CASE( ServiceSchedulerTest, StoreFailureRetriesLater )
{
    SchedulerFixture rig;
    rig._database.armFault( ServiceStoreFault::RejectCommit );
    rig._first.update( UnixMilliseconds{ kNowMs } );
    rig.pump();
    SW_EXPECT_TRUE( rig._firstSink._listRun.empty() );

    rig._first.update( UnixMilliseconds{ kNowMs + 4999 } );
    SW_EXPECT_EQUAL( 0, rig._first.getPendingJobCount() );

    rig._first.update( UnixMilliseconds{ kNowMs + 5000 } );
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 1 ), rig._firstSink._listRun.size() );
}

/** @brief [ServiceSchedulerTest] 실패로 끝낸 회차도 끝난 것이다 - 처리기가 멱등이어도 다른 서버가 다시 돌지 않고 다음 회차를 기다린다 */
SW_TEST_CASE( ServiceSchedulerTest, FailedRunIsFinished )
{
    SchedulerFixture rig;
    rig._first.update( UnixMilliseconds{ kNowMs } );
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._firstSink._listRun.size() );
    rig._first.completeRun( rig._firstSink._listRun[0], false );
    rig.pump();
    rig._second.update( UnixMilliseconds{ kNowMs + 1 } );
    rig.pump();
    SW_EXPECT_TRUE( rig._secondSink._listRun.empty() );
}

/** @brief [ServiceRetentionTest] 보존 기간이 지난 것만 지운다. 경계는 지난 것이고, 시각을 읽지 못하는 레코드는 남긴다 */
SW_TEST_CASE( ServiceRetentionTest, DeletesOnlyExpired )
{
    RetentionFixture rig;
    rig.put( "a.old", kNowMs - 2000 );
    rig.put( "b.edge", kNowMs - 1000 );
    rig.put( "c.new", kNowMs - 999 );
    ServiceTransaction unreadable;
    unreadable.put( rig._table, "d.bad", vector<uint8>{ 1 }, ServiceRecord::kAbsentVersion );
    SW_REQUIRE_TRUE( rig._database.commit( unreadable ) == ServiceStoreResult::Ok );

    ServiceRetentionRule   rule = rig.makeRule();
    ServiceRetentionReport report;
    SW_REQUIRE_TRUE( ServiceRetentionRule::sweep( rig._database, rule, kNowMs, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 2, report._deletedCount );
    SW_EXPECT_EQUAL( 4, report._scannedCount );
    SW_EXPECT_EQUAL( SW_TRUE, report._bWrapped );
    SW_EXPECT_EQUAL( 2, rig.count() );
    SW_EXPECT_TRUE( rule._cursor.empty() );
}

/** @brief [ServiceRetentionTest] 상한에 닿으면 커서를 남겨 다음 회차가 이어서 지운다 */
SW_TEST_CASE( ServiceRetentionTest, LimitsResumeFromCursor )
{
    RetentionFixture rig;
    for ( const utf8* pKey : { "a", "b", "c", "d" } )
    {
        rig.put( pKey, kNowMs - 2000 );
    }
    ServiceRetentionRule   rule = rig.makeRule();
    ServiceRetentionReport report;
    SW_REQUIRE_TRUE( ServiceRetentionRule::sweep( rig._database, rule, kNowMs, 100, 2, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 2, report._deletedCount );
    SW_EXPECT_EQUAL( 2, rig.count() );
    SW_EXPECT_EQUAL( string( "b" ), rule._cursor );

    SW_REQUIRE_TRUE( ServiceRetentionRule::sweep( rig._database, rule, kNowMs, 1, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 1, report._scannedCount );
    SW_EXPECT_EQUAL( 1, rig.count() );
    SW_REQUIRE_TRUE( ServiceRetentionRule::sweep( rig._database, rule, kNowMs, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 0, rig.count() );
}

/** @brief [ServiceRetentionTest] 쓰기가 실패하면 그 결과를 돌려주고 커서가 지나가지 않아 다음 회차가 같은 레코드를 지운다 */
SW_TEST_CASE( ServiceRetentionTest, FailedCommitDoesNotSkipRecords )
{
    RetentionFixture rig;
    rig.put( "a", kNowMs - 2000 );
    ServiceRetentionRule   rule = rig.makeRule();
    ServiceRetentionReport report;
    rig._database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( ServiceRetentionRule::sweep( rig._database, rule, kNowMs, 100, 100, report ) == ServiceStoreResult::Unavailable );
    SW_EXPECT_EQUAL( 1, rig.count() );

    SW_REQUIRE_TRUE( ServiceRetentionRule::sweep( rig._database, rule, kNowMs, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 1, report._deletedCount );
    SW_EXPECT_EQUAL( 0, rig.count() );
}

/** @brief [ServiceRetentionTest] 시각 읽기나 기간이 없는 규칙은 거절한다 */
SW_TEST_CASE( ServiceRetentionTest, InvalidRuleIsRefused )
{
    RetentionFixture       rig;
    ServiceRetentionRule   rule = rig.makeRule( 0 );
    ServiceRetentionReport report;
    SW_EXPECT_TRUE( ServiceRetentionRule::sweep( rig._database, rule, kNowMs, 100, 100, report ) == ServiceStoreResult::Invalid );
    rule              = rig.makeRule();
    rule._pfnReadTime = nullptr;
    SW_EXPECT_TRUE( ServiceRetentionRule::sweep( rig._database, rule, kNowMs, 100, 100, report ) == ServiceStoreResult::Invalid );
}

/** @brief [ServiceRetentionTest] 예약 작업: 회차를 차지하면 쓸고 끝을 적는다. 회차 시각을 기준으로 지운다. 규칙이 없으면 등록하지 않고, 틀린 규칙은 거절한다 */
SW_TEST_CASE( ServiceRetentionTest, JobSweepsOnSchedule )
{
    SW_EXPECT_LOG( "ServiceRetentionJob.ServiceRetentionJobRuleTableNeeds" );
    RetentionFixture   rig;
    MemoryServiceStore store( &rig._database );
    rig.put( "a", kDayStart - 2000 );
    rig.put( "b", kDayStart ); // 회차 시각 기준으로는 아직 남는다

    ServiceScheduler scheduler;
    scheduler.initialize( &store, 1 );
    ServiceRetentionJobConfig settings;
    settings._minuteOfDay = 0;

    ServiceRetentionJob empty;
    SW_EXPECT_TRUE( empty.initialize( &store, &scheduler, {}, settings ) );

    ServiceRetentionJob badRule;
    SW_EXPECT_FALSE( badRule.initialize( &store, &scheduler, { rig.makeRule( 0 ) }, settings ) );

    ServiceRetentionJob job;
    SW_REQUIRE_TRUE( job.initialize( &store, &scheduler, { rig.makeRule() }, settings ) );
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
