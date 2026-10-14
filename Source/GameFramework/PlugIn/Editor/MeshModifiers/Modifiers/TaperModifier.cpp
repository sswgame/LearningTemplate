/**
 * @file TaperModifier.cpp
 * @brief 주축을 따라 단면을 키우거나 줄이는 모디파이어입니다. 배율 = 1 + 양 · t^곡률(t 는 상자 아래 0 · 위 1)입니다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 주축 테이퍼입니다. */
    class TaperModifier final : public MeshModifier
    {
    public:
        TaperModifier()
            : MeshModifier()
            , _amountParam{ addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, -1.0f, 10.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
            , _curveParam{ addParam( MeshModifierParamDef{ "Curve", MeshModifierParamKind::Float, 0.1f, 8.0f, float3{ 1.0f, 0.0f, 0.0f } } ) }
        {
        }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 amount = getFloat( _amountParam );
            if ( MathUtil::abs( amount ) < MathUtil::kEpsilon )
                return point._position;
            const uint32  axis     = context._axisIndex;
            const float32 fraction = MathUtil::saturate( MeshModifierUtil::getAxisFraction( point._position, context ) );
            const float32 scale    = 1.0f + amount * MathUtil::pow( fraction, getFloat( _curveParam ) );
            float3        result   = point._position * scale;
            MeshModifierUtil::setComponent( result, axis, MeshModifierUtil::getComponent( point._position, axis ) );
            return result;
        }

    private:
        uint32 _amountParam;
        uint32 _curveParam;
    };

    SW_MESH_MODIFIER( TaperModifier, "taper", "Taper", "Deform" );
} // namespace sw::editor
