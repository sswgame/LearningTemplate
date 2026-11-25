/**
 * @file TestRHIRecordingBindings.cpp
 * @brief 공용 커맨드 컨텍스트가 지키는 바인딩 규약(뷰포트의 가위 · PSO 의 슬롯 비움 · 루트 상수 넘침 · 슬롯 범위) 테스트 — 다섯 백엔드가 같은 코드를 지난다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

using namespace sw;

namespace
{
    /** @brief 백버퍼 컬러 타깃 하나의 패스입니다. */
    RHIRenderPassBeginInfo makeBackBufferPass()
    {
        return RHIRenderPassBeginInfo::backBuffer( nullptr, RHIRenderPassLoadOp::Clear );
    }

    /** @brief 마지막으로 낸 뒤 바뀐 칸들입니다(칸 번호, 번호 — 뗀 칸은 무효). */
    vector<pair<uint32, RHIDescriptorIndex>> takeChanges( RHIShaderSlots& slots, RHIShaderSlotKind kind )
    {
        vector<pair<uint32, RHIDescriptorIndex>> listChange;
        slots.flush( kind, [&listChange]( uint32 slot, RHIDescriptorIndex index )
        { listChange.emplace_back( slot, index ); } );
        return listChange;
    }
} // namespace

/** @brief [RHIRecordingBindingsTest] 뷰포트를 걸면 가위가 그 뷰포트로 돌아가고 새 패스는 패스 전체다. 변이: `setViewport` 가 가위를 다시 걸지 않으면 앞 가위가 남아 진다(WebGPU 가 그랬다). */
SW_TEST_CASE( RHIRecordingBindingsTest, ViewportResetsTheScissor )
{
    test::FakeRHIDevice         device;
    test::FakeRHICommandContext context{ device };

    context.beginRenderPass( makeBackBufferPass() );
    SW_EXPECT_EQUAL( 64u, context._scissor._width );
    SW_EXPECT_EQUAL( 32u, context._scissor._height );
    context.setScissorRect( RHIScissorRect{ 8, 8, 4, 4 } );
    SW_EXPECT_EQUAL( 4u, context._scissor._width );

    RHIViewport viewport{};
    viewport._x      = 16.0f;
    viewport._y      = 4.0f;
    viewport._width  = 32.0f;
    viewport._height = 16.0f;
    context.setViewport( viewport );
    SW_EXPECT_EQUAL( 16u, context._scissor._x );
    SW_EXPECT_EQUAL( 4u, context._scissor._y );
    SW_EXPECT_EQUAL( 32u, context._scissor._width );
    SW_EXPECT_EQUAL( 16u, context._scissor._height );

    context.setScissorRect( RHIScissorRect{ 60, 30, 100, 100 } ); // 패스 밖은 자른다
    SW_EXPECT_EQUAL( 4u, context._scissor._width );
    SW_EXPECT_EQUAL( 2u, context._scissor._height );

    context.endRenderPass();
    context.beginRenderPass( makeBackBufferPass() );
    SW_EXPECT_EQUAL( 0u, context._scissor._x );
    SW_EXPECT_EQUAL( 64u, context._scissor._width );
    SW_EXPECT_EQUAL( 64.0f, context._viewport._width );
}

/** @brief [RHIRecordingBindingsTest] 서술이 타깃보다 크거나 비어 있어도 패스는 타깃 안이다. */
SW_TEST_CASE( RHIRecordingBindingsTest, PassExtentStaysInsideTheTarget )
{
    test::FakeRHIDevice         device;
    test::FakeRHICommandContext context{ device };

    context.beginRenderPass( makeBackBufferPass().setExtent( 40, 400 ) );
    SW_EXPECT_EQUAL( 40.0f, context._viewport._width );
    SW_EXPECT_EQUAL( 32.0f, context._viewport._height );
}

