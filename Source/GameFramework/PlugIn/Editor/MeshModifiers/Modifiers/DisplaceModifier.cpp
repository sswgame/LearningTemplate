/**
 * @file DisplaceModifier.cpp
 * @brief 높이 맵 대신 절차 함수 sin(2π f u + 위상) · cos(2π f v + 위상) 값만큼 법선 방향으로 옮기는 모디파이어입니다.
 * @details u, v 는 주축에 수직인 두 성분이다. 양이 0 이면 항등이다. 텍스처 높이 맵 변위(Displace Render Target 등)는 `AssetPath` 매개변수와
 *          `IMeshModifierInputs::trySampleTexture` 로 별도 모디파이어가 맡는다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 절차 변위입니다. */
    class DisplaceModifier final : public MeshModifier
    {
    public:
        DisplaceModifier()
            : MeshModifier()
            , _amountParam{ addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( nullptr, "Displacement strength along the normal (0 = no change)", "m" ) ) }
            , _frequencyParam{ addParam( MeshModifierParamDef{ "Frequency", MeshModifierParamKind::Float, 0.001f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( nullptr, "Pattern repeats per unit length" ) ) }
            , _phaseParam{ addParam( MeshModifierParamDef{ "Phase", MeshModifierParamKind::Float, -360.0f, 360.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( nullptr, "Offset of the pattern", "deg" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Moves vertices along their normals by a procedural sine pattern."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 amount = getFloat( _amountParam );
            if ( MathUtil::abs( amount ) < MathUtil::kEpsilon )
                return point._position;
            const uint32  axis      = context._axisIndex;
            const float32 coordU    = MeshModifierUtil::getComponent( point._position, ( axis + 1 ) % 3 );
            const float32 coordV    = MeshModifierUtil::getComponent( point._position, ( axis + 2 ) % 3 );
            const float32 frequency = MathUtil::kTwoPi * getFloat( _frequencyParam );
            const float32 phase     = MathUtil::toRadian( getFloat( _phaseParam ) );
            const float32 height    = MathUtil::sin( frequency * coordU + phase ) * MathUtil::cos( frequency * coordV + phase );
            return point._position + point._normal * ( amount * height );
        }

    private:
        uint32 _amountParam;
        uint32 _frequencyParam;
        uint32 _phaseParam;
    };

    SW_MESH_MODIFIER( DisplaceModifier, "displace", "Displace", "Surface" );
} // namespace sw::editor
