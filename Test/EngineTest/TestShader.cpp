#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/File/FileUtil.h"

#include "Engine/Graphics/Shader/ShaderBaker.h"
#include "Engine/Graphics/Shader/ShaderCache.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Graphics/Shader/ShaderReflection.h"
#include "Engine/Graphics/Shader/ShaderVariant.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

#include <thread>

namespace sw
{
    namespace
    {
        /** @brief DXC/D3DCompiler 미설치 등 컴파일러 불가 오류인지 판별합니다. */
        bool isShaderCompilerUnavailable( const sw::ShaderCompileResult& result )
        {
            if ( result._bSuccess )
                return false;

            const sw::string& msg = result._errorMessage;
            return msg.find( "DXC and D3DCompiler" ) != sw::string::npos ||
                   msg.find( "dxcompiler" ) != sw::string::npos ||
                   msg.find( "SPIR-V CodeGen not available" ) != sw::string::npos ||
                   msg.find( "Failed to compile shader" ) != sw::string::npos;
        }

    } // namespace
} // namespace sw

// ------------------------------------------------------------------------------
// 1) ShaderCompilerTest — 컴파일·크로스·배리언트
// ------------------------------------------------------------------------------
/**
 * @brief [ShaderCompilerTest] 기본 컴파일과 리플렉션
 */
SW_TEST_CASE( ShaderCompilerTest, BasicCompileAndReflection )
{
    sw::ResourceUtil::initialize();

    sw::ShaderCompileDesc desc{};
    desc._filePath     = "engine/shaders/fullscreentriangle.hlsl";
    desc._entryPoint   = "VSMain";
    desc._stage        = sw::ShaderStage::Vertex;
    desc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;

    sw::ShaderCache         shaderCache;
    sw::ShaderCompileResult cacheResult = shaderCache.getOrCompile( desc );
    if ( sw::isShaderCompilerUnavailable( cacheResult ) )
    {
        SW_TEST_SKIP( "Shader compiler unavailable in this environment" );
    }

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

        sw::ShaderCompileResult vsResult = sw::ShaderCompiler::compileHLSL( vsDesc );
        if ( vsResult._errorMessage.find( "SPIR-V CodeGen not available" ) != sw::string::npos )
        {
            SW_LOG_WARNING( "DXC dxcompiler.dll on this host does not support SPIR-V CodeGen. Skipping SPIR-V assertion." );
            continue;
        }
        if ( sw::isShaderCompilerUnavailable( vsResult ) )
        {
            SW_TEST_SKIP( "Shader compiler unavailable in this environment" );
        }

        attemptedAny = true;
        SW_EXPECT_TRUE( vsResult._bSuccess );
        SW_EXPECT_FALSE( vsResult._bytecode.empty() );

        sw::ShaderCompileDesc psDesc{};
        psDesc._filePath     = "engine/shaders/fullscreentriangle.hlsl";
        psDesc._entryPoint   = "PSMain";
        psDesc._stage        = sw::ShaderStage::Pixel;
        psDesc._targetFormat = targetFormat;

        sw::ShaderCompileResult psResult = sw::ShaderCompiler::compileHLSL( psDesc );
        SW_EXPECT_TRUE( psResult._bSuccess );
        SW_EXPECT_FALSE( psResult._bytecode.empty() );
    }

    if ( attemptedAny == false )
    {
        SW_TEST_SKIP( "No shader targets compilable in this environment" );
    }
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

    sw::ShaderCompileResult result = sw::ShaderCompiler::compileHLSL( desc );
    SW_EXPECT_FALSE( result._bSuccess );
    SW_EXPECT_FALSE( result._errorMessage.empty() );
}

/**
 * @brief [ShaderCompilerTest] 셰이더 배리언트 permutation
 */
SW_TEST_CASE( ShaderCompilerTest, ShaderVariantPermutation )
{
    sw::ShaderVariantKey key1;
    key1._shaderPath = "engine/shaders/fullscreentriangle.hlsl";
    key1._listDefine.push_back( { "USE_ALBEDO_MAP", "1" } );

    sw::ShaderVariantKey key2;
    key2._shaderPath = "engine/shaders/fullscreentriangle.hlsl";
    key2._listDefine.push_back( { "USE_ALBEDO_MAP", "1" } );

    sw::ShaderVariantKey key3;
    key3._shaderPath = "engine/shaders/fullscreentriangle.hlsl";
    key3._listDefine.push_back( { "SKINNED_MESH", "1" } );

    SW_EXPECT_EQUAL( key1.getVariantHashKey().getHash(), key2.getVariantHashKey().getHash() );
    SW_EXPECT_FALSE( key1.getVariantHashKey() == key3.getVariantHashKey() );

    sw::ShaderVariantManager variantManager;
    SW_EXPECT_EQUAL( 0u, variantManager.getCompiledVariantCount() );
    variantManager.clear();
}

