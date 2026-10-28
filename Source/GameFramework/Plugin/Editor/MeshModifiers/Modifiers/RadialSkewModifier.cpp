/**
 * @file RadialSkewModifier.cpp
 * @brief 주축에서 바깥쪽으로 높이에 비례해 기울이는 모디파이어입니다. 축까지 거리 += tan(각) · (높이 - 상자 아래)입니다.
 * @details 상자 아래 면은 그대로이고 위로 갈수록 원뿔처럼 벌어진다(음수 각은 오므린다). 거리는 0 아래로 내려가지 않는다. 각 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 방사형 기울임입니다. */
    class RadialSkewModifier final : public MeshModifier
    {
    public:
        RadialSkewModifier()
            : MeshModifier()
            , _angleParam{
                  addParam( MeshModifierParamDef{ "Angle", MeshModifierParamKind::Float, -85.0f, 85.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( nullptr, "Outward lean of the sides (0 = no change, negative narrows toward the top)", "deg" ) )
        }
        {
        }

        const utf8* getDescription() const override { return "Flares the mesh outward from the main axis in proportion to height, like a cone."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 angle = MathUtil::toRadian( getFloat( _angleParam ) );
            if ( MathUtil::abs( angle ) < MathUtil::kEpsilon )
                return point._position;
            const uint32 axis   = context._axisIndex;
            float3       radial = point._position;
            MeshModifierUtil::setComponent( radial, axis, 0.0f );
            const float32 distance = radial.getLength();
            if ( distance < MathUtil::kEpsilon )
                return point._position;
            const float32 height = MeshModifierUtil::getComponent( point._position, axis ) - MeshModifierUtil::getComponent( context._bounds._min, axis );
            const float32 target = MathUtil::max( distance + MathUtil::tan( angle ) * height, 0.0f );
            float3        result = radial * ( target / distance );
            MeshModifierUtil::setComponent( result, axis, MeshModifierUtil::getComponent( point._position, axis ) );
            return result;
        }

    private:
        uint32 _angleParam;
    };

    SW_MESH_MODIFIER( RadialSkewModifier, "radialskew", "Radial Skew", "Deform" );
} // namespace sw::editor
