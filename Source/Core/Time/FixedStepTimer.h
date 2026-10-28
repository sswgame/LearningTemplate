/**
 * @file FixedStepTimer.h
 * @brief 고정 스텝 누적기입니다. 들쭉날쭉한 프레임 시간을 같은 크기의 시뮬레이션 스텝으로 바꾸고, 남은 시간으로 보간 비를 냅니다.
 * @details 게임 시뮬레이션(열차 물리 · 경영 시뮬레이션 · 판 진행)과 씬 물리가 같은 이 타입을 씁니다(Gaffer "Fix Your Timestep").
 *          엔진 프레임 루프의 시간 정책(가변 델타 상한 · 시간 배율)은 `FixedTimestep` 입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct FixedStepTimer
     * @brief 프레임 시간을 쌓아 `_step` 이 찰 때마다 한 스텝을 냅니다. 한 프레임에 받는 시간은 `_maxFrameTime` 에서 잘라, 디버거 정지 · 로딩 히치가 수천
     *        스텝이 되거나 따라잡느라 다음 프레임이 더 길어지는 악순환을 막습니다(그 순간은 느리게 흐른다).
     * @details 화면은 마지막 두 스텝 사이를 `getAlpha()` 로 보간해 그립니다(유니티 Rigidbody interpolation). 상태 바이트는 `_accumulator` 를 그대로 적습니다.
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

        /**
         * @brief @p deltaTime 을 쌓고 이번에 낼 스텝 수를 돌려줍니다(남은 시간은 다음 프레임으로). 0 이하 · NaN 은 쌓지 않습니다.
         * @details 스텝의 10 만분의 1 은 봐 줍니다 — 1/60 초 프레임을 1/180 초 스텝 셋으로 나눌 때 float 반올림으로 한 스텝이 다음 프레임으로 밀려
         *          2 · 4 스텝이 번갈아 나오지 않게 합니다.
         */
        constexpr int32 consume( float32 deltaTime )
        {
            const bool bPositive = deltaTime > 0.0f; // NaN 은 비교가 모두 거짓이라 여기서 걸러진다
            if ( bPositive == false )
                return 0;
            _accumulator += deltaTime < _maxFrameTime ? deltaTime : _maxFrameTime;
            const float32 tolerance = _step * 1.0e-5f;
            int32         stepCount = 0;
            while ( _accumulator + tolerance >= _step )
            {
                _accumulator -= _step;
                ++stepCount;
            }
            if ( _accumulator < 0.0f )
                _accumulator = 0.0f;
            return stepCount;
        }

        constexpr void    reset() { _accumulator = 0.0f; }
        constexpr float32 getStep() const { return _step; }
        /** @brief 다음 스텝까지 찬 비율(0..1)입니다. 마지막 스텝과 다음 스텝 사이 어디를 그릴지 정합니다. */
        constexpr float32 getAlpha() const
        {
            const float32 alpha = _accumulator / _step;
            return alpha < 1.0f ? alpha : 1.0f;
        }
    };
} // namespace sw
