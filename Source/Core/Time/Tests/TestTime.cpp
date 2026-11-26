#include "pch.h"

#include "Core/Concurrency/thread.h"
#include "Core/Diagnostics/ScopedTimer.h"
#include "Core/TestFramework/TestFramework.h"
#include "Core/Time/CalendarDate.h"
#include "Core/Time/Clock.h"
#include "Core/Time/ExponentialBackoff.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/TimeRollover.h"

// ------------------------------------------------------------------------------
// 1) Core_Time — 스코프 타이머 · 스톱워치
//    스코프 타이머 · 스톱워치는 측정 도구라 실제 단조 시계로 잰다(경과가 음수가 아니고 읽기가 기준을 옮기지 않는지만 본다).
// ------------------------------------------------------------------------------
/**
 * @brief [TimeTest] 셰이더로 보내는 시간은 주기로 접혀 열 시간 뒤에도 1 ms 차이가 float32 에 남는다(감사 C-12 — 접지 않으면 한 눈금이 3.9 ms)
 * @details 접힌 값은 [0, 주기) 이고 주기를 넘으면 0 부터 다시 센다(Godot `time_rollover_secs` 와 같다). 음수 시간도 같은 범위로 접는다.
 */
SW_TEST_CASE( TimeTest, ShaderTimeFoldKeepsMillisecondsAfterTenHours )
{
    constexpr float64 kRollover = 3600.0;
    constexpr float64 kTenHours = 10.0 * 3600.0 + 12.25;
    const float32     before    = sw::TimeRolloverUtil::fold( kTenHours, kRollover );
    const float32     after     = sw::TimeRolloverUtil::fold( kTenHours + 0.001, kRollover );
    const float32     unfoldedA = static_cast<float32>( kTenHours );
    const float32     unfoldedB = static_cast<float32>( kTenHours + 0.001 );
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
    float32 stalled = 4194304.0f; // 2^22 초
    stalled += 0.25f;
    SW_EXPECT_TRUE_MSG( stalled == 4194304.0f, "an unrebased float32 clock stops at 2^22 s (the defect)" );

    float32 time        = 0.0f;
    float64 total       = 0.0;
    uint32  rebaseCount = 0;
    for ( uint32 step = 0; step < 40000; ++step )
    {
        time += 0.25f;
        total += 0.25;
        if ( sw::TimeRolloverUtil::rebase( time ) )
            ++rebaseCount;
    }
    SW_EXPECT_EQUAL( 2u, rebaseCount );
    SW_EXPECT_TRUE( time < sw::TimeRolloverUtil::kRebaseSeconds );
    SW_EXPECT_NEAR( total - 2.0 * sw::TimeRolloverUtil::kRebaseSeconds, static_cast<float64>( time ), 1e-9 );
}

// 결함(ScopedTimer): 경과를 읽을 때마다 프레임 타이머의 기준을 옮겨 두 번째 읽기가 0 이었다. 읽기는 const 라 기준을 옮기는 꼴이 컴파일되지 않는다.
static_assert( std::is_invocable_v<decltype( &sw::ScopedTimer::getElapsedSeconds ), const sw::ScopedTimer&>, "reading a scoped timer must not move its start" );

/**
 * @brief [TimeTest] ScopedTimer — 두 번 읽어도 둘째가 첫째보다 작지 않다(스코프 시작부터의 시간)
 */
SW_TEST_CASE( TimeTest, ScopedTimerReadTwiceKeepsTheStart )
{
    sw::ScopedTimer timer( "TestScopedTimer" );
    sw::ThreadUtil::sleepForMilliseconds( 2 );
    const float64 first  = timer.getElapsedSeconds();
    const float64 second = timer.getElapsedSeconds();
    SW_EXPECT_TRUE( first > 0.0 );
    SW_EXPECT_TRUE( second >= first );
}

/**
 * @brief [TimeTest] Stopwatch 랩 — 랩은 직전 랩부터의 길이이고 기준을 옮긴다. 경과 읽기는 옮기지 않는다
 */
SW_TEST_CASE( TimeTest, StopwatchLapMovesTheStartAndElapsedDoesNot )
{
    sw::Stopwatch stopwatch;
    sw::ThreadUtil::sleepForMilliseconds( 2 );
    const float64 elapsed = stopwatch.getElapsedSeconds();
    SW_EXPECT_TRUE( stopwatch.getElapsedSeconds() >= elapsed );
    const float64 lap = stopwatch.lapSeconds();
    SW_EXPECT_TRUE( lap >= elapsed );
    SW_EXPECT_TRUE( stopwatch.getElapsedSeconds() < lap + 1.0 );
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

/** @brief [CalendarDateTest] 유닉스 일 수와 날짜가 서로 되돌아온다(윤년 · 1970 앞 · 400 년 주기 경계) — 그 달에 없는 날은 isValid 가 거른다 */
SW_TEST_CASE( CalendarDateTest, UnixDaysRoundTrip )
{
    SW_EXPECT_EQUAL( int64( 0 ), ( sw::CalendarDate{ 1970, 1, 1 }.toUnixDays() ) );
    SW_EXPECT_EQUAL( int64( -1 ), ( sw::CalendarDate{ 1969, 12, 31 }.toUnixDays() ) );
    SW_EXPECT_EQUAL( int64( 11016 ), ( sw::CalendarDate{ 2000, 2, 29 }.toUnixDays() ) );
    for ( int64 days = -719468 - 400; days < 146097 * 2; days += 37 )
    {
        const sw::CalendarDate date = sw::CalendarDate::fromUnixDays( days );
        SW_REQUIRE_EQUAL( days, date.toUnixDays() );
        SW_REQUIRE_TRUE( date.isValid() );
    }
    SW_EXPECT_TRUE( ( sw::CalendarDate{ 2024, 2, 29 }.isValid() ) );
    SW_EXPECT_FALSE( ( sw::CalendarDate{ 2023, 2, 29 }.isValid() ) );
    SW_EXPECT_FALSE( ( sw::CalendarDate{ 1900, 2, 29 }.isValid() ) );
    SW_EXPECT_FALSE( ( sw::CalendarDate{ 2024, 13, 1 }.isValid() ) );
    SW_EXPECT_FALSE( ( sw::CalendarDate{ 2024, 4, 31 }.isValid() ) );
}
