#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"
#include "Engine/Renderer/Cook/ShaderCookDriver.h"
#include "Engine/Resource/ResourcePaths.h"

#include "EngineTest/ShaderCookTestUtil.h"

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

/**
 * @brief [ShaderDomainCompileTest] 요청의 퍼뮤테이션 해시가 바뀌어 다시 구우면 옛 바이너리는 쿠커가 지우고, 쿠킹 뒤 판정에 문제가 없다
 * @details 예전에는 옛 해시의 바이너리가 매니페스트 밖에 남아 `App --verify-shaders` 가 "매니페스트에 없는 바이너리" 로 졌고 통합이 손으로 지웠다.
 */
SW_TEST_CASE( ShaderDomainCompileTest, RecookRemovesBinariesOfAChangedPermutation )
{
    test::ScopedLogSuppressor suppressor;

    const test::ScopedTempResourceRoot resourceRoot;
    const sw::string&                  root = resourceRoot.getRootFolderPath();
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "game/domaintest/shaders/probe.hlsl", "float4 PSMain() : SV_Target { return 0; }\n" ) );
    const sw::string shaderPath = resourceRoot.makePath( "game/domaintest/shaders/probe.hlsl" );
    const sw::string binDir     = resourceRoot.makePath( "game/domaintest/shaders/bin/dx12" );

    sw::ShaderCookRequest request{};
    request._shaderPath          = shaderPath;
    request._entryPoint          = "PSMain";
    request._stage               = sw::ShaderStage::Pixel;
    request._listPermutation     = { "SW_PROBE_A" };
    request._permutationHash     = sw::ShaderCooker::computePermutationHash( request._listPermutation );
    const sw::string firstBinary = sw::FileUtil::joinPath( binDir, sw::ShaderCooker::computeBinaryFileName( "probe", request._stage, request._entryPoint, request._permutationHash, ".dxil" ) );

    const sw::ShaderCookSummary first = sw::ShaderCookDriver::cookRequests( root, test::makeShaderConfigPriority(), { request }, sw::ShaderTargetFormat::DXIL_D3D12, false );
    if ( first._failedCount > 0 && sw::FileUtil::exists( firstBinary ) == false )
        SW_TEST_SKIP( "No DXC compiler in this environment" );
    SW_ASSERT_TRUE( first.isClean() );
    SW_ASSERT_TRUE( sw::FileUtil::exists( firstBinary ) );

    // 퍼뮤테이션 정의가 바뀌었다 — 파일 이름의 해시가 달라지고 첫 바이너리는 어느 요청도 만들지 않는다.
    request._listPermutation      = { "SW_PROBE_B" };
    request._permutationHash      = sw::ShaderCooker::computePermutationHash( request._listPermutation );
    const sw::string secondBinary = sw::FileUtil::joinPath( binDir, sw::ShaderCooker::computeBinaryFileName( "probe", request._stage, request._entryPoint, request._permutationHash, ".dxil" ) );
    SW_ASSERT_TRUE( firstBinary != secondBinary );

    const sw::ShaderCookSummary second = sw::ShaderCookDriver::cookRequests( root, test::makeShaderConfigPriority(), { request }, sw::ShaderTargetFormat::DXIL_D3D12, false );
    SW_EXPECT_TRUE( second.isClean() );
    SW_EXPECT_EQUAL( 1u, second._removedCount );
    SW_EXPECT_TRUE( sw::FileUtil::exists( secondBinary ) );
    SW_EXPECT_FALSE( sw::FileUtil::exists( firstBinary ) );

    sw::ShaderCooker::invalidateSharedHeaderCache();
    sw::vector<sw::string> listProblem;
    sw::ShaderCooker::verifyCookedShaders( root, test::makeShaderConfigPriority(), "dx12", listProblem );
    SW_EXPECT_TRUE_MSG( listProblem.empty(), listProblem.empty() ? "" : listProblem.front().c_str() );
}

/**
 * @brief [ShaderDomainCompileTest] 요청이 여럿이면 병렬로 쿠킹해도 네 백엔드의 바이너리와 매니페스트가 요청 × 포맷과 같고, 모두 최신인 두 번째 쿠킹은 매니페스트 바이트를 바꾸지 않는다
 * @details 컴파일 · 리플렉션은 병렬이고 합치기는 요청 순서대로 직렬이다(`ShaderCookDriver::cookRequests`). 같은 요청이 두 번 들어 있어도(같은 바이너리를 만드는 요청은 한 사슬) 같은 파일을 두 스레드가 쓰지 않는다.
 */
