#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

// 패스 밖의 일(전이 · 복사 · 디스패치)이 열린 렌더 패스를 닫으면 그 패스의 남은 드로우가 빠진다(G버퍼가 패스를 연 뒤 입력을 걸어 장면이 사라졌다).
// 기반 커맨드 컨텍스트가 그 자리에서 알리고, 패스를 먼저 닫은 정상 순서는 알리지 않는다.

using namespace sw;

/** @brief [RHIRenderPassInterruptTest] 열린 패스 안의 전이는 `RHI.RenderPassInterrupted` 로 알리고 패스를 닫는다 — 그 뒤 드로우는 나가지 않는다. 변이: 알림을 빼면 기대한 로그가 없어 진다. */
SW_TEST_CASE( RHIRenderPassInterruptTest, TransitionInsideOpenPassIsReported )
{
    test::FakeRHIDevice         device;
    test::FakeRHICommandContext context{ device };
    SW_EXPECT_LOG( "RHI.RenderPassInterrupted" );
    SW_EXPECT_LOG( "RHI.DrawSkippedNoRenderPass" );

    context.beginRenderPass( RHIRenderPassBeginInfo{} );
    SW_REQUIRE_TRUE( context.isRenderPassOpen() );
    context.prepareTextureForShaderRead( 1 );
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

    context.prepareTextureForShaderRead( 1 ); // 입력은 패스 전에 건다
    context.beginRenderPass( RHIRenderPassBeginInfo{} );
    context.draw( 3 );
    context.endRenderPass();
    context.dispatchCompute( 1, 1, 1 );
    context.beginRenderPass( RHIRenderPassBeginInfo{} );
    context.endRenderPass();
    SW_EXPECT_EQUAL( 1u, context._drawNativeCount );
    SW_EXPECT_EQUAL( 2u, context._endNativeCount );
}
