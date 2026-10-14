/**
 * @file SinusCurveModifier.cpp
 * @brief 주축을 따라 사인 곡선으로 휘게 하는 모디파이어입니다. 옆 밀림 = 진폭 · sin(2π · 주기 수 · t + 위상), t 는 주축 위 비율입니다.
 * @details 밀리는 방향은 주축을 도는 각(도)이다. 진폭 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 사인 곡선 휘기입니다. */
    class SinusCurveModifier final : public MeshModifier
    {
    public:
        SinusCurveModifier()
            : MeshModifier()
            , _amplitudeParam{ addParam( MeshModifierParamDef{ "Amplitude", MeshModifierParamKind::Float, -10.0f, 10.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Shape", "Sideways offset at the peaks (0 = no change)", "m" ) ) }
            , _frequencyParam{ addParam( MeshModifierParamDef{ "Frequency", MeshModifierParamKind::Float, 0.0f, 20.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Shape", "Number of sine periods over the mesh height" ) ) }
            , _phaseParam{ addParam( MeshModifierParamDef{ "Phase", MeshModifierParamKind::Float, -360.0f, 360.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Shape", "Offset of the curve along the main axis", "deg" ) ) }
            , _directionParam{ addParam( MeshModifierParamDef{ "Direction", MeshModifierParamKind::Float, -180.0f, 180.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Shape", "Direction of the sideways offset, around the main axis", "deg" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Bends the mesh sideways along a sine curve that follows the main axis."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 amplitude = getFloat( _amplitudeParam );
            if ( MathUtil::abs( amplitude ) < MathUtil::kEpsilon )
                return point._position;
            const float32 fraction  = MeshModifierUtil::getAxisFraction( point._position, context );
            const float32 shift     = amplitude * MathUtil::sin( MathUtil::kTwoPi * getFloat( _frequencyParam ) * fraction + MathUtil::toRadian( getFloat( _phaseParam ) ) );
            const float32 direction = MathUtil::toRadian( getFloat( _directionParam ) );
            const uint32  axis      = context._axisIndex;
            const uint32  axisU     = ( axis + 1 ) % 3;
            const uint32  axisV     = ( axis + 2 ) % 3;
            float3        result    = point._position;
            MeshModifierUtil::setComponent( result, axisU, MeshModifierUtil::getComponent( result, axisU ) + shift * MathUtil::cos( direction ) );
            MeshModifierUtil::setComponent( result, axisV, MeshModifierUtil::getComponent( result, axisV ) + shift * MathUtil::sin( direction ) );
            return result;
        }

    private:
        uint32 _amplitudeParam;
        uint32 _frequencyParam;
        uint32 _phaseParam;
        uint32 _directionParam;
    };

    SW_MESH_MODIFIER( SinusCurveModifier, "sinuscurve", "Sinus Curve", "Deform" );
} // namespace sw::editor
