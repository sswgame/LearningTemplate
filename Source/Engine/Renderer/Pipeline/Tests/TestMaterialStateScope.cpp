#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RenderState/RenderState.h"
#include "Engine/Renderer/Pipeline/RenderPassKindInfo.h"

using namespace sw;

/** @brief [MaterialRenderStateTest] 머티리얼에 내주는 범위는 패스 종류의 표가 정한다 */
SW_TEST_CASE( MaterialRenderStateTest, ScopeComesFromThePassTable )
{
    using Scope = RenderStateOverride::Scope;
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::Transparent ).getMaterialStateScope() == Scope::FixedAndBlend );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::ForwardOpaque ).getMaterialStateScope() == Scope::FixedAndBlend );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::ForwardOpaqueNoDepthWrite ).getMaterialStateScope() == Scope::FixedAndBlend );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::ForwardOnlyOpaque ).getMaterialStateScope() == Scope::FixedAndBlend );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::GBuffer ).getMaterialStateScope() == Scope::FixedState );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::Shadow ).getMaterialStateScope() == Scope::CullOnly );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::DepthPrepass ).getMaterialStateScope() == Scope::CullOnly );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::MeshOutline ).getMaterialStateScope() == Scope::CullOnly );
}
