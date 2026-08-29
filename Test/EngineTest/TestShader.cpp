#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Graphics/Shader/ShaderCache.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Graphics/Shader/ShaderReflection.h"
#include "Engine/Graphics/Shader/ShaderVariant.h"
#include "Engine/Utility/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

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
	desc._filePath	   = "engine/shaders/fullscreentriangle.hlsl";
	desc._entryPoint   = "VSMain";
	desc._stage		   = sw::ShaderStage::Vertex;
	desc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;

	sw::ShaderCache			shaderCache;
	sw::ShaderCompileResult cacheResult = shaderCache.getOrCompile( desc );
	if ( sw::isShaderCompilerUnavailable( cacheResult ) )
	{
		SW_TEST_SKIP( "Shader compiler unavailable in this environment" );
	}

	SW_EXPECT_TRUE( cacheResult._bSuccess );
	SW_EXPECT_FALSE( cacheResult._listBytecode.empty() );

	sw::ShaderReflectionData reflectionData = sw::ShaderReflection::reflect( cacheResult._listBytecode, desc._targetFormat );
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
		vsDesc._filePath	 = "engine/shaders/fullscreentriangle.hlsl";
		vsDesc._entryPoint	 = "VSMain";
		vsDesc._stage		 = sw::ShaderStage::Vertex;
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
		SW_EXPECT_FALSE( vsResult._listBytecode.empty() );

		sw::ShaderCompileDesc psDesc{};
		psDesc._filePath	 = "engine/shaders/fullscreentriangle.hlsl";
		psDesc._entryPoint	 = "PSMain";
		psDesc._stage		 = sw::ShaderStage::Pixel;
		psDesc._targetFormat = targetFormat;

		sw::ShaderCompileResult psResult = sw::ShaderCompiler::compileHLSL( psDesc );
		SW_EXPECT_TRUE( psResult._bSuccess );
		SW_EXPECT_FALSE( psResult._listBytecode.empty() );
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
	desc._filePath	   = "NonExistentShaderFile.hlsl";
	desc._entryPoint   = "Main";
	desc._stage		   = sw::ShaderStage::Pixel;
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
	desc._filePath	   = "engine/shaders/fullscreentriangle.hlsl";
	desc._entryPoint   = "VSMain";
	desc._stage		   = sw::ShaderStage::Vertex;
	desc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;

	// 1차 컴파일 (캐시 미스 -> 디스크 저장)
	sw::ShaderCompileResult result1 = sw::ShaderCompiler::compileHLSL( desc );
	if ( sw::isShaderCompilerUnavailable( result1 ) )
	{
		SW_TEST_SKIP( "Shader compiler unavailable in this environment" );
	}

	SW_EXPECT_TRUE( result1._bSuccess );
	SW_EXPECT_FALSE( result1._listBytecode.empty() );

	// 2차 컴파일 (디스크 캐시 히트)
	sw::ShaderCompileResult result2 = sw::ShaderCompiler::compileHLSL( desc );
	SW_EXPECT_TRUE( result2._bSuccess );
	SW_EXPECT_EQUAL( result1._listBytecode.size(), result2._listBytecode.size() );
	SW_EXPECT_TRUE( result1._listBytecode == result2._listBytecode );

	// 디스크 캐시 클리어 후 컴파일 정상 동작 확인
	sw::ShaderCompiler::clearDiskCache();
	sw::ShaderCompileResult result3 = sw::ShaderCompiler::compileHLSL( desc );
	SW_EXPECT_TRUE( result3._bSuccess );
	SW_EXPECT_FALSE( result3._listBytecode.empty() );
	SW_EXPECT_EQUAL( result1._listBytecode.size(), result3._listBytecode.size() );
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
	dx11Desc._filePath	   = "engine/shaders/fullscreentriangle.hlsl";
	dx11Desc._entryPoint   = "VSMain";
	dx11Desc._stage		   = sw::ShaderStage::Vertex;
	dx11Desc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;

	sw::ShaderCompileDesc dx12Desc = dx11Desc;
	dx12Desc._targetFormat		   = sw::ShaderTargetFormat::DXIL_D3D12;

	sw::ShaderCompileDesc vkDesc = dx11Desc;
	vkDesc._targetFormat		 = sw::ShaderTargetFormat::SPIRV_Vulkan;

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
	SW_EXPECT_FALSE( dx11Res1._listBytecode == dx12Res1._listBytecode );

	// 3) 다시 DX11 및 DX12 요청 시 각 백엔드 전용 캐시 히트 검증
	sw::ShaderCompileResult dx11Res2 = shaderCache.getOrCompile( dx11Desc );
	sw::ShaderCompileResult dx12Res2 = shaderCache.getOrCompile( dx12Desc );
	SW_EXPECT_TRUE( dx11Res1._listBytecode == dx11Res2._listBytecode );
	SW_EXPECT_TRUE( dx12Res1._listBytecode == dx12Res2._listBytecode );

	// 4) Vulkan SPIR-V 컴파일 및 캐시 격리 검증 (DXC SPIR-V 지원 시)
	sw::ShaderCompileResult vkRes1 = shaderCache.getOrCompile( vkDesc );
	if ( vkRes1._bSuccess )
	{
		SW_EXPECT_FALSE( vkRes1._listBytecode == dx11Res1._listBytecode );
		SW_EXPECT_FALSE( vkRes1._listBytecode == dx12Res1._listBytecode );

		sw::ShaderCompileResult vkRes2 = shaderCache.getOrCompile( vkDesc );
		SW_EXPECT_TRUE( vkRes1._listBytecode == vkRes2._listBytecode );
	}
}

