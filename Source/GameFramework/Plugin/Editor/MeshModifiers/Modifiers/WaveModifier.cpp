/**
 * @file WaveModifier.cpp
 * @brief 다음 축(주축 + 1)을 따라 흐르는 사인 물결로 주축 방향을 밀어내는 모디파이어입니다.
 * @details 변위 = 진폭 · sin(2π · u / 파장 + 위상 + 속도 · 시간) · exp(-감쇠 · |u|). 진폭이 0 이면 항등이다. 속도가 있으면 시간에 따라 흐른다(Waving 의 자리).
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 직선 물결입니다. */
    class WaveModifier final : public MeshModifier
    {
    public:
        WaveModifier()
            : MeshModifier()
            , _amplitudeParam{
                  addParam( MeshModifierParamDef{ "Amplitude", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( "Wave", "Wave height", "m" ) )
        },
            _wavelengthParam{ addParam( MeshModifierParamDef{ "Wavelength", MeshModifierParamKind::Float, 0.001f, 1000.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Wave", "Distance between crests", "m" ) ) },
            _phaseParam{ addParam( MeshModifierParamDef{ "Phase", MeshModifierParamKind::Float, -360.0f, 360.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Wave", "Wave offset", "deg" ) ) },
            _decayParam{ addParam( MeshModifierParamDef{ "Decay", MeshModifierParamKind::Float, 0.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Falloff", "How fast the wave fades with distance" ) ) },
            _speedParam{ addParam( MeshModifierParamDef{ "Speed", MeshModifierParamKind::Float, -3600.0f, 3600.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Animation", "Phase change per second of preview time", "deg/s" ) ) }
        {
        }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 amplitude = getFloat( _amplitudeParam );
            if ( MathUtil::abs( amplitude ) < MathUtil::kEpsilon )
                return point._position;
            const uint32  axis   = context._axisIndex;
            const float32 coordU = MeshModifierUtil::getComponent( point._position, ( axis + 1 ) % 3 );
            const float32 phase  = MathUtil::toRadian( getFloat( _phaseParam ) + getFloat( _speedParam ) * context.getTime() );
            const float32 angle  = MathUtil::kTwoPi * coordU / getFloat( _wavelengthParam ) + phase;
            const float32 offset = amplitude * MathUtil::sin( angle ) * MathUtil::exp( -getFloat( _decayParam ) * MathUtil::abs( coordU ) );
            float3        result = point._position;
            MeshModifierUtil::setComponent( result, axis, MeshModifierUtil::getComponent( result, axis ) + offset );
            return result;
        }

    private:
        uint32 _amplitudeParam;
        uint32 _wavelengthParam;
        uint32 _phaseParam;
        uint32 _decayParam;
        uint32 _speedParam;
    };

    SW_MESH_MODIFIER( WaveModifier, "wave", "Wave", "Wave" );
} // namespace sw::editor
