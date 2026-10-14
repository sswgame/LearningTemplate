/**
 * @file CurveDeformModifier.cpp
 * @brief 주축 위 위치를 곡선 두 개(변위 · 단면 배율)로 읽어 정점을 옮기는 모디파이어입니다.
 * @details t = 주축 비율(상자 아래 0 · 위 1, [0,1] 로 자름). 주축에 수직인 두 성분 (u, v) 를 배율 곡선(t) 로 키우고,
 *          수직 평면 안의 방향(방향 각)으로 양 × 변위 곡선(t) 만큼 민다. 기본 변위 곡선 0 · 배율 곡선 1 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 곡선 변형입니다. */
    class CurveDeformModifier final : public MeshModifier
    {
    public:
        CurveDeformModifier()
            : MeshModifier()
            , _amountParam{
                  addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } }
                                .withLayout( "Deform", "Overall strength of the curve displacement" ) )
        },
            _directionParam{ addParam( MeshModifierParamDef{ "Direction", MeshModifierParamKind::Float, -180.0f, 180.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Deform", "Direction of the displacement, around the main axis", "deg" ) ) },
            _displaceCurveParam{ addParam( MeshModifierParamDef{ "Displace Curve", MeshModifierParamKind::Curve, -10.0f, 10.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Curves", "Sideways displacement along the main axis (0 = bottom, 1 = top)" ) ) },
            _scaleCurveParam{ addParam( MeshModifierParamDef{ "Scale Curve", MeshModifierParamKind::Curve, 0.0f, 10.0f, float3{ 1.0f, 1.0f, 0.0f } }.withLayout( "Curves", "Cross-section scale along the main axis (0 = bottom, 1 = top)" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Displaces and scales the mesh cross-section along its main axis using curves."; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const uint32  axis      = context._axisIndex;
            const uint32  axisU     = ( axis + 1 ) % 3;
            const uint32  axisV     = ( axis + 2 ) % 3;
            const float32 fraction  = MathUtil::saturate( MeshModifierUtil::getAxisFraction( point._position, context ) );
            const float32 scale     = evaluateCurve( _scaleCurveParam, fraction );
            const float32 offset    = getFloat( _amountParam ) * evaluateCurve( _displaceCurveParam, fraction );
            const float32 direction = MathUtil::toRadian( getFloat( _directionParam ) );

            float3 result = point._position;
            MeshModifierUtil::setComponent( result, axisU, MeshModifierUtil::getComponent( point._position, axisU ) * scale + offset * MathUtil::cos( direction ) );
            MeshModifierUtil::setComponent( result, axisV, MeshModifierUtil::getComponent( point._position, axisV ) * scale + offset * MathUtil::sin( direction ) );
            return result;
        }

    private:
        uint32 _amountParam;
        uint32 _directionParam;
        uint32 _displaceCurveParam;
        uint32 _scaleCurveParam;
    };

    SW_MESH_MODIFIER( CurveDeformModifier, "curvedeform", "Curve Deform", "Curve" );
} // namespace sw::editor
