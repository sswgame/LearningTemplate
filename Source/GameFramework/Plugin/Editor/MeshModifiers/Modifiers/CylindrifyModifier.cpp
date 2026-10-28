/**
 * @file CylindrifyModifier.cpp
 * @brief 정점을 주축 중심의 원통면(반경 고정) 쪽으로 비율만큼 끌어당기는 모디파이어입니다. 주축 좌표는 그대로입니다.
 * @details 비율 0 이면 항등, 1 이면 모든 정점이 원통면 위에 놓인다. 축 위의 정점은 방향이 없어 그대로 둔다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 원통면으로 끌어당기기입니다. */
    class CylindrifyModifier final : public MeshModifier
    {
    public:
        CylindrifyModifier()
            : MeshModifier()
            , _percentParam{
                  addParam( MeshModifierParamDef{ "Percent", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( nullptr, "Blend toward the cylinder surface (0 = no change, 1 = fully on the cylinder)" ) )
        },
            _radiusParam{ addParam( MeshModifierParamDef{ "Radius", MeshModifierParamKind::Float, 0.0f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( nullptr, "Radius of the target cylinder", "m" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Pulls vertices toward a cylinder around the main axis."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 percent = getFloat( _percentParam );
            if ( percent < MathUtil::kEpsilon )
                return point._position;
            const uint32 axis   = context._axisIndex;
            float3       radial = point._position;
            MeshModifierUtil::setComponent( radial, axis, 0.0f );
            const float32 distance = radial.getLength();
            if ( distance < MathUtil::kEpsilon )
                return point._position;
            const float32 target = distance + ( getFloat( _radiusParam ) - distance ) * percent;
            float3        result = radial * ( target / distance );
            MeshModifierUtil::setComponent( result, axis, MeshModifierUtil::getComponent( point._position, axis ) );
            return result;
        }

    private:
        uint32 _percentParam;
        uint32 _radiusParam;
    };

    SW_MESH_MODIFIER( CylindrifyModifier, "cylindrify", "Cylindrify", "Deform" );
} // namespace sw::editor
