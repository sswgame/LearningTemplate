/**
 * @file TestComputeShaderParameters.cpp
 * @brief 컴퓨트 셰이더 매개변수 구조체 테스트 — PSO 를 만들 때 리플렉션 이름을 레지스터로 풀고, 기록은 풀어 둔 번호로만 건다. 셰이더가 선언한 자원을 표가 모르면 그 PSO 를 쓰지 않는다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIComputeBindingLog.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Graphics/Shader/Binding/ComputeShaderParameters.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"

using namespace sw;

namespace
{
    /** @brief 시험용 셰이더 형식 — 읽기 버퍼 둘 · 쓰기 하나 · 이 변형이 읽지 않는 이름 하나. */
    struct TestComputeShader
    {
        enum Parameter : uint8
        {
            Input,
            Extra,
            Output,
            Unused,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            { Input,            "g_Input", ComputeShaderParameterKind::Buffer},
            { Extra,            "g_Extra", ComputeShaderParameterKind::Buffer},
            {Output,         "g_OutputRW",    ComputeShaderParameterKind::UAV},
            {Unused, "g_NotInThisVariant",    ComputeShaderParameterKind::UAV},
        };
    };

    struct ComputeShaderParametersTestUtil
    {
        static void addResource( ShaderReflectionData& reflection, const utf8* pName, ShaderBindingKind kind, uint32 bindPoint, uint32 space = 0 )
        {
            ShaderReflectedBinding resource{};
            resource._name          = pName;
            resource._kind          = kind;
            resource._registerSpace = space;
            resource._bindPoint     = bindPoint;
            resource._bindCount     = 1;
            reflection._listResource.push_back( resource );
        }

        /** @brief 셰이더가 g_Input(t3) · g_Extra(t1) · g_OutputRW(u2) 와 bindless 배열(space 1)을 선언한 레이아웃입니다. */
        static ShaderBindingLayout makeLayout( bool bUndeclaredBuffer )
        {
            ShaderReflectionData reflection{};
            addResource( reflection, "g_Input", ShaderBindingKind::StructuredBuffer, 3 );
            addResource( reflection, "g_Extra", ShaderBindingKind::StructuredBuffer, 1 );
            addResource( reflection, "g_OutputRW", ShaderBindingKind::RwStructuredBuffer, 2 );
            addResource( reflection, "g_SwBindlessRWTex2D", ShaderBindingKind::RwTexture, 0, 1 ); // 번호로 고르는 표 — 이름으로 걸지 않는다
            if ( bUndeclaredBuffer )
                addResource( reflection, "g_Forgotten", ShaderBindingKind::StructuredBuffer, 0 );
            vector<pair<ShaderStage, const ShaderReflectionData*>> listStage;
            listStage.push_back( { ShaderStage::Compute, &reflection } );
            return ShaderBindingLayout::make( listStage );
        }
    };
} // namespace

/** @brief [ComputeShaderParametersTest] 이름을 리플렉션 레지스터로 풀어 그 번호에 건다 — 표의 순서와 레지스터 순서가 달라도. 변이: 서수를 번호로 쓰면 칸이 어긋난다. */
SW_TEST_CASE( ComputeShaderParametersTest, BindsByReflectedRegister )
{
    using Util                       = ComputeShaderParametersTestUtil;
    const ShaderBindingLayout layout = Util::makeLayout( false );
    ComputePipeline           pipeline{};
    pipeline._pso = RHIPipelineStateHandle{ 40 };
    SW_REQUIRE_TRUE( ComputeShaderBindings::resolve( layout, TestComputeShader::kParameters, "test.hlsl", pipeline._bindings ) );

    test::FakeRHIDevice        device;
    test::FakeRHIBufferFactory factory;
    device._pResourceFactory = &factory;
    test::FakeRHICommandContext context{ device };
    RHIComputeBindingLog&       log = device.getComputeBindingLog();
    log.setEnabled( true );

    ComputeShaderParameters<TestComputeShader> parameters;
    parameters.set( TestComputeShader::Input, RHIBufferSRVIndex{ 21 } );
    parameters.set( TestComputeShader::Output, RHIUnorderedAccessIndex{ 22 } );
    parameters.set( TestComputeShader::Unused, RHIUnorderedAccessIndex{ 23 } ); // 이 변형의 셰이더가 읽지 않는다 — 걸지 않는다
    SW_REQUIRE_TRUE( parameters.apply( context, pipeline ) );
    context.dispatchCompute( 1, 1, 1 );

    const optional<RHIComputeBindingLog::Binding> input = log.find( RHIDescriptorIndex{ 21 }, false );
    SW_REQUIRE_TRUE( input.has_value() );
    SW_EXPECT_TRUE( input->_pso == RHIPipelineStateHandle{ 40 } );
    SW_EXPECT_EQUAL( 3u, input->_slot );
    const optional<RHIComputeBindingLog::Binding> output = log.find( RHIDescriptorIndex{ 22 }, true );
    SW_REQUIRE_TRUE( output.has_value() );
    SW_EXPECT_EQUAL( 2u, output->_slot );
    SW_EXPECT_FALSE( log.find( RHIDescriptorIndex{ 23 }, true ).has_value() );
    log.setEnabled( false );
}

