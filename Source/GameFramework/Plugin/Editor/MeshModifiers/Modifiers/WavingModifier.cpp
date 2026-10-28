/**
 * @file WavingModifier.cpp
 * @brief 한쪽 끝이 고정된 깃발처럼 펄럭이는 모디파이어입니다.
 * @details d = 고정 축 좌표 - (상자 시작 + 고정 위치), d ≤ 0 인 점은 고정(그대로). 비율 k = d / 고정 축 길이.
 *          변위(주축, 고정 축과 같으면 그다음 축) = 진폭 · k · sin(2π (d / 파장 - 속도 · 시간)).
 *          진폭이 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 깃발 펄럭임입니다. */
    class WavingModifier final : public MeshModifier
    {
    public:
        WavingModifier()
            : MeshModifier()
            , _amplitudeParam{
                  addParam( MeshModifierParamDef{ "Amplitude", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( "Shape", "Wave height; 0 leaves the mesh unchanged", "m" ) )
        },
            _wavelengthParam{ addParam( MeshModifierParamDef{ "Wavelength", MeshModifierParamKind::Float, 0.001f, 1000.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Shape", "Distance between wave crests", "m" ) ) },
            _speedParam{ addParam( MeshModifierParamDef{ "Speed", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Animation", "How fast waves travel; 0 freezes them" ) ) },
            _fixedAxisParam{ addParam( MeshModifierParamDef{ "Fixed Axis", MeshModifierParamKind::Axis, 0.0f, 2.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Range", "Axis along which the fixed edge is measured" ) ) },
            _fixedPositionParam{ addParam( MeshModifierParamDef{ "Fixed Position", MeshModifierParamKind::Float, 0.0f, 10000.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Range", "Distance from the bounds start that stays still", "m" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Waves the mesh like a flag fixed along one edge."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 amplitude = getFloat( _amplitudeParam );
            if ( MathUtil::abs( amplitude ) < MathUtil::kEpsilon || context._bounds.isValid() == false )
                return point._position;
            const uint32 fixedAxis = getAxisIndex( _fixedAxisParam );
            uint32       moveAxis  = context._axisIndex;
            if ( moveAxis == fixedAxis )
                moveAxis = ( fixedAxis + 1 ) % 3;

            const float32 start    = MeshModifierUtil::getComponent( context._bounds._min, fixedAxis ) + getFloat( _fixedPositionParam );
            const float32 distance = MeshModifierUtil::getComponent( point._position, fixedAxis ) - start;
            if ( distance <= 0.0f )
                return point._position;
            const float32 span   = MeshModifierUtil::getComponent( context._bounds.getSize(), fixedAxis );
            const float32 ratio  = distance / ( span > MathUtil::kEpsilon ? span : 1.0f );
            const float32 phase  = distance / MathUtil::max( getFloat( _wavelengthParam ), 0.001f ) - getFloat( _speedParam ) * context.getTime();
            const float32 offset = amplitude * ratio * MathUtil::sin( MathUtil::kTwoPi * phase );
            float3        result = point._position;
            MeshModifierUtil::setComponent( result, moveAxis, MeshModifierUtil::getComponent( result, moveAxis ) + offset );
            return result;
        }

    private:
        uint32 _amplitudeParam;
        uint32 _wavelengthParam;
        uint32 _speedParam;
        uint32 _fixedAxisParam;
        uint32 _fixedPositionParam;
    };

    SW_MESH_MODIFIER( WavingModifier, "waving", "Waving", "Wave" );
} // namespace sw::editor
