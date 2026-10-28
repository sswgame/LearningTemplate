/**
 * @file SpherifyModifier.cpp
 * @brief 정점을 상자 중심의 구면 쪽으로 비율만큼 끌어당기는 모디파이어입니다. 구 반경은 상자의 가장 긴 변의 절반입니다.
 * @details 비율 0 이면 항등이다. 상자가 무효이거나 반경이 0 이면 항등이다. 중심과 겹친 정점은 그대로 둔다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 구면화입니다. */
    class SpherifyModifier final : public MeshModifier
    {
    public:
        SpherifyModifier()
            : MeshModifier()
            , _percentParam{
                  addParam( MeshModifierParamDef{ "Percent", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( nullptr, "Blend toward the sphere surface (0 = no change, 1 = fully spherical)" ) )
        }
        {
        }

        const utf8* getDescription() const override { return "Pulls vertices toward a sphere sized from the mesh bounds."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 percent = getFloat( _percentParam );
            if ( percent < MathUtil::kEpsilon || context._bounds.isValid() == false )
                return point._position;
            const float3  size   = context._bounds.getSize();
            const float32 radius = 0.5f * MathUtil::max( size._x, MathUtil::max( size._y, size._z ) );
            if ( radius < MathUtil::kEpsilon )
                return point._position;
            const float3  center   = context._bounds.getCenter();
            const float3  offset   = point._position - center;
            const float32 distance = offset.getLength();
            if ( distance < MathUtil::kEpsilon )
                return point._position;
            const float32 target = distance + ( radius - distance ) * percent;
            return center + offset * ( target / distance );
        }

    private:
        uint32 _percentParam;
    };

    SW_MESH_MODIFIER( SpherifyModifier, "spherify", "Spherify", "Deform" );
} // namespace sw::editor
