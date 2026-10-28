/**
 * @file NoiseModifier.cpp
 * @brief 결정적 3D 값 노이즈(정수 격자 해시 + 부드러운 삼선형 보간)로 세 축을 흔드는 모디파이어입니다.
 * @details 같은 시드 · 같은 위치면 어느 기계에서나 같은 값이다(해시는 32 비트 정수 연산만 쓴다). 진폭이 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierNoise.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct NoiseModifierInternal
        {
            /** @brief 축마다 다른 노이즈를 얻으려고 더하는 어긋남입니다. */
            static constexpr float32 kAxisOffsetY = 31.416f;
            static constexpr float32 kAxisOffsetZ = 67.123f;
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 값 노이즈 흔들기입니다. */
    class NoiseModifier final : public MeshModifier
    {
    public:
        NoiseModifier()
            : MeshModifier()
            , _amplitudeParam{
                  addParam( MeshModifierParamDef{ "Amplitude", MeshModifierParamKind::Float, 0.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( "Noise", "Largest displacement", "m" ) )
        },
            _frequencyParam{ addParam( MeshModifierParamDef{ "Frequency", MeshModifierParamKind::Float, 0.001f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Noise", "Noise cells per unit length; larger is finer" ) ) },
            _seedParam{ addParam( MeshModifierParamDef{ "Seed", MeshModifierParamKind::Int, 0.0f, 65535.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Noise", "Random seed; change it for a different pattern" ) ) },
            _strengthParam{ addParam( MeshModifierParamDef{ "Axis Strength", MeshModifierParamKind::Vector3, 0.0f, 10.0f, float3{ 1.0f, 1.0f, 1.0f } }.withLayout( "Noise", "Displacement scale per axis" ) ) },
            _speedParam{ addParam( MeshModifierParamDef{ "Speed", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Animation", "How fast the noise moves with preview time" ) ) }
        {
        }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 amplitude = getFloat( _amplitudeParam );
            if ( amplitude < MathUtil::kEpsilon )
                return point._position;
            const uint32  seed     = static_cast<uint32>( getInt( _seedParam ) );
            const float32 drift    = getFloat( _speedParam ) * context.getTime();
            const float3  sample   = point._position * getFloat( _frequencyParam ) + float3{ drift, drift, drift };
            const float3& strength = getVector3( _strengthParam );
            const float3  offset{ MeshModifierNoise::valueNoise3D( sample, seed ) * strength._x,
                                 MeshModifierNoise::valueNoise3D( sample + float3{ NoiseModifierInternal::kAxisOffsetY, 0.0f, 0.0f }, seed ) * strength._y,
                                 MeshModifierNoise::valueNoise3D( sample + float3{ 0.0f, 0.0f, NoiseModifierInternal::kAxisOffsetZ }, seed ) * strength._z };
            return point._position + offset * amplitude;
        }

    private:
        uint32 _amplitudeParam;
        uint32 _frequencyParam;
        uint32 _seedParam;
        uint32 _strengthParam;
        uint32 _speedParam;
    };

    SW_MESH_MODIFIER( NoiseModifier, "noise", "Noise", "Noise" );
} // namespace sw::editor
