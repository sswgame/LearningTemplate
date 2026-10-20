#include "pch.h"

#include "Engine/Environment/Sky/SkyGradient.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    float3 SkyGradient::evaluateRadiance( const float3& direction ) const
    {
        // sky.hlsl 의 evaluateSky 와 같은 식이다. 길이가 0 인 방향은 y 가 0(지평선)이다.
        const float32 length        = MathUtil::sqrt( direction.dot( direction ) );
        const float32 y             = length > 0.0f ? MathUtil::clamp( direction._y / length, -1.0f, 1.0f ) : 0.0f;
        const float32 horizonWeight = MathUtil::pow( 1.0f - MathUtil::abs( y ), _horizonExponent );
        const float3& poleColor     = y >= 0.0f ? _zenithColor : _groundColor;
        const float3  gradientColor = float3::lerp( poleColor, _horizonColor, horizonWeight );
        return gradientColor * _intensity;
    }
} // namespace sw
