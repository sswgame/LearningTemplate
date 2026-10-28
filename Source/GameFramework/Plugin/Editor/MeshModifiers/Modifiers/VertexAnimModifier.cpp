/**
 * @file VertexAnimModifier.cpp
 * @brief 정점마다 다른 위상의 사인(또는 값 노이즈) 오프셋을 시간에 따라 더하는 단순 정점 애니메이션 모디파이어입니다.
 * @details 오프셋 = `Amplitude`(축마다) × 신호(시간 × `Speed` + 정점 위상). 정점 위상은 정점 번호의 정수 해시로 정해 결정적이고,
 *          `Phase Variation` 이 0 이면 모든 정점이 같이 움직인다. `Noise` 는 사인과 값 노이즈(정점마다 다른 씨앗, 정수 시간 칸 사이 부드러운 보간)를 섞는 비율이다.
 *          축마다 위상을 조금 어긋나게 해 한 직선으로만 흔들리지 않게 한다. 진폭이 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierNoise.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 정점별 사인 · 노이즈 오프셋 애니메이션입니다. */
    class VertexAnimModifier final : public MeshModifier
    {
    public:
        VertexAnimModifier()
            : MeshModifier()
            , _amplitudeParam{
                  addParam( MeshModifierParamDef{ "Amplitude", MeshModifierParamKind::Vector3, -10.0f, 10.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( "Motion", "Per-axis offset amplitude", "m" ) )
        },
            _speedParam{ addParam( MeshModifierParamDef{ "Speed", MeshModifierParamKind::Float, 0.0f, 50.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Motion", "Oscillation speed over time" ) ) },
            _phaseVariationParam{ addParam( MeshModifierParamDef{ "Phase Variation", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Variation", "How much each vertex phase differs (0 = all move together)" ) ) },
            _noiseParam{ addParam( MeshModifierParamDef{ "Noise", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Variation", "Blend from sine (0) to smooth value noise (1)" ) ) },
            _seedParam{ addParam( MeshModifierParamDef{ "Seed", MeshModifierParamKind::Int, 0.0f, 10000.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Variation", "Random seed for per-vertex phase and noise" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Adds a per-vertex sine or noise offset over time with deterministic phase per vertex."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float3& amplitude = getVector3( _amplitudeParam );
            if ( amplitude.getLengthSquared() < MathUtil::kEpsilon * MathUtil::kEpsilon )
                return point._position;
            const uint32  seed     = static_cast<uint32>( getInt( _seedParam ) ) * 7919u + point._vertexIndex;
            const float32 phase    = MeshModifierNoise::hashToUnit( seed ) * getFloat( _phaseVariationParam );
            const float32 cycle    = context.getTime() * getFloat( _speedParam ) + phase;
            const float32 noiseMix = getFloat( _noiseParam );
            float3        offset{ 0.0f, 0.0f, 0.0f };
            for ( uint32 axis = 0; axis < 3; ++axis )
            {
                const float32 axisCycle = cycle + static_cast<float32>( axis ) * 0.25f;
                const float32 sine      = MathUtil::sin( MathUtil::kTwoPi * axisCycle );
                const float32 noise     = MeshModifierNoise::valueNoise1D( seed * 3u + axis, axisCycle );
                MeshModifierUtil::setComponent( offset, axis, MeshModifierUtil::getComponent( amplitude, axis ) * ( sine + ( noise - sine ) * noiseMix ) );
            }
            return point._position + offset;
        }

    private:
        uint32 _amplitudeParam;
        uint32 _speedParam;
        uint32 _phaseVariationParam;
        uint32 _noiseParam;
        uint32 _seedParam;
    };

    SW_MESH_MODIFIER( VertexAnimModifier, "vertexanim", "Vertex Anim", "Animation" );
} // namespace sw::editor
