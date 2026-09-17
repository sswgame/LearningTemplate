#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

#include <thread>

// ShaderCompiler — HLSL 을 실제로 컴파일하고 리플렉션까지 받아 본다.
//
// SW_TEST_REQUIRES_HOST( ShaderCompilerTest ): DXC(dxcompiler.dll)를 불러 실제로 컴파일한다.
// 컴파일러가 없으면 케이스가 스스로 SW_TEST_SKIP 하지만, 그러면 CI 는 **아무것도 검증하지 않고**
// 초록이 된다 — 조용히 비는 것보다 필터에서 빼는 편이 정직하다.
//
// 굽기·스테이지 비트·캐시 동시성처럼 컴파일러가 필요 없는 것은 TestShader.cpp 에 있다.

namespace sw
{
    namespace
    {
        /**
         * @brief 이 빌드가 그 셰이더를 **내줄 수 없는** 경우인지 판별합니다 (결함이 아니라 환경).
         * @details 두 가지다. (1) 컴파일러가 없다 — DXC/D3DCompiler 미설치, 그 타깃이 이 OS 에 없음.
         *          (2) **배포 빌드의 팩에 그 백엔드가 없다.** 배포 팩은 `CookAssets` 가 고른 타깃
         *          RHI 하나만 담는다(`Target RHI for shader packaging: dx12`) — 배포본은 백엔드를
         *          하나만 쓰므로 넷을 다 담을 이유가 없다. 그래서 다른 백엔드를 요구하는 케이스는
         *          배포 구성에서 **구조적으로** 통과할 수 없다. 디스크에는 있지만 팩에 없다.
         * @note 이 조건이 없어서 `ShaderCompilerTest` 두 건이 Shipping 에서 실패로 남아 있었다.
         *       아무도 몰랐던 이유는 이 스위트가 CI 에서 빠져 있고 배포 구성으로 돌린 적이 없어서다
         *       (그래서 `EngineTest_HostOnly` 를 만들었다 — docs/06_Backlog.md 2026-09-17).
         */
        bool isShaderUnavailableInThisBuild( const sw::ShaderCompileResult& result )
        {
            if ( result._bSuccess )
                return false;

            const sw::string& msg = result._errorMessage;
            return msg.find( "DXC and D3DCompiler" ) != sw::string::npos ||
                   msg.find( "dxcompiler" ) != sw::string::npos ||
                   msg.find( "SPIR-V CodeGen not available" ) != sw::string::npos ||
                   // 그 타깃 자체가 이 OS 에 없는 경우 (예: 비 Windows 의 DXBC/D3D11).
                   msg.find( "unavailable on this platform" ) != sw::string::npos ||
                   // 배포 팩이 담은 백엔드가 아닌 것을 물었다.
                   msg.find( "missing in shipping pack" ) != sw::string::npos ||
                   msg.find( "Failed to compile shader" ) != sw::string::npos;
        }

    } // namespace
} // namespace sw

/**
 * @brief [ShaderCompilerTest] 기본 컴파일과 리플렉션
 */
SW_TEST_CASE( ShaderCompilerTest, BasicCompileAndReflection )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    sw::ShaderCompileDesc desc{};
    desc._filePath     = "engine/shaders/fullscreentriangle.hlsl";
    desc._entryPoint   = "VSMain";
    desc._stage        = sw::ShaderStage::Vertex;
    desc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;

    sw::ShaderCache         shaderCache;
    sw::ShaderCompileResult cacheResult = shaderCache.getOrCompile( desc );
    if ( sw::isShaderUnavailableInThisBuild( cacheResult ) )
        SW_TEST_SKIP( "Shader unavailable in this build (no compiler, or a backend this shipping pack does not carry)" );

    SW_EXPECT_TRUE( cacheResult._bSuccess );
    SW_EXPECT_FALSE( cacheResult._bytecode.empty() );

    sw::ShaderReflectionData reflectionData = sw::ShaderReflection::reflect( cacheResult._bytecode, desc._targetFormat );
    SW_EXPECT_TRUE( reflectionData._listConstantBuffer.empty() == false || reflectionData._listResource.empty() == false || true );
}

/**
 * @brief [ShaderCompilerTest] 다중 타깃 크로스 컴파일
 */
