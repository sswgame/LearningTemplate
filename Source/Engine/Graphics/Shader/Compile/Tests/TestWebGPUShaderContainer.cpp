#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Shader/Compile/WebGPUShaderContainer.h"

using namespace sw;

// WebGPU 셰이더 꾸러미(WebGPUShaderContainer) — 손으로 짠 바이트와 WGSL 글로 본다. Tint · 장치 없음.

/**
 * @brief [WebGPUShaderContainerTest] 묶은 SPIR-V 와 WGSL 을 그대로 다시 찾고, 꾸러미가 아닌 바이트 · 잘린 꾸러미는 거절한다
 */
SW_TEST_CASE( WebGPUShaderContainerTest, PackRoundTripsAndRejectsTruncatedBytes )
{
    const uint8       arrSpirv[] = { 0x03, 0x02, 0x23, 0x07, 0x11, 0x22, 0x33, 0x44 };
    const string_view wgsl       = "@vertex fn VSMain() -> @builtin(position) vec4f { return vec4f(); }";
    vector<uint8>     container;
    WebGPUShaderContainer::pack( arrSpirv, wgsl, container );

    vector_reference<const uint8> spirv;
    string_view                   foundWgsl;
    SW_REQUIRE_TRUE( WebGPUShaderContainer::findSpirv( container, spirv ) );
    SW_REQUIRE_TRUE( WebGPUShaderContainer::findWgsl( container, foundWgsl ) );
    SW_EXPECT_EQUAL( sizeof( arrSpirv ), spirv.size() );
    SW_EXPECT_TRUE( spirv[0] == 0x03 && spirv[7] == 0x44 );
    SW_EXPECT_TRUE( foundWgsl == wgsl );

    vector<uint8> truncated( container.begin(), container.end() - 1 );
    SW_EXPECT_FALSE( WebGPUShaderContainer::findWgsl( truncated, foundWgsl ) );
    SW_EXPECT_FALSE( WebGPUShaderContainer::findSpirv( vector<uint8>( arrSpirv, arrSpirv + sizeof( arrSpirv ) ), spirv ) );
}

/**
 * @brief [WebGPUShaderContainerTest] 그룹 0 의 바인딩 번호만 오름차순 · 중복 없이 모으고, 다른 그룹과 깨진 속성은 건너뛴다
 */
SW_TEST_CASE( WebGPUShaderContainerTest, CollectBindingsReadsGroupZeroOnly )
{
    const string_view wgsl = "@group(0) @binding(68) var<storage, read> g_SwInstances : S;\n"
                             "@group( 0 ) @binding( 2 ) var<uniform> SwRootConstants : R;\n"
                             "@group(1) @binding(5) var other : texture_2d<f32>;\n"
                             "@group(0) @binding(137) var g_SwSampler0 : sampler;\n"
                             "@group(0) @binding(68) var<storage, read> again : S;\n"
                             "@group(0) @binding() var broken : sampler;\n";
    vector<uint32>    listBinding;
    WebGPUShaderContainer::collectBindings( wgsl, listBinding );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listBinding.size() );
    SW_EXPECT_EQUAL( 2u, listBinding[0] );
    SW_EXPECT_EQUAL( 68u, listBinding[1] );
    SW_EXPECT_EQUAL( 137u, listBinding[2] );
}
