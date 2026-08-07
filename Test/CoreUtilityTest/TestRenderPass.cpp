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

#include "Core/Graphics/RenderPass/RenderGraph.h"

SW_TEST_CASE( RenderPassTest, RenderGraphCompileOrder )
{
	sw::RenderGraph graph;
	SW_EXPECT_FALSE( graph.compile() );

	graph.addPass( sw::hashed_string( "Depth" ), {}, { sw::hashed_string( "DepthBuffer" ) } );
	graph.addPass( sw::hashed_string( "Shading" ), { sw::hashed_string( "DepthBuffer" ) }, { sw::hashed_string( "Color" ) } );

	SW_ASSERT_TRUE( graph.compile() );
	SW_EXPECT_EQUAL( 2u, graph.getNodeCount() );
	SW_ASSERT_EQUAL( size_t( 2 ), graph.getExecutionOrder().size() );
	SW_EXPECT_TRUE( graph.getExecutionOrder()[0] == sw::hashed_string( "Depth" ) );
	SW_EXPECT_TRUE( graph.getExecutionOrder()[1] == sw::hashed_string( "Shading" ) );
}

SW_TEST_CASE( RenderPassTest, RenderGraphCullUnusedPasses )
{
	sw::RenderGraph graph;
	graph.addPass( sw::hashed_string( "Depth" ), {}, { sw::hashed_string( "DepthBuffer" ) } );
	graph.addPass( sw::hashed_string( "DebugOverlay" ), {}, { sw::hashed_string( "DebugRT" ) } );
	graph.addPass( sw::hashed_string( "Present" ), { sw::hashed_string( "DepthBuffer" ) }, { sw::hashed_string( "Swapchain" ) } );

	graph.cullUnusedPasses( sw::hashed_string( "Swapchain" ) );

	SW_EXPECT_FALSE( graph.isPassCulled( sw::hashed_string( "Depth" ) ) );
	SW_EXPECT_TRUE( graph.isPassCulled( sw::hashed_string( "DebugOverlay" ) ) );
	SW_EXPECT_FALSE( graph.isPassCulled( sw::hashed_string( "Present" ) ) );

	const std::vector<sw::hashed_string>& order = graph.getExecutionOrder();
	SW_EXPECT_EQUAL( size_t( 2 ), order.size() );
	for ( const sw::hashed_string& pass : order )
	{
		SW_EXPECT_TRUE( pass != sw::hashed_string( "DebugOverlay" ) );
	}
}

SW_TEST_CASE( RenderPassTest, RenderGraphMermaidAndDotExport )
{
	sw::RenderGraph graph;
	graph.addPass( sw::hashed_string( "PassA" ), {}, { sw::hashed_string( "RT0" ) } );
	graph.addPass( sw::hashed_string( "PassB" ), { sw::hashed_string( "RT0" ) }, { sw::hashed_string( "RT1" ) } );
	SW_ASSERT_TRUE( graph.compile() );

	const std::string mermaid = graph.exportToMermaid();
	SW_EXPECT_TRUE_MSG( mermaid.find( "graph TD" ) != std::string::npos, "Mermaid export should start with graph TD" );
	SW_EXPECT_TRUE( mermaid.find( "PassA" ) != std::string::npos );
	SW_EXPECT_TRUE( mermaid.find( "RT0" ) != std::string::npos );

	const std::string dot = graph.exportToDot();
	SW_EXPECT_TRUE_MSG( dot.find( "digraph" ) != std::string::npos, "DOT export should declare a digraph" );
	SW_EXPECT_TRUE( dot.find( "PassB" ) != std::string::npos );

	graph.clear();
	SW_EXPECT_EQUAL( 0u, graph.getNodeCount() );
}
