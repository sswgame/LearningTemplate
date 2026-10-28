/**
 * @file BendModifier.cpp
 * @brief 주축을 따라 휘게 하는 모디파이어입니다. 휘는 방향은 주축을 도는 각으로 정하고, 범위 밖은 끝의 접선을 따라 곧게 이어집니다.
 * @details 반지름 r = L / θ 인 원호에 주축 좌표를 감는다(L = 상자의 주축 길이, θ = 전체 휨 각). 각이 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 주축 휘기입니다. */
    class BendModifier final : public MeshModifier
    {
    public:
        BendModifier()
            : MeshModifier()
            , _angleParam{
                  addParam( MeshModifierParamDef{ "Angle", MeshModifierParamKind::Float, -1080.0f, 1080.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( "Bend", "Total bend angle along the main axis", "deg" ) )
        },
            _directionParam{ addParam( MeshModifierParamDef{ "Direction", MeshModifierParamKind::Float, -180.0f, 180.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Bend", "Direction the bend leans toward, around the main axis", "deg" ) ) },
            _lowerParam{ addParam( MeshModifierParamDef{ "Lower Limit", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Limits", "Start of the bent range along the main axis (0 = bottom)" ) ) },
            _upperParam{ addParam( MeshModifierParamDef{ "Upper Limit", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Limits", "End of the bent range along the main axis (1 = top)" ) ) }
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

            const float32 direction = MathUtil::toRadian( getFloat( _directionParam ) );
            const float32 cosDir    = MathUtil::cos( direction );
            const float32 sinDir    = MathUtil::sin( direction );
            const float32 coordU    = MeshModifierUtil::getComponent( point._position, axisU );
            const float32 coordV    = MeshModifierUtil::getComponent( point._position, axisV );
            const float32 bendX     = coordU * cosDir + coordV * sinDir; // 휘는 방향 성분
            const float32 sideY     = -coordU * sinDir + coordV * cosDir;

            const float32 height  = MeshModifierUtil::getComponent( point._position, axis );
            const float32 clamped = MathUtil::clamp( height, lower, upper );
            const float32 radius  = length / angle;
            const float32 phi     = clamped / radius;
            const float32 cosPhi  = MathUtil::cos( phi );
            const float32 sinPhi  = MathUtil::sin( phi );
            const float32 extra   = height - clamped; // 범위 밖은 끝 접선을 따라 곧게
            const float32 newX    = radius - ( radius - bendX ) * cosPhi + extra * sinPhi;
            const float32 newH    = ( radius - bendX ) * sinPhi + extra * cosPhi;

            float3 result = point._position;
            MeshModifierUtil::setComponent( result, axis, newH );
            MeshModifierUtil::setComponent( result, axisU, newX * cosDir - sideY * sinDir );
            MeshModifierUtil::setComponent( result, axisV, newX * sinDir + sideY * cosDir );
            return result;
        }

    private:
        uint32 _angleParam;
        uint32 _directionParam;
        uint32 _lowerParam;
        uint32 _upperParam;
    };

    SW_MESH_MODIFIER( BendModifier, "bend", "Bend", "Deform" );
} // namespace sw::editor
