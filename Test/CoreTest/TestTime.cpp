#include "pch.h"

#include "Core/Time/CpuTimer.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_Time — CPUTimer·스코프 타이머
// ------------------------------------------------------------------------------
/**
 * @brief [TimeTest] CPUTimer 기본
 */

SW_TEST_CASE( TimeTest, CPUTimerBasic )
{
    sw::CpuTimer timer;
    // 생성자는 중지 상태로 둔다 (헤더가 처음부터 그렇게 적고 있었다).
    SW_EXPECT_TRUE( timer.isStopped() );

    timer.resetTimer();
    timer.startTimer();
    std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
    timer.updateTimer();

    SW_EXPECT_TRUE( timer.getDeltaTime() >= 0.005f );
    SW_EXPECT_TRUE( timer.getTotalTime() >= 0.005f );

    timer.stopTimer();
    SW_EXPECT_TRUE( timer.isStopped() );
}

/**
 * @brief [TimeTest] ScopeCpuTimer 기본
 */
SW_TEST_CASE( TimeTest, ScopeCpuTimerBasic )
{
    float32 elapsedSec{ 0.0f };
    {
        sw::ScopeCpuTimer timer( "TestScopeCpuTimer" );
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        elapsedSec = timer.getElapsedTimeInSeconds();
    }
    SW_EXPECT_TRUE( elapsedSec > 0.005f );
}

/**
 * @brief [TimeTest] 만들자마자 start 해도 첫 델타가 부팅 이후 시간이 되지 않는다
 * @details 생성자는 "중지 상태로 둡니다" 라고 적혀 있었는데 실제로는 **돌고 있었다**. 그래서
 *          `startTimer()` 가 `if ( _bStopped )` 에 걸려 아무 일도 하지 않았고, `_prevTime` 이 0 인 채로
 *          첫 `updateTimer()` 가 돌아 델타가 **QPC 기준점 이후 전체 시간**이 됐다. 호출부 다섯 곳이
 *          전부 `resetTimer()` 를 먼저 불러서 가려져 있었을 뿐이다.
 */
SW_TEST_CASE( TimeTest, FreshTimerDoesNotReportTimeSinceBoot )
{
    sw::CpuTimer timer;
    SW_EXPECT_TRUE_MSG( timer.isStopped(), "생성자 주석은 중지 상태라고 말한다" );

    timer.startTimer();
    SW_EXPECT_FALSE( timer.isStopped() );

    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    timer.updateTimer();

    SW_EXPECT_TRUE( timer.getDeltaTime() >= 0.005f );
    SW_EXPECT_TRUE_MSG( timer.getDeltaTime() < 1.0f, "델타가 스코프 길이가 아니라 부팅 이후 시간이다" );
    SW_EXPECT_TRUE_MSG( timer.getTotalTime() < 1.0f, "누적이 스코프 길이가 아니라 부팅 이후 시간이다" );
}

/**
 * @brief [TimeTest] CPUTimer 리셋과 일시정지
 */
SW_TEST_CASE( TimeTest, CPUTimerResetAndPause )
{
    sw::CpuTimer timer;
    timer.resetTimer();
    timer.startTimer();
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    timer.stopTimer();

    float32 pausedTotal = timer.getTotalTime();
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );

    SW_EXPECT_NEAR_EQUAL( pausedTotal, timer.getTotalTime(), 1e-2f );

    timer.startTimer();
    SW_EXPECT_FALSE( timer.isStopped() );
}

/**
 * @brief [TimeTest] 연속 프레임 틱 및 누적 시간 무결성 검증
 */
SW_TEST_CASE( TimeTest, ContinuousFrameTicksAndTotalTime )
{
    sw::CpuTimer timer;
    timer.resetTimer();
    timer.startTimer();

    float32 accumulatedDelta{ 0.0f };
    for ( int32 frame = 0; frame < 5; ++frame )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 3 ) );
        timer.updateTimer();
        accumulatedDelta += timer.getDeltaTime();
        SW_EXPECT_TRUE( timer.getDeltaTime() > 0.0f );
    }

    SW_EXPECT_NEAR_EQUAL( accumulatedDelta, timer.getTotalTime(), 2e-2f );
    timer.stopTimer();
}