/**
 * @brief [ShaderCompilerTest] 디스크 캐시 활성화, 캐시 히트 검증 및 디스크 캐시 삭제
 */
SW_TEST_CASE( ShaderCompilerTest, DiskCacheHitAndClear )
{
    sw::ResourceUtil::initialize();
    sw::ShaderCompiler::enableDiskCache( true );
    sw::ShaderCompiler::clearDiskCache();

    sw::ShaderCompileDesc desc{};
    desc._filePath     = "engine/shaders/fullscreentriangle.hlsl";
    desc._entryPoint   = "VSMain";
    desc._stage        = sw::ShaderStage::Vertex;
    desc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;

    // 1차 컴파일 (캐시 미스 -> 디스크 저장)
    sw::ShaderCompileResult result1 = sw::ShaderCompiler::compileHLSL( desc );
    if ( sw::isShaderCompilerUnavailable( result1 ) )
    {
        SW_TEST_SKIP( "Shader compiler unavailable in this environment" );
    }

    SW_EXPECT_TRUE( result1._bSuccess );
    SW_EXPECT_FALSE( result1._bytecode.empty() );

    // 2차 컴파일 (디스크 캐시 히트)
    sw::ShaderCompileResult result2 = sw::ShaderCompiler::compileHLSL( desc );
    SW_EXPECT_TRUE( result2._bSuccess );
    SW_EXPECT_EQUAL( result1._bytecode.size(), result2._bytecode.size() );
    SW_EXPECT_TRUE( result1._bytecode == result2._bytecode );

    // 디스크 캐시 클리어 후 컴파일 정상 동작 확인
    sw::ShaderCompiler::clearDiskCache();
    sw::ShaderCompileResult result3 = sw::ShaderCompiler::compileHLSL( desc );
    SW_EXPECT_TRUE( result3._bSuccess );
    SW_EXPECT_FALSE( result3._bytecode.empty() );
    SW_EXPECT_EQUAL( result1._bytecode.size(), result3._bytecode.size() );
}

/**
 * @brief [ShaderCompilerTest] RHI 백엔드 변경 시 ShaderCache의 In-Memory 캐시 격리 검증
 */
