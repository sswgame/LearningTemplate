/**
 * @file VertexAnimModifier.cpp
 * @brief 정점마다 다른 위상의 사인(또는 값 노이즈) 오프셋을 시간에 따라 더하는 단순 정점 애니메이션 모디파이어입니다.
 * @details 오프셋 = `Amplitude`(축마다) × 신호(시간 × `Speed` + 정점 위상). 정점 위상은 정점 번호의 정수 해시로 정해 결정적이고,
 *          `Phase Variation` 이 0 이면 모든 정점이 같이 움직인다. `Noise` 는 사인과 값 노이즈(정점마다 다른 씨앗, 정수 시간 칸 사이 부드러운 보간)를 섞는 비율이다.
 *          축마다 위상을 조금 어긋나게 해 한 직선으로만 흔들리지 않게 한다. 진폭이 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct VertexAnimModifierInternal
        {
            /** @brief 정수 해시를 [0,1) 로 냅니다. */
            static float32 hashToUnit( uint32 value )
            {
                value ^= value >> 16;
                value *= 0x7feb352du;
                value ^= value >> 15;
                value *= 0x846ca68bu;
                value ^= value >> 16;
                return static_cast<float32>( value & 0x00ffffffu ) / 16777216.0f;
            }

            /** @brief 씨앗별 1 차원 값 노이즈 [-1,1] 입니다(정수 칸 사이 smoothstep 보간). */
            static float32 valueNoise( uint32 seed, float32 time )
            {
                const float32 safeTime = MathUtil::clamp( time, -1.0e6f, 1.0e6f ); // int32 로 바꿀 때 넘치지 않게
                const float32 cell     = MathUtil::floor( safeTime );
                const float32 ratio    = safeTime - cell;
                const int32   index  = static_cast<int32>( cell );
                const float32 valueA = hashToUnit( seed * 0x9e3779b9u + static_cast<uint32>( index ) );
                const float32 valueB = hashToUnit( seed * 0x9e3779b9u + static_cast<uint32>( index + 1 ) );
                const float32 smooth = ratio * ratio * ( 3.0f - 2.0f * ratio );
                return ( valueA + ( valueB - valueA ) * smooth ) * 2.0f - 1.0f;
            }
        };
    } // namespace

    /** @brief 정점별 사인 · 노이즈 오프셋 애니메이션입니다. */
    class VertexAnimModifier final : public MeshModifier
    {
    public:
        VertexAnimModifier()
            : MeshModifier()
            , _amplitudeParam{ addParam( MeshModifierParamDef{ "Amplitude", MeshModifierParamKind::Vector3, -10.0f, 10.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
            , _speedParam{ addParam( MeshModifierParamDef{ "Speed", MeshModifierParamKind::Float, 0.0f, 50.0f, float3{ 1.0f, 0.0f, 0.0f } } ) }
            , _phaseVariationParam{ addParam( MeshModifierParamDef{ "Phase Variation", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 1.0f, 0.0f, 0.0f } } ) }
            , _noiseParam{ addParam( MeshModifierParamDef{ "Noise", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
            , _seedParam{ addParam( MeshModifierParamDef{ "Seed", MeshModifierParamKind::Int, 0.0f, 10000.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
        {
        }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float3& amplitude = getVector3( _amplitudeParam );
            if ( amplitude.getLengthSquared() < MathUtil::kEpsilon * MathUtil::kEpsilon )
                return point._position;
            const uint32  seed      = static_cast<uint32>( getInt( _seedParam ) ) * 7919u + point._vertexIndex;
            const float32 phase     = VertexAnimModifierInternal::hashToUnit( seed ) * getFloat( _phaseVariationParam );
            const float32 cycle     = context.getTime() * getFloat( _speedParam ) + phase;
            const float32 noiseMix  = getFloat( _noiseParam );
            float3        offset{ 0.0f, 0.0f, 0.0f };
            for ( uint32 axis = 0; axis < 3; ++axis )
            {
                const float32 axisCycle = cycle + static_cast<float32>( axis ) * 0.25f;
                const float32 sine      = MathUtil::sin( MathUtil::kTwoPi * axisCycle );
                const float32 noise     = VertexAnimModifierInternal::valueNoise( seed * 3u + axis, axisCycle );
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
