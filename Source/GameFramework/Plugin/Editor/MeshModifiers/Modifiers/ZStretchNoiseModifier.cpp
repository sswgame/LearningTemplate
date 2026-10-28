/**
 * @file ZStretchNoiseModifier.cpp
 * @brief 한 축(기본 Z) 방향으로 늘이고 그 축 방향 노이즈를 더하는 모디파이어입니다.
 * @details 축 좌표' = 축 좌표 · (1 + 양) + 노이즈 · noise(위치 · 주파수, 시드). 기즈모 중심(원점)이 늘이기의 고정점이다. 양 · 노이즈가 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierNoise.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 축 늘이기 + 노이즈입니다. */
    class ZStretchNoiseModifier final : public MeshModifier
    {
    public:
        ZStretchNoiseModifier()
            : MeshModifier()
            , _amountParam{
                  addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, -0.99f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( "Shape", "Stretch along the axis; 0 keeps the original length" ) )
        },
            _noiseParam{ addParam( MeshModifierParamDef{ "Noise", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Noise", "Noise strength added to the stretch; 0 disables noise" ) ) },
            _frequencyParam{ addParam( MeshModifierParamDef{ "Frequency", MeshModifierParamKind::Float, 0.001f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Noise", "Noise features per meter" ) ) },
            _seedParam{ addParam( MeshModifierParamDef{ "Seed", MeshModifierParamKind::Int, 0.0f, 65535.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Noise", "Random seed for the noise pattern" ) ) },
            _stretchAxisParam{ addParam( MeshModifierParamDef{ "Stretch Axis", MeshModifierParamKind::Axis, 0.0f, 2.0f, float3{ 2.0f, 0.0f, 0.0f } }.withLayout( "Shape", "Axis the mesh is stretched along" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Stretches the mesh along an axis with a noisy variation."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            (void)context;
            const float32 amount = getFloat( _amountParam );
            const float32 noise  = getFloat( _noiseParam );
            if ( MathUtil::abs( amount ) < MathUtil::kEpsilon && MathUtil::abs( noise ) < MathUtil::kEpsilon )
                return point._position;
            const uint32  axis       = getAxisIndex( _stretchAxisParam );
            const uint32  seed       = static_cast<uint32>( getInt( _seedParam ) );
            const float32 coordinate = MeshModifierUtil::getComponent( point._position, axis );
            const float32 wobble     = MeshModifierNoise::valueNoise3D( point._position * getFloat( _frequencyParam ), seed );
            float3        result     = point._position;
            MeshModifierUtil::setComponent( result, axis, coordinate * ( 1.0f + amount ) + noise * wobble );
            return result;
        }

    private:
        uint32 _amountParam;
        uint32 _noiseParam;
        uint32 _frequencyParam;
        uint32 _seedParam;
        uint32 _stretchAxisParam;
    };

    SW_MESH_MODIFIER( ZStretchNoiseModifier, "zstretchnoise", "ZStretch Noise", "Noise" );
} // namespace sw::editor