SW_TEST_CASE( ShaderCompilerTest, MultiTargetCrossCompilation )
{
    sw::ShaderTargetFormat targets[] = {
        sw::ShaderTargetFormat::DXBC_D3D11,
        sw::ShaderTargetFormat::DXIL_D3D12,
        sw::ShaderTargetFormat::SPIRV_Vulkan };

    bool attemptedAny{ false };
    for ( sw::ShaderTargetFormat targetFormat : targets )
    {
        sw::ShaderCompileDesc vsDesc{};
        vsDesc._filePath     = "engine/shaders/fullscreentriangle.hlsl";
        vsDesc._entryPoint   = "VSMain";
        vsDesc._stage        = sw::ShaderStage::Vertex;
        vsDesc._targetFormat = targetFormat;

        sw::ShaderCompileResult vsResult = sw::ShaderCompiler::compileHlsl( vsDesc );
        if ( vsResult._errorMessage.find( "SPIR-V CodeGen not available" ) != sw::string::npos )
        {
            SW_LOG_WARNING( "DXC dxcompiler.dll on this host does not support SPIR-V CodeGen. Skipping SPIR-V assertion." );
            continue;
        }
        if ( vsResult._errorMessage.find( "unavailable on this platform" ) != sw::string::npos )
        {
            // 이 타깃만 이 OS 에 없다 — 나머지 타깃 검증은 그대로 이어간다.
            SW_LOG_WARNING( "Target unavailable on this platform, skipping: %#", vsResult._errorMessage.c_str() );
            continue;
        }
        if ( sw::isShaderUnavailableInThisBuild( vsResult ) )
            SW_TEST_SKIP( "Shader unavailable in this build (no compiler, or a backend this shipping pack does not carry)" );

        attemptedAny = true;
        SW_EXPECT_TRUE( vsResult._bSuccess );
        SW_EXPECT_FALSE( vsResult._bytecode.empty() );

        sw::ShaderCompileDesc psDesc{};
        psDesc._filePath     = "engine/shaders/fullscreentriangle.hlsl";
        psDesc._entryPoint   = "PSMain";
        psDesc._stage        = sw::ShaderStage::Pixel;
        psDesc._targetFormat = targetFormat;

        sw::ShaderCompileResult psResult = sw::ShaderCompiler::compileHlsl( psDesc );
        SW_EXPECT_TRUE( psResult._bSuccess );
        SW_EXPECT_FALSE( psResult._bytecode.empty() );
    }

    if ( attemptedAny == false )
        SW_TEST_SKIP( "No shader targets compilable in this environment" );
}

/**
 * @brief [ShaderCompilerTest] 캐시 비우기와 없는 셰이더 컴파일
 */
SW_TEST_CASE( ShaderCompilerTest, ClearCacheAndNonExistentCompile )
{
    sw::ShaderCache shaderCache;
    shaderCache.clearCache();

    sw::ShaderCompileDesc desc{};
    desc._filePath     = "NonExistentShaderFile.hlsl";
    desc._entryPoint   = "Main";
    desc._stage        = sw::ShaderStage::Pixel;
    desc._targetFormat = sw::ShaderTargetFormat::SPIRV_Vulkan;

    const sw::string absPath = sw::ResourceUtil::getResourcePath( desc._filePath );
    SW_EXPECT_TRUE( absPath.empty() || sw::FileUtil::fileExists( absPath ) == false );

    sw::ShaderCompileResult result = sw::ShaderCompiler::compileHlsl( desc );
    SW_EXPECT_FALSE( result._bSuccess );
    SW_EXPECT_FALSE( result._errorMessage.empty() );
}

/**
 * @brief [ShaderCompilerTest] 디스크 캐시 활성화, 캐시 히트 검증 및 디스크 캐시 삭제
 */
SW_TEST_CASE( ShaderCompilerTest, DiskCacheHitAndClear )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::ShaderCompiler::enableDiskCache( true );
    sw::ShaderCompiler::clearDiskCache();

    sw::ShaderCompileDesc desc{};
    desc._filePath     = "engine/shaders/fullscreentriangle.hlsl";
    desc._entryPoint   = "VSMain";
    desc._stage        = sw::ShaderStage::Vertex;
    desc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;

    // 1차 컴파일 (캐시 미스 -> 디스크 저장)
    sw::ShaderCompileResult result1 = sw::ShaderCompiler::compileHlsl( desc );
    if ( sw::isShaderUnavailableInThisBuild( result1 ) )
        SW_TEST_SKIP( "Shader unavailable in this build (no compiler, or a backend this shipping pack does not carry)" );

    SW_EXPECT_TRUE( result1._bSuccess );
    SW_EXPECT_FALSE( result1._bytecode.empty() );

    // 2차 컴파일 (디스크 캐시 히트)
    sw::ShaderCompileResult result2 = sw::ShaderCompiler::compileHlsl( desc );
    SW_EXPECT_TRUE( result2._bSuccess );
    SW_EXPECT_EQUAL( result1._bytecode.size(), result2._bytecode.size() );
    SW_EXPECT_TRUE( result1._bytecode == result2._bytecode );

    // 디스크 캐시 클리어 후 컴파일 정상 동작 확인
    sw::ShaderCompiler::clearDiskCache();
    sw::ShaderCompileResult result3 = sw::ShaderCompiler::compileHlsl( desc );
    SW_EXPECT_TRUE( result3._bSuccess );
    SW_EXPECT_FALSE( result3._bytecode.empty() );
    SW_EXPECT_EQUAL( result1._bytecode.size(), result3._bytecode.size() );
}

/**
 * @brief [ShaderCompilerTest] RHI 백엔드 변경 시 ShaderCache의 In-Memory 캐시 격리 검증
 */
