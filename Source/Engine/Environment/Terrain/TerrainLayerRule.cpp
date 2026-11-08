#include "pch.h"

#include "Engine/Environment/Terrain/TerrainLayerRule.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    float32 TerrainLayerRule::computeCoverage( float32 slopeDegrees, float32 altitude ) const
    {
        const float32 slope  = computeBand( slopeDegrees, _slopeRange._x, _slopeRange._y, _slopeFade );
        const float32 height = computeBand( altitude, _altitudeRange._x, _altitudeRange._y, _altitudeFade );
        return MathUtil::saturate( _strength ) * slope * height;
    }

    float32 TerrainLayerRule::computeBand( float32 value, float32 low, float32 high, float32 fade )
    {
        const float32 width = MathUtil::max( fade, kMinFade );
        const float32 rise  = MathUtil::saturate( ( value - low ) / width + 0.5f );
        const float32 fall  = MathUtil::saturate( ( high - value ) / width + 0.5f );
        return rise * fall;
    }

    float32 TerrainLayerRule::computeSlopeDegrees( float32 normalY )
    {
        return MathUtil::acos( MathUtil::clamp( normalY, -1.0f, 1.0f ) ) * MathUtil::kRadianToDegree;
    }
} // namespace sw
