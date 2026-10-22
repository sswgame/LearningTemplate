#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"
#include "Engine/Resource/ResourcePaths.h"

#include "TestFramework/Harness/TestHostRuntime.h"
#include "TestFramework/TestFramework.h"

// 셰이더 도메인 발견의 컴파일 쪽 — 임시 게임 도메인의 셰이더가 머티리얼과 같은 경로 풀이(`findLooseFile`)로 DXC 컴파일 · 리플렉션까지 가는지 본다.
// 컴파일러가 필요해 호스트 전용이다. 컴파일러 없는 판정은 TestShaderDomain.cpp 가 한다.

SW_TEST_REQUIRES_HOST( ShaderDomainCompileTest, "compiles HLSL with DXC, which CI runners do not ship" );

namespace
{
    /** @brief 컴파일러 자체가 없어 건너뛸 환경이면 true 입니다(셰이더 결함이 아니라 환경). */
    bool isCompilerMissing( const sw::ShaderCompileResult& result )
    {
        return result._bSuccess == false && ( result._errorMessage.find( "dxcompiler" ) != sw::string::npos || result._errorMessage.find( "DXC and D3DCompiler" ) != sw::string::npos );
    }

    sw::ShaderCompileDesc makeProbeDesc()
    {
        sw::ShaderCompileDesc desc{};
        desc._filePath     = "game/domaintest/shaders/probe.hlsl"; // 머티리얼 XML 의 shaderPath 와 같은 리소스 id
        desc._entryPoint   = "PSMain";
        desc._stage        = sw::ShaderStage::Pixel;
        desc._targetFormat = sw::ShaderTargetFormat::DXIL_D3D12;
        return desc;
    }
} // namespace

/**
 * @brief [ShaderDomainCompileTest] 임의의 게임 도메인 셰이더가 engine 헤더를 include 해 컴파일되고 리플렉션까지 된다
 */
SW_TEST_CASE( ShaderDomainCompileTest, ArbitraryDomainShaderCompilesAndReflects )
{
    const test::ScopedTempResourceRoot resourceRoot;
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/domaininc.hlsli", "static const float kDomainScale = 0.5;\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "game/domaintest/shaders/probe.hlsl",
                                                "#include \"domaininc.hlsli\"\n"
                                                "cbuffer ProbeCB : register(b0) { float4 gTint; };\n"
                                                "float4 PSMain() : SV_Target { return gTint * kDomainScale; }\n" ) );
    SW_ASSERT_FALSE( sw::ResourcePaths::findLooseFile( "game/domaintest/shaders/probe.hlsl" ).getDiskPath().empty() ); // 머티리얼과 같은 경로 풀이

    const sw::ShaderCompileResult result = sw::ShaderCompiler::compileHlsl( makeProbeDesc() );
    if ( isCompilerMissing( result ) )
        SW_TEST_SKIP( "No DXC compiler in this environment" );
    SW_ASSERT_TRUE_MSG( result._bSuccess, result._errorMessage.c_str() );
    SW_ASSERT_FALSE( result._bytecode.empty() );

    const sw::ShaderReflectionData reflection = sw::ShaderReflection::reflect( result._bytecode, sw::ShaderTargetFormat::DXIL_D3D12 );
    SW_EXPECT_EQUAL( size_t( 1 ), reflection._listConstantBuffer.size() );
}

/**
 * @brief [ShaderDomainCompileTest] 게임 폴더의 같은 이름 헤더가 engine 헤더를 가리면 컴파일이 오류로 멈춘다(조용히 게임 쪽을 쓰지 않는다)
 */
SW_TEST_CASE( ShaderDomainCompileTest, SameNameHeaderIsACompileError )
{
    test::ScopedLogSuppressor suppressor;

    const test::ScopedTempResourceRoot resourceRoot;
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/domaininc.hlsli", "static const float kDomainScale = 0.5;\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "game/domaintest/shaders/domaininc.hlsli", "static const float kDomainScale = 2.0;\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "game/domaintest/shaders/probe.hlsl", "#include \"domaininc.hlsli\"\nfloat4 PSMain() : SV_Target { return kDomainScale; }\n" ) );

    const sw::ShaderCompileResult result = sw::ShaderCompiler::compileHlsl( makeProbeDesc() );
    SW_EXPECT_FALSE( result._bSuccess );
    SW_EXPECT_TRUE( result._errorMessage.find( "would hide" ) != sw::string::npos );
}
