/**
 * @file RubberModifier.cpp
 * @brief 주축 방향으로 고무처럼 늘어지는 모디파이어입니다. 주축 밀림 = 양 · w, 단면 배율 = 1/√(1 + |양| · w / L) 이고 w = 감쇠 곡선(t) 입니다.
 * @details t 는 주축 위 비율(상자 아래 0 · 위 1, [0,1] 로 자른다), L 은 상자의 주축 길이다. 많이 밀린 곳일수록 가늘어진다.
 *          양 0 이면 항등이다. GPU 로 옮긴다 — 감쇠 곡선은 32 개 균등 표본으로 싣는다(`packGPUParams` 기본 배치).
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 탄성 늘어짐입니다. */
    class RubberModifier final : public MeshModifier
    {
    public:
        RubberModifier()
            : MeshModifier()
            , _amountParam{
                  addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, -10.0f, 10.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( nullptr, "Stretch distance at full weight (0 = no change, negative pulls down)", "m" ) )
        },
            _falloffParam{ addParam( MeshModifierParamDef{ "Falloff", MeshModifierParamKind::Curve, 0.0f, 1.0f, float3{ 0.0f, 1.0f, 0.0f } }.withLayout( nullptr, "Weight curve from bottom (0) to top (1) of the mesh" ) ) }
        {
        }

        bool supportsGPU() const override { return true; }

        const utf8* getDescription() const override { return "Stretches the mesh along the main axis like rubber, thinning the stretched part."; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 amount = getFloat( _amountParam );
            if ( MathUtil::abs( amount ) < MathUtil::kEpsilon )
                return point._position;
            const uint32  axis     = context._axisIndex;
            const float32 fraction = MathUtil::saturate( MeshModifierUtil::getAxisFraction( point._position, context ) );
            const float32 weight   = evaluateCurve( _falloffParam, fraction );
            const float32 thinning = 1.0f / MathUtil::sqrt( 1.0f + MathUtil::abs( amount * weight ) / MeshModifierUtil::getAxisLength( context ) );
            float3        result   = point._position * thinning;
            MeshModifierUtil::setComponent( result, axis, MeshModifierUtil::getComponent( point._position, axis ) + amount * weight );
            return result;
        }

    private:
        uint32 _amountParam;
        uint32 _falloffParam;
    };

    SW_MESH_MODIFIER( RubberModifier, "rubber", "Rubber", "Deform" );
} // namespace sw::editor
