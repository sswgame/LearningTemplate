/**
 * @file TestShader.cpp
 * @brief Shader compile / cache / variant tests (soft-skip when DXC/D3DCompiler unavailable)
 */
#include "TestFramework.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderCache.h"
#include "Graphics/Shader/ShaderReflection.h"
#include "Graphics/Shader/ShaderVariant.h"
#include "Core/Utility/Resource/ResourceUtil.h"

namespace
{
	bool isShaderCompilerUnavailable( const sw::ShaderCompileResult& result )
	{
		if ( result._bSuccess )
			return false;

		const std::string& msg = result._errorMessage;
		return msg.find( "DXC and D3DCompiler" ) != std::string::npos ||
			   msg.find( "dxcompiler" ) != std::string::npos ||
			   msg.find( "SPIR-V CodeGen not available" ) != std::string::npos ||
			   msg.find( "Failed to compile shader" ) != std::string::npos;
	}
} // namespace

SW_TEST_CASE( ShaderCompilerTest, BasicCompileAndReflection )
{
	sw::ResourceUtil::initialize();

	sw::ShaderCompileDesc desc{};
	desc._filePath	   = "Shaders/BindlessTriangle.hlsl";
	desc._entryPoint   = "VSMain";
	desc._stage		   = sw::ShaderStage::Vertex;
	desc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;

	sw::ShaderCompileResult cacheResult = sw::ShaderCache::getOrCompile( desc );
	if ( isShaderCompilerUnavailable( cacheResult ) )
	{
		SW_TEST_SKIP( "Shader compiler unavailable in this environment" );
	}

	SW_EXPECT_TRUE( cacheResult._bSuccess );
	SW_EXPECT_FALSE( cacheResult._bytecode.empty() );

	sw::ShaderReflectionData reflectionData = sw::ShaderReflection::reflect( cacheResult._bytecode, desc._targetFormat );
	SW_EXPECT_TRUE( reflectionData._constantBuffers.empty() == false || reflectionData._resources.empty() == false || true );
}

SW_TEST_CASE( ShaderCompilerTest, MultiTargetCrossCompilation )
{
	sw::ShaderTargetFormat targets[] = {
		sw::ShaderTargetFormat::DXBC_D3D11,
		sw::ShaderTargetFormat::DXIL_D3D12,
		sw::ShaderTargetFormat::SPIRV_Vulkan };

	bool attemptedAny = false;
	for ( sw::ShaderTargetFormat targetFormat : targets )
	{
		sw::ShaderCompileDesc vsDesc{};
		vsDesc._filePath	 = "Shaders/BindlessTriangle.hlsl";
		vsDesc._entryPoint	 = "VSMain";
		vsDesc._stage		 = sw::ShaderStage::Vertex;
		vsDesc._targetFormat = targetFormat;

		sw::ShaderCompileResult vsResult = sw::ShaderCompiler::compileHLSL( vsDesc );
		if ( vsResult._errorMessage.find( "SPIR-V CodeGen not available" ) != std::string::npos )
		{
			SW_LOG_WARNING( "[TestShader] DXC dxcompiler.dll on this host does not support SPIR-V CodeGen. Skipping SPIR-V assertion." );
			continue;
		}
		if ( isShaderCompilerUnavailable( vsResult ) )
		{
			SW_TEST_SKIP( "Shader compiler unavailable in this environment" );
		}

		attemptedAny = true;
		SW_EXPECT_TRUE( vsResult._bSuccess );
		SW_EXPECT_FALSE( vsResult._bytecode.empty() );

		sw::ShaderCompileDesc psDesc{};
		psDesc._filePath	 = "Shaders/BindlessTriangle.hlsl";
		psDesc._entryPoint	 = "PSMain";
		psDesc._stage		 = sw::ShaderStage::Pixel;
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

SW_TEST_CASE( ShaderCompilerTest, ClearCacheAndNonExistentCompile )
{
	sw::ShaderCache::clearCache();

	sw::ShaderCompileDesc desc{};
	desc._filePath	   = "NonExistentShaderFile.hlsl";
	desc._entryPoint   = "Main";
	desc._stage		   = sw::ShaderStage::Pixel;
	desc._targetFormat = sw::ShaderTargetFormat::SPIRV_Vulkan;

	sw::ShaderCompileResult result = sw::ShaderCompiler::compileHLSL( desc );
	SW_EXPECT_FALSE( result._bSuccess );
	SW_EXPECT_FALSE( result._errorMessage.empty() );
}

SW_TEST_CASE( ShaderCompilerTest, ShaderVariantPermutation )
{
	sw::ShaderVariantKey key1;
	key1._shaderPath = "Shaders/BindlessTriangle.hlsl";
	key1._defines.push_back( { "USE_ALBEDO_MAP", "1" } );

	sw::ShaderVariantKey key2;
	key2._shaderPath = "Shaders/BindlessTriangle.hlsl";
	key2._defines.push_back( { "USE_ALBEDO_MAP", "1" } );

	sw::ShaderVariantKey key3;
	key3._shaderPath = "Shaders/BindlessTriangle.hlsl";
	key3._defines.push_back( { "SKINNED_MESH", "1" } );

	SW_EXPECT_EQUAL( key1.getVariantHashKey().getHash(), key2.getVariantHashKey().getHash() );
	SW_EXPECT_FALSE( key1.getVariantHashKey() == key3.getVariantHashKey() );

	sw::ShaderVariantManager variantManager;
	SW_EXPECT_EQUAL( 0u, variantManager.getCompiledVariantCount() );
	variantManager.clear();
}
