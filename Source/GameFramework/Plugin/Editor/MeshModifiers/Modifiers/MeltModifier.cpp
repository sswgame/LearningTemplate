/**
 * @file MeltModifier.cpp
 * @brief 주축 아래로 녹아내리는 모디파이어입니다. 높이를 상자 바닥 쪽으로 누르고, 바닥에 가까울수록 단면을 퍼뜨립니다.
 * @details h' = 바닥 + (h - 바닥) · (1 - 양), 단면 배율 = 1 + 퍼짐 · 양 · (1 - t). 양이 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 녹아내리기입니다. */
    class MeltModifier final : public MeshModifier
    {
    public:
        MeltModifier()
            : MeshModifier()
            , _amountParam{
                  addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( "Melt", "How far the mesh has melted (0 = solid, 1 = flat)" ) )
        },
            _spreadParam{ addParam( MeshModifierParamDef{ "Spread", MeshModifierParamKind::Float, 0.0f, 10.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Melt", "How far melted vertices spread outward" ) ) }
        {
        }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 amount = getFloat( _amountParam );
            if ( amount < MathUtil::kEpsilon )
                return point._position;
            const uint32  axis        = context._axisIndex;
            const float32 floorHeight = MeshModifierUtil::getComponent( context._bounds._min, axis );
            const float32 height      = MeshModifierUtil::getComponent( point._position, axis );
            const float32 fraction    = MathUtil::saturate( MeshModifierUtil::getAxisFraction( point._position, context ) );
            const float32 spread      = 1.0f + getFloat( _spreadParam ) * amount * ( 1.0f - fraction );
            float3        result      = point._position * spread;
            MeshModifierUtil::setComponent( result, axis, floorHeight + ( height - floorHeight ) * ( 1.0f - amount ) );
            return result;
        }

    private:
        uint32 _amountParam;
        uint32 _spreadParam;
    };

    SW_MESH_MODIFIER( MeltModifier, "melt", "Melt", "Deform" );
} // namespace sw::editor
