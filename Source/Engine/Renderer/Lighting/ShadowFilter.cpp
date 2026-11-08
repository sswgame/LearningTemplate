#include "pch.h"

#include "Engine/Renderer/Lighting/ShadowFilter.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/UserSettings/UserSettingsVariables.h"

namespace sw
{
    ShadowFilterMode ShadowFilter::getActiveMode()
    {
        const int32 value = static_cast<int32>( gv_shadowFilter );
        if ( value < 0 || value >= static_cast<int32>( ShadowFilterMode::Count ) )
        {
            SW_LOG_ERROR_ONCE_ID( "ShadowFilter.InvalidMode", "gv_shadowFilter=%# is not a shadow filter (0 PCF 3x3, 1 PCSS) - drawing PCF 3x3", value );
            return ShadowFilterMode::Pcf3x3;
        }
        return static_cast<ShadowFilterMode>( value );
    }

    float32 ShadowFilter::computeDirectionalPenumbraScale( float32 depthRange, float32 sourceAngle, float32 texelWorldSize )
    {
        if ( texelWorldSize <= MathUtil::kEpsilon || sourceAngle <= 0.0f )
            return 0.0f;
        return depthRange * 2.0f * MathUtil::tan( sourceAngle * 0.5f ) / texelWorldSize;
    }

    float32 ShadowFilter::computeDirectionalKernelRadius( float32 receiverDepth, float32 blockerDepth, float32 penumbraScale, float32 cascadeScale )
    {
        const float32 penumbraWidth = MathUtil::max( receiverDepth - blockerDepth, 0.0f ) * penumbraScale * cascadeScale;
        return MathUtil::clamp( penumbraWidth * 0.5f, kMinKernelRadiusTexels, kMaxKernelRadiusTexels );
    }

    float32 ShadowFilter::computeLocalKernelRadius( float32 receiverDistance, float32 blockerDistance, float32 sourceRadius, float32 texelAngle )
    {
        const float32 penumbraHalfWidth = sourceRadius * MathUtil::max( receiverDistance - blockerDistance, 0.0f ) / MathUtil::max( blockerDistance, MathUtil::kEpsilon );
        const float32 receiverTexel     = MathUtil::max( texelAngle * receiverDistance, MathUtil::kEpsilon );
        return MathUtil::clamp( penumbraHalfWidth / receiverTexel, kMinKernelRadiusTexels, kMaxKernelRadiusTexels );
    }

    float32 ShadowFilter::linearizePerspectiveDepth( float32 ndcDepth, float32 nearPlane, float32 farPlane )
    {
        // D3D 원근: ndc = f / (f − n) · (1 − n / z) — 거꾸로 풀면 z = n f / (f − ndc (f − n)).
        const float32 denominator = MathUtil::max( farPlane - ndcDepth * ( farPlane - nearPlane ), MathUtil::kEpsilon );
        return nearPlane * farPlane / denominator;
    }

    float4 ShadowFilter::makeShaderParams( ShadowFilterMode mode, float32 directionalPenumbraScale )
    {
        return float4{ static_cast<float32>( mode ), directionalPenumbraScale, kMaxKernelRadiusTexels, kMinKernelRadiusTexels };
    }
} // namespace sw