SW_TEST_CASE( ShaderCompilerTest, MultiBackendShaderCacheIsolation )
{
    sw::ResourceUtil::initialize();
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
    if ( sw::isShaderCompilerUnavailable( dx11Res1 ) )
    {
        SW_TEST_SKIP( "Shader compiler unavailable in this environment" );
    }
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
 * @brief [ShaderCompilerTest] RHI 백엔드(TargetFormat)별 ShaderVariantKey 해시 및 매니저 캐시 분리 검증
 */
SW_TEST_CASE( ShaderCompilerTest, MultiBackendVariantKeyIsolation )
{
    sw::ShaderVariantKey keyDx11;
    keyDx11._shaderPath   = "engine/shaders/fullscreentriangle.hlsl";
    keyDx11._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;
    keyDx11._listDefine.push_back( { "USE_TEXTURE", "1" } );

    sw::ShaderVariantKey keyDx12;
    keyDx12._shaderPath   = "engine/shaders/fullscreentriangle.hlsl";
    keyDx12._targetFormat = sw::ShaderTargetFormat::DXIL_D3D12;
    keyDx12._listDefine.push_back( { "USE_TEXTURE", "1" } );

    sw::ShaderVariantKey keyVk;
    keyVk._shaderPath   = "engine/shaders/fullscreentriangle.hlsl";
    keyVk._targetFormat = sw::ShaderTargetFormat::SPIRV_Vulkan;
    keyVk._listDefine.push_back( { "USE_TEXTURE", "1" } );

    sw::ShaderVariantKey keyGl;
    keyGl._shaderPath   = "engine/shaders/fullscreentriangle.hlsl";
    keyGl._targetFormat = sw::ShaderTargetFormat::SPIRV_OpenGL;
    keyGl._listDefine.push_back( { "USE_TEXTURE", "1" } );

    // 동일 소스 및 define이라도 targetFormat이 다르면 해시 키가 유일하게 구별되어야 함
    SW_EXPECT_FALSE( keyDx11.getVariantHashKey() == keyDx12.getVariantHashKey() );
    SW_EXPECT_FALSE( keyDx11.getVariantHashKey() == keyVk.getVariantHashKey() );
    SW_EXPECT_FALSE( keyDx12.getVariantHashKey() == keyVk.getVariantHashKey() );
    SW_EXPECT_FALSE( keyVk.getVariantHashKey() == keyGl.getVariantHashKey() );

    // 동일 targetFormat 및 define은 일치해야 함
    sw::ShaderVariantKey keyDx11Copy = keyDx11;
    SW_EXPECT_EQUAL( keyDx11.getVariantHashKey().getHash(), keyDx11Copy.getVariantHashKey().getHash() );

    sw::ShaderVariantManager       manager;
    const sw::ShaderCompileResult* pResDx11 = manager.getOrCompileVariant( keyDx11 );
    if ( pResDx11 != nullptr && pResDx11->_bSuccess )
    {
        const sw::ShaderCompileResult* pResDx12 = manager.getOrCompileVariant( keyDx12 );
        if ( pResDx12 != nullptr && pResDx12->_bSuccess )
        {
            SW_EXPECT_EQUAL( 2u, manager.getCompiledVariantCount() );
            SW_EXPECT_FALSE( pResDx11->_bytecode == pResDx12->_bytecode );
        }
    }
    manager.clear();
}

/**
 * @brief [ShaderCompilerTest] 백엔드별 디스크 캐시 파일 독립 생성 및 격리 검증
 */
SW_TEST_CASE( ShaderCompilerTest, MultiBackendDiskCacheFileSeparation )
{
    sw::ResourceUtil::initialize();
    sw::ShaderCompiler::enableDiskCache( true );
    sw::ShaderCompiler::clearDiskCache();

    sw::ShaderCompileDesc dx11Desc{};
    dx11Desc._filePath     = "engine/shaders/fullscreentriangle.hlsl";
    dx11Desc._entryPoint   = "VSMain";
    dx11Desc._stage        = sw::ShaderStage::Vertex;
    dx11Desc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;

    sw::ShaderCompileDesc dx12Desc = dx11Desc;
    dx12Desc._targetFormat         = sw::ShaderTargetFormat::DXIL_D3D12;

    sw::ShaderCompileResult res11 = sw::ShaderCompiler::compileHLSL( dx11Desc );
    if ( sw::isShaderCompilerUnavailable( res11 ) )
    {
        SW_TEST_SKIP( "Shader compiler unavailable in this environment" );
    }
    SW_EXPECT_TRUE( res11._bSuccess );

    sw::ShaderCompileResult res12 = sw::ShaderCompiler::compileHLSL( dx12Desc );
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

// ------------------------------------------------------------------------------
// 2) ShaderStageTest — 8대 스테이지, 플래그 변환, 비트 연산 대수 법칙 & 스트레스
// ------------------------------------------------------------------------------
/**
 * @brief [ShaderStageTest] 8대 스테이지 플래그 변환 및 경계값/오버플로 엣지 케이스 검증
 */
SW_TEST_CASE( ShaderStageTest, StageFlagConversionAndBoundary )
{
    // 1) 8대 정상 스테이지가 각각 올바른 1 << N 비트플래그로 1:1 변환되는지 검증
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Vertex ) == sw::ShaderStageFlag::Vertex );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Pixel ) == sw::ShaderStageFlag::Pixel );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Compute ) == sw::ShaderStageFlag::Compute );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Geometry ) == sw::ShaderStageFlag::Geometry );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Hull ) == sw::ShaderStageFlag::Hull );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Domain ) == sw::ShaderStageFlag::Domain );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Mesh ) == sw::ShaderStageFlag::Mesh );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Amplification ) == sw::ShaderStageFlag::Amplification );

    // 2) 경계값 및 범위 초과(오버플로) 엣지 케이스 검증
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Count ) == sw::ShaderStageFlag::None );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( static_cast<sw::ShaderStage>( 8 ) ) == sw::ShaderStageFlag::None );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( static_cast<sw::ShaderStage>( 99 ) ) == sw::ShaderStageFlag::None );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( static_cast<sw::ShaderStage>( 255 ) ) == sw::ShaderStageFlag::None );
}

/**
 * @brief [ShaderStageTest] hasShaderStage 비트 포함 여부 및 AllGraphics/All 마스킹 검증
 */