/** @brief [RHIRecordingBindingsTest] 다른 그래픽스 PSO 를 걸면 셰이더 슬롯 · 정점 슬롯이 비고(백엔드가 뗄 칸으로 온다), 같은 PSO 는 그대로 둔다. 변이: 비우기를 빼면 앞 PSO 의 번호가 남아 진다. */
SW_TEST_CASE( RHIRecordingBindingsTest, AnotherPipelineClearsTheSlots )
{
    test::FakeRHIDevice         device;
    test::FakeRHICommandContext context{ device };
    RHIShaderSlots&             slots = context.getBindings().getSlots( RHIBindPoint::Graphics );

    context.beginRenderPass( makeBackBufferPass() );
    context.setPipelineState( RHIPipelineStateHandle{ 10 } );
    context.bindStructuredBuffer( RHIBufferSRVIndex{ 7 }, 4 );
    context.bindShaderResource( RHITextureSRVIndex{ 9 }, shaderslot::kMaterialTexture0 );
    context.bindConstantBuffer( RHIBufferSRVIndex{ 3 }, 0 );
    context.setVertexBuffer( 0, RHIBufferHandle{ 5 }, 12 );
    SW_EXPECT_EQUAL( size_t{ 2 }, takeChanges( slots, RHIShaderSlotKind::ShaderResource ).size() );

    context.setPipelineState( RHIPipelineStateHandle{ 10 } );
    SW_EXPECT_TRUE( slots.isBound( RHIShaderSlotKind::ShaderResource, 4 ) );
    SW_EXPECT_FALSE( slots.isDirty( RHIShaderSlotKind::ShaderResource ) );
    SW_EXPECT_TRUE( context.getBindings()._geometry._arrVertexSlot[0]._buffer == RHIBufferHandle{ 5 } );

    context.setPipelineState( RHIPipelineStateHandle{ 11 } );
    SW_EXPECT_FALSE( slots.isBound( RHIShaderSlotKind::ShaderResource, 4 ) );
    SW_EXPECT_FALSE( slots.isBound( RHIShaderSlotKind::ConstantBuffer, 0 ) );
    SW_EXPECT_TRUE( context.getBindings()._geometry._arrVertexSlot[0]._buffer == 0 );
    const vector<pair<uint32, RHIDescriptorIndex>> listChange = takeChanges( slots, RHIShaderSlotKind::ShaderResource );
    SW_REQUIRE_TRUE( listChange.size() == 2 );
    SW_EXPECT_TRUE( listChange[0].second == kInvalidDescriptorIndex );
    SW_EXPECT_TRUE( listChange[1].second == kInvalidDescriptorIndex );
}

/** @brief [RHIRecordingBindingsTest] 렌더 패스를 열면 그래픽스 PSO · 슬롯이 비고 컴퓨트 쪽은 그대로다. */
SW_TEST_CASE( RHIRecordingBindingsTest, OpeningAPassResetsGraphicsOnly )
{
    test::FakeRHIDevice        device;
    test::FakeRHIBufferFactory factory;
    device._pResourceFactory = &factory;
    test::FakeRHICommandContext context{ device };

    context.setComputePipelineState( RHIPipelineStateHandle{ 20 } );
    context.bindComputeUAV( RHIUnorderedAccessIndex{ 2 }, 0 );
    context.beginRenderPass( makeBackBufferPass() );
    context.setPipelineState( RHIPipelineStateHandle{ 10 } );
    context.bindStructuredBuffer( RHIBufferSRVIndex{ 7 }, 4 );
    context.endRenderPass();
    context.beginRenderPass( makeBackBufferPass() );
    SW_EXPECT_TRUE( context.getBindings()._graphicsPSO == 0 );
    SW_EXPECT_FALSE( context.getBindings().getSlots( RHIBindPoint::Graphics ).isBound( RHIShaderSlotKind::ShaderResource, 4 ) );
    SW_EXPECT_TRUE( context.getBindings()._computePSO == RHIPipelineStateHandle{ 20 } );
    SW_EXPECT_TRUE( context.getBindings().getSlots( RHIBindPoint::Compute ).isBound( RHIShaderSlotKind::UnorderedAccess, 0 ) );
}

