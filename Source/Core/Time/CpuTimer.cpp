#include "pch.h"

#include "Core/Time/CpuTimer.h"

#include "Core/Math/MathUtil.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"
#endif

namespace sw
{
    namespace
    {
        struct CpuTimerInternal
        {
            /**
             * @brief OS 고해상도 카운터의 한 틱이 몇 초인지 반환합니다.
             */
            static float64 getPerformanceSecondsPerCount() noexcept
            {
                static const float64 s_secondsPerCount = []()
                {
#if defined( SW_PLATFORM_WINDOWS )
                    int64 countsPerSec{};
                    QueryPerformanceFrequency( reinterpret_cast<LARGE_INTEGER*>( &countsPerSec ) );
                    return 1.0 / static_cast<float64>( countsPerSec );
#elif defined( SW_PLATFORM_LINUX )
                    // Linux CLOCK_MONOTONIC: 1ns = 1e-9s
                    return constant::kSecondsPerNanosecond;
#elif defined( SW_PLATFORM_MACOS )
                    // macOS mach_absolute_time: 1ns = 1e-9s
                    return constant::kSecondsPerNanosecond;
#else
    #error "Unsupported platform"
#endif
                }();
                return s_secondsPerCount;
            }

            /**
             * @brief 현재 OS 고해상도 카운터 값을 반환합니다.
             */
            static int64 getCurrentPerformanceCount() noexcept
            {
                int64 currTime{};
#if defined( SW_PLATFORM_WINDOWS )
                QueryPerformanceCounter( reinterpret_cast<LARGE_INTEGER*>( &currTime ) );
#elif defined( SW_PLATFORM_LINUX )
                timespec time{};
                clock_gettime( CLOCK_MONOTONIC, &time );
                currTime = static_cast<int64>( time.tv_sec ) * constant::kNanosecondsPerSecond + static_cast<int64>( time.tv_nsec );
#elif defined( SW_PLATFORM_MACOS )
                mach_timebase_info_data_t timebaseInfo;
                mach_timebase_info( &timebaseInfo );
                uint64 time = mach_absolute_time();
                currTime    = static_cast<int64>( time * timebaseInfo.numer ) / static_cast<int64>( timebaseInfo.denom );
#else
    #error "Unsupported platform"
#endif
                return currTime;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CpuTimer::CpuTimer() noexcept
        : _secondsPerCount{ CpuTimerInternal::getPerformanceSecondsPerCount() }
        , _deltaTime{ -1.0 }
        , _baseTime{ 0 }
        , _pausedTime{ 0 }
        , _stopTime{ 0 }
        , _prevTime{ 0 }
        , _currentTime{ 0 }
        // **중지 상태로 둔다.** 헤더도, `FrameRenderer::initialize` 의 주석도 그렇게 적고 있었지만 실제로는 돌고 있었다.
        // 그래서 `startTimer()` 가 `if ( _bStopped )` 에 걸려 아무 일도 하지 않았고, `_prevTime` 이 0 인 채로 첫
        // `updateTimer()` 가 돌아 델타가 **QPC 기준점 이후의 전체 시간**(부팅 이후 몇 시간)이 됐다. 호출부 다섯 곳이 모두
        // `resetTimer()` 를 먼저 불러서 드러나지 않았을 뿐이다. 그 순서를 잊는 순간 터진다.
        //
        // 중지 상태로 두면 `startTimer()` 가 제 역할을 한다. `_pausedTime += ( 시작 시각 - _stopTime(0) )` 이 기준을
        // 시작 시각으로 옮겨 주므로, reset 없이 만들어 바로 start 해도 누적과 델타가 맞는다.
        , _bStopped{ true }
    {
    }

    /**
     * @brief 기준 시각(_baseTime) 이후 일시정지 시간을 뺀 총 경과 시간(초)을 반환합니다.
     */
    float32 CpuTimer::getTotalTime() const noexcept
    {
        if ( _bStopped )
        {
            const float64 total = static_cast<float64>( ( _stopTime - _pausedTime ) - _baseTime ) * _secondsPerCount;
            return static_cast<float32>( MathUtil::max( 0.0, total ) );
        }

        const int64   currTime = ( _currentTime != 0 ) ? _currentTime : CpuTimerInternal::getCurrentPerformanceCount();
        const float64 total    = static_cast<float64>( ( currTime - _pausedTime ) - _baseTime ) * _secondsPerCount;
        return static_cast<float32>( MathUtil::max( 0.0, total ) );
    }

    /**
     * @brief 직전 프레임(updateTimer) 이후 경과한 델타 시간(초)을 반환합니다.
     */
    float32 CpuTimer::getDeltaTime() const noexcept
    {
        return static_cast<float32>( _deltaTime );
    }

    /**
     * @brief 타이머를 리셋하고 현재 시각을 새 기준 시각(_baseTime)으로 둡니다.
     */
    void CpuTimer::resetTimer() noexcept
    {
        const int64 currTime = CpuTimerInternal::getCurrentPerformanceCount();
        _baseTime            = currTime;
        _prevTime            = currTime;
        _currentTime         = currTime;
        _stopTime            = 0;
        _pausedTime          = 0;
        _bStopped            = false;
    }

    void CpuTimer::startTimer() noexcept
    {
        const int64 startTime = CpuTimerInternal::getCurrentPerformanceCount();
        if ( _bStopped )
        {
            _pausedTime += ( startTime - _stopTime );
            _prevTime    = startTime;
            _currentTime = startTime;
            _stopTime    = 0;
            _bStopped    = false;
        }
    }

    void CpuTimer::stopTimer() noexcept
    {
        if ( _bStopped == false )
        {
            _stopTime = CpuTimerInternal::getCurrentPerformanceCount();
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

        _currentTime = CpuTimerInternal::getCurrentPerformanceCount();
        _deltaTime   = static_cast<float64>( _currentTime - _prevTime ) * _secondsPerCount;
        _prevTime    = _currentTime;

        if ( _deltaTime < 0.0 )
            _deltaTime = 0.0;
    }
} // namespace sw