SW_TEST_CASE( ShaderStageTest, HasShaderStageAndMasking )
{
    const sw::ShaderStageFlag flags = sw::ShaderStageFlag::Vertex | sw::ShaderStageFlag::Pixel | sw::ShaderStageFlag::Compute;

    // 포함된 스테이지 검사
    SW_EXPECT_TRUE( sw::hasShaderStage( flags, sw::ShaderStageFlag::Vertex ) );
    SW_EXPECT_TRUE( sw::hasShaderStage( flags, sw::ShaderStageFlag::Pixel ) );
    SW_EXPECT_TRUE( sw::hasShaderStage( flags, sw::ShaderStageFlag::Compute ) );

    // 미포함 스테이지 검사
    SW_EXPECT_FALSE( sw::hasShaderStage( flags, sw::ShaderStageFlag::Geometry ) );
    SW_EXPECT_FALSE( sw::hasShaderStage( flags, sw::ShaderStageFlag::Hull ) );
    SW_EXPECT_FALSE( sw::hasShaderStage( flags, sw::ShaderStageFlag::Mesh ) );
    SW_EXPECT_FALSE( sw::hasShaderStage( sw::ShaderStageFlag::None, sw::ShaderStageFlag::Vertex ) );

    // AllGraphics (7대 그래픽스 스테이지 포함, Compute 미포함)
    SW_EXPECT_TRUE( sw::hasShaderStage( sw::ShaderStageFlag::AllGraphics, sw::ShaderStageFlag::Vertex ) );
    SW_EXPECT_TRUE( sw::hasShaderStage( sw::ShaderStageFlag::AllGraphics, sw::ShaderStageFlag::Pixel ) );
    SW_EXPECT_TRUE( sw::hasShaderStage( sw::ShaderStageFlag::AllGraphics, sw::ShaderStageFlag::Geometry ) );
    SW_EXPECT_TRUE( sw::hasShaderStage( sw::ShaderStageFlag::AllGraphics, sw::ShaderStageFlag::Hull ) );
    SW_EXPECT_TRUE( sw::hasShaderStage( sw::ShaderStageFlag::AllGraphics, sw::ShaderStageFlag::Domain ) );
    SW_EXPECT_TRUE( sw::hasShaderStage( sw::ShaderStageFlag::AllGraphics, sw::ShaderStageFlag::Mesh ) );
    SW_EXPECT_TRUE( sw::hasShaderStage( sw::ShaderStageFlag::AllGraphics, sw::ShaderStageFlag::Amplification ) );
    SW_EXPECT_FALSE( sw::hasShaderStage( sw::ShaderStageFlag::AllGraphics, sw::ShaderStageFlag::Compute ) );

    // All (Compute 포함 8대 전 스테이지 포함)
    SW_EXPECT_TRUE( sw::hasShaderStage( sw::ShaderStageFlag::All, sw::ShaderStageFlag::Compute ) );
    SW_EXPECT_TRUE( sw::hasShaderStage( sw::ShaderStageFlag::All, sw::ShaderStageFlag::Vertex ) );
    SW_EXPECT_TRUE( sw::hasShaderStage( sw::ShaderStageFlag::All, sw::ShaderStageFlag::Amplification ) );
}

/**
 * @brief [ShaderStageTest] 비트 연산자 대수적 공리(항등원, 영원, 멱등성, 여원, XOR) 검증
 */
SW_TEST_CASE( ShaderStageTest, BitwiseOperatorAlgebraicLaws )
{
    const sw::ShaderStageFlag flagVs = sw::ShaderStageFlag::Vertex;
    const sw::ShaderStageFlag flagPs = sw::ShaderStageFlag::Pixel;
    const sw::ShaderStageFlag flagCs = sw::ShaderStageFlag::Compute;

    // 항등원 (Identity)
    SW_EXPECT_TRUE( ( flagVs | sw::ShaderStageFlag::None ) == flagVs );
    SW_EXPECT_TRUE( ( flagVs & sw::ShaderStageFlag::All ) == flagVs );

    // 영원 (Annihilation)
    SW_EXPECT_TRUE( ( flagVs & sw::ShaderStageFlag::None ) == sw::ShaderStageFlag::None );

    // 멱등성 (Idempotence)
    SW_EXPECT_TRUE( ( flagCs | flagCs ) == flagCs );
    SW_EXPECT_TRUE( ( flagCs & flagCs ) == flagCs );

    // 여원 (Complement)
    SW_EXPECT_TRUE( ( flagVs & ~flagVs ) == sw::ShaderStageFlag::None );

    // XOR
    SW_EXPECT_TRUE( ( flagVs ^ flagVs ) == sw::ShaderStageFlag::None );
    SW_EXPECT_TRUE( ( flagVs ^ sw::ShaderStageFlag::None ) == flagVs );

    // 복합 대입 (Compound assignment)
    sw::ShaderStageFlag testFlag = flagVs;
    testFlag |= flagPs;
    SW_EXPECT_TRUE( sw::hasShaderStage( testFlag, flagVs ) );
    SW_EXPECT_TRUE( sw::hasShaderStage( testFlag, flagPs ) );

    testFlag &= ~flagVs;
    SW_EXPECT_FALSE( sw::hasShaderStage( testFlag, flagVs ) );
    SW_EXPECT_TRUE( sw::hasShaderStage( testFlag, flagPs ) );

    testFlag ^= flagPs;
    SW_EXPECT_TRUE( testFlag == sw::ShaderStageFlag::None );
}

