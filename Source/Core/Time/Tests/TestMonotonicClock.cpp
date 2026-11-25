#include "pch.h"

#include "Core/Concurrency/thread.h"
#include "Core/TestFramework/TestFramework.h"
#include "Core/Time/GameTimer.h"
#include "Core/Time/MonotonicClock.h"

#include <thread>

/**
 * @brief [MonotonicClockTest] 지금 시각은 줄지 않고, 스톱워치 · 기한이 실제로 흐른 시간을 본다
 * @details 엔진의 모든 시간 측정(프로파일러 · 태스크 대기 · 로거 기한)이 이 시계 하나를 쓴다. 카운터 → 나노초 변환이 넘치면
 *          (counter * 1e9 를 그대로 곱하면 며칠 만에 int64 를 넘는다) 시각이 거꾸로 가거나 음수가 된다.
 */
SW_TEST_CASE( MonotonicClockTest, ClockStopwatchAndDeadlineFollowRealTime )
{
    const int64 first  = sw::MonotonicClock::nowNanoseconds();
    const int64 second = sw::MonotonicClock::nowNanoseconds();
    SW_EXPECT_TRUE( first > 0 );
    SW_EXPECT_TRUE( second >= first );
    SW_EXPECT_TRUE( sw::MonotonicClock::getCountsPerSecond() > 0 );

    // 아직 지나지 않은 기한은 부하로 선점돼도 지나지 않을 만큼 먼 것으로 본다 — 20 ms 기한을 곧바로 "아직" 이라 단언하면 그 사이 선점이 20 ms 를 넘을 때 진다.
    const sw::Deadline farDeadline = sw::Deadline::afterMilliseconds( 600'000 );
    SW_EXPECT_FALSE( farDeadline.isExpired() );
    SW_EXPECT_TRUE( farDeadline.getRemainingMilliseconds() > 0 && farDeadline.getRemainingMilliseconds() <= 600'000 );

    // 지난 쪽은 하한만 본다 — 잠은 적어도 30 ms 이고, 부하는 경과를 늘리기만 한다.
    const sw::Stopwatch stopwatch;
    const sw::Deadline  deadline = sw::Deadline::afterMilliseconds( 20 );
    sw::ThreadUtil::sleepForMilliseconds( 30 );
    const int64 elapsedMilli = stopwatch.getElapsedMilliseconds(); // 밀리초를 먼저 읽는다 — 뒤에 읽은 마이크로초는 그보다 작을 수 없다
    const int64 elapsedMicro = stopwatch.getElapsedMicroseconds();
    SW_EXPECT_TRUE( elapsedMilli >= 25 );
    SW_EXPECT_TRUE( elapsedMicro >= elapsedMilli * 1000 );
    SW_EXPECT_TRUE( deadline.isExpired() );
    SW_EXPECT_EQUAL( int64( 0 ), deadline.getRemainingMilliseconds() );
}

/**
 * @brief [MonotonicClockTest] 카운터 → 나노초 변환이 오래 가동한 카운터 값에서도 넘치지 않는다
 * @details Windows QPC 는 10 MHz 라 30 일 가동이면 카운터가 2.6e13 이고, 여기에 1e9 를 그대로 곱하면 int64 를 넘는다.
 */
SW_TEST_CASE( MonotonicClockTest, CountConversionDoesNotOverflowAfterLongUptime )
{
    constexpr int64 kQpcFrequency = 10'000'000;
    constexpr int64 kThirtyDays   = 30LL * 24 * 60 * 60;
    SW_EXPECT_EQUAL( kThirtyDays * 1'000'000'000LL, sw::MonotonicClock::countsToNanoseconds( kThirtyDays * kQpcFrequency, kQpcFrequency ) );
    SW_EXPECT_EQUAL( kThirtyDays * 1'000'000'000LL + 300, sw::MonotonicClock::countsToNanoseconds( kThirtyDays * kQpcFrequency + 3, kQpcFrequency ) );
    // 비정수 비율(예: 3 MHz)도 나머지를 버리지 않는다.
    SW_EXPECT_EQUAL( 1'000'000'333LL, sw::MonotonicClock::countsToNanoseconds( 3'000'001, 3'000'000 ) );
    // 이미 나노초인 카운터(Linux)는 그대로다.
    SW_EXPECT_EQUAL( 123LL, sw::MonotonicClock::countsToNanoseconds( 123, 1'000'000'000LL ) );
}

/**
 * @brief [MonotonicClockTest] GameTimer 와 MonotonicClock 이 같은 카운터를 읽는다 — 타이머 구간은 안쪽 스톱워치 구간을 덮고 바깥쪽 구간 안에 든다
 * @details 두 값의 차이를 허용 오차로 보면 두 읽기 사이의 선점(부하)에서 진다. 대신 바깥 스톱워치는 `resetTimer` 앞에서 시작해 `updateTimer` 뒤에 읽고,
 *          안쪽 스톱워치는 `resetTimer` 뒤에서 시작해 `updateTimer` 앞에서 읽는다 — 같은 카운터면 안쪽 ≤ 타이머 ≤ 바깥이 선점과 무관하게 선다.
 */
SW_TEST_CASE( MonotonicClockTest, GameTimerUsesTheSameCounter )
{
    constexpr int64     kFloatToleranceNanos = 10'000; // 델타가 float32 초라 깎인다 — 선점으로 구간이 10 초까지 늘어도 1 us 안쪽이다
    sw::GameTimer       timer;
    const sw::Stopwatch outer;
    timer.resetTimer();
    const sw::Stopwatch inner;
    sw::ThreadUtil::sleepForMilliseconds( 20 );
    const int64 innerNanos = inner.getElapsedNanoseconds();
    timer.updateTimer();
    const int64 outerNanos = outer.getElapsedNanoseconds();
    const int64 timerNanos = static_cast<int64>( static_cast<float64>( timer.getDeltaTime() ) * static_cast<float64>( sw::constant::time::kNanosecondsPerSecond ) );
    SW_EXPECT_TRUE( innerNanos >= 15 * sw::constant::time::kNanosecondsPerMillisecond );
    SW_EXPECT_TRUE( innerNanos <= timerNanos + kFloatToleranceNanos );
    SW_EXPECT_TRUE( timerNanos <= outerNanos + kFloatToleranceNanos );
}

/**
 * @brief [MonotonicClockTest] 마감까지 잠자기는 마감 전에 돌아오지 않고, 지난 마감은 바로 돌아온다
 */
SW_TEST_CASE( MonotonicClockTest, SleepUntilDoesNotWakeBeforeTheDeadline )
{
    const int64 start    = sw::MonotonicClock::nowNanoseconds();
    const int64 deadline = start + 20 * 1000000;
    sw::MonotonicClock::sleepUntilNanoseconds( deadline );
    SW_EXPECT_TRUE( sw::MonotonicClock::nowNanoseconds() >= deadline );
    const int64 pastStart = sw::MonotonicClock::nowNanoseconds();
    sw::MonotonicClock::sleepUntilNanoseconds( pastStart - 1000000 );
    // 지난 마감에 잠들지 않았다 — 상한은 멈춤(음수 길이를 큰 수로 읽어 영영 잔다)을 가르는 그물이라 부하의 선점(수십 ms)보다 넉넉히 둔다.
    SW_EXPECT_TRUE( sw::MonotonicClock::nowNanoseconds() - pastStart < 5 * sw::constant::time::kNanosecondsPerSecond );
}