/** @brief [ComputeShaderParametersTest] 셰이더가 선언한 버퍼를 표가 모르면 풀지 않는다 — 그 PSO 는 쓰지 않는다. 변이: 모르는 이름을 건너뛰면 걸지 않은 칸을 읽는 디스패치가 생긴다. */
SW_TEST_CASE( ComputeShaderParametersTest, UndeclaredShaderResourceFailsResolve )
{
    using Util                       = ComputeShaderParametersTestUtil;
    const ShaderBindingLayout layout = Util::makeLayout( true );
    ComputeShaderBindings     bindings;
    SW_EXPECT_LOG( "ComputeShaderBindings.UndeclaredParameter" );
    SW_EXPECT_FALSE( ComputeShaderBindings::resolve( layout, TestComputeShader::kParameters, "test.hlsl", bindings ) );
    SW_EXPECT_FALSE( bindings.isResolvedFor( TestComputeShader::kParameters ) );
}

/**
 * @brief [ComputeShaderParametersTest] 공용 도우미가 선언한 에뮬 RW 칸(g_SwRWSlot1 · 2)은 표에 없어도 푼다 — 번호로 고르는 칸이라 패스는 쓰는 칸만 적는다
 * @details vfx_simulate 가 칸 0 만 적었는데 도우미가 셋을 모두 선언해 DX11 · GL 에서 PSO 를 버렸다. 변이: 이름 접두를 보지 않으면 이 케이스가 풀지 못한다.
 */
SW_TEST_CASE( ComputeShaderParametersTest, EmulatedRwSlotsOutsideTheTableResolve )
{
    using Util = ComputeShaderParametersTestUtil;
    ShaderReflectionData reflection{};
    Util::addResource( reflection, "g_Input", ShaderBindingKind::StructuredBuffer, 3 );
    Util::addResource( reflection, "g_Extra", ShaderBindingKind::StructuredBuffer, 1 );
    Util::addResource( reflection, "g_OutputRW", ShaderBindingKind::RwStructuredBuffer, 2 );
    Util::addResource( reflection, "g_SwRWSlot1", ShaderBindingKind::RwTexture, 6 );
    Util::addResource( reflection, "g_SwRWSlot2", ShaderBindingKind::RwTexture, 7 );
    vector<pair<ShaderStage, const ShaderReflectionData*>> listStage;
    listStage.push_back( { ShaderStage::Compute, &reflection } );
    const ShaderBindingLayout layout = ShaderBindingLayout::make( listStage );
    ComputeShaderBindings     bindings;
    SW_EXPECT_TRUE( ComputeShaderBindings::resolve( layout, TestComputeShader::kParameters, "test.hlsl", bindings ) );
    SW_EXPECT_TRUE( bindings.isResolvedFor( TestComputeShader::kParameters ) );
}

/** @brief [ComputeShaderParametersTest] 다른 셰이더 형식의 표로 푼 PSO 에는 걸지 않는다. 변이: 표를 대조하지 않으면 남의 서수로 칸을 고른다. */
SW_TEST_CASE( ComputeShaderParametersTest, PipelineResolvedForAnotherShaderIsNotApplied )
{
    test::FakeRHIDevice        device;
    test::FakeRHIBufferFactory factory;
    device._pResourceFactory = &factory;
    test::FakeRHICommandContext context{ device };
    ComputePipeline             pipeline{};
    pipeline._pso = RHIPipelineStateHandle{ 41 }; // 표로 풀지 않은 PSO
    SW_EXPECT_LOG( "ComputeShaderBindings.PipelineTableMismatch" );
    SW_EXPECT_FALSE( ComputeShaderParameters<TestComputeShader>{}.apply( context, pipeline ) );
}