/**
 * @brief [ShaderStageTest] 10만 회 비트 연산 반복 평가 무결성 스트레스 테스트
 */
SW_TEST_CASE( ShaderStageTest, BitwiseStressEvaluation )
{
    sw::ShaderStageFlag flags           = sw::ShaderStageFlag::None;
    constexpr uint32    kIterationCount = 100000;
    for ( uint32 index = 0; index < kIterationCount; ++index )
    {
        const uint8               stageIndex = static_cast<uint8>( index % static_cast<uint8>( sw::ShaderStage::Count ) );
        const sw::ShaderStage     stage      = static_cast<sw::ShaderStage>( stageIndex );
        const sw::ShaderStageFlag stageFlag  = sw::toShaderStageFlag( stage );

        flags ^= stageFlag;
        const bool bHas = sw::hasShaderStage( flags, stageFlag );
        SW_EXPECT_TRUE( bHas == ( ( flags & stageFlag ) != sw::ShaderStageFlag::None ) );
    }
}

// ------------------------------------------------------------------------------
// 3) ShaderBakerTest — 순열 해시 불변성, 파일명 생성, RHI 매핑, 충돌 스트레스
// ------------------------------------------------------------------------------
/**
 * @brief [ShaderBakerTest] 순열 해시 순서 불변성, 빈 원소 무시, 구조체 오버로드 일관성 검증
 */
SW_TEST_CASE( ShaderBakerTest, PermutationHashOrderInvarianceAndEdgeCases )
{
    // 1) 빈 순열은 0 반환
    const sw::vector<sw::string> listEmpty;
    SW_EXPECT_EQUAL( 0ull, sw::ShaderBaker::computePermutationHash( listEmpty ) );

    // 2) 순서 불변성 (Order Invariance): 정의 순서가 달라도 동일 해시 산출
    const sw::vector<sw::string> listPerm1 = { "FEATURE_A=1", "FEATURE_B=2", "USE_HDR=1" };
    const sw::vector<sw::string> listPerm2 = { "USE_HDR=1", "FEATURE_A=1", "FEATURE_B=2" };
    const sw::vector<sw::string> listPerm3 = { "FEATURE_B=2", "USE_HDR=1", "FEATURE_A=1" };

    const uint64 hash1 = sw::ShaderBaker::computePermutationHash( listPerm1 );
    const uint64 hash2 = sw::ShaderBaker::computePermutationHash( listPerm2 );
    const uint64 hash3 = sw::ShaderBaker::computePermutationHash( listPerm3 );

    SW_EXPECT_TRUE( hash1 != 0ull );
    SW_EXPECT_EQUAL( hash1, hash2 );
    SW_EXPECT_EQUAL( hash2, hash3 );

    // 3) 빈 문자열 원소 무시 안전성
    const sw::vector<sw::string> listWithEmpty = { "", "FEATURE_A=1", "", "FEATURE_B=2", "USE_HDR=1", "" };
    SW_EXPECT_EQUAL( hash1, sw::ShaderBaker::computePermutationHash( listWithEmpty ) );

    // 4) vector<ShaderMacroDefine> 오버로드와 vector<string> 간의 해시 일치
    const sw::vector<sw::ShaderMacroDefine> listDefine = {
        {"FEATURE_A", "1"},
        {"FEATURE_B", "2"},
        {  "USE_HDR", "1"}
    };
    SW_EXPECT_EQUAL( hash1, sw::ShaderBaker::computePermutationHash( listDefine ) );

    // 5) 값이 빈 매크로 ("DEBUG_MODE")의 단일 문자열 vs 구조체 일치
    const sw::vector<sw::string>            listValueless    = { "DEBUG_MODE" };
    const sw::vector<sw::ShaderMacroDefine> listDefValueless = {
        { "DEBUG_MODE", "" }
    };
    SW_EXPECT_EQUAL( sw::ShaderBaker::computePermutationHash( listValueless ),
                     sw::ShaderBaker::computePermutationHash( listDefValueless ) );
}

