/**
 * @file GlobeModifier.cpp
 * @brief 정점을 기즈모 중심의 고정 반경 구면 쪽으로 비율만큼 끌어당기는 모디파이어입니다.
 * @details Spherify 는 반경을 메시 상자에서 정하지만 Globe 는 사용자가 준 반경을 쓴다. 비율 0 이면 항등이다. 중심의 정점은 그대로 둔다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 고정 반경 구면으로 말기입니다. */
    class GlobeModifier final : public MeshModifier
    {
    public:
        GlobeModifier()
            : MeshModifier()
            , _percentParam{
                  addParam( MeshModifierParamDef{ "Percent", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( nullptr, "Blend toward the sphere surface (0 = no change, 1 = fully on the sphere)" ) )
        },
            _radiusParam{ addParam( MeshModifierParamDef{ "Radius", MeshModifierParamKind::Float, 0.0f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( nullptr, "Radius of the target sphere", "m" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Pulls vertices toward a sphere of fixed radius around the gizmo center."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& ) const override
        {
            const float32 percent = getFloat( _percentParam );
            if ( percent < MathUtil::kEpsilon )
                return point._position;
            const float32 distance = point._position.getLength();
            if ( distance < MathUtil::kEpsilon )
                return point._position;
            const float32 target = distance + ( getFloat( _radiusParam ) - distance ) * percent;
            return point._position * ( target / distance );
        }

    private:
        uint32 _percentParam;
        uint32 _radiusParam;
    };

    SW_MESH_MODIFIER( GlobeModifier, "globe", "Globe", "Deform" );
} // namespace sw::editor
