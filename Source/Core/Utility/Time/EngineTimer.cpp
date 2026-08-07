/**
 * @file EngineTimer.cpp
 * @brief Auto-generated documentation header
 */
#include "pch.h"
#include "Core/Utility/Time/EngineTimer.h"

namespace sw
{
	namespace
	{
		float64 getPerformanceSecondsPerCount() noexcept
		{
			static const float64 s_secondsPerCount = []()
			{
				int64 countsPerSec{};
				QueryPerformanceFrequency( reinterpret_cast<LARGE_INTEGER*>( &countsPerSec ) );
				return 1.0 / static_cast<float64>( countsPerSec );
			}();
			return s_secondsPerCount;
		}
	}

	CpuTimer::CpuTimer() noexcept
	{
		_secondsPerCount = getPerformanceSecondsPerCount();
	}

	float32 CpuTimer::getTotalTime() const noexcept
	{
		if ( _bStopped == true )
			return static_cast<float32>( static_cast<float64>( ( _stopTime - _pausedTime ) - _baseTime ) * _secondsPerCount );

		return static_cast<float32>( static_cast<float64>( ( _currentTime - _pausedTime ) - _baseTime ) * _secondsPerCount );
	}

	float32 CpuTimer::getDeltaTime() const noexcept
	{
		return static_cast<float32>( _deltaTime );
	}

	void CpuTimer::resetTimer() noexcept
	{
		int64 currTime{};
		QueryPerformanceCounter( reinterpret_cast<LARGE_INTEGER*>( &currTime ) );

		_baseTime	= currTime;
		_prevTime	= currTime;
		_stopTime	= 0;
		_pausedTime = 0;
		_bStopped	= false;
	}

	void CpuTimer::startTimer() noexcept
	{
		int64 startTime{};
		QueryPerformanceCounter( reinterpret_cast<LARGE_INTEGER*>( &startTime ) );

		if ( _bStopped == true )
		{
			_pausedTime += ( startTime - _stopTime );
			_prevTime = startTime;
			_stopTime = 0;
			_bStopped = false;
		}
	}

	void CpuTimer::stopTimer() noexcept
	{
		if ( _bStopped == false )
		{
			int64 currTime{};
			QueryPerformanceCounter( reinterpret_cast<LARGE_INTEGER*>( &currTime ) );

			_stopTime = currTime;
			_bStopped = true;
		}
	}

	void CpuTimer::updateTimer() noexcept
	{
		if ( _bStopped == true )
		{
			_deltaTime = 0.0;
			return;
		}

		int64 currTime{};
		QueryPerformanceCounter( reinterpret_cast<LARGE_INTEGER*>( &currTime ) );
		_currentTime = currTime;

		_deltaTime = static_cast<float64>( _currentTime - _prevTime ) * _secondsPerCount;
		_prevTime  = _currentTime;

		if ( _deltaTime < 0.0 )
			_deltaTime = 0.0;
	}
}
