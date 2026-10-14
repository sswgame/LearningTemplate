/**
 * @file VertNoiseModifier.cpp
 * @brief 정점을 법선 방향으로만 결정적 값 노이즈만큼 미는 모디파이어입니다.
 * @details 변위 = 법선 · 진폭 · noise(위치 · 주파수 + 속도 · 시간). 속도가 0 이면 시간과 무관하다. 진폭이 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierNoise.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 법선 방향 노이즈 변위입니다. */
    class VertNoiseModifier final : public MeshModifier
    {
    public:
        VertNoiseModifier()
            : MeshModifier()
            , _amplitudeParam{
                  addParam( MeshModifierParamDef{ "Amplitude", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( "Shape", "Noise offset strength; 0 leaves the mesh unchanged", "m" ) )
        },
            _frequencyParam{ addParam( MeshModifierParamDef{ "Frequency", MeshModifierParamKind::Float, 0.001f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Noise", "Noise features per meter" ) ) },
            _seedParam{ addParam( MeshModifierParamDef{ "Seed", MeshModifierParamKind::Int, 0.0f, 65535.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Noise", "Random seed for the noise pattern" ) ) },
            _speedParam{ addParam( MeshModifierParamDef{ "Speed", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Animation", "How fast the noise scrolls over time; 0 is static" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Offsets vertices with smooth noise, optionally animated."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 amplitude = getFloat( _amplitudeParam );
            if ( MathUtil::abs( amplitude ) < MathUtil::kEpsilon )
                return point._position;
            const uint32  seed   = static_cast<uint32>( getInt( _seedParam ) );
            const float32 drift  = getFloat( _speedParam ) * context.getTime();
            const float3  sample = point._position * getFloat( _frequencyParam ) + float3{ drift, drift * 0.5f, drift * 0.25f };
            return point._position + point._normal * ( MeshModifierNoise::valueNoise3D( sample, seed ) * amplitude );
        }

    private:
        uint32 _amplitudeParam;
        uint32 _frequencyParam;
        uint32 _seedParam;
        uint32 _speedParam;
    };

    SW_MESH_MODIFIER( VertNoiseModifier, "vertnoise", "Vert Noise", "Noise" );
} // namespace sw::editor
