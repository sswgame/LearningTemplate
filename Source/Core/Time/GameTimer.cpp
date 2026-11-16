#include "pch.h"

#include "Core/Time/GameTimer.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct GameTimerInternal
        {
            /** @brief 나노초 하나의 초입니다(시계는 나노초를 준다). */
            static constexpr float64 kSecondsPerNanosecond = 1.0 / static_cast<float64>( constant::time::kNanosecondsPerSecond );
        };
    } // namespace
} // namespace sw

namespace sw
{
    GameTimer::GameTimer() noexcept
        : GameTimer{ SystemMonotonicClock::get() }
    {
    }

    GameTimer::GameTimer( const IClock& clock ) noexcept
        : _pClock{ &clock }
        , _deltaTime{ -1.0 }
        , _baseTime{ 0 }
        , _pausedTime{ 0 }
        , _stopTime{ 0 }
        , _prevTime{ 0 }
        , _currentTime{ 0 }
        // **중지 상태로 둔다.** 돌고 있는 상태로 만들면 `startTimer()` 가 `if ( _bStopped )` 에 걸려 아무 일도 하지 않고,
        // `_prevTime` 이 0 인 채로 첫 `updateTimer()` 가 돌아 델타가 **시계 기준점 이후의 전체 시간**(부팅 이후 몇 시간)이 된다
        // (`resetTimer()` 를 먼저 부르면 가려진다).
        //
        // 중지 상태로 두면 `startTimer()` 가 제 역할을 한다. `_pausedTime += ( 시작 시각 - _stopTime(0) )` 이 기준을
        // 시작 시각으로 옮겨 주므로, reset 없이 만들어 바로 start 해도 누적과 델타가 맞는다.
        , _bStopped{ SW_TRUE }
    {
    }

    /**
     * @brief 기준 시각(_baseTime) 이후 일시정지 시간을 뺀 총 경과 시간(초)을 반환합니다. 돌고 있으면 직전 갱신(reset · start · update) 시각까지입니다.
     */
    float64 GameTimer::getTotalTime() const noexcept
    {
        // float64 초 — float32 로 좁히면 한 눈금이 9 시간에 3.9 ms, 36 시간에 15.6 ms 라 종일 켠 에디터 · 서버에서 시간이 끊긴다(감사 C-12, 언리얼 FApp::CurrentTime · 유니티 timeAsDouble).
        const int64   endTime = _bStopped ? _stopTime : _currentTime;
        const float64 total   = static_cast<float64>( ( endTime - _pausedTime ) - _baseTime ) * GameTimerInternal::kSecondsPerNanosecond;
        return MathUtil::max( 0.0, total );
    }

    /**
     * @brief 직전 프레임(updateTimer) 이후 경과한 델타 시간(초)을 반환합니다.
     */
    float32 GameTimer::getDeltaTime() const noexcept
    {
        return static_cast<float32>( _deltaTime );
    }

    /**
     * @brief 타이머를 리셋하고 현재 시각을 새 기준 시각(_baseTime)으로 둡니다.
     */
    void GameTimer::resetTimer() noexcept
    {
        const int64 currTime = _pClock->nowNanoseconds();
        _baseTime            = currTime;
        _prevTime            = currTime;
        _currentTime         = currTime;
        _stopTime            = 0;
        _pausedTime          = 0;
        _bStopped            = SW_FALSE;
    }

    void GameTimer::startTimer() noexcept
    {
        const int64 startTime = _pClock->nowNanoseconds();
        if ( _bStopped )
        {
            _pausedTime += ( startTime - _stopTime );
            _prevTime    = startTime;
            _currentTime = startTime;
            _stopTime    = 0;
            _bStopped    = SW_FALSE;
        }
    }

    void GameTimer::stopTimer() noexcept
    {
        if ( _bStopped == false )
        {
            _stopTime = _pClock->nowNanoseconds();
            _bStopped = SW_TRUE;
        }
    }

    void GameTimer::updateTimer() noexcept
    {
        if ( _bStopped )
        {
            _deltaTime = 0.0;
            return;
        }

        _currentTime = _pClock->nowNanoseconds();
        _deltaTime   = static_cast<float64>( _currentTime - _prevTime ) * GameTimerInternal::kSecondsPerNanosecond;
        _prevTime    = _currentTime;

        if ( _deltaTime < 0.0 )
            _deltaTime = 0.0;
    }
} // namespace sw
