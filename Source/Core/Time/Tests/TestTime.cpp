#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"
#include "Core/Time/ExponentialBackoff.h"
#include "Core/Time/GameTimer.h"
#include "Core/Time/TimeRollover.h"

// ------------------------------------------------------------------------------
// 1) Core_Time — GameTimer·스코프 타이머
//    시계는 손으로 민다(`ManualClock`) — 잠들지 않고 경과를 정하므로 단언은 정확한 값이다(예전에는 실제로 잠들어 "적어도" 만 볼 수 있었다).
// ------------------------------------------------------------------------------
namespace
{
    /** @brief 시험용 시계입니다. 0 이 아닌 시각에서 시작한다(타이머의 기준점 0 과 섞이지 않게). */
    sw::ManualClock makeClock()
    {
        return sw::ManualClock{ sw::MonotonicMilliseconds{ 1000 } };
    }
} // namespace

/**
 * @brief [TimeTest] 열 시간 뒤에도 누적 시간의 1 ms 차이가 1 ms 로 보인다(감사 C-12 — float32 초면 한 눈금이 3.9 ms 였다)
 */
SW_TEST_CASE( TimeTest, TotalTimeKeepsMillisecondsAfterTenHours )
{
    sw::ManualClock clock = makeClock();
    sw::GameTimer   timer{ clock };
    timer.resetTimer();
    timer.startTimer();
    clock.advanceMilliseconds( 10 * 3600 * 1000 );
    timer.updateTimer();
    const float64 before = timer.getTotalTime();
    clock.advanceMilliseconds( 1 );
    timer.updateTimer();
    SW_EXPECT_NEAR( 0.001, timer.getTotalTime() - before, 1e-6 );
}

/**
 * @brief [TimeTest] 셰이더로 보내는 시간은 주기로 접혀 열 시간 뒤에도 1 ms 차이가 float32 에 남는다(감사 C-12 — 접지 않으면 한 눈금이 3.9 ms)
 * @details 접힌 값은 [0, 주기) 이고 주기를 넘으면 0 부터 다시 센다(Godot `time_rollover_secs` 와 같다). 음수 시간도 같은 범위로 접는다.
 */
SW_TEST_CASE( TimeTest, ShaderTimeFoldKeepsMillisecondsAfterTenHours )
{
    constexpr sw::float64 kRollover = 3600.0;
    constexpr sw::float64 kTenHours = 10.0 * 3600.0 + 12.25;
    const sw::float32     before    = sw::TimeRolloverUtil::fold( kTenHours, kRollover );
    const sw::float32     after     = sw::TimeRolloverUtil::fold( kTenHours + 0.001, kRollover );
    const sw::float32     unfoldedA = static_cast<sw::float32>( kTenHours );
    const sw::float32     unfoldedB = static_cast<sw::float32>( kTenHours + 0.001 );
    SW_EXPECT_NEAR( 12.25f, before, 1e-3f );
    SW_EXPECT_NEAR( 0.001f, after - before, 3e-4f );
    SW_EXPECT_TRUE_MSG( unfoldedB - unfoldedA == 0.0f || unfoldedB - unfoldedA > 0.002f, "the unfolded float32 must lose the millisecond (the defect this fold removes)" );
    SW_EXPECT_NEAR( 0.5f, sw::TimeRolloverUtil::fold( kRollover + 0.5, kRollover ), 1e-4f );
    SW_EXPECT_NEAR( 3599.0f, sw::TimeRolloverUtil::fold( -1.0, kRollover ), 1e-3f );
}

/**
 * @brief [TimeTest] 차이만 뜻이 있는 float32 시뮬레이션 시각은 기준을 옮겨 0.25 초 걸음을 잃지 않는다(감사 K-5 — 옮기지 않으면 2^22 초에서 멈춘다)
 */
SW_TEST_CASE( TimeTest, SimulationTimeRebaseKeepsQuarterSecondSteps )
{
    sw::float32 stalled = 4194304.0f; // 2^22 초
    stalled += 0.25f;
    SW_EXPECT_TRUE_MSG( stalled == 4194304.0f, "an unrebased float32 clock stops at 2^22 s (the defect)" );

    sw::float32 time    = 0.0f;
    sw::float64 total   = 0.0;
    sw::uint32  rebases = 0;
    for ( sw::uint32 step = 0; step < 40000; ++step )
    {
        time += 0.25f;
        total += 0.25;
        if ( sw::TimeRolloverUtil::rebase( time ) )
            ++rebases;
    }
    SW_EXPECT_EQUAL( 2u, rebases );
    SW_EXPECT_TRUE( time < sw::TimeRolloverUtil::kRebaseSeconds );
    SW_EXPECT_NEAR( total - 2.0 * sw::TimeRolloverUtil::kRebaseSeconds, static_cast<sw::float64>( time ), 1e-9 );
}

/**
 * @brief [TimeTest] GameTimer 기본 — 민 만큼이 델타이자 누적이다
 */
SW_TEST_CASE( TimeTest, GameTimerBasic )
{
    sw::ManualClock clock = makeClock();
    sw::GameTimer   timer{ clock };
    // 생성자는 중지 상태로 둔다(헤더의 계약).
    SW_EXPECT_TRUE( timer.isStopped() );

    timer.resetTimer();
    timer.startTimer();
    clock.advanceMilliseconds( 15 );
    timer.updateTimer();

    SW_EXPECT_NEAR( 0.015f, timer.getDeltaTime(), 1e-6f );
    SW_EXPECT_NEAR( 0.015f, timer.getTotalTime(), 1e-6f );

    timer.stopTimer();
    SW_EXPECT_TRUE( timer.isStopped() );
}

