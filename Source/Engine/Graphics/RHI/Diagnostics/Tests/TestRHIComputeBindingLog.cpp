/**
 * @file TestRHIComputeBindingLog.cpp
 * @brief 컴퓨트 바인딩 기록(`RHIComputeBindingLog`) 테스트 — 디스패치 순간의 PSO 로 적고, 앞 디스패치가 건 번호는 다음 PSO 의 것이 되지 않는다.
 */
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIComputeBindingLog.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

using namespace sw;

/** @brief [RHIComputeBindingLogTest] 디스패치 순간의 PSO 로 적고, 다음 PSO 의 디스패치는 새로 건 번호만 적는다. 변이: PSO 를 바꿀 때 컴퓨트 슬롯을 비우지 않으면 앞 번호가 둘째 PSO 로 바뀐다. */
SW_TEST_CASE( RHIComputeBindingLogTest, DispatchRecordsTheBindingsMadeForItsPipeline )
{
    test::FakeRHIDevice        device;
    test::FakeRHIBufferFactory factory;
    device._pResourceFactory = &factory;
    test::FakeRHICommandContext context{ device };
    RHIComputeBindingLog&       log = device.getComputeBindingLog();
    log.setEnabled( true );

    context.setComputePipelineState( RHIPipelineStateHandle{ 100 } );
    context.bindComputeShaderResource( RHIBufferSRVIndex{ 11 }, 0 );
    context.bindComputeUAV( RHIUnorderedAccessIndex{ 11 }, 2 );
    context.dispatchCompute( 1, 1, 1 );
    context.setComputePipelineState( RHIPipelineStateHandle{ 200 } );
    context.bindComputeShaderResource( RHIBufferSRVIndex{ 12 }, 1 );
    context.dispatchCompute( 1, 1, 1 );

    const optional<RHIComputeBindingLog::Binding> srv = log.find( RHIDescriptorIndex{ 11 }, false );
    SW_REQUIRE_TRUE( srv.has_value() );
    SW_EXPECT_TRUE( srv->_pso == RHIPipelineStateHandle{ 100 } );
    SW_EXPECT_EQUAL( 0u, srv->_slot );
    const optional<RHIComputeBindingLog::Binding> uav = log.find( RHIDescriptorIndex{ 11 }, true );
    SW_REQUIRE_TRUE( uav.has_value() );
    SW_EXPECT_EQUAL( 2u, uav->_slot );
    SW_EXPECT_TRUE( log.find( RHIDescriptorIndex{ 12 }, false )->_pso == RHIPipelineStateHandle{ 200 } );

    log.setEnabled( false );
    SW_EXPECT_FALSE( log.find( RHIDescriptorIndex{ 11 }, false ).has_value() );
}
