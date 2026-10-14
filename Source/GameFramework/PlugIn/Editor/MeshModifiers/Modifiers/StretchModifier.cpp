/**
 * @file StretchModifier.cpp
 * @brief 주축으로 늘이거나(양 > 0) 누르는(양 < 0) 모디파이어입니다. 부피 보존 1 이면 단면을 1/√(1+양) 으로 맞춰 부피가 그대로입니다.
 * @details Squeeze 는 같은 수학에서 부피 보존만 다르게 둔 것이라 별도 모디파이어로 더할 수 있다(지금은 이 하나가 겸한다).
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 주축 늘이기 · 누르기입니다. */
    class StretchModifier final : public MeshModifier
    {
    public:
        StretchModifier()
            : MeshModifier()
            , _amountParam{
                  addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, -0.95f, 10.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( "Stretch", "Length change along the main axis (0 = none)" ) )
        },
            _volumeParam{ addParam( MeshModifierParamDef{ "Volume Preservation", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Stretch", "How much the cross section shrinks to keep volume" ) ) }
        {
        }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 alongScale = 1.0f + getFloat( _amountParam );
            const float32 sideScale  = MathUtil::pow( alongScale, -0.5f * getFloat( _volumeParam ) );
            const uint32  axis       = context._axisIndex;
            float3        result     = point._position * sideScale;
            MeshModifierUtil::setComponent( result, axis, MeshModifierUtil::getComponent( point._position, axis ) * alongScale );
            return result;
        }

    private:
        uint32 _amountParam;
        uint32 _volumeParam;
    };

    SW_MESH_MODIFIER( StretchModifier, "stretch", "Stretch", "Deform" );
} // namespace sw::editor
