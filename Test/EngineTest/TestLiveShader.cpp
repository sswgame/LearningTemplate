#include "pch.h"

#include "Engine/Graphics/Shader/LiveShaderManager.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) LiveShaderTest — 감시·인클루드 의존
// ------------------------------------------------------------------------------
/**
 * @brief [LiveShaderTest] 초기화와 감시
 */
SW_TEST_CASE( LiveShaderTest, InitializationAndWatch )
{
	const sw::string shaderRelPath = "shaders/testlive.hlsl";
	const sw::string engineFolder  = sw::ResourceUtil::getEngineFolderPath();
	const sw::string fullPath	   = engineFolder.empty() ? shaderRelPath : sw::FileUtil::joinPath( engineFolder, shaderRelPath );

	sw::FileUtil::createDirectory( sw::FileUtil::getDirectoryPart( fullPath ) );
	sw::FileUtil::writeTextFile( fullPath, "// dummy shader" );

	sw::LiveShaderManager manager;
	manager.initialize();

	sw::ShaderCompileDesc vsDesc{};
	vsDesc._filePath	 = shaderRelPath;
	vsDesc._entryPoint	 = "main";
	vsDesc._stage		 = sw::ShaderStage::Vertex;
	vsDesc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;

	bool						 callbackTriggered{ false };
	sw::ShaderRecompiledDelegate delegate = SW_DELEGATE_LAMBDA( sw::ShaderRecompiledDelegate, [&callbackTriggered]( const std::string_view path, const sw::ShaderCompileResult& result )
	{
		(void)path;
		(void)result;
		callbackTriggered = true;
	} );

	manager.watchShader( vsDesc, delegate );

	manager.update();

	manager.shutdown();

	sw::FileUtil::removeFile( fullPath );
}

/**
 * @brief [LiveShaderTest] 셰이더 include 의존성 파싱
 */
SW_TEST_CASE( LiveShaderTest, ShaderIncludeDependencyParsing )
{
	sw::string shaderCode = R"(
#include "Common/Lighting.hlsl"
#include "Common/Math.hlsl"
struct VSInput { float3 pos : POSITION; };
)";

	sw::vector<sw::string> includes = sw::ShaderIncludeResolver::parseIncludes( shaderCode );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( includes.size() ) );
	if ( includes.size() == 2 )
	{
		SW_EXPECT_EQUAL( sw::string( "Common/Lighting.hlsl" ), includes[0] );
		SW_EXPECT_EQUAL( sw::string( "Common/Math.hlsl" ), includes[1] );
	}
}
