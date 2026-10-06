/**
 * @file FixedStepTimer.h
 * @brief 고정 스텝 누적기 — 들쭉날쭉한 프레임 시간을 같은 크기의 시뮬레이션 걸음으로 바꿉니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct FixedStepTimer
     * @brief 프레임 시간을 쌓아 `_step` 이 찰 때마다 한 걸음을 냅니다. 한 프레임에 받는 시간은 `_maxFrameTime` 에서 잘라 디버거 정지가 수천 걸음이 되지
     *        않게 합니다. 열차 물리 · 경영 시뮬레이션처럼 프레임 수와 상관없이 같은 결과를 내야 하는 곳이 씁니다(Gaffer "Fix Your Timestep").
     * @code
     *     const int32 stepCount = _timer.consume( deltaTime );
     *     for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
     *         integrate( _timer.getStep() );
     * @endcode
     */
    struct FixedStepTimer
    {
        float32 _step{ 1.0f / 60.0f };
        float32 _maxFrameTime{ 0.25f };
        float32 _accumulator{ 0.0f };

        constexpr FixedStepTimer() = default;
        constexpr FixedStepTimer( float32 step, float32 maxFrameTime )
            : _step{ step > 0.0f ? step : 1.0f / 60.0f }
            , _maxFrameTime{ maxFrameTime }
            , _accumulator{ 0.0f }
        {
        }

        /** @brief @p deltaTime 을 쌓고 이번에 낼 걸음 수를 돌려줍니다(남은 시간은 다음 프레임으로). */
        constexpr int32 consume( float32 deltaTime )
        {
            if ( deltaTime <= 0.0f )
                return 0;
            _accumulator += deltaTime < _maxFrameTime ? deltaTime : _maxFrameTime;
            int32 stepCount = 0;
            while ( _accumulator >= _step )
            {
                _accumulator -= _step;
                ++stepCount;
            }
            return stepCount;
        }

        constexpr void    reset() { _accumulator = 0.0f; }
        constexpr float32 getStep() const { return _step; }
        /** @brief 다음 걸음까지 찬 비율(0..1)입니다 — 그리기 보간에 씁니다. */
        constexpr float32 getAlpha() const { return _accumulator / _step; }
    };
} // namespace sw