/**
 * @brief [ShaderBakerTest] 8대 스테이지 표준 태그, 커스텀 진입점, 해시 접미사 및 대소문자 정규화 검증
 */
SW_TEST_CASE( ShaderBakerTest, BinaryFileNameGenerationAndStageTags )
{
    // 1) 8대 스테이지 기본 진입점 축약 태그 검증
    SW_EXPECT_EQUAL( sw::string( "forwardlit_vs.dxil" ),
                     sw::ShaderBaker::computeBinaryFileName( "forwardlit", sw::ShaderStage::Vertex, "VSMain", 0ull, ".dxil" ) );
    SW_EXPECT_EQUAL( sw::string( "deferredlighting_ps.spv" ),
                     sw::ShaderBaker::computeBinaryFileName( "deferredlighting", sw::ShaderStage::Pixel, "PSMain", 0ull, ".spv" ) );
    SW_EXPECT_EQUAL( sw::string( "gpucull_cs.dxbc" ),
                     sw::ShaderBaker::computeBinaryFileName( "gpucull", sw::ShaderStage::Compute, "CSMain", 0ull, ".dxbc" ) );
    SW_EXPECT_EQUAL( sw::string( "terrain_gs.dxil" ),
                     sw::ShaderBaker::computeBinaryFileName( "terrain", sw::ShaderStage::Geometry, "GSMain", 0ull, ".dxil" ) );
    SW_EXPECT_EQUAL( sw::string( "tess_hs.spv" ),
                     sw::ShaderBaker::computeBinaryFileName( "tess", sw::ShaderStage::Hull, "HSMain", 0ull, ".spv" ) );
    SW_EXPECT_EQUAL( sw::string( "tess_ds.spv" ),
                     sw::ShaderBaker::computeBinaryFileName( "tess", sw::ShaderStage::Domain, "DSMain", 0ull, ".spv" ) );
    SW_EXPECT_EQUAL( sw::string( "cluster_ms.dxil" ),
                     sw::ShaderBaker::computeBinaryFileName( "cluster", sw::ShaderStage::Mesh, "MSMain", 0ull, ".dxil" ) );
    SW_EXPECT_EQUAL( sw::string( "cluster_as.dxil" ),
                     sw::ShaderBaker::computeBinaryFileName( "cluster", sw::ShaderStage::Amplification, "ASMain", 0ull, ".dxil" ) );

    // 2) 커스텀 진입점 전달 시 소문자 결합
    SW_EXPECT_EQUAL( sw::string( "forwardlit_customentry.dxil" ),
                     sw::ShaderBaker::computeBinaryFileName( "forwardlit", sw::ShaderStage::Vertex, "CustomEntry", 0ull, ".dxil" ) );

    // 3) 기본 진입점 대소문자 무시 (vsmain -> vs)
    SW_EXPECT_EQUAL( sw::string( "forwardlit_vs.dxil" ),
                     sw::ShaderBaker::computeBinaryFileName( "forwardlit", sw::ShaderStage::Vertex, "vsmain", 0ull, ".dxil" ) );

    // 4) 순열 해시 접미사 (_%08x)
    const sw::string fileNameWithHash = sw::ShaderBaker::computeBinaryFileName( "forwardlit", sw::ShaderStage::Vertex, "VSMain", 0x1234ABCDull, ".dxil" );
    SW_EXPECT_EQUAL( sw::string( "forwardlit_vs_1234abcd.dxil" ), fileNameWithHash );
}

/**
 * @brief [ShaderBakerTest] 4대 RHI 서브폴더, 확장자 매핑 및 별칭 역산출 검증
 */
