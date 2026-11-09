#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Online/Schedule/Server/Server/ServicePeriodicJob.h"

/**
 * @brief [ServicePeriodicJobTest] 주기가 있으면 첫 틱에 한 번, 도는 중에는 다시 시작하지 않고, 주기가 지나거나 요청되면 다시 돈다
 */
SW_TEST_CASE( ServicePeriodicJobTest, StartsOnceThenOnPeriodOrRequest )
{
    using sw::UnixMilliseconds;
    sw::ServicePeriodicJob job;
    job.setPeriod( 1000 );

    SW_EXPECT_TRUE( job.tryStart( UnixMilliseconds{ 0 } ) );     // 처음은 주기가 지난 상태
    SW_EXPECT_FALSE( job.tryStart( UnixMilliseconds{ 5000 } ) ); // 도는 중
    job.finish();
    SW_EXPECT_FALSE( job.tryStart( UnixMilliseconds{ 999 } ) );
    SW_EXPECT_TRUE( job.tryStart( UnixMilliseconds{ 1000 } ) ); // 주기
    job.finish();
    job.request();
    SW_EXPECT_TRUE( job.tryStart( UnixMilliseconds{ 1001 } ) ); // 요청
    job.finish();
}

/**
 * @brief [ServicePeriodicJobTest] 주기가 0 이면 요청으로만 돌고, 차지한 예약 회차는 한 번만 돌려준다
 */
SW_TEST_CASE( ServicePeriodicJobTest, ZeroPeriodRunsOnRequestAndHandsBackHeldRun )
{
    using sw::UnixMilliseconds;
    sw::ServicePeriodicJob job;

    SW_EXPECT_FALSE( job.tryStart( UnixMilliseconds{ 100000 } ) ); // 요청이 없다
    sw::ScheduledRun run;
    run._jobID    = "test.job";
    run._runToken = 7;
    job.holdRun( run );
    SW_EXPECT_TRUE( job.tryStart( UnixMilliseconds{ 100000 } ) );
    job.finish();
    sw::ScheduledRun taken;
    SW_REQUIRE_TRUE( job.takeHeldRun( taken ) );
    SW_EXPECT_EQUAL( uint64{ 7 }, taken._runToken );
    SW_EXPECT_FALSE( job.takeHeldRun( taken ) ); // 한 번만 돌려준다
}
