/**
 * @file HumpModifier.cpp
 * @brief 주축 방향으로 국소 혹을 솟게 하는 모디파이어입니다. 높이 · exp(-(d / 반경)²), d 는 주축에 수직인 평면에서 중심까지 거리입니다.
 * @details 높이가 0 이거나 반경이 0 이면 항등이다. 중심의 주축 성분은 쓰지 않는다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 가우시안 혹입니다. */
    class HumpModifier final : public MeshModifier
    {
    public:
        HumpModifier()
            : MeshModifier()
            , _heightParam{
                  addParam( MeshModifierParamDef{ "Height", MeshModifierParamKind::Float, -10.0f, 10.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( "Hump", "Hump height; negative makes a dent", "m" ) )
        },
            _radiusParam{ addParam( MeshModifierParamDef{ "Radius", MeshModifierParamKind::Float, 0.0f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Hump", "Radius of the hump", "m" ) ) },
            _centerParam{ addParam( MeshModifierParamDef{ "Center", MeshModifierParamKind::Vector3, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Hump", "Center of the hump in modifier space", "m" ) ) }
        {
        }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 height = getFloat( _heightParam );
            const float32 radius = getFloat( _radiusParam );
            if ( MathUtil::abs( height ) < MathUtil::kEpsilon || radius < MathUtil::kEpsilon )
                return point._position;
            const uint32 axis   = context._axisIndex;
            float3       offset = point._position - getVector3( _centerParam );
            MeshModifierUtil::setComponent( offset, axis, 0.0f );
            const float32 ratio  = offset.getLength() / radius;
            float3        result = point._position;
            MeshModifierUtil::setComponent( result, axis, MeshModifierUtil::getComponent( result, axis ) + height * MathUtil::exp( -ratio * ratio ) );
            return result;
        }

    private:
        uint32 _heightParam;
        uint32 _radiusParam;
        uint32 _centerParam;
    };

    SW_MESH_MODIFIER( HumpModifier, "hump", "Hump", "Deform" );
} // namespace sw::editor
