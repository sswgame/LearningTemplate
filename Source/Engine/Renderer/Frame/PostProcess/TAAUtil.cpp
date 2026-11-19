/**
 * @file TAAUtil.cpp
 * @brief TAA 수식의 순수 함수입니다. 셰이더(taa.hlsl)와 같은 식이다.
 */
#include "pch.h"

#include "Engine/Renderer/Frame/PostProcess/TAAUtil.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct TAAUtilInternal
        {
            /// Catmull-Rom 은 장력 0.5 의 카디널 스플라인이다 — 네 탭 가중의 계수가 모두 이 값에서 나온다.
            static constexpr float32 kTension = 0.5f;
            /// 안쪽 탭(텍셀 0)의 f² · f³ 계수(-2.5 · 1.5)와 바깥쪽 안 탭(텍셀 +1)의 f² · f³ 계수(2 · -1.5)입니다.
            static constexpr float32 kInnerSquare = kTension - 3.0f;
            static constexpr float32 kInnerCube   = 2.0f - kTension;
            static constexpr float32 kOuterSquare = 3.0f - 2.0f * kTension;
            static constexpr float32 kOuterCube   = kTension - 2.0f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    float4 TAAUtil::computeCatmullRomWeights( float32 fraction )
    {
        using Internal  = TAAUtilInternal;
        const float32 f = fraction;
        const float32 t = Internal::kTension;
        return float4{ f * ( -t + f * ( 2.0f * t - t * f ) ), 1.0f + f * f * ( Internal::kInnerSquare + Internal::kInnerCube * f ),
                       f * ( t + f * ( Internal::kOuterSquare + Internal::kOuterCube * f ) ), f * f * ( -t + t * f ) };
    }

    float32 TAAUtil::computeHistoryWeight( float32 currentLuminance, float32 historyLuminance, float32 velocityPixels, float32 minWeight, float32 maxWeight, float32 velocityRejectPixels )
    {
        const float32 lowWeight  = MathUtil::min( minWeight, maxWeight );
        const float32 difference = MathUtil::abs( currentLuminance - historyLuminance );
        const float32 trust      = 1.0f - MathUtil::saturate( difference / MathUtil::max( MathUtil::max( currentLuminance, historyLuminance ), kTrustLuminanceFloor ) );
        const float32 motion     = MathUtil::saturate( velocityPixels / MathUtil::max( velocityRejectPixels, MathUtil::kEpsilon ) );
        return MathUtil::lerp( lowWeight, maxWeight, trust * trust * ( 1.0f - motion ) );
    }
} // namespace sw