/**
 * @brief [TimeTest] ScopedTimer 기본 — 측정 도구라 실제 단조 시계로 잰다(측정에 쓰는 시간은 주입하지 않는다). 경과는 음수가 아니다(누적 계산은 위 GameTimer 테스트가 본다)
 */
SW_TEST_CASE( TimeTest, ScopedTimerBasic )
{
    float32 elapsedSec{ -1.0f };
    {
        sw::ScopedTimer timer( "TestScopedTimer" );
        elapsedSec = timer.getElapsedTimeInSeconds();
    }
    SW_EXPECT_TRUE( elapsedSec >= 0.0f );
}

/**
 * @brief [TimeTest] 만들자마자 start 해도 첫 델타가 시계 기준점 이후 시간이 되지 않는다
 * @details 생성자가 타이머를 돌고 있는 상태로 두면 `startTimer()` 가 `if ( _bStopped )` 에 걸려 아무 일도 하지 않고,
 *          `_prevTime` 이 0 인 채로 첫 `updateTimer()` 가 돌아 델타가 **시계 기준점 이후 전체 시간**이 된다.
 *          호출부가 `resetTimer()` 를 먼저 부르면 가려진다. 시험 시계는 1 초에서 시작하므로 그 결함이면 델타가 1.01 초다.
 */
SW_TEST_CASE( TimeTest, FreshTimerDoesNotReportTimeSinceBoot )
{
    sw::ManualClock clock = makeClock();
    sw::GameTimer   timer{ clock };
    SW_EXPECT_TRUE_MSG( timer.isStopped(), "생성자 주석은 중지 상태라고 말한다" );

    timer.startTimer();
    SW_EXPECT_FALSE( timer.isStopped() );

    clock.advanceMilliseconds( 10 );
    timer.updateTimer();

    SW_EXPECT_NEAR( 0.01f, timer.getDeltaTime(), 1e-6f );
    SW_EXPECT_NEAR( 0.01f, timer.getTotalTime(), 1e-6f );
}

/**
 * @brief [TimeTest] GameTimer 리셋과 일시정지 — 멈춘 동안 민 시간은 누적에 들지 않는다
 */
SW_TEST_CASE( TimeTest, GameTimerResetAndPause )
{
    sw::ManualClock clock = makeClock();
    sw::GameTimer   timer{ clock };
    timer.resetTimer();
    timer.startTimer();
    clock.advanceMilliseconds( 5 );
    timer.stopTimer();

    const float64 pausedTotal = timer.getTotalTime();
    SW_EXPECT_NEAR( 0.005f, pausedTotal, 1e-6f );
    clock.advanceMilliseconds( 5 );
    SW_EXPECT_NEAR( pausedTotal, timer.getTotalTime(), 1e-6f );

    timer.startTimer();
    SW_EXPECT_FALSE( timer.isStopped() );
    clock.advanceMilliseconds( 5 );
    timer.updateTimer();
    SW_EXPECT_NEAR( 0.01f, timer.getTotalTime(), 1e-6f ); // 멈춘 5 ms 는 빠진다
}

/**
 * @brief [TimeTest] 연속 프레임 틱 및 누적 시간 무결성 검증
 */
SW_TEST_CASE( TimeTest, ContinuousFrameTicksAndTotalTime )
{
    sw::ManualClock clock = makeClock();
    sw::GameTimer   timer{ clock };
    timer.resetTimer();
    timer.startTimer();

    float32 accumulatedDelta{ 0.0f };
    for ( int32 frame = 0; frame < 5; ++frame )
    {
        clock.advanceMilliseconds( 3 );
        timer.updateTimer();
        accumulatedDelta += timer.getDeltaTime();
        SW_EXPECT_NEAR( 0.003f, timer.getDeltaTime(), 1e-6f );
    }

    SW_EXPECT_NEAR( accumulatedDelta, timer.getTotalTime(), 1e-5f );
    SW_EXPECT_NEAR( 0.015f, timer.getTotalTime(), 1e-5f );
    timer.stopTimer();
}

SW_TEST_CASE( ExponentialBackoffTest, FirstRetryIsImmediateThenDoublesUpToTheCap )
{
    sw::ExponentialBackoff backoff( 100, 7u );
    const auto             expectWithin = [&backoff]( int64 lowMs, int64 highMs )
    {
        const int64 delayMs = backoff.takeDelayMs( 350 );
        SW_EXPECT_TRUE( lowMs <= delayMs && delayMs <= highMs );
    };
    expectWithin( 0, 100 ); // 첫 실패는 [0, 첫 지연]
    SW_EXPECT_EQUAL( int64( 100 ), backoff.getNextDelayMs() );
    expectWithin( 50, 100 ); // 그다음은 [지연/2, 지연]
    expectWithin( 100, 200 );
    expectWithin( 175, 350 ); // 상한
    expectWithin( 175, 350 );
    backoff.reset();
    expectWithin( 0, 100 ); // 성공 뒤에는 처음부터
}

/**
 * @brief [ExponentialBackoffTest] 씨앗이 다른 두 클라이언트는 같은 실패 순서에서 다른 시각에 다시 시도한다(감사 C-11 — 한꺼번에 몰리지 않는다)
 */
SW_TEST_CASE( ExponentialBackoffTest, DifferentSeedsSpreadTheRetries )
{
    sw::ExponentialBackoff first( 1000, 1u );
    sw::ExponentialBackoff second( 1000, 2u );
    int32                  differentCount = 0;
    for ( int32 failure = 0; failure < 6; ++failure )
    {
        differentCount += first.takeDelayMs( 30000 ) != second.takeDelayMs( 30000 ) ? 1 : 0;
    }
    SW_EXPECT_TRUE( differentCount >= 5 );
}
