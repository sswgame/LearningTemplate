/**
 * @file PivotAdjustModifier.cpp
 * @brief 피벗을 옮기는 모디파이어입니다 — 모든 정점을 오프셋의 반대로 옮겨 원점(피벗)이 오프셋 자리에 온 것처럼 만듭니다.
 * @details 기즈모 중심 이동(모디파이어 공간의 기준점)과 다르다: 기즈모는 다른 모디파이어의 중심을 정하고, 이 오프셋은 메시 자체를 민다.
 *          오프셋은 모디파이어 공간(기즈모 회전 반영) 벡터다. 오프셋이 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 피벗 오프셋입니다. */
    class PivotAdjustModifier final : public MeshModifier
    {
    public:
        PivotAdjustModifier()
            : MeshModifier()
            , _offsetParam{
                  addParam( MeshModifierParamDef{ "Pivot Offset", MeshModifierParamKind::Vector3, -10000.0f, 10000.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( nullptr, "Offset applied to every vertex; 0 leaves the mesh unchanged", "m" ) )
        }
        {
        }

        const utf8* getDescription() const override { return "Moves the mesh relative to its pivot by an offset."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            (void)context;
            return point._position - getVector3( _offsetParam );
        }

    private:
        uint32 _offsetParam;
    };

    SW_MESH_MODIFIER( PivotAdjustModifier, "pivotadjust", "Pivot Adjust", "Transform" );
} // namespace sw::editor
