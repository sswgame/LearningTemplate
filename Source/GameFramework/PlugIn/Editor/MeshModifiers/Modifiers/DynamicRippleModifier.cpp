/**
 * @file DynamicRippleModifier.cpp
 * @brief 기즈모 중심에서 바깥으로 퍼져 나가며 시간에 따라 감쇠하는 물결 모디파이어입니다.
 * @details r = 주축에 수직인 평면에서 중심까지의 거리, 파면 = 속도 · 시간.
 *          변위(주축) = 진폭 · sin(2π (r - 파면) / 파장) · exp(-감쇠 · 시간) · 반경 감쇠, 파면이 아직 닿지 않은 점(r > 파면)은 0.
 *          반경 감쇠는 반경이 0 이면 1, 아니면 saturate(1 - r / 반경). 진폭이 0 이면 항등이고, 시간 0 에서는 파면이 원점이라 변위가 없다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 퍼져 나가는 감쇠 물결입니다. */
    class DynamicRippleModifier final : public MeshModifier
    {
    public:
        DynamicRippleModifier()
            : MeshModifier()
            , _amplitudeParam{ addParam( MeshModifierParamDef{ "Amplitude", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
            , _wavelengthParam{ addParam( MeshModifierParamDef{ "Wavelength", MeshModifierParamKind::Float, 0.001f, 1000.0f, float3{ 1.0f, 0.0f, 0.0f } } ) }
            , _speedParam{ addParam( MeshModifierParamDef{ "Speed", MeshModifierParamKind::Float, 0.0f, 1000.0f, float3{ 1.0f, 0.0f, 0.0f } } ) }
            , _dampingParam{ addParam( MeshModifierParamDef{ "Damping", MeshModifierParamKind::Float, 0.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
            , _radiusParam{ addParam( MeshModifierParamDef{ "Radius", MeshModifierParamKind::Float, 0.0f, 100000.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
        {
        }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 amplitude = getFloat( _amplitudeParam );
            if ( MathUtil::abs( amplitude ) < MathUtil::kEpsilon )
                return point._position;
            const float32 time   = MathUtil::max( context.getTime(), 0.0f );
            const uint32  axis   = context._axisIndex;
            const float32 coordU = MeshModifierUtil::getComponent( point._position, ( axis + 1 ) % 3 );
            const float32 coordV = MeshModifierUtil::getComponent( point._position, ( axis + 2 ) % 3 );
            const float32 radius = MathUtil::sqrt( coordU * coordU + coordV * coordV );
            const float32 front  = getFloat( _speedParam ) * time;
            if ( radius > front )
                return point._position;
            const float32 limit   = getFloat( _radiusParam );
            const float32 falloff = limit > MathUtil::kEpsilon ? MathUtil::saturate( 1.0f - radius / limit ) : 1.0f;
            const float32 angle   = MathUtil::kTwoPi * ( radius - front ) / MathUtil::max( getFloat( _wavelengthParam ), 0.001f );
            const float32 offset  = amplitude * MathUtil::sin( angle ) * MathUtil::exp( -getFloat( _dampingParam ) * time ) * falloff;
            float3        result  = point._position;
            MeshModifierUtil::setComponent( result, axis, MeshModifierUtil::getComponent( result, axis ) + offset );
            return result;
        }

    private:
        uint32 _amplitudeParam;
        uint32 _wavelengthParam;
        uint32 _speedParam;
        uint32 _dampingParam;
        uint32 _radiusParam;
    };

    SW_MESH_MODIFIER( DynamicRippleModifier, "dynamicripple", "Dynamic Ripple", "Wave" );
} // namespace sw::editor