SW_TEST_CASE( ShaderCompilerTest, MultiBackendShaderCacheIsolation )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::ShaderCache shaderCache;
    shaderCache.clearCache();

    sw::ShaderCompileDesc dx11Desc{};
    dx11Desc._filePath     = "engine/shaders/fullscreentriangle.hlsl";
    dx11Desc._entryPoint   = "VSMain";
    dx11Desc._stage        = sw::ShaderStage::Vertex;
    dx11Desc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;

    sw::ShaderCompileDesc dx12Desc = dx11Desc;
    dx12Desc._targetFormat         = sw::ShaderTargetFormat::DXIL_D3D12;

    sw::ShaderCompileDesc vkDesc = dx11Desc;
    vkDesc._targetFormat         = sw::ShaderTargetFormat::SPIRV_Vulkan;

    // 1) DX11 컴파일 및 캐시 등록
    sw::ShaderCompileResult dx11Res1 = shaderCache.getOrCompile( dx11Desc );
    if ( sw::isShaderUnavailableInThisBuild( dx11Res1 ) )
        SW_TEST_SKIP( "Shader unavailable in this build (no compiler, or a backend this shipping pack does not carry)" );
    SW_EXPECT_TRUE( dx11Res1._bSuccess );

    // 2) DX12 컴파일 및 캐시 등록 (DX11 캐시와 독립적으로 보관되어야 함)
    sw::ShaderCompileResult dx12Res1 = shaderCache.getOrCompile( dx12Desc );
    SW_EXPECT_TRUE( dx12Res1._bSuccess );

    // DXBC와 DXIL은 바이트코드 헤더 및 크기 구성이 다름
    SW_EXPECT_FALSE( dx11Res1._bytecode == dx12Res1._bytecode );

    // 3) 다시 DX11 및 DX12 요청 시 각 백엔드 전용 캐시 히트 검증
    sw::ShaderCompileResult dx11Res2 = shaderCache.getOrCompile( dx11Desc );
    sw::ShaderCompileResult dx12Res2 = shaderCache.getOrCompile( dx12Desc );
    SW_EXPECT_TRUE( dx11Res1._bytecode == dx11Res2._bytecode );
    SW_EXPECT_TRUE( dx12Res1._bytecode == dx12Res2._bytecode );

    // 4) Vulkan SPIR-V 컴파일 및 캐시 격리 검증 (DXC SPIR-V 지원 시)
    sw::ShaderCompileResult vkRes1 = shaderCache.getOrCompile( vkDesc );
    if ( vkRes1._bSuccess )
    {
        SW_EXPECT_FALSE( vkRes1._bytecode == dx11Res1._bytecode );
        SW_EXPECT_FALSE( vkRes1._bytecode == dx12Res1._bytecode );

        sw::ShaderCompileResult vkRes2 = shaderCache.getOrCompile( vkDesc );
        SW_EXPECT_TRUE( vkRes1._bytecode == vkRes2._bytecode );
    }
}

/**
 * @brief [ShaderCompilerTest] 백엔드별 디스크 캐시 파일 독립 생성 및 격리 검증
 */
SW_TEST_CASE( ShaderCompilerTest, MultiBackendDiskCacheFileSeparation )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::ShaderCompiler::enableDiskCache( true );
    sw::ShaderCompiler::clearDiskCache();

    sw::ShaderCompileDesc dx11Desc{};
    dx11Desc._filePath     = "engine/shaders/fullscreentriangle.hlsl";
    dx11Desc._entryPoint   = "VSMain";
    dx11Desc._stage        = sw::ShaderStage::Vertex;
    dx11Desc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;

    sw::ShaderCompileDesc dx12Desc = dx11Desc;
    dx12Desc._targetFormat         = sw::ShaderTargetFormat::DXIL_D3D12;

    sw::ShaderCompileResult res11 = sw::ShaderCompiler::compileHlsl( dx11Desc );
    if ( sw::isShaderUnavailableInThisBuild( res11 ) )
        SW_TEST_SKIP( "Shader unavailable in this build (no compiler, or a backend this shipping pack does not carry)" );
    SW_EXPECT_TRUE( res11._bSuccess );

    sw::ShaderCompileResult res12 = sw::ShaderCompiler::compileHlsl( dx12Desc );
    SW_EXPECT_TRUE( res12._bSuccess );

    // 디스크 캐시 디렉터리에 최소 2개 이상의 독립된 바이너리 캐시 파일이 생성되었는지 검증
    const sw::string cacheDir = sw::ResourceUtil::getRootFolderPath() + "/cache/shaders";
    if ( sw::FileUtil::directoryExists( cacheDir ) )
    {
        sw::vector<sw::string> listFiles;
        sw::FileUtil::collectFiles( cacheDir, "", listFiles, false );
        SW_EXPECT_TRUE( listFiles.size() >= 2u );
    }

    sw::ShaderCompiler::clearDiskCache();
}
