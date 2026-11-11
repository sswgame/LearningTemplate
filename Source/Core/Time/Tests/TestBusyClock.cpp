// BusyClock — 겹친 일의 구간을 한 번만 세는지(합집합), 진행 중인 구간을 지금까지 세는지.
#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Time/BusyClock.h"

using namespace sw;

/** @brief [BusyClockTest] 겹친 두 일(0~10, 5~20)과 떨어진 일(30~35)은 20 + 5 를 센다 — 일의 합(30)이 아니다. */
SW_TEST_CASE( BusyClockTest, OverlappingJobsCountOnce )
{
    BusyClock clock;
    clock.begin( 0 );
    clock.begin( 5 );
    clock.end( 10 );
    SW_EXPECT_EQUAL( int64{ 15 }, clock.getBusyNanoseconds( 15 ) ); // 아직 하나가 진행 중 — 지금까지 센다
    clock.end( 20 );
    SW_EXPECT_EQUAL( int64{ 20 }, clock.getBusyNanoseconds( 25 ) ); // 쉬는 동안은 늘지 않는다
    clock.begin( 30 );
    clock.end( 35 );
    SW_EXPECT_EQUAL( int64{ 25 }, clock.getBusyNanoseconds( 100 ) );
}
