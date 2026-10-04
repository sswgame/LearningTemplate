#include "pch.h"

#include "Engine/Physics/FixedStepAccumulator.h"

namespace sw
{
    FixedStepAccumulator::FixedStepAccumulator()
        : _accumulated{ 0.0 }
        , _droppedTime{ 0.0 }
        , _fixedTimeStep{ 1.0f / 60.0f }
        , _maxStepsPerFrame{ 4 }
    {
    }

    void FixedStepAccumulator::configure( float32 fixedTimeStep, uint32 maxStepsPerFrame )
    {
        if ( fixedTimeStep > 0.0f )
            _fixedTimeStep = fixedTimeStep;
        if ( maxStepsPerFrame > 0 )
            _maxStepsPerFrame = maxStepsPerFrame;
    }

    uint32 FixedStepAccumulator::advance( float32 deltaTime )
    {
        // NaN 은 비교가 모두 거짓이라 `deltaTime > 0` 이 거른다.
        if ( deltaTime > 0.0f )
            _accumulated += static_cast<float64>( deltaTime );

        const float64 step      = static_cast<float64>( _fixedTimeStep );
        uint32        stepCount = 0;
        // 끝의 작은 오차로 한 스텝이 다음 프레임으로 밀리지 않게 스텝의 백만분의 일을 봐 준다.
        const float64 tolerance = step * 1.0e-6;
        while ( _accumulated + tolerance >= step && stepCount < _maxStepsPerFrame )
        {
            _accumulated -= step;
            ++stepCount;
        }
        if ( _accumulated < 0.0 )
            _accumulated = 0.0;
        // 상한에 걸렸으면 남은 것 가운데 한 스텝 미만만 남기고 버린다 — 다음 프레임이 또 상한까지 따라잡느라 늦어지지 않게.
        if ( _accumulated >= step )
        {
            const float64 kept = _accumulated - step * static_cast<float64>( static_cast<uint64>( _accumulated / step ) );
            _droppedTime += _accumulated - kept;
            _accumulated = kept;
        }
        return stepCount;
    }

    void FixedStepAccumulator::reset()
    {
        _accumulated = 0.0;
    }

    float32 FixedStepAccumulator::getAlpha() const
    {
        const float32 alpha = static_cast<float32>( _accumulated / static_cast<float64>( _fixedTimeStep ) );
        if ( alpha < 0.0f )
            return 0.0f;
        return alpha > 1.0f ? 1.0f : alpha;
    }
} // namespace sw
