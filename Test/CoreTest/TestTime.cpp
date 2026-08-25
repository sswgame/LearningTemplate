#include "pch.h"

#include "Core/Time/CpuTimer.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_Time — CPUTimer·스코프 타이머
// ------------------------------------------------------------------------------
/**
 * @brief [Core_Time] CPUTimer 기본
 */

SW_TEST_CASE( Core_Time, CPUTimerBasic )
{
	CpuTimer timer;
	SW_EXPECT_FALSE( timer.isStopped() );

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
 * @brief [Core_Time] ScopeCpuTimer 기본
 */
SW_TEST_CASE( Core_Time, ScopeCpuTimerBasic )
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
 * @brief [Core_Time] CPUTimer 리셋과 일시정지
 */
SW_TEST_CASE( Core_Time, CPUTimerResetAndPause )
{
	CpuTimer timer;
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
 * @brief [Core_Time] 연속 프레임 틱 및 누적 시간 무결성 검증
 */
SW_TEST_CASE( Core_Time, ContinuousFrameTicksAndTotalTime )
{
	CpuTimer timer;
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