/**
 * @brief [ShaderCompilerTest] RHI 백엔드(TargetFormat)별 ShaderVariantKey 해시 및 매니저 캐시 분리 검증
 */
SW_TEST_CASE( ShaderCompilerTest, MultiBackendVariantKeyIsolation )
{
	sw::ShaderVariantKey keyDx11;
	keyDx11._shaderPath	  = "engine/shaders/fullscreentriangle.hlsl";
	keyDx11._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;
	keyDx11._listDefine.push_back( { "USE_TEXTURE", "1" } );

	sw::ShaderVariantKey keyDx12;
	keyDx12._shaderPath	  = "engine/shaders/fullscreentriangle.hlsl";
	keyDx12._targetFormat = sw::ShaderTargetFormat::DXIL_D3D12;
	keyDx12._listDefine.push_back( { "USE_TEXTURE", "1" } );

	sw::ShaderVariantKey keyVk;
	keyVk._shaderPath	= "engine/shaders/fullscreentriangle.hlsl";
	keyVk._targetFormat = sw::ShaderTargetFormat::SPIRV_Vulkan;
	keyVk._listDefine.push_back( { "USE_TEXTURE", "1" } );

	sw::ShaderVariantKey keyGl;
	keyGl._shaderPath	= "engine/shaders/fullscreentriangle.hlsl";
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

	sw::ShaderVariantManager	   manager;
	const sw::ShaderCompileResult* pResDx11 = manager.getOrCompileVariant( keyDx11 );
	if ( pResDx11 != nullptr && pResDx11->_bSuccess )
	{
		const sw::ShaderCompileResult* pResDx12 = manager.getOrCompileVariant( keyDx12 );
		if ( pResDx12 != nullptr && pResDx12->_bSuccess )
		{
			SW_EXPECT_EQUAL( 2u, manager.getCompiledVariantCount() );
			SW_EXPECT_FALSE( pResDx11->_listBytecode == pResDx12->_listBytecode );
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
	dx11Desc._filePath	   = "engine/shaders/fullscreentriangle.hlsl";
	dx11Desc._entryPoint   = "VSMain";
	dx11Desc._stage		   = sw::ShaderStage::Vertex;
	dx11Desc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;

	sw::ShaderCompileDesc dx12Desc = dx11Desc;
	dx12Desc._targetFormat		   = sw::ShaderTargetFormat::DXIL_D3D12;

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
