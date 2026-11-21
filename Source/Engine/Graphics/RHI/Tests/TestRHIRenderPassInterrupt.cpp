#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

// 패스 밖의 일(전이 · 복사 · 디스패치)이 열린 렌더 패스를 닫으면 그 패스의 남은 드로우가 빠진다(G버퍼가 패스를 연 뒤 입력을 걸어 장면이 사라졌다).
// 기반 커맨드 컨텍스트가 그 자리에서 알리고, 패스를 먼저 닫은 정상 순서는 알리지 않는다.

using namespace sw;

namespace
{
    /** @brief 백버퍼 컬러 타깃 하나의 패스입니다(컬러 타깃 0 은 무효라 기본값으로는 패스가 열리지 않는다). */
    RHIRenderPassBeginInfo backBufferPass()
    {
        RHIRenderPassBeginInfo beginInfo{};
        beginInfo.setColorTarget( kRHIBackBufferTexture );
        return beginInfo;
    }
} // namespace

/** @brief [RHIRenderPassInterruptTest] 열린 패스 안의 전이는 `RHI.RenderPassInterrupted` 로 알리고 패스를 닫는다 — 그 뒤 드로우는 나가지 않는다. 변이: 알림을 빼면 기대한 로그가 없어 진다. */
SW_TEST_CASE( RHIRenderPassInterruptTest, TransitionInsideOpenPassIsReported )
{
    test::FakeRHIDevice         device;
    test::FakeRHICommandContext context{ device };
    SW_EXPECT_LOG( "RHI.RenderPassInterrupted" );
    SW_EXPECT_LOG( "RHI.DrawSkippedNoRenderPass" );

    context.beginRenderPass( backBufferPass() );
    SW_REQUIRE_TRUE( context.isRenderPassOpen() );
    context.prepareTextureForShaderRead( RHITextureHandle{ 1 } );
    SW_EXPECT_FALSE( context.isRenderPassOpen() ); // 닫기는 그대로 한다
    SW_EXPECT_EQUAL( 1u, context._endNativeCount );
    context.draw( 3 );
    SW_EXPECT_EQUAL( 0u, context._drawNativeCount );
}

/** @brief [RHIRenderPassInterruptTest] 패스를 닫은 뒤의 전이 · 디스패치, 닫힌 상태의 새 패스는 알리지 않는다(케이스 밖의 오류 로그는 테스트를 진다). */
SW_TEST_CASE( RHIRenderPassInterruptTest, TransitionAfterEndRenderPassIsSilent )
{
    test::FakeRHIDevice         device;
    test::FakeRHICommandContext context{ device };

    context.prepareTextureForShaderRead( RHITextureHandle{ 1 } ); // 입력은 패스 전에 건다
    context.beginRenderPass( backBufferPass() );
    context.draw( 3 );
    context.endRenderPass();
    context.dispatchCompute( 1, 1, 1 );
    context.beginRenderPass( backBufferPass() );
    context.endRenderPass();
    SW_EXPECT_EQUAL( 1u, context._drawNativeCount );
    SW_EXPECT_EQUAL( 2u, context._endNativeCount );
}

/** @brief [RHIRenderPassInterruptTest] 컬러 타깃 핸들 0 인 패스는 열지 않고 `RHI.NullTextureHandle` 로 알린다 — 백엔드까지 가지 않고, 그 뒤 드로우도 버린다. 변이: 검사를 빼면 패스가 열려 진다. */
SW_TEST_CASE( RHIRenderPassInterruptTest, NullColorTargetIsRejected )
{
    test::FakeRHIDevice         device;
    test::FakeRHICommandContext context{ device };
    SW_EXPECT_LOG( "RHI.NullTextureHandle" );
    SW_EXPECT_LOG( "RHI.DrawSkippedNoRenderPass" );

    context.beginRenderPass( RHIRenderPassBeginInfo{} ); // 컬러 수 0 + 컬러 걸기 = 0 번 하나(핸들 0)
    SW_EXPECT_FALSE( context.isRenderPassOpen() );
    SW_EXPECT_EQUAL( 0u, context._beginNativeCount );
    context.draw( 3 );
    SW_EXPECT_EQUAL( 0u, context._drawNativeCount );
}

/** @brief [RHIRenderPassInterruptTest] 깊이만 거는 패스(컬러 걸기 끔)는 컬러 핸들을 보지 않는다 — 깊이 0 은 "깊이 없음" 이라 오류가 아니다. 변이: 컬러 걸기를 무시하고 검사하면 진다. */
SW_TEST_CASE( RHIRenderPassInterruptTest, DepthOnlyPassIgnoresColorHandles )
{
    test::FakeRHIDevice         device;
    test::FakeRHICommandContext context{ device };

    RHIRenderPassBeginInfo beginInfo{};
    beginInfo._bBindColor  = SW_FALSE;
    beginInfo._depthTarget = RHITextureHandle{ 7 };
    context.beginRenderPass( beginInfo );
    SW_EXPECT_TRUE( context.isRenderPassOpen() );
    SW_EXPECT_EQUAL( 1u, context._beginNativeCount );
}

/** @brief [RHIRenderPassInterruptTest] 전이 · 블릿의 핸들 0 은 알리고 그 일만 버린다 — 묶음의 다른 원소는 원래 차례대로 백엔드에 닿는다. 변이: 0 원소를 넘기거나 묶음째 버리면 진다. */
SW_TEST_CASE( RHIRenderPassInterruptTest, NullTextureTransitionAndBlitAreDropped )
{
    test::FakeRHIDevice         device;
    test::FakeRHICommandContext context{ device };
    SW_EXPECT_LOG( "RHI.NullTextureHandle" );

    const RHITextureTransition arrTransition[] = {
        RHITextureTransition{RHITextureHandle{ 3 }, RHITextureState::ShaderResource},
        RHITextureTransition{                    0,   RHITextureState::RenderTarget},
        RHITextureTransition{RHITextureHandle{ 5 },   RHITextureState::RenderTarget},
        RHITextureTransition{kRHIBackBufferTexture,   RHITextureState::RenderTarget},
    };
    context.transitionTextures( arrTransition, 4 );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( context._listTransitionTexture.size() ) );
    SW_EXPECT_EQUAL( RHITextureHandle{ 3 }, context._listTransitionTexture[0] );
    SW_EXPECT_EQUAL( RHITextureHandle{ 5 }, context._listTransitionTexture[1] );
    SW_EXPECT_EQUAL( kRHIBackBufferTexture, context._listTransitionTexture[2] );

    context.prepareTextureForShaderRead( 0 );
    context.blitTexture( 0, kRHIBackBufferTexture );
    context.blitTexture( RHITextureHandle{ 4 }, kRHIBackBufferTexture );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( context._listTransitionTexture.size() ) );
    SW_EXPECT_EQUAL( 1u, context._blitNativeCount );
}
