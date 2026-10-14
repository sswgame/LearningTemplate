/**
 * @file SkewModifier.cpp
 * @brief 주축 위치에 비례해 주축에 수직인 방향으로 미는 모디파이어입니다(전단). 밀림 = 양 · s, s 는 범위 안 비율(범위 아래 0 · 위 1)입니다.
 * @details 방향은 주축을 도는 각(도)이다. 범위 폭이 0 이면 범위 아래 0 · 위 1 로 계단이 된다. 양 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 축 기울임(전단)입니다. */
    class SkewModifier final : public MeshModifier
    {
    public:
        SkewModifier()
            : MeshModifier()
            , _amountParam{ addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Skew", "Sideways shift at the top of the main axis", "m" ) ) }
            , _directionParam{ addParam( MeshModifierParamDef{ "Direction", MeshModifierParamKind::Float, -180.0f, 180.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Skew", "Direction of the shift around the main axis", "deg" ) ) }
            , _lowerParam{ addParam( MeshModifierParamDef{ "Lower Limit", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Limits", "Start of the skewed range along the main axis (0 = bottom)" ) ) }
            , _upperParam{ addParam( MeshModifierParamDef{ "Upper Limit", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Limits", "End of the skewed range along the main axis (1 = top)" ) ) }
        {
        }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 amount = getFloat( _amountParam );
            if ( MathUtil::abs( amount ) < MathUtil::kEpsilon )
                return point._position;
            const float32 lower    = MathUtil::min( getFloat( _lowerParam ), getFloat( _upperParam ) );
            const float32 upper    = MathUtil::max( getFloat( _lowerParam ), getFloat( _upperParam ) );
            const float32 fraction = MeshModifierUtil::getAxisFraction( point._position, context );
            float32       ratio    = 0.0f;
            if ( upper - lower < MathUtil::kEpsilon )
                ratio = fraction >= lower ? 1.0f : 0.0f;
            else
                ratio = MathUtil::saturate( ( fraction - lower ) / ( upper - lower ) );

            const uint32  axis      = context._axisIndex;
            const float32 direction = MathUtil::toRadian( getFloat( _directionParam ) );
            const float32 shift     = amount * ratio;
            float3        result    = point._position;
            const uint32  axisU     = ( axis + 1 ) % 3;
            const uint32  axisV     = ( axis + 2 ) % 3;
            MeshModifierUtil::setComponent( result, axisU, MeshModifierUtil::getComponent( result, axisU ) + shift * MathUtil::cos( direction ) );
            MeshModifierUtil::setComponent( result, axisV, MeshModifierUtil::getComponent( result, axisV ) + shift * MathUtil::sin( direction ) );
            return result;
        }

    private:
        uint32 _amountParam;
        uint32 _directionParam;
        uint32 _lowerParam;
        uint32 _upperParam;
    };

    SW_MESH_MODIFIER( SkewModifier, "skew", "Skew", "Deform" );
} // namespace sw::editor