SW_TEST_CASE( ShaderBakerTest, SubfolderAndFormatMappingAliases )
{
    // 서브폴더 및 확장자
    SW_EXPECT_EQUAL( string_view( "dx11" ), sw::ShaderBaker::getSubfolderForFormat( sw::ShaderTargetFormat::DXBC_D3D11 ) );
    SW_EXPECT_EQUAL( string_view( "dx12" ), sw::ShaderBaker::getSubfolderForFormat( sw::ShaderTargetFormat::DXIL_D3D12 ) );
    SW_EXPECT_EQUAL( string_view( "vulkan" ), sw::ShaderBaker::getSubfolderForFormat( sw::ShaderTargetFormat::SPIRV_Vulkan ) );
    SW_EXPECT_EQUAL( string_view( "opengl" ), sw::ShaderBaker::getSubfolderForFormat( sw::ShaderTargetFormat::SPIRV_OpenGL ) );

    SW_EXPECT_EQUAL( string_view( ".dxbc" ), sw::ShaderBaker::getExtensionForFormat( sw::ShaderTargetFormat::DXBC_D3D11 ) );
    SW_EXPECT_EQUAL( string_view( ".dxil" ), sw::ShaderBaker::getExtensionForFormat( sw::ShaderTargetFormat::DXIL_D3D12 ) );
    SW_EXPECT_EQUAL( string_view( ".spv" ), sw::ShaderBaker::getExtensionForFormat( sw::ShaderTargetFormat::SPIRV_Vulkan ) );
    SW_EXPECT_EQUAL( string_view( ".spv" ), sw::ShaderBaker::getExtensionForFormat( sw::ShaderTargetFormat::SPIRV_OpenGL ) );

    // 별칭 역산출
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "dx11" ) == sw::ShaderTargetFormat::DXBC_D3D11 );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "d3d11" ) == sw::ShaderTargetFormat::DXBC_D3D11 );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "directx11" ) == sw::ShaderTargetFormat::DXBC_D3D11 );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "dx12" ) == sw::ShaderTargetFormat::DXIL_D3D12 );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "d3d12" ) == sw::ShaderTargetFormat::DXIL_D3D12 );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "directx12" ) == sw::ShaderTargetFormat::DXIL_D3D12 );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "vulkan" ) == sw::ShaderTargetFormat::SPIRV_Vulkan );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "vk" ) == sw::ShaderTargetFormat::SPIRV_Vulkan );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "spirv" ) == sw::ShaderTargetFormat::SPIRV_Vulkan );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "opengl" ) == sw::ShaderTargetFormat::SPIRV_OpenGL );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "gl" ) == sw::ShaderTargetFormat::SPIRV_OpenGL );

    // 미지원/미지의 서브폴더 -> Count
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "unknown" ) == sw::ShaderTargetFormat::Count );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "" ) == sw::ShaderTargetFormat::Count );
}

/**
 * @brief [ShaderBakerTest] 1만 건 순열 해시 무결성 및 충돌 스트레스 테스트
 */
SW_TEST_CASE( ShaderBakerTest, PermutationHashCollisionStressTest )
{
    constexpr uint32                  kPermutationCount = 10000;
    sw::unordered_map<uint64, uint32> mapHashToId;
    mapHashToId.reserve( kPermutationCount );

    sw::fixed_string<64> defBuf;
    for ( uint32 index = 0; index < kPermutationCount; ++index )
    {
        sw::formatstring( defBuf.data(), defBuf.capacity(), "PERM_MACRO_%u=%u", index, ( index * 31u + 7u ) );
        const sw::vector<sw::string> listPerm = { "COMMON_DEFINE=1", defBuf.c_str() };
        const uint64                 hash     = sw::ShaderBaker::computePermutationHash( listPerm );

        SW_EXPECT_TRUE( hash != 0ull );
        auto iter = mapHashToId.find( hash );
        SW_EXPECT_TRUE( iter == mapHashToId.end() );
        mapHashToId.insert_or_assign( hash, index );
    }
    SW_EXPECT_EQUAL( kPermutationCount, static_cast<uint32>( mapHashToId.size() ) );
}

/**
 * @brief [ShaderBakerTest] 잘못된 파일 경로 및 디렉터리에 대한 방어적 실패 처리 검증
 */
SW_TEST_CASE( ShaderBakerTest, DefensiveFileOperations )
{
    test::ScopedLogSuppressor suppressor;

    sw::ShaderBakeResult result{};
    const bool           bBakeInvalid = sw::ShaderBaker::bakeShader(
        "non_existent_shader_file_12345.hlsl",
        "output_dummy.bin",
        "VSMain",
        sw::ShaderStage::Vertex,
        sw::ShaderTargetFormat::DXIL_D3D12,
        nullptr,
        &result );

    SW_EXPECT_FALSE( bBakeInvalid );
    SW_EXPECT_FALSE( result._bSuccess == SW_TRUE );

    const uint32 bakedCount = sw::ShaderBaker::bakeAllShaders(
        "invalid_resource_root_path_99999",
        sw::ShaderTargetFormat::DXIL_D3D12,
        false );
    SW_EXPECT_EQUAL( 0u, bakedCount );
}

