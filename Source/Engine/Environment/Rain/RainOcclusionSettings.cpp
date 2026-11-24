#include "pch.h"

#include "Engine/Environment/Rain/RainOcclusionSettings.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    float3 RainOcclusionSettings::computeEye( const float3& viewPosition ) const
    {
        const float32 texel = _extent / static_cast<float32>( MathUtil::max( _resolution, 1u ) );
        return float3{ MathUtil::floor( viewPosition._x / texel ) * texel, MathUtil::floor( viewPosition._y ) + _heightAbove, MathUtil::floor( viewPosition._z / texel ) * texel };
    }

    float4x4 RainOcclusionSettings::computeViewProjection( const float3& eye ) const
    {
        return float4x4::makeLookAt( eye, eye - float3::Up, float3::Forward ) * float4x4::makeOrthographic( _extent, _extent, 0.0f, _heightAbove + _depthBelow );
    }

    float4 RainOcclusionSettings::makeShaderParams() const
    {
        return float4{ _depthBias / ( _heightAbove + _depthBelow ), _normalOffset, 1.0f / static_cast<float32>( MathUtil::max( _resolution, 1u ) ), 1.0f };
    }
} // namespace sw
