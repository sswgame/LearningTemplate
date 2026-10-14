/**
 * @file PushModifier.cpp
 * @brief 법선 방향으로 밀어내는(Inflate) 모디파이어입니다. 음수는 안으로 줄입니다. 양이 0 이면 항등입니다.
 */
#include "pch.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 법선 밀기입니다. */
    class PushModifier final : public MeshModifier
    {
    public:
        PushModifier()
            : MeshModifier()
            , _amountParam{ addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
        {
        }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            (void)context;
            return point._position + point._normal * getFloat( _amountParam );
        }

    private:
        uint32 _amountParam;
    };

    SW_MESH_MODIFIER( PushModifier, "push", "Push", "Surface" );
} // namespace sw::editor
