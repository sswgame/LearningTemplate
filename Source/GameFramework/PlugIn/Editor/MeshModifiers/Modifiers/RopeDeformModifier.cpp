/**
 * @file RopeDeformModifier.cpp
 * @brief 메시를 경로 전체 길이에 맞춰 늘여 반경 있는 밧줄 · 튜브로 감는 모디파이어입니다.
 * @details 주축 비율 t(상자 아래 0 · 위 1)를 경로 비율로 보고, 단면은 반경 배율 × 반경 곡선(t) 로 키운다. 경로는 월드에서 읽는다.
 *          입력 창구가 위치 · 접선만 주므로 단면 프레임은 고정 기준 축으로 만든다(2D 경로 가정). 경로 참조가 비면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/PathModifierUtil.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 밧줄 감기입니다. */
    class RopeDeformModifier final : public MeshModifier
    {
    public:
        RopeDeformModifier()
            : MeshModifier()
            , _pathParam{ addParam( MeshModifierParamDef{ "Path", MeshModifierParamKind::Reference, 0.0f, 0.0f, float3{ 0.0f, 0.0f, 0.0f }, 1, "Spline object" }.withLayout( "Path", "Spline object the rope follows" ).withRequiredComponent( "SplineComponent" ) ) }
            , _radiusParam{ addParam( MeshModifierParamDef{ "Radius", MeshModifierParamKind::Float, 0.0f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Shape", "Cross-section scale of the rope" ) ) }
            , _radiusCurveParam{ addParam( MeshModifierParamDef{ "Radius Curve", MeshModifierParamKind::Curve, 0.0f, 10.0f, float3{ 1.0f, 1.0f, 0.0f } }.withLayout( "Shape", "Radius multiplier along the rope (0 = start, 1 = end)" ) ) }
            , _startParam{ addParam( MeshModifierParamDef{ "Start", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Range", "Start of the rope along the path (0 = path start)" ) ) }
            , _endParam{ addParam( MeshModifierParamDef{ "End", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Range", "End of the rope along the path (1 = path end)" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Stretches the mesh over the whole length of a spline as a rope or tube."; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            float32 length = 0.0f;
            if ( PathModifierUtil::tryGetPathLength( context, getText( _pathParam ), length ) == false )
                return point._position;
            const uint32  axis     = context._axisIndex;
            const float32 fraction = MeshModifierUtil::getAxisFraction( point._position, context );
            const float32 start    = getFloat( _startParam );
            const float32 end      = getFloat( _endParam );
            const float32 along    = ( start + ( end - start ) * fraction ) * length;
            const float32 scale    = getFloat( _radiusParam ) * evaluateCurve( _radiusCurveParam, MathUtil::saturate( fraction ) );
            const float32 coordU   = MeshModifierUtil::getComponent( point._position, ( axis + 1 ) % 3 ) * scale;
            const float32 coordV   = MeshModifierUtil::getComponent( point._position, ( axis + 2 ) % 3 ) * scale;
            float3        result{ 0.0f, 0.0f, 0.0f };
            if ( PathModifierUtil::tryPlaceOnPath( context, getText( _pathParam ), PathModifierUtil::PathSpace::World, length, along, false, coordU, coordV, result )
                 == false )
                result = point._position; // 경로를 못 얻으면 변형 없이 통과한다
            return result;
        }

    private:
        uint32 _pathParam;
        uint32 _radiusParam;
        uint32 _radiusCurveParam;
        uint32 _startParam;
        uint32 _endParam;
    };

    SW_MESH_MODIFIER( RopeDeformModifier, "ropedeform", "Rope Deform", "Path" );
} // namespace sw::editor
