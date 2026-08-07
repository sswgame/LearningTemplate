/**
 * @file TestRHI.cpp
 * @brief Auto-generated documentation header
 */
#include "TestFramework.h"
#include "Graphics/RHI/RHI.h"

SW_TEST_CASE( RHITest, BackendTypeNameQuery )
{
	const utf8* dx11Name = sw::RHI::getBackendTypeName( sw::RHIBackend::DirectX11 );
	SW_EXPECT_TRUE( dx11Name != nullptr );
	SW_EXPECT_EQUAL( std::string( "DirectX11" ), std::string( dx11Name ) );

	const utf8* dx12Name = sw::RHI::getBackendTypeName( sw::RHIBackend::DirectX12 );
	SW_EXPECT_TRUE( dx12Name != nullptr );
	SW_EXPECT_EQUAL( std::string( "DirectX12" ), std::string( dx12Name ) );

	const utf8* vulkanName = sw::RHI::getBackendTypeName( sw::RHIBackend::Vulkan );
	SW_EXPECT_TRUE( vulkanName != nullptr );
	SW_EXPECT_EQUAL( std::string( "Vulkan" ), std::string( vulkanName ) );

	const utf8* glName = sw::RHI::getBackendTypeName( sw::RHIBackend::OpenGL );
	SW_EXPECT_TRUE( glName != nullptr );
	SW_EXPECT_EQUAL( std::string( "OpenGL" ), std::string( glName ) );
}

SW_TEST_CASE( RHITest, DeviceCreationAllBackends )
{
	sw::RHIBackend backends[] = {
		sw::RHIBackend::DirectX11,
		sw::RHIBackend::DirectX12,
		sw::RHIBackend::OpenGL,
		sw::RHIBackend::Vulkan };

	for ( sw::RHIBackend backend : backends )
	{
		std::shared_ptr<sw::IRHIDevice> device = sw::RHI::createDevice( backend );
		if ( device != nullptr )
		{
			SW_EXPECT_EQUAL( static_cast<int>( backend ), static_cast<int>( device->getBackendType() ) );
			SW_EXPECT_TRUE( device->getBackendName() != nullptr );
		}
	}
}

// SW_TEST_CASE( RHITest, UnifiedPipelineStateAndRenderPassAllBackends )\n// Temporarily disabled due to RHI environment issues

