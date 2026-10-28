/**
 * @file TwistModifier.cpp
 * @brief 주축을 중심으로 비트는 모디파이어입니다. 회전각 = 각 · (주축 좌표 / 주축 길이)이고 범위 밖은 끝의 각을 그대로 씁니다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 주축 비틀기입니다. */
    class TwistModifier final : public MeshModifier
    {
    public:
        TwistModifier()
            : MeshModifier()
            , _angleParam{
                  addParam( MeshModifierParamDef{ "Angle", MeshModifierParamKind::Float, -3600.0f, 3600.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( "Twist", "Total twist angle from bottom to top", "deg" ) )
        },
            _lowerParam{ addParam( MeshModifierParamDef{ "Lower Limit", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Limits", "Start of the twisted range along the main axis (0 = bottom)" ) ) },
            _upperParam{ addParam( MeshModifierParamDef{ "Upper Limit", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Limits", "End of the twisted range along the main axis (1 = top)" ) ) }
        {
        }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 angle = MathUtil::toRadian( getFloat( _angleParam ) );
            if ( MathUtil::abs( angle ) < MathUtil::kEpsilon )
                return point._position;
            const uint32  axis       = context._axisIndex;
            const uint32  axisU      = ( axis + 1 ) % 3;
            const uint32  axisV      = ( axis + 2 ) % 3;
            const float32 length     = MeshModifierUtil::getAxisLength( context );
            const float32 lowerBound = MeshModifierUtil::getComponent( context._bounds._min, axis );
            const float32 lower      = lowerBound + MathUtil::min( getFloat( _lowerParam ), getFloat( _upperParam ) ) * length;
            const float32 upper      = lowerBound + MathUtil::max( getFloat( _lowerParam ), getFloat( _upperParam ) ) * length;
            const float32 height     = MathUtil::clamp( MeshModifierUtil::getComponent( point._position, axis ), lower, upper );
            const float32 phi        = angle * height / length;
            const float32 cosPhi     = MathUtil::cos( phi );
            const float32 sinPhi     = MathUtil::sin( phi );
            const float32 coordU     = MeshModifierUtil::getComponent( point._position, axisU );
            const float32 coordV     = MeshModifierUtil::getComponent( point._position, axisV );

            float3 result = point._position;
            MeshModifierUtil::setComponent( result, axisU, coordU * cosPhi - coordV * sinPhi );
            MeshModifierUtil::setComponent( result, axisV, coordU * sinPhi + coordV * cosPhi );
            return result;
        }

    private:
        uint32 _angleParam;
        uint32 _lowerParam;
        uint32 _upperParam;
    };

    SW_MESH_MODIFIER( TwistModifier, "twist", "Twist", "Deform" );
} // namespace sw::editor
