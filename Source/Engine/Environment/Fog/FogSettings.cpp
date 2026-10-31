#include "pch.h"

#include "Engine/Environment/Fog/FogSettings.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct FogSettingsInternal
        {
            /// 지수 인자의 상한입니다. 기준 높이보다 아주 아래의 점이 exp 를 넘치게 하지 않는다(fog.hlsl 의 kMaxExponent 와 같은 값).
            static constexpr float32 kMaxExponent = 60.0f;
            /// 지수 인자가 이 크기보다 작으면 (1 - exp(-x)) / x 를 테일러 두 항으로 쓴다(fog.hlsl 의 kSmallExponent 와 같은 값).
            static constexpr float32 kSmallExponent = 1.0e-3f;
        };
    } // namespace

    float32 FogSettings::computeOpticalDepth( const float3& eye, const float3& point ) const
    {
        using Internal           = FogSettingsInternal;
        const float3  delta      = point - eye;
        const float32 length     = MathUtil::sqrt( delta.dot( delta ) );
        const float32 clearedLen = MathUtil::max( length - _startDistance, 0.0f );
        if ( clearedLen <= 0.0f || length <= 0.0f )
            return 0.0f;
        const float32 directionY = delta._y / length;
        // 안개가 시작되는 점의 높이에서 밀도를 잡고, 거기서 끝점까지 지수 밀도를 적분한다.
        const float32 startHeight = eye._y + directionY * _startDistance;
        const float32 baseExp     = MathUtil::min( -_heightFalloff * ( startHeight - _baseHeight ), Internal::kMaxExponent );
        const float32 baseDensity = _density * MathUtil::exp( baseExp );
        const float32 exponent    = _heightFalloff * directionY * clearedLen;
        // 지수는 -kMaxExponent 아래로 자른다 — 아주 가파르게 내려가는 시선의 exp 가 넘치지 않는다(fog.hlsl · fog_common.hlsli 와 같다).
        const bool    bSmall  = MathUtil::abs( exponent ) < Internal::kSmallExponent;
        const float32 rate    = _heightFalloff * directionY;
        const float32 falloff = bSmall ? clearedLen * ( 1.0f - 0.5f * exponent ) : ( 1.0f - MathUtil::exp( -MathUtil::max( exponent, -Internal::kMaxExponent ) ) ) / rate;
        return MathUtil::max( baseDensity * falloff, 0.0f );
    }

    float32 FogSettings::computeOpacity( const float3& eye, const float3& point ) const
    {
        return MathUtil::min( _maxOpacity, 1.0f - MathUtil::exp( -computeOpticalDepth( eye, point ) ) );
    }

    float32 FogSettings::computeInscatterWeight( const float3& viewDirection, const float3& toSunDirection ) const
    {
        return MathUtil::pow( MathUtil::saturate( viewDirection.dot( toSunDirection ) ), _inscatterExponent );
    }
} // namespace sw