/** @brief [RHIRecordingBindingsTest] 루트 상수가 블록을 넘으면 알리고 한 칸도 쓰지 않는다(자르지 않는다). 변이: 자르면 앞 칸이 바뀌어 진다. */
SW_TEST_CASE( RHIRecordingBindingsTest, RootConstantOverflowIsRejected )
{
    test::FakeRHIDevice         device;
    test::FakeRHICommandContext context{ device };
    uint32                      arrValue[shaderslot::kRootConstantDwords + 1u]{};
    for ( uint32& value : arrValue )
    {
        value = 0x5Au;
    }
    RHIRootConstants& constants = context.getBindings().getRootConstants( RHIBindPoint::Graphics );

    context.setGraphicsRootConstants( shaderslot::kRootConstantDwords, arrValue ); // 꽉 찬 블록은 된다
    SW_EXPECT_EQUAL( 0x5Au, constants._arrDword[0] );
    constants.markFlushed();
    arrValue[0] = 1u;
    SW_EXPECT_LOG( "RHI.RootConstantsOverflow" );
    context.setGraphicsRootConstants( shaderslot::kRootConstantDwords + 1u, arrValue );
    context.setGraphicsRootConstants( 2, arrValue, shaderslot::kRootConstantDwords - 1u );
    context.setGraphicsRootConstants( 1, arrValue, 0xFFFFFFFFu );
    SW_EXPECT_EQUAL( 0x5Au, constants._arrDword[0] );
    SW_EXPECT_EQUAL( 0x5Au, constants._arrDword[shaderslot::kRootConstantDwords - 1u] );
    SW_EXPECT_FALSE( constants.isDirty() );
}

/** @brief [RHIRecordingBindingsTest] 컴퓨트와 그래픽스의 루트 상수는 따로이고 바뀐 구간만 낼 것으로 남는다. */
SW_TEST_CASE( RHIRecordingBindingsTest, RootConstantsArePerBindPoint )
{
    test::FakeRHIDevice         device;
    test::FakeRHICommandContext context{ device };
    RHIRootConstants&           graphics = context.getBindings().getRootConstants( RHIBindPoint::Graphics );
    RHIRootConstants&           compute  = context.getBindings().getRootConstants( RHIBindPoint::Compute );
    SW_EXPECT_TRUE( graphics.isDirty() ); // 새 스트림은 0 을 낸다
    graphics.markFlushed();
    compute.markFlushed();

    const uint32 arrValue[2]{ 3u, 4u };
    context.setComputeRootConstants( 2, arrValue, 5 );
    SW_EXPECT_FALSE( graphics.isDirty() );
    SW_EXPECT_EQUAL( 0u, graphics._arrDword[5] );
    SW_EXPECT_EQUAL( 5u, compute._dirtyBegin );
    SW_EXPECT_EQUAL( 7u, compute._dirtyEnd );
    SW_EXPECT_EQUAL( 4u, compute._arrDword[6] );
}

/** @brief [RHIRecordingBindingsTest] 범위 밖 슬롯 · 종류에 맞지 않는 슬롯(텍스처를 버퍼 칸에)은 알리고 버린다. 변이: 검사를 빼면 표 밖을 쓴다. */
SW_TEST_CASE( RHIRecordingBindingsTest, SlotOutsideTheRangeIsRejected )
{
    test::FakeRHIDevice         device;
    test::FakeRHICommandContext context{ device };
    SW_EXPECT_LOG( "RHI.ShaderSlotOutOfRange" );
    context.bindConstantBuffer( RHIBufferSRVIndex{ 1 }, RHIShaderSlots::kConstantBufferCount );
    context.bindStructuredBuffer( RHIBufferSRVIndex{ 1 }, RHIShaderSlots::kBufferResourceCount );
    context.bindShaderResource( RHITextureSRVIndex{ 1 }, 0 );
    context.bindComputeUAV( RHIUnorderedAccessIndex{ 1 }, RHIShaderSlots::kUnorderedAccessCount );
    for ( const RHIShaderSlots& slots : context.getBindings()._arrSlots )
    {
        for ( const uint64 mask : slots._arrBoundMask )
        {
            SW_EXPECT_EQUAL( uint64{ 0 }, mask );
        }
    }
}

/** @brief [RHIRecordingBindingsTest] 무효 번호는 칸을 뗀다 — 앞 번호가 남지 않는다. */
SW_TEST_CASE( RHIRecordingBindingsTest, InvalidIndexUnbindsTheSlot )
{
    test::FakeRHIDevice         device;
    test::FakeRHICommandContext context{ device };
    RHIShaderSlots&             slots = context.getBindings().getSlots( RHIBindPoint::Graphics );
    context.bindStructuredBuffer( RHIBufferSRVIndex{ 7 }, 4 );
    (void)takeChanges( slots, RHIShaderSlotKind::ShaderResource );
    context.bindStructuredBuffer( kInvalidDescriptorIndex, 4 );
    SW_EXPECT_FALSE( slots.isBound( RHIShaderSlotKind::ShaderResource, 4 ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, takeChanges( slots, RHIShaderSlotKind::ShaderResource ).size() );
}
