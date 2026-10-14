/**
 * @file PokeModifier.cpp
 * @brief 기즈모 중심 둘레를 주축 방향으로 볼록(양수)하거나 오목(음수)하게 찌르는 모디파이어입니다.
 * @details 무게 = (1 - (d / 반지름)²)^감쇠 (d < 반지름), d 는 중심까지의 거리. 양이 0 이면 항등이다. 상자에 맞추면 반지름이 상자 반폭이 된다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 국소 볼록 · 오목입니다. */
    class PokeModifier final : public MeshModifier
    {
    public:
        PokeModifier()
            : MeshModifier()
            , _amountParam{
                  addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( nullptr, "Height of the bump at the center (0 = no change, negative dents)", "m" ) )
        },
            _radiusParam{ addParam( MeshModifierParamDef{ "Radius", MeshModifierParamKind::Float, 0.001f, 10000.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( nullptr, "Radius of the affected area", "m" ) ) },
            _falloffParam{ addParam( MeshModifierParamDef{ "Falloff", MeshModifierParamKind::Float, 0.1f, 8.0f, float3{ 2.0f, 0.0f, 0.0f } }.withLayout( nullptr, "Sharpness of the bump edge (higher = narrower peak)" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Pokes a smooth bump or dent along the main axis around the gizmo center."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 amount = getFloat( _amountParam );
            if ( MathUtil::abs( amount ) < MathUtil::kEpsilon )
                return point._position;
            const float32 radius   = getFloat( _radiusParam );
            const float32 distance = point._position.getLength();
            if ( distance >= radius )
                return point._position;
            const float32 ratio  = distance / radius;
            const float32 weight = MathUtil::pow( 1.0f - ratio * ratio, getFloat( _falloffParam ) );
            float3        result = point._position;
            MeshModifierUtil::setComponent( result, context._axisIndex, MeshModifierUtil::getComponent( result, context._axisIndex ) + amount * weight );
            return result;
        }

    protected:
        void onFitToBounds( const MeshModifierBounds& modifierBounds ) override
        {
            const float3 size = modifierBounds.getSize();
            setScalar( _radiusParam, 0.5f * MathUtil::max( size._x, MathUtil::max( size._y, size._z ) ) );
        }

    private:
        uint32 _amountParam;
        uint32 _radiusParam;
        uint32 _falloffParam;
    };

    SW_MESH_MODIFIER( PokeModifier, "poke", "Poke", "Surface" );
} // namespace sw::editor
