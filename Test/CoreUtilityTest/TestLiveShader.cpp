/**
 * @file TestLiveShader.cpp
 * @brief Auto-generated documentation header
 */
#include "TestFramework.h"
#include "Graphics/Shader/LiveShaderManager.h"
#include "Graphics/Shader/ShaderCompiler.h"

SW_TEST_CASE( LiveShaderTest, InitializationAndWatch )
{
	sw::LiveShaderManager manager;
	manager.initialize();

	sw::ShaderCompileDesc vsDesc{};
	vsDesc._filePath	 = "Shaders/TestLive.hlsl";
	vsDesc._entryPoint	 = "main";
	vsDesc._stage		 = sw::ShaderStage::Vertex;
	vsDesc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;

	bool callbackTriggered = false;
	auto callback		   = [&callbackTriggered]( const std::string& path, const sw::ShaderCompileResult& result )
	{
		(void)path;
		(void)result;
		callbackTriggered = true;
	};
	sw::ShaderRecompiledDelegate delegate = SW_DELEGATE_LAMBDA( sw::ShaderRecompiledDelegate, callback );

	manager.watchShader( vsDesc, delegate );

	manager.update();

	manager.shutdown();
}

SW_TEST_CASE( LiveShaderTest, ShaderIncludeDependencyParsing )
{
	std::string shaderCode = R"(
#include "Common/Lighting.hlsl"
#include "Common/Math.hlsl"
struct VSInput { float3 pos : POSITION; };
)";

	std::vector<std::string> includes = sw::ShaderIncludeResolver::parseIncludes( shaderCode );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( includes.size() ) );
	if ( includes.size() == 2 )
	{
		SW_EXPECT_EQUAL( std::string( "Common/Lighting.hlsl" ), includes[0] );
		SW_EXPECT_EQUAL( std::string( "Common/Math.hlsl" ), includes[1] );
	}
}
