#include "pch.h"

#include "GameFramework/Base/Online/Schedule/ServiceScheduler.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"

#include "TestFramework/TestFramework.h"

// 예약 작업 — 회차 계산(일일 · 주간 · 기간 경계, UTC), 서버 둘이 같은 회차를 한 번만, 꺼졌다 켜면 최근 회차 하나만, 임대가 지나면 이어받기, 끝난 회차는 다시 안 돈다.

using namespace sw;

namespace
{
    struct TestServiceSchedulerInternal
    {
        static constexpr int64 kHourMs = 60 * 60 * 1000;
        static constexpr int64 kDayMs  = 24 * kHourMs;
        static constexpr int64 kDay    = 20000; ///< 1970 부터 20000 일째 — 금요일(ISO 4)

        static ScheduleDefinition makeDaily( int32 minuteOfDay )
        {
            ScheduleDefinition definition;
            definition._jobID       = "daily_reward";
            definition._kind        = ScheduleKind::Daily;
            definition._minuteOfDay = minuteOfDay;
            definition._leaseMs     = kHourMs;
            return definition;
        }
    };

    /** @brief 불린 회차를 적어 두는 처리기입니다. */
    class RecordingJobHandler final : public IScheduledJobHandler
    {
    public:
        vector<ScheduledRun> _listRun;

        RecordingJobHandler()
            : _listRun{}
        {
        }

        void onScheduledRun( const ScheduledRun& run ) override { _listRun.push_back( run ); }
    };
} // namespace

SW_TEST_CASE( ServiceSchedulerTest, LatestOccurrenceFollowsDailyWeeklyAndWindowBoundaries )
{
    using Internal                        = TestServiceSchedulerInternal;
    const int64              dayStartMs   = Internal::kDay * Internal::kDayMs;
    const ScheduleDefinition daily        = Internal::makeDaily( 10 * 60 );
    const int64              todayAtTenMs = dayStartMs + 10 * Internal::kHourMs;
    SW_EXPECT_EQUAL( todayAtTenMs - Internal::kDayMs, ServiceScheduler::computeLatestOccurrence( daily, todayAtTenMs - 1 ) ); // 아직이면 어제
    SW_EXPECT_EQUAL( todayAtTenMs, ServiceScheduler::computeLatestOccurrence( daily, todayAtTenMs ) );                        // 그 시각을 포함한다
    SW_EXPECT_EQUAL( todayAtTenMs, ServiceScheduler::computeLatestOccurrence( daily, dayStartMs + Internal::kDayMs - 1 ) );

    ScheduleDefinition weekly = daily;
    weekly._kind              = ScheduleKind::Weekly;
    weekly._dayOfWeek         = 0; // 월요일 — kDay 는 금요일이라 나흘 전
    SW_EXPECT_EQUAL( todayAtTenMs - 4 * Internal::kDayMs, ServiceScheduler::computeLatestOccurrence( weekly, todayAtTenMs + Internal::kHourMs ) );
    weekly._dayOfWeek = 4; // 금요일 — 오늘이지만 10 시 전이면 지난주
    SW_EXPECT_EQUAL( todayAtTenMs - 7 * Internal::kDayMs, ServiceScheduler::computeLatestOccurrence( weekly, todayAtTenMs - 1 ) );
    SW_EXPECT_EQUAL( todayAtTenMs, ServiceScheduler::computeLatestOccurrence( weekly, todayAtTenMs ) );

    ScheduleDefinition window = daily;
    window._kind              = ScheduleKind::Window;
    window._windowStartMs     = dayStartMs;
    window._windowEndMs       = dayStartMs + Internal::kDayMs;
    SW_EXPECT_EQUAL( int64( -1 ), ServiceScheduler::computeLatestOccurrence( window, dayStartMs - 1 ) );
    SW_EXPECT_EQUAL( dayStartMs, ServiceScheduler::computeLatestOccurrence( window, dayStartMs ) );
    SW_EXPECT_EQUAL( int64( -1 ), ServiceScheduler::computeLatestOccurrence( window, dayStartMs + Internal::kDayMs ) ); // 끝은 빠진다

    ScheduleDefinition broken = daily;
    broken._jobID             = "Daily Reward";
    SW_EXPECT_FALSE( ServiceScheduler::isValidDefinition( broken ) );
}

SW_TEST_CASE( ServiceSchedulerTest, TwoServersRunEachOccurrenceOnce )
{
    using Internal = TestServiceSchedulerInternal;
    MemoryServiceDatabase database;
    MemoryServiceStore    frontA{ &database };
    MemoryServiceStore    frontB{ &database };
    RecordingJobHandler   handlerA;
    RecordingJobHandler   handlerB;
    ServiceScheduler      schedulerA;
    ServiceScheduler      schedulerB;
    schedulerA.initialize( &frontA, 1 );
    schedulerB.initialize( &frontB, 2 );
    SW_ASSERT_TRUE( schedulerA.registerJob( Internal::makeDaily( 10 * 60 ), &handlerA ) );
    SW_ASSERT_TRUE( schedulerB.registerJob( Internal::makeDaily( 10 * 60 ), &handlerB ) );

    const int64 nowMs = Internal::kDay * Internal::kDayMs + 11 * Internal::kHourMs;
    schedulerA.tick( nowMs );
    schedulerB.tick( nowMs );
    SW_EXPECT_EQUAL( size_t( 0 ), handlerA._listRun.size() ); // 처리기는 완료를 거둘 때 불린다
    frontA.pollCompletions();
    frontB.pollCompletions();
    SW_EXPECT_EQUAL( size_t( 1 ), handlerA._listRun.size() + handlerB._listRun.size() );
    SW_ASSERT_EQUAL( size_t( 1 ), handlerA._listRun.size() );
    SW_EXPECT_EQUAL( nowMs - Internal::kHourMs, handlerA._listRun[0]._occurrenceMs );

    schedulerA.completeRun( handlerA._listRun[0], true );
    frontA.pollCompletions();
    for ( int64 minute = 1; minute <= 120; ++minute )
    {
        schedulerA.tick( nowMs + minute * 60 * 1000 );
        schedulerB.tick( nowMs + minute * 60 * 1000 );
        frontA.pollCompletions();
        frontB.pollCompletions();
    }
    SW_EXPECT_EQUAL( size_t( 1 ), handlerA._listRun.size() + handlerB._listRun.size() ); // 끝난 회차는 임대가 지나도 다시 안 돈다
    SW_EXPECT_EQUAL( 0, schedulerA.getPendingWorkCount() );
    SW_EXPECT_EQUAL( 0, schedulerB.getPendingWorkCount() );
}

