#include "pch.h"

#include "Core/Time/CpuTimer.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include "Core/Common/PlatformOsHeaders.h"
#endif

namespace sw
{

	namespace
	{

		/**
		 * @brief OS 고해상도 카운터 1틱당 경과 초(Second) 계수를 반환합니다.
		 */
		float64 getPerformanceSecondsPerCount() noexcept
		{
			static const float64 s_secondsPerCount = []()
			{
#if defined( SW_PLATFORM_WINDOWS )
				int64 countsPerSec{};
				QueryPerformanceFrequency( reinterpret_cast<LARGE_INTEGER*>( &countsPerSec ) );
				return 1.0 / static_cast<float64>( countsPerSec );
#elif defined( SW_PLATFORM_LINUX )
				// Linux CLOCK_MONOTONIC: 1ns = 1e-9s
				return 1e-9;
#elif defined( SW_PLATFORM_MACOS )
				// macOS mach_absolute_time: 1ns = 1e-9s
				return 1e-9;
#else
	#error "Unsupported platform"
#endif
			}();
			return s_secondsPerCount;
		}

		/**
		 * @brief 현재 OS 고해상도 하드웨어 카운터 값을 반환합니다.
		 */
		int64 getCurrentPerformanceCount() noexcept
		{
			int64 currTime{};
#if defined( SW_PLATFORM_WINDOWS )
			QueryPerformanceCounter( reinterpret_cast<LARGE_INTEGER*>( &currTime ) );
#elif defined( SW_PLATFORM_LINUX )
			timespec time{};
			clock_gettime( CLOCK_MONOTONIC, &time );
			currTime = static_cast<int64>( time.tv_sec ) * 1'000'000'000LL + static_cast<int64>( time.tv_nsec );
#elif defined( SW_PLATFORM_MACOS )
			mach_timebase_info_data_t timebaseInfo;
			mach_timebase_info( &timebaseInfo );
			uint64 time = mach_absolute_time();
			currTime	= static_cast<int64>( time * timebaseInfo.numer ) / static_cast<int64>( timebaseInfo.denom );
#else
	#error "Unsupported platform"
#endif
			return currTime;
		}

	} // namespace

	CpuTimer::CpuTimer() noexcept
		: _secondsPerCount{ getPerformanceSecondsPerCount() }
		, _deltaTime{ -1.0 }
		, _baseTime{ 0 }
		, _pausedTime{ 0 }
		, _stopTime{ 0 }
		, _prevTime{ 0 }
		, _currentTime{ 0 }
		, _bStopped{ false }
	{
	}

	/**
	 * @brief 타이머 시작(BaseTime) 이후 일시 정지 시간을 제외한 총 경과 시간(초)을 반환합니다.
	 */
	float32 CpuTimer::getTotalTime() const noexcept
	{
		if ( _bStopped )
			return static_cast<float32>( static_cast<float64>( ( _stopTime - _pausedTime ) - _baseTime ) * _secondsPerCount );

		return static_cast<float32>( static_cast<float64>( ( _currentTime - _pausedTime ) - _baseTime ) * _secondsPerCount );
	}

	/**
	 * @brief 이전 프레임(Tick) 대비 경과된 델타 타임(Delta Time, 초)을 반환합니다.
	 */
	float32 CpuTimer::getDeltaTime() const noexcept
	{
		return static_cast<float32>( _deltaTime );
	}

	/**
	 * @brief 타이머를 리셋하고 현재 시점을 새로운 기준 시점(_baseTime)으로 설정합니다.
	 */
	void CpuTimer::resetTimer() noexcept
	{
		const int64 currTime = getCurrentPerformanceCount();
		_baseTime			 = currTime;
		_prevTime			 = currTime;
		_stopTime			 = 0;
		_pausedTime			 = 0;
		_bStopped			 = false;
	}

	void CpuTimer::startTimer() noexcept
	{
		const int64 startTime = getCurrentPerformanceCount();
		if ( _bStopped )
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
			_stopTime = getCurrentPerformanceCount();
			_bStopped = true;
		}
	}

	void CpuTimer::updateTimer() noexcept
	{
		if ( _bStopped )
		{
			_deltaTime = 0.0;
			return;
		}

		_currentTime = getCurrentPerformanceCount();
		_deltaTime	 = static_cast<float64>( _currentTime - _prevTime ) * _secondsPerCount;
		_prevTime	 = _currentTime;

		if ( _deltaTime < 0.0 )
			_deltaTime = 0.0;
	}
} // namespace sw
