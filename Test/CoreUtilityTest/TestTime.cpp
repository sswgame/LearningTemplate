/**
 * @file TestTime.cpp
 * @brief Auto-generated documentation header
 */
#include "TestFramework.h"
#include "Core/Utility/Time/EngineTimer.h"

SW_TEST_CASE( Utility_Time, CPUTimerBasic )
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

SW_TEST_CASE( Utility_Time, ScopeTimerBasic )
{
	float elapsedSec = 0.0f;
	{
		sw::ScopeTimer timer( "TestScopeTimer" );
		std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
		elapsedSec = timer.getElapsedTimeInSeconds();
	}
	SW_EXPECT_TRUE( elapsedSec > 0.005f );
}

SW_TEST_CASE( Utility_Time, CPUTimerResetAndPause )
{
	CpuTimer timer;
	timer.resetTimer();
	timer.startTimer();
	std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
	timer.stopTimer();

	float pausedTotal = timer.getTotalTime();
	std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );

	SW_EXPECT_NEAR_EQUAL( pausedTotal, timer.getTotalTime(), 1e-2f );

	timer.startTimer();
	SW_EXPECT_FALSE( timer.isStopped() );
}
