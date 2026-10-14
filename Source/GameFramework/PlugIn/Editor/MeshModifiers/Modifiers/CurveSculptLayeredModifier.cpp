/**
 * @file CurveSculptLayeredModifier.cpp
 * @brief 곡선 조각 층 여러 개(목록)를 겹쳐 법선 방향으로 미는 모디파이어입니다.
 * @details 층 하나 = (u, v, 반경, 높이). u, v, 반경은 상자 수직 평면 비율 [0,1] 좌표이고, 층 중심에서의 거리 d / 반경 이 1 보다 작으면
 *          높이 × 모양 곡선(d / 반경) 을 더한다. 층마다 독립 곡선을 둘 매개변수 종류가 없어 모양 곡선은 모든 층이 함께 쓴다.
 *          목록이 비면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 겹친 곡선 조각입니다. */
    class CurveSculptLayeredModifier final : public MeshModifier
    {
    public:
        /** @brief Layers 표의 열 — u · v · 반경 · 높이(stride 4). */
        static constexpr MeshModifierListColumn kArrColumn[] = {
            { "U", MeshModifierListColumnKind::Float },
            { "V", MeshModifierListColumnKind::Float },
            { "Radius", MeshModifierListColumnKind::Float },
            { "Height", MeshModifierListColumnKind::Float },
        };

        CurveSculptLayeredModifier()
            : MeshModifier()
            , _shapeParam{ addParam( MeshModifierParamDef{ "Shape", MeshModifierParamKind::Curve, -10.0f, 10.0f, float3{ 1.0f, 0.0f, 0.0f } } ) }
            , _layersParam{ addParam(
                  MeshModifierParamDef{ "Layers", MeshModifierParamKind::NumberList, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f }, 4, "u v radius height" }.withColumns( kArrColumn ) ) }
        {
        }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const vector<float32>& listLayer = getNumberList( _layersParam );
            if ( listLayer.size() < 4 )
                return point._position;
            const uint32  axis  = context._axisIndex;
            const uint32  axisU = ( axis + 1 ) % 3;
            const uint32  axisV = ( axis + 2 ) % 3;
            const float3  size  = context._bounds.getSize();
            const float32 sizeU = MathUtil::max( MeshModifierUtil::getComponent( size, axisU ), MathUtil::kEpsilon );
            const float32 sizeV = MathUtil::max( MeshModifierUtil::getComponent( size, axisV ), MathUtil::kEpsilon );
            const float32 ratioU = ( MeshModifierUtil::getComponent( point._position, axisU ) - MeshModifierUtil::getComponent( context._bounds._min, axisU ) ) / sizeU;
            const float32 ratioV = ( MeshModifierUtil::getComponent( point._position, axisV ) - MeshModifierUtil::getComponent( context._bounds._min, axisV ) ) / sizeV;

            float32 height = 0.0f;
            for ( size_t index = 0; index + 3 < listLayer.size(); index += 4 )
            {
                const float32 radius = listLayer[index + 2];
                if ( radius <= MathUtil::kEpsilon )
                    continue;
                const float32 deltaU   = ratioU - listLayer[index];
                const float32 deltaV   = ratioV - listLayer[index + 1];
                const float32 distance = MathUtil::sqrt( deltaU * deltaU + deltaV * deltaV ) / radius;
                if ( distance >= 1.0f )
                    continue;
                height += listLayer[index + 3] * evaluateCurve( _shapeParam, distance );
            }
            return point._position + point._normal * height;
        }

    private:
        uint32 _shapeParam;
        uint32 _layersParam;
    };

    SW_MESH_MODIFIER( CurveSculptLayeredModifier, "curvesculptlayered", "Curve Sculpt Layered", "Curve" );
} // namespace sw::editor
