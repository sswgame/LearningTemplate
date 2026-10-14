/**
 * @file ScaleModifier.cpp
 * @brief 기즈모 중심에서 크기를 바꾸는 모디파이어입니다. 결과 = 위치 · 균일 배율 · 축별 배율(성분마다)입니다.
 * @details 모디파이어 공간 축(기즈모 회전을 뺀 축)마다 배율이 걸린다. 기본값(1, 1 1 1)은 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 균일 · 비균일 스케일입니다. */
    class ScaleModifier final : public MeshModifier
    {
    public:
        ScaleModifier()
            : MeshModifier()
            , _uniformParam{ addParam( MeshModifierParamDef{ "Uniform", MeshModifierParamKind::Float, -10.0f, 10.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( nullptr, "Scale applied to all axes (1 = no change)" ) ) }
            , _axisScaleParam{ addParam( MeshModifierParamDef{ "Axis Scale", MeshModifierParamKind::Vector3, -10.0f, 10.0f, float3{ 1.0f, 1.0f, 1.0f } }.withLayout( nullptr, "Extra scale per modifier-space axis (1 1 1 = no change)" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Scales the mesh around the gizmo center, uniformly and per axis."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& ) const override
        {
            const float32 uniform   = getFloat( _uniformParam );
            const float3& axisScale = getVector3( _axisScaleParam );
            return float3{ point._position._x * uniform * axisScale._x, point._position._y * uniform * axisScale._y, point._position._z * uniform * axisScale._z };
        }

    private:
        uint32 _uniformParam;
        uint32 _axisScaleParam;
    };

    SW_MESH_MODIFIER( ScaleModifier, "scale", "Scale", "Deform" );
} // namespace sw::editor
