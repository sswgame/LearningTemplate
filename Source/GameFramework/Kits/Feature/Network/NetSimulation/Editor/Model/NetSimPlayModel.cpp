#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/Editor/Model/NetSimPlayModel.h"

#include "Core/Math/MathUtil.h"

namespace sw::editor
{
    NetSimStepPacer::NetSimStepPacer()
        : _accumulatedSeconds{ 0.0 }
    {
    }

    uint32 NetSimStepPacer::consume( float64 frameSeconds, float64 tickInterval, uint32 maxSteps )
    {
        // NaN 이 한 번 들어가면 모은 시간이 영원히 NaN 이라 재생이 말없이 멈췄다. 음수는 뒤로 가는 시간이다.
        const bool bFrameTimeUsable = frameSeconds > 0.0;
        if ( bFrameTimeUsable == false )
            return 0;
        _accumulatedSeconds += frameSeconds;
        uint32 stepCount = 0;
        while ( _accumulatedSeconds >= tickInterval && stepCount < maxSteps )
        {
            _accumulatedSeconds -= tickInterval;
            ++stepCount;
        }
        // 상한을 넘겨 밀린 것만 버린다. 예전에는 상한에 딱 닿기만 해도 남은 몫을 버려 그만큼 재생이 느려졌다.
        if ( _accumulatedSeconds >= tickInterval )
            _accumulatedSeconds = 0.0;
        return stepCount;
    }

    float2 NetSimWorldLayout::computeViewSize( float2 available, int32 worldCount, float32 itemSpacingX, float32 lineHeight, float32 minHeight )
    {
        const int32 columnCount = MathUtil::max( 1, worldCount );
        return float2{ MathUtil::max( 1.0f, available._x / static_cast<float32>( columnCount ) - itemSpacingX ), MathUtil::max( minHeight, available._y - lineHeight * 2.0f ) };
    }
} // namespace sw::editor