SW_TEST_CASE( ShaderDomainCompileTest, ManyRequestsCookInParallelToTheSameManifest )
{
    test::ScopedLogSuppressor suppressor;

    const test::ScopedTempResourceRoot resourceRoot;
    const sw::string&                  root = resourceRoot.getRootFolderPath();
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "game/domaintest/shaders/probe.hlsl",
                                                "cbuffer ProbeCB : register(b0) { float4 gTint; };\n"
                                                "float4 PSMain() : SV_Target { return gTint; }\n" ) );
    const sw::string shaderPath = resourceRoot.makePath( "game/domaintest/shaders/probe.hlsl" );

    constexpr uint32                  kRequestCount = 6;
    sw::vector<sw::ShaderCookRequest> listRequest;
    for ( uint32 permutationIndex = 0; permutationIndex < kRequestCount; ++permutationIndex )
    {
        sw::ShaderCookRequest request{};
        request._shaderPath      = shaderPath;
        request._entryPoint      = "PSMain";
        request._stage           = sw::ShaderStage::Pixel;
        request._listPermutation = { "SW_PROBE_" + sw::to_string( permutationIndex ) };
        request._permutationHash = sw::ShaderCooker::computePermutationHash( request._listPermutation );
        listRequest.push_back( std::move( request ) );
    }
    listRequest.push_back( listRequest.front() ); // 같은 바이너리를 만드는 요청이 또 있다

    const sw::ShaderTargetFormat arrFormat[] = { sw::ShaderTargetFormat::DXBC_D3D11, sw::ShaderTargetFormat::DXIL_D3D12,
                                                 sw::ShaderTargetFormat::SPIRV_Vulkan, sw::ShaderTargetFormat::SPIRV_OpenGL };

    const sw::ShaderCookSummary first      = sw::ShaderCookDriver::cookRequests( root, test::makeShaderConfigPriority(), listRequest, sw::ShaderTargetFormat::Count, false );
    const sw::string            dxilBinary = sw::FileUtil::joinPath( resourceRoot.makePath( "game/domaintest/shaders/bin/dx12" ),
                                                                     sw::ShaderCooker::computeBinaryFileName( "probe", sw::ShaderStage::Pixel, "PSMain", listRequest.front()._permutationHash, ".dxil" ) );
    if ( first._failedCount > 0 && sw::FileUtil::exists( dxilBinary ) == false )
        SW_TEST_SKIP( "No DXC compiler in this environment" );
    SW_ASSERT_TRUE( first.isClean() );
    SW_EXPECT_TRUE( first._cookedCount >= kRequestCount * static_cast<uint32>( std::size( arrFormat ) ) );
    SW_EXPECT_EQUAL( 0u, first._removedCount );

    sw::vector<sw::vector<uint8>> listFirstManifest;
    for ( const sw::ShaderTargetFormat format : arrFormat )
    {
        const sw::string  binDir = resourceRoot.makePath( ( sw::string( "game/domaintest/shaders/bin/" ) + sw::string( sw::ShaderCooker::getSubfolderForFormat( format ) ) ).c_str() );
        sw::vector<uint8> manifestBytes;
        SW_ASSERT_TRUE_MSG( sw::FileUtil::readFile( sw::FileUtil::joinPath( binDir, "reflection.manifest" ), manifestBytes ), binDir.c_str() );
        const sw::string_view manifest{ reinterpret_cast<const utf8*>( manifestBytes.data() ), manifestBytes.size() };
        for ( uint32 requestIndex = 0; requestIndex < kRequestCount; ++requestIndex )
        {
            const sw::string fileName = sw::ShaderCooker::computeBinaryFileName( "probe", listRequest[requestIndex]._stage, listRequest[requestIndex]._entryPoint,
                                                                                 listRequest[requestIndex]._permutationHash, sw::ShaderCooker::getExtensionForFormat( format ) );
            SW_EXPECT_TRUE_MSG( sw::FileUtil::exists( sw::FileUtil::joinPath( binDir, fileName ) ), fileName.c_str() );
            SW_EXPECT_TRUE_MSG( manifest.find( fileName ) != sw::string_view::npos, fileName.c_str() );
        }
        listFirstManifest.push_back( std::move( manifestBytes ) );
    }

    // 모두 최신이다 — 다시 쿠킹하면 컴파일은 없고(리플렉션만 병렬로 다시 뽑는다) 매니페스트는 바이트까지 같다.
    sw::ShaderCooker::invalidateSharedHeaderCache(); // 방금 쓴 스탬프를 읽게 한다
    const sw::ShaderCookSummary second = sw::ShaderCookDriver::cookRequests( root, test::makeShaderConfigPriority(), listRequest, sw::ShaderTargetFormat::Count, false );
    SW_EXPECT_TRUE( second.isClean() );
    SW_EXPECT_EQUAL( 0u, second._cookedCount );
    SW_EXPECT_EQUAL( 0u, second._removedCount );
    for ( size_t formatIndex = 0; formatIndex < std::size( arrFormat ); ++formatIndex )
    {
        const sw::string  binDir = resourceRoot.makePath( ( sw::string( "game/domaintest/shaders/bin/" ) + sw::string( sw::ShaderCooker::getSubfolderForFormat( arrFormat[formatIndex] ) ) ).c_str() );
        sw::vector<uint8> manifestBytes;
        SW_ASSERT_TRUE_MSG( sw::FileUtil::readFile( sw::FileUtil::joinPath( binDir, "reflection.manifest" ), manifestBytes ), binDir.c_str() );
        SW_EXPECT_TRUE_MSG( manifestBytes == listFirstManifest[formatIndex], binDir.c_str() );
    }

    sw::ShaderCooker::invalidateSharedHeaderCache();
    sw::vector<sw::string> listProblem;
    for ( const sw::ShaderTargetFormat format : arrFormat )
    {
        sw::ShaderCooker::verifyCookedShaders( root, test::makeShaderConfigPriority(), sw::ShaderCooker::getSubfolderForFormat( format ), listProblem );
    }
    SW_EXPECT_TRUE_MSG( listProblem.empty(), listProblem.empty() ? "" : listProblem.front().c_str() );
}
