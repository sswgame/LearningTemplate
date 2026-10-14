/**
 * @file SimpleModModifier.cpp
 * @brief 축마다 2 차식으로 위치를 바꾸는 단순 변형입니다. 성분 k 마다 p_k += A_k + B_k · p_k + C_k · p_k² 입니다.
 * @details p 는 모디파이어 공간 좌표(기즈모 중심이 원점)다. 세 성분은 서로 독립이다. 계수가 모두 0(기본값)이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 축별 2 차식 변형입니다. */
    class SimpleModModifier final : public MeshModifier
    {
    public:
        SimpleModModifier()
            : MeshModifier()
            , _offsetParam{ addParam( MeshModifierParamDef{ "Offset", MeshModifierParamKind::Vector3, -10.0f, 10.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( nullptr, "Constant term per axis (0 = no change)", "m" ) ) }
            , _linearParam{ addParam( MeshModifierParamDef{ "Linear", MeshModifierParamKind::Vector3, -10.0f, 10.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( nullptr, "Linear term per axis, multiplies the coordinate (0 = no change)" ) ) }
            , _quadraticParam{ addParam( MeshModifierParamDef{ "Quadratic", MeshModifierParamKind::Vector3, -10.0f, 10.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( nullptr, "Quadratic term per axis, multiplies the squared coordinate (0 = no change)" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Offsets each axis with a simple quadratic of its own coordinate."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& ) const override
        {
            float3 result = point._position;
            for ( uint32 axis = 0; axis < 3; ++axis )
            {
                const float32 coord = MeshModifierUtil::getComponent( point._position, axis );
                const float32 delta = MeshModifierUtil::getComponent( getVector3( _offsetParam ), axis ) + MeshModifierUtil::getComponent( getVector3( _linearParam ), axis ) * coord +
                                      MeshModifierUtil::getComponent( getVector3( _quadraticParam ), axis ) * coord * coord;
                MeshModifierUtil::setComponent( result, axis, coord + delta );
            }
            return result;
        }

    private:
        uint32 _offsetParam;
        uint32 _linearParam;
        uint32 _quadraticParam;
    };

    SW_MESH_MODIFIER( SimpleModModifier, "simplemod", "Simple Mod", "Deform" );
} // namespace sw::editor