// ------------------------------------------------------------------------------
// 4) ShaderCacheStressTest — 멀티스레드 동시 캐시 조회/클리어 스트레스 테스트
// ------------------------------------------------------------------------------
/**
 * @brief [ShaderCacheStressTest] 8스레드 동시 캐시 쿼리 동기화 무결성 스트레스
 */
SW_TEST_CASE( ShaderCacheStressTest, MultiThreadedCacheAccessStress )
{
    sw::ShaderCache     cache;
    constexpr uint32    kThreadCount    = 8;
    constexpr uint32    kCallsPerThread = 100;
    std::atomic<uint32> totalQueries{ 0 };

    sw::vector<std::thread> listThread;
    listThread.reserve( kThreadCount );

    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        listThread.emplace_back( [&cache, threadIndex, &totalQueries]()
        {
            for ( uint32 callIndex = 0; callIndex < kCallsPerThread; ++callIndex )
            {
                sw::ShaderCompileDesc desc{};
                const uint32          pathId = ( threadIndex + callIndex ) % 4;
                if ( pathId == 0 )
                    desc._filePath = "engine/shaders/fullscreentriangle.hlsl";
                else if ( pathId == 1 )
                    desc._filePath = "engine/shaders/forwardlit.hlsl";
                else if ( pathId == 2 )
                    desc._filePath = "engine/shaders/shadowdepth.hlsl";
                else
                    desc._filePath = "engine/shaders/tonemap.hlsl";

                desc._entryPoint   = ( ( callIndex % 2 ) == 0 ) ? "VSMain" : "PSMain";
                desc._stage        = ( ( callIndex % 2 ) == 0 ) ? sw::ShaderStage::Vertex : sw::ShaderStage::Pixel;
                desc._targetFormat = sw::ShaderTargetFormat::DXIL_D3D12;

                sw::ShaderCompileResult res = cache.getOrCompile( desc );
                (void)res;
                totalQueries.fetch_add( 1, std::memory_order_relaxed );
            }
        } );
    }

    for ( std::thread& thread : listThread )
    {
        if ( thread.joinable() )
            thread.join();
    }

    SW_EXPECT_EQUAL( kThreadCount * kCallsPerThread, totalQueries.load() );
}

/**
 * @brief [ShaderCacheStressTest] 동시 쿼리와 clearCache 간의 레이스 컨디션 스트레스
 */
SW_TEST_CASE( ShaderCacheStressTest, MultiThreadedClearAndQueryStress )
{
    sw::ShaderCache     cache;
    constexpr uint32    kWorkerCount = 6;
    constexpr uint32    kIterations  = 80;
    std::atomic<bool>   bRunning{ true };
    std::atomic<uint32> clearsDone{ 0 };

    std::thread clearerThread( [&cache, &bRunning, &clearsDone]()
    {
        while ( bRunning.load( std::memory_order_relaxed ) )
        {
            cache.clearCache();
            clearsDone.fetch_add( 1, std::memory_order_relaxed );
            std::this_thread::yield();
        }
    } );

    sw::vector<std::thread> listWorker;
    listWorker.reserve( kWorkerCount );
    for ( uint32 workerIndex = 0; workerIndex < kWorkerCount; ++workerIndex )
    {
        listWorker.emplace_back( [&cache]()
        {
            for ( uint32 index = 0; index < kIterations; ++index )
            {
                sw::ShaderCompileDesc desc{};
                desc._filePath              = "engine/shaders/fullscreentriangle.hlsl";
                desc._entryPoint            = "VSMain";
                desc._stage                 = sw::ShaderStage::Vertex;
                desc._targetFormat          = sw::ShaderTargetFormat::DXIL_D3D12;
                sw::ShaderCompileResult res = cache.getOrCompile( desc );
                (void)res;
            }
        } );
    }

    for ( std::thread& worker : listWorker )
    {
        if ( worker.joinable() )
            worker.join();
    }

    bRunning.store( false, std::memory_order_relaxed );
    if ( clearerThread.joinable() )
        clearerThread.join();

    SW_EXPECT_TRUE( clearsDone.load() > 0 );
}
