/**
 * @file EngineTimer.cpp
 * @brief 엔진 타이머 구현
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
#if defined( SW_PLATFORM_WINDOWS )
				int64 countsPerSec{};
				QueryPerformanceFrequency( reinterpret_cast<LARGE_INTEGER*>( &countsPerSec ) );
				return 1.0 / static_cast<float64>( countsPerSec );
#elif defined( SW_PLATFORM_LINUX )
				timespec time{};
				clock_gettime( CLOCK_MONOTONIC, &time );
				return static_cast<int64>( time.tv_sec ) * 1'000'000'000LL + static_cast<int64>( time.tv_nsec );
#elif defined( SW_PLATFORM_MACOS )
				// macOS에서 clock_gettime은 macOS 10.12 이상에서만 지원됩니다.
				// 이전 버전에서는 mach_absolute_time을 사용해야 합니다.
				// 여기서는 간단히 mach_absolute_time을 사용합니다.
				mach_timebase_info_data_t timebaseInfo;
				mach_timebase_info( &timebaseInfo );
				uint64_t time = mach_absolute_time();
				return static_cast<float64>( time * timebaseInfo.numer ) / static_cast<float64>( timebaseInfo.denom );
#else
	#error "Unsupported platform"
#endif
			}();
			return s_secondsPerCount;
		}
	} // namespace

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

		_baseTime	= currTime;
		_prevTime	= currTime;
		_stopTime	= 0;
		_pausedTime = 0;
		_bStopped	= false;
	}

	void CpuTimer::startTimer() noexcept
	{
		int64 startTime{};
#if defined( SW_PLATFORM_WINDOWS )
		QueryPerformanceCounter( reinterpret_cast<LARGE_INTEGER*>( &startTime ) );
#elif defined( SW_PLATFORM_LINUX )
		timespec time{};
		clock_gettime( CLOCK_MONOTONIC, &time );
		startTime = static_cast<int64>( time.tv_sec ) * 1'000'000'000LL + static_cast<int64>( time.tv_nsec );
#elif defined( SW_PLATFORM_MACOS )
		mach_timebase_info_data_t timebaseInfo;
		mach_timebase_info( &timebaseInfo );
		uint64 time = mach_absolute_time();
		startTime	= static_cast<int64>( time * timebaseInfo.numer ) / static_cast<int64>( timebaseInfo.denom );
#else
	#error "Unsupported platform"
#endif

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
		_currentTime = currTime;

		_deltaTime = static_cast<float64>( _currentTime - _prevTime ) * _secondsPerCount;
		_prevTime  = _currentTime;

		if ( _deltaTime < 0.0 )
			_deltaTime = 0.0;
	}
} // namespace sw
