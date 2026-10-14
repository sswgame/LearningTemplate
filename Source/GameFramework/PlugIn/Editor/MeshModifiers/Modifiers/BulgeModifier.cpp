/**
 * @file BulgeModifier.cpp
 * @brief 주축을 따라 범위 가운데를 볼록하게 키우는 모디파이어입니다. 단면 배율 = 1 + 양 · (1 - |2s - 1|^곡률), s 는 범위 안 비율입니다.
 * @details 범위 밖과 범위 양 끝은 배율 1 이라 이음매가 없다. 양이 0 이거나 범위 폭이 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 주축 가운데 볼록 키우기입니다. */
    class BulgeModifier final : public MeshModifier
    {
    public:
        BulgeModifier()
            : MeshModifier()
            , _amountParam{ addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, -1.0f, 10.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
            , _curvatureParam{ addParam( MeshModifierParamDef{ "Curvature", MeshModifierParamKind::Float, 0.1f, 8.0f, float3{ 2.0f, 0.0f, 0.0f } } ) }
            , _lowerParam{ addParam( MeshModifierParamDef{ "Lower Limit", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
            , _upperParam{ addParam( MeshModifierParamDef{ "Upper Limit", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 1.0f, 0.0f, 0.0f } } ) }
        {
        }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 amount = getFloat( _amountParam );
            const float32 lower  = MathUtil::min( getFloat( _lowerParam ), getFloat( _upperParam ) );
            const float32 upper  = MathUtil::max( getFloat( _lowerParam ), getFloat( _upperParam ) );
            if ( MathUtil::abs( amount ) < MathUtil::kEpsilon || upper - lower < MathUtil::kEpsilon )
                return point._position;
            const float32 fraction = MeshModifierUtil::getAxisFraction( point._position, context );
            if ( fraction <= lower || fraction >= upper )
                return point._position;
            const float32 ratio   = ( fraction - lower ) / ( upper - lower );
            const float32 profile = 1.0f - MathUtil::pow( MathUtil::abs( 2.0f * ratio - 1.0f ), getFloat( _curvatureParam ) );
            const float32 scale   = MathUtil::max( 1.0f + amount * profile, 0.0f );
            const uint32  axis    = context._axisIndex;
            float3        result  = point._position * scale;
            MeshModifierUtil::setComponent( result, axis, MeshModifierUtil::getComponent( point._position, axis ) );
            return result;
        }

    private:
        uint32 _amountParam;
        uint32 _curvatureParam;
        uint32 _lowerParam;
        uint32 _upperParam;
    };

    SW_MESH_MODIFIER( BulgeModifier, "bulge", "Bulge", "Deform" );
} // namespace sw::editor
