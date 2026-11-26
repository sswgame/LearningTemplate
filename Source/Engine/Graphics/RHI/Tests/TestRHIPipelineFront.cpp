/**
 * @file TestRHIPipelineFront.cpp
 * @brief PSO 앞단(`IRHIResourceFactory::createPipelineState` · `RHIShaderRequest::buildGraphicsStages`)이 컴파일 전에 끊어야 할 것을 끊는가 — 묶지 않은 앞단, 기동 능력에 없는 선택 스테이지 —
 *        그리고 정점 속성 마스크를 리플렉션(바이트코드가 아니라)에서 읽는가입니다(nogpu · 셰이더 캐시 없음).
 */
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHICompiledShaderStages.h"
#include "Engine/Graphics/RHI/RHIPipelineState.h"
#include "Engine/Graphics/RHI/RHIPipelineStateDesc.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Graphics/Shader/Compile/RHIShaderRequest.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionData.h"

using namespace sw;

/**
 * @brief [RHIPipelineFrontTest] 장치가 앞단을 묶기 전의 PSO 생성은 오류이고 핸들 0 이다(셰이더 캐시를 부르기 전에 끊는다)
 * @details 변이: 묶임 검사를 빼면 셰이더 형식이 값 초기화(0)인 채로 컴파일을 부른다.
 */
SW_TEST_CASE( RHIPipelineFrontTest, UnboundFrontRejectsBeforeCompiling )
{
    SW_EXPECT_LOG( "RHI.PipelineFrontUnbound" );
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory = &factory;
    RHIPipelineStateDesc desc;
    desc._vertexShaderPath = "engine/shaders/forward_lit.hlsl";
    RHIPipelineState pso;
    SW_EXPECT_FALSE( pso.createGraphics( device, desc ) );
    SW_EXPECT_FALSE( pso.createCompute( device, "engine/shaders/cull.hlsl" ) );
}

/**
 * @brief [RHIPipelineFrontTest] 기동 능력에 지오메트리 · 테셀레이션이 없으면 컴파일하기 전에 거절한다(예전에는 WebGPU 와 Vulkan 이 각자 다른 차례로 거절했다)
 * @details 변이: 능력 검사를 빼면 셰이더 캐시를 불러(이 테스트에는 없다) 멈추거나 컴파일 실패 경고가 난다.
 */
SW_TEST_CASE( RHIPipelineFrontTest, OptionalStageOutsideTheCapabilitiesIsRejected )
{
    SW_EXPECT_LOG( "RHI.PipelineOptionalStageUnsupported" );
    RHIPipelineStateDesc desc;
    desc._vertexShaderPath   = "engine/shaders/forward_lit.hlsl";
    desc._geometryEntryPoint = "GSMain";
    RHICapabilities           capabilities{}; // 선택 스테이지 없음(WebGPU 와 같다)
    RHICompiledGraphicsStages stages;
    SW_EXPECT_FALSE( RHIShaderRequest::buildGraphicsStages( desc, ShaderTargetFormat::WGSL_WebGPU, capabilities, stages ) );
}

/**
 * @brief [RHIPipelineFrontTest] 정점 속성 마스크는 리플렉션의 시맨틱으로 정한다 — 입력이 없으면 0, POSITION 을 읽으면 위치 슬롯이 켜진다
 * @details 변이: 시맨틱 대조를 빼거나 리플렉션 대신 늘 전체 표를 내면 둘째 · 셋째 단언이 진다.
 */
SW_TEST_CASE( RHIPipelineFrontTest, ConsumedMaskComesFromTheReflection )
{
    ShaderReflectionData empty;
    SW_EXPECT_EQUAL( 0u, RHIShaderRequest::computeConsumedVertexAttributeMask( empty ) );

    ShaderReflectionData  positionOnly;
    ShaderVertexInputInfo input{};
    input._semantic = "POSITION";
    positionOnly._listVertexInput.push_back( input );
    const uint32 mask = RHIShaderRequest::computeConsumedVertexAttributeMask( positionOnly );
    SW_EXPECT_EQUAL( 1u, mask );
    SW_EXPECT_NOT_EQUAL( 0u, RHIShaderRequest::toVertexSlotMask( mask ) );
}
