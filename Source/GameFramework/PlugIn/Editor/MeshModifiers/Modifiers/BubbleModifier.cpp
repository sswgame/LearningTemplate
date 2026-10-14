/**
 * @file BubbleModifier.cpp
 * @brief 한 점 둘레를 구 모양으로 부풀리는 모디파이어입니다. 반경 안의 정점을 중심에서 멀어지는 쪽으로 세기 · (1 - (d / r)²)² 만큼 밉니다.
 * @details 세기가 0 이거나 반경이 0 이면 항등이다. 중심과 겹친 정점은 방향이 없어 그대로 둔다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 구형 부풀림입니다. */
    class BubbleModifier final : public MeshModifier
    {
    public:
        BubbleModifier()
            : MeshModifier()
            , _strengthParam{ addParam( MeshModifierParamDef{ "Strength", MeshModifierParamKind::Float, -10.0f, 10.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
            , _radiusParam{ addParam( MeshModifierParamDef{ "Radius", MeshModifierParamKind::Float, 0.0f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } } ) }
            , _centerParam{ addParam( MeshModifierParamDef{ "Center", MeshModifierParamKind::Vector3, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
        {
        }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& ) const override
        {
            const float32 strength = getFloat( _strengthParam );
            const float32 radius   = getFloat( _radiusParam );
            if ( MathUtil::abs( strength ) < MathUtil::kEpsilon || radius < MathUtil::kEpsilon )
                return point._position;
            const float3  offset   = point._position - getVector3( _centerParam );
            const float32 distance = offset.getLength();
            if ( distance >= radius || distance < MathUtil::kEpsilon )
                return point._position;
            const float32 ratio   = distance / radius;
            const float32 falloff = ( 1.0f - ratio * ratio ) * ( 1.0f - ratio * ratio );
            return point._position + offset * ( strength * falloff / distance );
        }

    private:
        uint32 _strengthParam;
        uint32 _radiusParam;
        uint32 _centerParam;
    };

    SW_MESH_MODIFIER( BubbleModifier, "bubble", "Bubble", "Deform" );
} // namespace sw::editor
