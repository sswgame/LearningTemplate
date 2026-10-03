#include "pch.h"

#include "Core/Time/CpuTimer.h"

#include "Core/Math/MathUtil.h"
#include "Core/Time/CpuClock.h"

namespace sw
{
    CpuTimer::CpuTimer() noexcept
        : _secondsPerCount{ 1.0 / static_cast<float64>( CpuClock::getCountsPerSecond() ) }
        , _deltaTime{ -1.0 }
        , _baseTime{ 0 }
        , _pausedTime{ 0 }
        , _stopTime{ 0 }
        , _prevTime{ 0 }
        , _currentTime{ 0 }
        // **중지 상태로 둔다.** 돌고 있는 상태로 만들면 `startTimer()` 가 `if ( _bStopped )` 에 걸려 아무 일도 하지 않고,
        // `_prevTime` 이 0 인 채로 첫 `updateTimer()` 가 돌아 델타가 **QPC 기준점 이후의 전체 시간**(부팅 이후 몇 시간)이 된다
        // (`resetTimer()` 를 먼저 부르면 가려진다).
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

        const int64   currTime = ( _currentTime != 0 ) ? _currentTime : CpuClock::readCounter();
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
        const int64 currTime = CpuClock::readCounter();
        _baseTime            = currTime;
        _prevTime            = currTime;
        _currentTime         = currTime;
        _stopTime            = 0;
        _pausedTime          = 0;
        _bStopped            = false;
    }

    void CpuTimer::startTimer() noexcept
    {
        const int64 startTime = CpuClock::readCounter();
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
            _stopTime = CpuClock::readCounter();
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

        _currentTime = CpuClock::readCounter();
        _deltaTime   = static_cast<float64>( _currentTime - _prevTime ) * _secondsPerCount;
        _prevTime    = _currentTime;

        if ( _deltaTime < 0.0 )
            _deltaTime = 0.0;
    }
} // namespace sw
