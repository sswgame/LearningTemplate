/**
 * @file TreeBendModifier.cpp
 * @brief 바람에 흔들리는 나무처럼 높이의 제곱에 비례해 옆으로 휘는 모디파이어입니다.
 * @details 높이 h = 주축 좌표 - (상자 아래 + 뿌리 높이), h ≤ 0 인 점(뿌리)은 그대로. t = h / 주축 길이.
 *          옆 변위 = 풍향(주축 성분을 뺀 단위 벡터) · 세기 · (1 + 흔들림 · sin(2π · 주파수 · 시간)) · t².
 *          풍향이 주축과 평행하거나 세기가 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 바람 휘기입니다. */
    class TreeBendModifier final : public MeshModifier
    {
    public:
        TreeBendModifier()
            : MeshModifier()
            , _strengthParam{
                  addParam( MeshModifierParamDef{ "Strength", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( "Shape", "Bend amount; 0 leaves the mesh unchanged" ) )
        },
            _windParam{ addParam( MeshModifierParamDef{ "Wind Direction", MeshModifierParamKind::Vector3, -1.0f, 1.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Shape", "Direction the mesh bends toward" ) ) },
            _swayParam{ addParam( MeshModifierParamDef{ "Sway", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.5f, 0.0f, 0.0f } }.withLayout( "Animation", "Portion of the bend that oscillates over time; 0 is static" ) ) },
            _frequencyParam{ addParam( MeshModifierParamDef{ "Frequency", MeshModifierParamKind::Float, 0.0f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Animation", "Sway oscillations per second; 0 stops swaying", "Hz" ) ) },
            _rootHeightParam{ addParam( MeshModifierParamDef{ "Root Height", MeshModifierParamKind::Float, 0.0f, 10000.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Shape", "Height above the bounds bottom below which nothing bends", "m" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Bends a tree-like mesh in the wind, more toward the top."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 strength = getFloat( _strengthParam );
            if ( MathUtil::abs( strength ) < MathUtil::kEpsilon || context._bounds.isValid() == false )
                return point._position;
            const uint32 axis = context._axisIndex;
            float3       wind = getVector3( _windParam );
            MeshModifierUtil::setComponent( wind, axis, 0.0f );
            const float32 windLength = wind.getLength();
            if ( windLength < MathUtil::kEpsilon )
                return point._position;
            wind = wind * ( 1.0f / windLength );

            const float32 root   = MeshModifierUtil::getComponent( context._bounds._min, axis ) + getFloat( _rootHeightParam );
            const float32 height = MeshModifierUtil::getComponent( point._position, axis ) - root;
            if ( height <= 0.0f )
                return point._position;
            const float32 ratio = height / MeshModifierUtil::getAxisLength( context );
            const float32 sway  = 1.0f + getFloat( _swayParam ) * MathUtil::sin( MathUtil::kTwoPi * getFloat( _frequencyParam ) * context.getTime() );
            return point._position + wind * ( strength * sway * ratio * ratio );
        }

    private:
        uint32 _strengthParam;
        uint32 _windParam;
        uint32 _swayParam;
        uint32 _frequencyParam;
        uint32 _rootHeightParam;
    };

    SW_MESH_MODIFIER( TreeBendModifier, "treebend", "Tree Bend", "Wave" );
} // namespace sw::editor
