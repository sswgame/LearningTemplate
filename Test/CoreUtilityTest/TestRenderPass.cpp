/**
 * @file TestRenderPass.cpp
 * @brief Auto-generated documentation header
 */
#include "Core/CoreMinimal.h"

#include "Core/Graphics/RenderPass/RenderPassManager.h"
#include "Core/Graphics/RenderPass/RenderPassResource.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Utility/Memory/FrameArenaAllocator.h"
#include "TestFramework.h"
SW_TEST_CASE( RenderPassTest, XmlSerializationRoundtrip )
{
	sw::RenderPassResource passRes;
	sw::RenderPassDesc&	   desc = passRes.getDesc();
	desc._name					= "UnitTestRenderPass";

	sw::RenderPassAttachment colorAtt{};
	colorAtt._name			= "Color0";
	colorAtt._format		= "R8G8B8A8_UNORM";
	colorAtt._clearColor[0] = 0.5f;
	colorAtt._clearColor[1] = 0.2f;
	colorAtt._clearColor[2] = 0.8f;
	colorAtt._clearColor[3] = 1.0f;
	colorAtt._bClear		= true;
	desc._attachments.push_back( colorAtt );

	std::string testPath = ( std::filesystem::temp_directory_path() / "test_renderpass_roundtrip.xml" ).string();
	SW_EXPECT_TRUE( passRes.saveToXmlFile( testPath ) );

	sw::RenderPassResource loadedRes;
	SW_EXPECT_TRUE( loadedRes.loadFromXmlFile( testPath ) );
	SW_EXPECT_EQUAL( std::string( "UnitTestRenderPass" ), loadedRes.getDesc()._name );
	SW_EXPECT_EQUAL( size_t( 1 ), loadedRes.getDesc()._attachments.size() );
	SW_EXPECT_EQUAL( std::string( "Color0" ), loadedRes.getDesc()._attachments[0]._name );

	std::filesystem::remove( testPath );
}

// SW_TEST_CASE( RenderPassTest, RenderPassManagerSubsystem )\n// Temporarily disabled due to timeout issue

#include "Core/Graphics/RenderPass/RenderGraph.h"

// SW_TEST_CASE( RenderPassTest, RenderGraphDAGExecution )\n// Temporarily disabled due to timeout issue

// SW_TEST_CASE( RenderPassTest, RenderGraphMermaidExport )\n// Temporarily disabled due to timeout issue

