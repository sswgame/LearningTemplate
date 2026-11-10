#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RenderState/RenderState.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"

using namespace sw;

/** @brief [MaterialRenderStateTest] 머티리얼에 내주는 범위는 패스 종류의 표가 정한다 */
SW_TEST_CASE( MaterialRenderStateTest, ScopeComesFromThePassTable )
{
    using Scope = RenderStateOverride::Scope;
    SW_EXPECT_TRUE( FrameRendererUtil::getMaterialStateScope( RenderPassType::Transparent ) == Scope::FixedAndBlend );
    SW_EXPECT_TRUE( FrameRendererUtil::getMaterialStateScope( RenderPassType::ForwardOpaque ) == Scope::FixedAndBlend );
    SW_EXPECT_TRUE( FrameRendererUtil::getMaterialStateScope( RenderPassType::ForwardOpaqueNoDepthWrite ) == Scope::FixedAndBlend );
    SW_EXPECT_TRUE( FrameRendererUtil::getMaterialStateScope( RenderPassType::ForwardOnlyOpaque ) == Scope::FixedAndBlend );
    SW_EXPECT_TRUE( FrameRendererUtil::getMaterialStateScope( RenderPassType::GBuffer ) == Scope::FixedState );
    SW_EXPECT_TRUE( FrameRendererUtil::getMaterialStateScope( RenderPassType::Shadow ) == Scope::CullOnly );
    SW_EXPECT_TRUE( FrameRendererUtil::getMaterialStateScope( RenderPassType::DepthPrepass ) == Scope::CullOnly );
    SW_EXPECT_TRUE( FrameRendererUtil::getMaterialStateScope( RenderPassType::MeshOutline ) == Scope::CullOnly );
}