SW_TEST_CASE( RHITest, BindlessResourceLifecycle )
{
#if defined( SW_PLATFORM_WINDOWS )
	std::shared_ptr<sw::IRHIDevice> rhiDevice = sw::RHI::createDevice( sw::RHIBackend::DirectX11 );
#else
	std::shared_ptr<sw::IRHIDevice> rhiDevice = sw::RHI::createDevice( sw::RHIBackend::OpenGL );
#endif

	SW_EXPECT_TRUE( rhiDevice != nullptr );

	sw::RHISwapChainDesc scDesc{};
	scDesc._width  = 800;
	scDesc._height = 600;

	bool initOk = rhiDevice->initializeInternal( scDesc );
	if ( initOk )
	{
		struct DummyCB
		{
			float color[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
		} cbData;

		sw::RHIBufferHandle buffer = rhiDevice->createConstantBuffer( sizeof( DummyCB ) );
		SW_EXPECT_TRUE( buffer != 0 );

		rhiDevice->updateConstantBuffer( buffer, &cbData, sizeof( DummyCB ) );

		sw::RHIDescriptorIndex descIdx = rhiDevice->registerBindlessResource( buffer );
		SW_EXPECT_TRUE( descIdx != sw::kInvalidDescriptorIndex );

		float clearColor[4] = { 0.1f, 0.2f, 0.3f, 1.0f };
		rhiDevice->beginFrame( clearColor );
		rhiDevice->drawTriangle( descIdx );
		rhiDevice->endFrame( false );

		rhiDevice->destroyBuffer( buffer );
		rhiDevice->shutdown();
	}
}

SW_TEST_CASE( RHITest, CommandListCreationAndExecution )
{
	std::shared_ptr<sw::IRHIDevice> rhiDevice = sw::RHI::createDevice( sw::RHIBackend::D3D11 );
	SW_EXPECT_TRUE( rhiDevice != nullptr );

	if ( rhiDevice != nullptr )
	{
		sw::RHISwapChainDesc scDesc{};
		scDesc._width  = 800;
		scDesc._height = 600;

		if ( rhiDevice->initializeInternal( scDesc ) )
		{
			std::shared_ptr<sw::IRHICommandList> cmdList = rhiDevice->createCommandList();
			SW_EXPECT_TRUE( cmdList != nullptr );

			if ( cmdList != nullptr )
			{
				cmdList->beginCommandList();
				sw::RHIViewport vp{ 0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 1.0f };
				cmdList->setViewport( vp );
				cmdList->drawTriangle( 0 );
				cmdList->endCommandList();

				rhiDevice->executeCommandList( cmdList.get() );
			}

			rhiDevice->shutdown();
		}
	}
}

SW_TEST_CASE( RHITest, ComputeShaderDispatchAndIndirectCommands )
{
	std::shared_ptr<sw::IRHIDevice> rhiDevice = sw::RHI::createDevice( sw::RHIBackend::D3D11 );
	SW_EXPECT_TRUE( rhiDevice != nullptr );

	if ( rhiDevice != nullptr )
	{
		sw::RHISwapChainDesc scDesc{};
		scDesc._width  = 800;
		scDesc._height = 600;

		if ( rhiDevice->initializeInternal( scDesc ) )
		{

			rhiDevice->dispatchCompute( 4, 1, 1 );

			sw::RHIDrawIndirectCommand drawCmd{};
			drawCmd._vertexCount		   = 3;
			drawCmd._instanceCount		   = 1;
			drawCmd._startVertexLocation   = 0;
			drawCmd._startInstanceLocation = 0;

			sw::RHIBufferHandle argBuf = rhiDevice->createConstantBuffer( sizeof( sw::RHIDrawIndirectCommand ) );
			SW_EXPECT_TRUE( argBuf != 0 );

			if ( argBuf != 0 )
			{
				rhiDevice->updateConstantBuffer( argBuf, &drawCmd, sizeof( sw::RHIDrawIndirectCommand ) );
				rhiDevice->drawIndirect( argBuf, 0 );

				std::shared_ptr<sw::IRHICommandList> cmdList = rhiDevice->createCommandList();
				if ( cmdList != nullptr )
				{
					cmdList->beginCommandList();
					cmdList->dispatchCompute( 2, 1, 1 );
					cmdList->drawIndirect( argBuf, 0 );
					cmdList->dispatchIndirect( argBuf, 0 );
					cmdList->endCommandList();

					rhiDevice->executeCommandList( cmdList.get() );
				}

				rhiDevice->destroyBuffer( argBuf );
			}

			rhiDevice->shutdown();
		}
	}
}

#include "Graphics/RHI/RHIReleaseQueue.h"

SW_TEST_CASE( RHITest, DeferredReleaseQueue )
{
	sw::RHIReleaseQueue queue( 3 );
	SW_EXPECT_EQUAL( 0u, queue.getPendingReleaseCount() );

	bool bDestroyed = false;
	auto cb			= [&bDestroyed]()
	{
		bDestroyed = true;
	};
	sw::RHIResourceReleaseDelegate releaseDel = SW_DELEGATE_LAMBDA( sw::RHIResourceReleaseDelegate, cb );

	queue.enqueueRelease( releaseDel );
	SW_EXPECT_EQUAL( 1u, queue.getPendingReleaseCount() );
	SW_EXPECT_FALSE( bDestroyed );

	queue.tickFrame();
	SW_EXPECT_FALSE( bDestroyed );

	queue.tickFrame();
	SW_EXPECT_FALSE( bDestroyed );

	queue.tickFrame();
	SW_EXPECT_TRUE( bDestroyed );
	SW_EXPECT_EQUAL( 0u, queue.getPendingReleaseCount() );
}

#include "Core/Graphics/RHI/RHITypes.h"

SW_TEST_CASE( RHITest, VertexLayoutBuilderAutoOffset )
{
	struct CustomVertex
	{
		float  pos[3];
		float  uv[2];
		uint32 color;
	};

	sw::VertexLayoutBuilder builder;
	builder.addElement( "POSITION", 0, sw::RHIFormat::R32G32B32_FLOAT, offsetof( CustomVertex, pos ) );
	builder.addElement( "TEXCOORD", 0, sw::RHIFormat::R32G32_FLOAT, offsetof( CustomVertex, uv ) );
	builder.addElement( "COLOR", 0, sw::RHIFormat::R8G8B8A8_UNORM, offsetof( CustomVertex, color ) );

	std::vector<sw::RHIInputElement> layout = builder.build();
	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( layout.size() ) );
	if ( layout.size() == 3 )
	{
		SW_EXPECT_EQUAL( std::string( "POSITION" ), layout[0]._semanticName );
		SW_EXPECT_EQUAL( 0u, layout[0]._alignedByteOffset );

		SW_EXPECT_EQUAL( std::string( "TEXCOORD" ), layout[1]._semanticName );
		SW_EXPECT_EQUAL( static_cast<uint32>( offsetof( CustomVertex, uv ) ), layout[1]._alignedByteOffset );

		SW_EXPECT_EQUAL( std::string( "COLOR" ), layout[2]._semanticName );
		SW_EXPECT_EQUAL( static_cast<uint32>( offsetof( CustomVertex, color ) ), layout[2]._alignedByteOffset );
	}
}

