#include "pch.h"

#include "Engine/Graphics/LOD/LODView.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    float32 LODViewUtil::computeScreenSize( const LODView& view, const float3& center, float32 radius )
    {
        // 절두체 밖이면 0 — 판정 식은 GPU 컬링과 같다(Frustum 의 여섯 평면).
        if ( view._frustum.overlapsSphere( center, radius ) == false )
            return 0.0f;
        float32 arrDepthRow[4];
        float32 scaleY{ 0.0f };
        computeProjectionTerms( view._viewProj, arrDepthRow, scaleY );
        const float32 clipW      = center._x * arrDepthRow[0] + center._y * arrDepthRow[1] + center._z * arrDepthRow[2] + arrDepthRow[3];
        const float3  toEye      = center - view._position;
        const bool    bEyeInside = toEye.getLengthSquared() <= radius * radius;
        if ( bEyeInside || clipW <= MathUtil::kEpsilon )
            return 1.0f;
        return radius * scaleY / clipW;
    }

    void LODViewUtil::computeProjectionTerms( const float4x4& viewProj, float32 ( &outArrDepthRow )[4], float32& outScaleY )
    {
        outArrDepthRow[0] = viewProj._14;
        outArrDepthRow[1] = viewProj._24;
        outArrDepthRow[2] = viewProj._34;
        outArrDepthRow[3] = viewProj._44;
        outScaleY         = MathUtil::sqrt( viewProj._12 * viewProj._12 + viewProj._22 * viewProj._22 + viewProj._32 * viewProj._32 );
    }
} // namespace sw
