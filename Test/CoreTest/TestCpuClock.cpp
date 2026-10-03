#include "pch.h"

#include "Core/Time/CpuClock.h"
#include "Core/Time/CpuTimer.h"

#include "TestFramework/TestFramework.h"

#include <thread>

/**
 * @brief [CpuClockTest] 지금 시각은 줄지 않고, 스톱워치 · 기한이 실제로 흐른 시간을 본다
 * @details 엔진의 모든 시간 측정(프로파일러 · 태스크 대기 · 로거 기한)이 이 시계 하나를 쓴다. 카운터 → 나노초 변환이 넘치면
 *          (counter * 1e9 를 그대로 곱하면 며칠 만에 int64 를 넘는다) 시각이 거꾸로 가거나 음수가 된다.
 */

SW_TEST_CASE( CpuClockTest, ClockStopwatchAndDeadlineFollowRealTime )
{
    const int64 first  = sw::CpuClock::nowNanoseconds();
    const int64 second = sw::CpuClock::nowNanoseconds();
    SW_EXPECT_TRUE( first > 0 );
    SW_EXPECT_TRUE( second >= first );
    SW_EXPECT_TRUE( sw::CpuClock::getCountsPerSecond() > 0 );

    const sw::CpuStopwatch stopwatch;
    const sw::CpuDeadline  deadline = sw::CpuDeadline::afterMilliseconds( 20 );
    SW_EXPECT_FALSE( deadline.isExpired() );
    SW_EXPECT_TRUE( deadline.getRemainingMilliseconds() <= 20 );

    std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
    SW_EXPECT_TRUE( stopwatch.getElapsedMilliseconds() >= 25 );
    SW_EXPECT_TRUE( stopwatch.getElapsedMicroseconds() >= stopwatch.getElapsedMilliseconds() * 1000 );
    SW_EXPECT_TRUE( deadline.isExpired() );
    SW_EXPECT_EQUAL( int64( 0 ), deadline.getRemainingMilliseconds() );
}

/**
 * @brief [CpuClockTest] 카운터 → 나노초 변환이 오래 가동한 카운터 값에서도 넘치지 않는다
 * @details Windows QPC 는 10 MHz 라 30 일 가동이면 카운터가 2.6e13 이고, 여기에 1e9 를 그대로 곱하면 int64 를 넘는다.
 */
SW_TEST_CASE( CpuClockTest, CountConversionDoesNotOverflowAfterLongUptime )
{
    constexpr int64 kQpcFrequency = 10'000'000;
    constexpr int64 kThirtyDays   = 30LL * 24 * 60 * 60;
    SW_EXPECT_EQUAL( kThirtyDays * 1'000'000'000LL, sw::CpuClock::countsToNanoseconds( kThirtyDays * kQpcFrequency, kQpcFrequency ) );
    SW_EXPECT_EQUAL( kThirtyDays * 1'000'000'000LL + 300, sw::CpuClock::countsToNanoseconds( kThirtyDays * kQpcFrequency + 3, kQpcFrequency ) );
    // 비정수 비율(예: 3 MHz)도 나머지를 버리지 않는다.
    SW_EXPECT_EQUAL( 1'000'000'333LL, sw::CpuClock::countsToNanoseconds( 3'000'001, 3'000'000 ) );
    // 이미 나노초인 카운터(Linux · macOS)는 그대로다.
    SW_EXPECT_EQUAL( 123LL, sw::CpuClock::countsToNanoseconds( 123, 1'000'000'000LL ) );
}

/**
 * @brief [CpuClockTest] CpuTimer 와 CpuClock 이 같은 카운터를 읽는다 — 같은 구간을 재면 거의 같은 값이다
 */
SW_TEST_CASE( CpuClockTest, CpuTimerUsesTheSameCounter )
{
    sw::CpuTimer timer;
    timer.resetTimer();
    const sw::CpuStopwatch stopwatch;
    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    timer.updateTimer();
    const float64 timerMilli     = static_cast<float64>( timer.getDeltaTime() ) * 1000.0;
    const float64 stopwatchMilli = static_cast<float64>( stopwatch.getElapsedMicroseconds() ) / 1000.0;
    SW_EXPECT_TRUE( timerMilli >= 15.0 );
    SW_EXPECT_NEAR_EQUAL( stopwatchMilli, timerMilli, 5.0 );
}