SW_TEST_CASE( ServiceSchedulerTest, RestartAfterDaysRunsOnlyTheLatestOccurrence )
{
    using Internal = TestServiceSchedulerInternal;
    MemoryServiceDatabase database;
    const int64           firstMs = Internal::kDay * Internal::kDayMs + 11 * Internal::kHourMs;
    {
        MemoryServiceStore  front{ &database };
        RecordingJobHandler handler;
        ServiceScheduler    scheduler;
        scheduler.initialize( &front, 1 );
        SW_ASSERT_TRUE( scheduler.registerJob( Internal::makeDaily( 10 * 60 ), &handler ) );
        scheduler.tick( firstMs );
        front.pollCompletions();
        SW_ASSERT_EQUAL( size_t( 1 ), handler._listRun.size() );
        scheduler.completeRun( handler._listRun[0], true );
        front.pollCompletions();
    }
    MemoryServiceStore  front{ &database }; // 서버 재시작 — 데이터만 남았다
    RecordingJobHandler handler;
    ServiceScheduler    scheduler;
    scheduler.initialize( &front, 1 );
    SW_ASSERT_TRUE( scheduler.registerJob( Internal::makeDaily( 10 * 60 ), &handler ) );
    scheduler.tick( firstMs ); // 이미 끝난 회차
    front.pollCompletions();
    SW_EXPECT_EQUAL( size_t( 0 ), handler._listRun.size() );

    const int64 laterMs = firstMs + 3 * Internal::kDayMs;
    scheduler.tick( laterMs ); // 사흘 꺼져 있었다 — 밀린 둘은 건너뛰고 최근 하나만
    front.pollCompletions();
    scheduler.tick( laterMs + 1000 );
    front.pollCompletions();
    SW_ASSERT_EQUAL( size_t( 1 ), handler._listRun.size() );
    SW_EXPECT_EQUAL( laterMs - Internal::kHourMs, handler._listRun[0]._occurrenceMs );
}

SW_TEST_CASE( ServiceSchedulerTest, ExpiredLeaseIsTakenOverByAnotherServer )
{
    using Internal = TestServiceSchedulerInternal;
    MemoryServiceDatabase database;
    MemoryServiceStore    frontA{ &database };
    MemoryServiceStore    frontB{ &database };
    RecordingJobHandler   handlerA;
    RecordingJobHandler   handlerB;
    ServiceScheduler      schedulerA;
    ServiceScheduler      schedulerB;
    schedulerA.initialize( &frontA, 1 );
    schedulerB.initialize( &frontB, 2 );
    SW_ASSERT_TRUE( schedulerA.registerJob( Internal::makeDaily( 10 * 60 ), &handlerA ) );
    SW_ASSERT_TRUE( schedulerB.registerJob( Internal::makeDaily( 10 * 60 ), &handlerB ) );

    const int64 nowMs = Internal::kDay * Internal::kDayMs + 11 * Internal::kHourMs;
    schedulerA.tick( nowMs ); // A 가 차지하고 끝을 알리지 못한 채 멈췄다
    frontA.pollCompletions();
    SW_ASSERT_EQUAL( size_t( 1 ), handlerA._listRun.size() );

    schedulerB.tick( nowMs + Internal::kHourMs - 1 ); // 임대 안 — 기다린다
    frontB.pollCompletions();
    SW_EXPECT_EQUAL( size_t( 0 ), handlerB._listRun.size() );
    schedulerB.tick( nowMs + Internal::kHourMs ); // 임대가 지났다 — 이어받는다
    frontB.pollCompletions();
    SW_ASSERT_EQUAL( size_t( 1 ), handlerB._listRun.size() );
    SW_EXPECT_EQUAL( handlerA._listRun[0]._occurrenceMs, handlerB._listRun[0]._occurrenceMs );

    const uint64 commitsBefore = database.getCommitCount();
    schedulerA.completeRun( handlerA._listRun[0], true ); // 늦게 깨어난 A 의 끝 알림은 판이 낡아 적히지 않는다
    frontA.pollCompletions();
    SW_EXPECT_EQUAL( commitsBefore, database.getCommitCount() );
    schedulerB.completeRun( handlerB._listRun[0], true );
    frontB.pollCompletions();
    SW_EXPECT_EQUAL( commitsBefore + 1, database.getCommitCount() );
}
