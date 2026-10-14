/**
 * @file CurveSculptModifier.cpp
 * @brief 주축에서 떨어진 거리를 곡선 높이로 읽어 법선 방향으로 미는 조각 모디파이어입니다.
 * @details r = 수직 평면 거리 / 상자 수직 반폭(큰 쪽), [0,1] 로 자름. 변위 = 세기 × 곡선(r). 기본 곡선이 0 이라 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 곡선 조각입니다. */
    class CurveSculptModifier final : public MeshModifier
    {
    public:
        CurveSculptModifier()
            : MeshModifier()
            , _curveParam{ addParam( MeshModifierParamDef{ "Curve", MeshModifierParamKind::Curve, -10.0f, 10.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
            , _strengthParam{ addParam( MeshModifierParamDef{ "Strength", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } } ) }
        {
        }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const uint32  axis    = context._axisIndex;
            const uint32  axisU   = ( axis + 1 ) % 3;
            const uint32  axisV   = ( axis + 2 ) % 3;
            const float3  size    = context._bounds.getSize();
            const float3  center  = context._bounds.getCenter();
            float32       radius  = 0.5f * MathUtil::max( MeshModifierUtil::getComponent( size, axisU ), MeshModifierUtil::getComponent( size, axisV ) );
            radius                = radius > MathUtil::kEpsilon ? radius : 1.0f;
            const float32 coordU  = MeshModifierUtil::getComponent( point._position, axisU ) - MeshModifierUtil::getComponent( center, axisU );
            const float32 coordV  = MeshModifierUtil::getComponent( point._position, axisV ) - MeshModifierUtil::getComponent( center, axisV );
            const float32 ratio   = MathUtil::saturate( MathUtil::sqrt( coordU * coordU + coordV * coordV ) / radius );
            const float32 height  = getFloat( _strengthParam ) * evaluateCurve( _curveParam, ratio );
            return point._position + point._normal * height;
        }

    private:
        uint32 _curveParam;
        uint32 _strengthParam;
    };

    SW_MESH_MODIFIER( CurveSculptModifier, "curvesculpt", "Curve Sculpt", "Curve" );
} // namespace sw::editor
