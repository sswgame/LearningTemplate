/**
 * @file SqueezeModifier.cpp
 * @brief 기즈모 중심을 기준으로 주축을 누르고 직교 방향을 넓히는 모디파이어입니다. 주축 배율 a = 1 - 양, 직교 배율 = 1 + 보존 · (1/√a - 1)입니다.
 * @details 보존 1 이면 a · (1/√a)² = 1 이라 부피가 그대로다. 양은 0.9 를 넘지 않아 a ≥ 0.1 이다. 양 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 축 압축 + 직교 팽창입니다. */
    class SqueezeModifier final : public MeshModifier
    {
    public:
        SqueezeModifier()
            : MeshModifier()
            , _amountParam{ addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, -2.0f, 0.9f, float3{ 0.0f, 0.0f, 0.0f } } ) }
            , _preserveParam{ addParam( MeshModifierParamDef{ "Volume Preservation", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 1.0f, 0.0f, 0.0f } } ) }
        {
        }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 amount = getFloat( _amountParam );
            if ( MathUtil::abs( amount ) < MathUtil::kEpsilon )
                return point._position;
            const float32 axisScale   = MathUtil::max( 1.0f - amount, 0.1f );
            const float32 radialScale = 1.0f + getFloat( _preserveParam ) * ( 1.0f / MathUtil::sqrt( axisScale ) - 1.0f );
            const uint32  axis        = context._axisIndex;
            float3        result      = point._position * radialScale;
            MeshModifierUtil::setComponent( result, axis, MeshModifierUtil::getComponent( point._position, axis ) * axisScale );
            return result;
        }

    private:
        uint32 _amountParam;
        uint32 _preserveParam;
    };

    SW_MESH_MODIFIER( SqueezeModifier, "squeeze", "Squeeze", "Deform" );
} // namespace sw::editor
