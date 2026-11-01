#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Online/Service/ServiceReloadSchedule.h"

/**
 * @brief [ServiceReloadScheduleTest] 서자마자 한 번, 읽는 중에는 다시 시작하지 않고, 주기가 지나거나 요청되면 다시 읽는다
 */
SW_TEST_CASE( ServiceReloadScheduleTest, StartsOnceThenOnPeriodOrRequest )
{
    using sw::UnixMilliseconds;
    sw::ServiceReloadSchedule schedule;
    constexpr int64           kPeriodMs = 1000;

    SW_EXPECT_TRUE( schedule.shouldStart( UnixMilliseconds{ 0 }, kPeriodMs ) ); // 처음은 요청된 상태
    schedule.markStarted( UnixMilliseconds{ 0 } );
    SW_EXPECT_FALSE( schedule.shouldStart( UnixMilliseconds{ 5000 }, kPeriodMs ) ); // 읽는 중
    schedule.markFinished();
    SW_EXPECT_FALSE( schedule.shouldStart( UnixMilliseconds{ 999 }, kPeriodMs ) );
    SW_EXPECT_TRUE( schedule.shouldStart( UnixMilliseconds{ 1000 }, kPeriodMs ) ); // 주기
    schedule.markStarted( UnixMilliseconds{ 1000 } );
    schedule.markFinished();
    schedule.request();
    SW_EXPECT_TRUE( schedule.shouldStart( UnixMilliseconds{ 1001 }, kPeriodMs ) ); // 요청
}
