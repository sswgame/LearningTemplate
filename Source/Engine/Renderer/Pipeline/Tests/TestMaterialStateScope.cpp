#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RenderState/RenderState.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"

using namespace sw;

/** @brief [MaterialRenderStateTest] 머티리얼에 내주는 범위는 패스 종류의 표가 정한다 */
SW_TEST_CASE( MaterialRenderStateTest, ScopeComesFromThePassTable )
{
    using Scope = RenderStateOverride::Scope;
    SW_EXPECT_TRUE( getRenderPassTypeInfo( RenderPassType::Transparent ).getMaterialStateScope() == Scope::FixedAndBlend );
    SW_EXPECT_TRUE( getRenderPassTypeInfo( RenderPassType::ForwardOpaque ).getMaterialStateScope() == Scope::FixedAndBlend );
    SW_EXPECT_TRUE( getRenderPassTypeInfo( RenderPassType::ForwardOpaqueNoDepthWrite ).getMaterialStateScope() == Scope::FixedAndBlend );
    SW_EXPECT_TRUE( getRenderPassTypeInfo( RenderPassType::ForwardOnlyOpaque ).getMaterialStateScope() == Scope::FixedAndBlend );
    SW_EXPECT_TRUE( getRenderPassTypeInfo( RenderPassType::GBuffer ).getMaterialStateScope() == Scope::FixedState );
    SW_EXPECT_TRUE( getRenderPassTypeInfo( RenderPassType::Shadow ).getMaterialStateScope() == Scope::CullOnly );
    SW_EXPECT_TRUE( getRenderPassTypeInfo( RenderPassType::DepthPrepass ).getMaterialStateScope() == Scope::CullOnly );
    SW_EXPECT_TRUE( getRenderPassTypeInfo( RenderPassType::MeshOutline ).getMaterialStateScope() == Scope::CullOnly );
}
