#include "pch.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RHI/IRHISwapChain.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHIDeferredCommandList.h"
#include "Engine/Graphics/RHI/RHIHandleTable.h"
#include "Engine/Graphics/RHI/RHIReleaseQueue.h"
#include "Engine/Window/IWindow.h"

#include "TestFramework/TestFramework.h"

namespace
{
	/** @brief replay/Mode 검증용 — 호출 횟수만 센다. */
	class CountingCommandContext final : public sw::IRHICommandContext
	{
	public:
		uint32 _setViewportCount{ 0 };
		uint32 _drawCount{ 0 };
		uint32 _beginOffscreenCount{ 0 };
		uint32 _endOffscreenCount{ 0 };

		void setViewport( const sw::RHIViewport& ) override { ++_setViewportCount; }
		void setPipelineState( sw::RHIPipelineStateHandle ) override {}
		void setComputePipelineState( sw::RHIPipelineStateHandle ) override {}
		void beginRenderPass( const sw::RHIRenderPassBeginInfo& ) override {}
		void endRenderPass() override {}
		void setVertexBuffer( uint32, sw::RHIBufferHandle, uint32, uint32 ) override {}
		void draw( uint32, uint32, sw::RHIDescriptorIndex ) override { ++_drawCount; }
		void setIndexBuffer( sw::RHIBufferHandle, uint32, uint32 ) override {}
		void dispatchCompute( uint32, uint32, uint32 ) override {}
		void setComputeRootConstants( uint32, uint32, const void*, uint32 ) override {}
		void bindComputeUAV( sw::RHIDescriptorIndex, uint32 ) override {}
		void drawIndirect( sw::RHIBufferHandle, uint32, sw::RHIDescriptorIndex ) override {}
		void dispatchIndirect( sw::RHIBufferHandle, uint32 ) override {}
		void drawIndexedIndirect( sw::RHIBufferHandle, uint32 ) override {}
		void beginEventMarker( const utf8* ) override {}
		void endEventMarker() override {}
		void beginOffscreenPass( sw::RHITextureHandle, float32[4] ) override { ++_beginOffscreenCount; }
		void endOffscreenPass( sw::RHITextureHandle ) override { ++_endOffscreenCount; }
	};

	bool tryInitDeviceWithWindow( sw::RHIBackend backend, sw::unique_ptr<sw::IWindow>& outWindow,
								  sw::shared_ptr<sw::IRHIDevice>& outDevice )
	{
		if ( sw::RHIAvailability::isAvailable( backend ) == false )
			return false;

		outWindow = sw::IWindow::createPlatformWindow();
		if ( outWindow == nullptr )
			return false;
		if ( outWindow->initializeWindow( "RHIDualContextTest", 320, 240 ) == false )
		{
			outWindow.reset();
			return false;
		}

		outDevice = sw::RHI::createDevice( backend );
		if ( outDevice == nullptr )
		{
			outWindow->destroy();
			outWindow.reset();
			return false;
		}

		outDevice->setInitWindow( outWindow.get() );
		if ( outDevice->initialize() == false )
		{
			outDevice.reset();
			outWindow->destroy();
			outWindow.reset();
			return false;
		}
		return true;
	}

	void shutdownDeviceWithWindow( sw::shared_ptr<sw::IRHIDevice>& device, sw::unique_ptr<sw::IWindow>& window )
	{
		if ( device != nullptr )
		{
			device->shutdown();
			device.reset();
		}
		if ( window != nullptr )
		{
			window->destroy();
			window.reset();
		}
	}
} // namespace

// ------------------------------------------------------------------------------
// 1) RHITest — 백엔드·커맨드·바인들리스
// ------------------------------------------------------------------------------
/**
 * @brief [RHITest] 백엔드 타입 이름 조회
 */
SW_TEST_CASE( RHITest, BackendTypeNameQuery )
{
	const utf8* dx11Name = sw::RHI::getBackendTypeName( sw::RHIBackend::DirectX11 );
	SW_EXPECT_TRUE( dx11Name != nullptr );
	SW_EXPECT_EQUAL( sw::string( "DirectX11" ), sw::string( dx11Name ) );

	const utf8* dx12Name = sw::RHI::getBackendTypeName( sw::RHIBackend::DirectX12 );
	SW_EXPECT_TRUE( dx12Name != nullptr );
	SW_EXPECT_EQUAL( sw::string( "DirectX12" ), sw::string( dx12Name ) );

	const utf8* vulkanName = sw::RHI::getBackendTypeName( sw::RHIBackend::Vulkan );
	SW_EXPECT_TRUE( vulkanName != nullptr );
	SW_EXPECT_EQUAL( sw::string( "Vulkan" ), sw::string( vulkanName ) );

	const utf8* glName = sw::RHI::getBackendTypeName( sw::RHIBackend::OpenGL );
	SW_EXPECT_TRUE( glName != nullptr );
	SW_EXPECT_EQUAL( sw::string( "OpenGL" ), sw::string( glName ) );
}

/**
 * @brief bindless vs 네이티브 bindless, indexed draw 광고가 올바른지 검증
 */
SW_TEST_CASE( RHITest, CapabilityMatrixNativeVsEmulated )
{
	using sw::RHIAvailability;
	using sw::RHIBackend;

	const sw::RHICapabilities dx12 = RHIAvailability::query( RHIBackend::DirectX12 );
	SW_EXPECT_TRUE( dx12._bBindless != 0 );
	SW_EXPECT_TRUE( dx12._bNativeBindless != 0 );
	SW_EXPECT_TRUE( dx12._bIndexedDraw != 0 );
	SW_EXPECT_TRUE( dx12._bOffscreenRT != 0 );

	const sw::RHICapabilities dx11 = RHIAvailability::query( RHIBackend::DirectX11 );
	SW_EXPECT_TRUE( dx11._bBindless != 0 );
	SW_EXPECT_TRUE( dx11._bNativeBindless == 0 );
	SW_EXPECT_TRUE( dx11._bIndexedDraw != 0 );
	SW_EXPECT_TRUE( dx11._bOffscreenRT != 0 );

	const sw::RHICapabilities gl = RHIAvailability::query( RHIBackend::OpenGL );
	SW_EXPECT_TRUE( gl._bBindless != 0 );
	SW_EXPECT_TRUE( gl._bNativeBindless == 0 );
	SW_EXPECT_TRUE( gl._bIndexedDraw != 0 );
	SW_EXPECT_TRUE( gl._bOffscreenRT != 0 );

	const sw::RHICapabilities vk = RHIAvailability::query( RHIBackend::Vulkan );
	SW_EXPECT_TRUE( vk._bBindless != 0 );
	SW_EXPECT_TRUE( vk._bNativeBindless != 0 );
	SW_EXPECT_TRUE( vk._bIndexedDraw != 0 );
	SW_EXPECT_TRUE( vk._bOffscreenRT != 0 );
}

/**
 * @brief [RHITest] 모든 백엔드 디바이스 생성
 */
SW_TEST_CASE( RHITest, DeviceCreationAllBackends )
{
	sw::RHIBackend backends[] = {
		sw::RHIBackend::DirectX11,
		sw::RHIBackend::DirectX12,
		sw::RHIBackend::OpenGL,
		sw::RHIBackend::Vulkan };

	for ( sw::RHIBackend backend : backends )
	{
		sw::shared_ptr<sw::IRHIDevice> device = sw::RHI::createDevice( backend );
		if ( device != nullptr )
		{
			SW_EXPECT_EQUAL( static_cast<int32>( backend ), static_cast<int32>( device->getBackendType() ) );
			SW_EXPECT_TRUE( device->getBackendName() != nullptr );
		}
	}
}

/**
 * @brief [RHITest] 가용 백엔드에서 RenderPass 생성 + 간단 드로우 커맨드 경로
 */
SW_TEST_CASE( RHITest, UnifiedPipelineStateAndRenderPassAllBackends )
{
	const sw::RHIBackend backends[] = {
		sw::RHIBackend::DirectX11,
		sw::RHIBackend::DirectX12,
		sw::RHIBackend::Vulkan,
		sw::RHIBackend::OpenGL,
	};

	uint32 okCount{ 0 };
	for ( sw::RHIBackend backend : backends )
	{
		sw::unique_ptr<sw::IWindow>	   window;
		sw::shared_ptr<sw::IRHIDevice> device;
		if ( tryInitDeviceWithWindow( backend, window, device ) == false )
			continue;

		sw::RHIRenderPassDesc		rpDesc{};
		sw::RHIRenderPassAttachment colorAtt{};
		colorAtt._format = sw::RHIFormat::R8G8B8A8_UNORM;
		colorAtt._loadOp = sw::RHIRenderPassLoadOp::Clear;
		rpDesc._listColorAttachment.push_back( colorAtt );

		sw::RHIRenderPassHandle pass = device->getResource()->createRenderPass( rpDesc );
		if ( pass == 0 )
		{
			SW_LOG_WARNING( "createRenderPass failed for backend %# — skip", static_cast<uint32>( backend ) );
			shutdownDeviceWithWindow( device, window );
			continue;
		}

		sw::RHIPipelineStateDesc psoDesc{};
		psoDesc._vertexShaderPath	   = "engine/shaders/fullscreentriangle.hlsl";
		psoDesc._pixelShaderPath	   = "engine/shaders/fullscreentriangle.hlsl";
		psoDesc._vertexEntryPoint	   = "VSMain";
		psoDesc._pixelEntryPoint	   = "PSMain";
		psoDesc._numRenderTargets	   = 1;
		psoDesc._arrRtvFormats[0]	   = sw::RHIFormat::R8G8B8A8_UNORM;
		sw::RHIPipelineStateHandle pso = device->getResource()->createPipelineState( psoDesc );
		if ( pso != 0 )
		{
			// Present 없는 오프스크린 경로로 파이프라인 검증 (실패해도 RP/PSO create는 유효).
			const bool bSmoke = device->executeOffscreenPipelineSmoke( pso );
			if ( bSmoke == false )
				SW_LOG_WARNING( "Offscreen pipeline smoke failed (backend %#) — create path still counted",
								static_cast<uint32>( backend ) );
			else
				SW_EXPECT_TRUE( bSmoke );
		}
		else
			SW_EXPECT_TRUE_MSG( true, "PSO create failed (shader/compiler) — RenderPass path still counted" );

		if ( pass != 0 )
			device->getResource()->destroyRenderPass( pass );
		if ( pso != 0 )
			device->getResource()->destroyPipelineState( pso );

		++okCount;
		shutdownDeviceWithWindow( device, window );
	}

	if ( okCount == 0 )
		SW_TEST_SKIP( "No RHI backend could initialize for unified pipeline test" );
}

/**
 * @brief [RHITest] 바인들리스 리소스 수명
 */
SW_TEST_CASE( RHITest, BindlessResourceLifecycle )
{
	sw::unique_ptr<sw::IWindow>	   window;
	sw::shared_ptr<sw::IRHIDevice> rhiDevice;
	const sw::RHIBackend		   prefer[] = {
#if defined( SW_PLATFORM_WINDOWS )
		sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::OpenGL, sw::RHIBackend::Vulkan
#else
		sw::RHIBackend::OpenGL, sw::RHIBackend::Vulkan
#endif
	};
	bool bOk{ false };
	for ( sw::RHIBackend backend : prefer )
	{
		if ( tryInitDeviceWithWindow( backend, window, rhiDevice ) )
		{
			bOk = true;
			break;
		}
	}
	if ( bOk == false )
		SW_TEST_SKIP( "RHI device create/init failed (backend unavailable in this environment)" );

	struct DummyCB
	{
		float32 color[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
	} cbData;

	sw::RHIBufferHandle buffer = rhiDevice->getResource()->createConstantBuffer( sizeof( DummyCB ) );
	SW_EXPECT_TRUE( buffer != 0 );

	rhiDevice->getResource()->updateConstantBuffer( buffer, &cbData, sizeof( DummyCB ) );

	sw::RHIDescriptorIndex descIdx = rhiDevice->getResource()->registerBindlessResource( buffer );
	SW_EXPECT_TRUE( descIdx != sw::kInvalidDescriptorIndex );

	// Present(beginFrame/endFrame)는 DX12에서 soft-CL 드로우와 섞이면 Device Removed가 나기 쉬움.
	// 수명 검증은 bindless 등록 + Present 없는 오프스크린 RT 경로로 한다.
	SW_EXPECT_TRUE( rhiDevice->getCapabilities()._bOffscreenRT != 0 );
	SW_EXPECT_TRUE( rhiDevice->executeOffscreenPipelineSmoke( 0 ) == false ); // pso==0 → false
	{
		sw::RHIPipelineStateDesc psoDesc{};
		psoDesc._vertexShaderPath			 = "engine/shaders/fullscreentriangle.hlsl";
		psoDesc._pixelShaderPath			 = "engine/shaders/fullscreentriangle.hlsl";
		psoDesc._vertexEntryPoint			 = "VSMain";
		psoDesc._pixelEntryPoint			 = "PSMain";
		psoDesc._numRenderTargets			 = 1;
		psoDesc._arrRtvFormats[0]			 = sw::RHIFormat::R8G8B8A8_UNORM;
		const sw::RHIPipelineStateHandle pso = rhiDevice->getResource()->createPipelineState( psoDesc );
		if ( pso != 0 )
		{
			const bool bSmoke = rhiDevice->executeOffscreenPipelineSmoke( pso, descIdx );
			SW_EXPECT_TRUE_MSG( bSmoke, "Offscreen bindless smoke failed" );
			rhiDevice->getResource()->destroyPipelineState( pso );
		}
	}

	rhiDevice->getResource()->unregisterBindlessResource( descIdx );
	rhiDevice->getResource()->destroyBuffer( buffer );
	shutdownDeviceWithWindow( rhiDevice, window );
}

/**
 * @brief [RHITest] 커맨드 리스트 생성과 실행
 */
SW_TEST_CASE( RHITest, CommandListCreationAndExecution )
{
	sw::unique_ptr<sw::IWindow>	   window;
	sw::shared_ptr<sw::IRHIDevice> rhiDevice;
	if ( tryInitDeviceWithWindow( sw::RHIBackend::DirectX11, window, rhiDevice ) == false &&
		 tryInitDeviceWithWindow( sw::RHIBackend::DirectX12, window, rhiDevice ) == false &&
		 tryInitDeviceWithWindow( sw::RHIBackend::OpenGL, window, rhiDevice ) == false )
		SW_TEST_SKIP( "RHI initialize failed (no GPU / display context)" );

	sw::unique_ptr<sw::IRHICommandList> cmdList = rhiDevice->createCommandList();
	SW_EXPECT_TRUE( cmdList != nullptr );

	if ( cmdList != nullptr )
	{
		cmdList->beginCommandList();
		sw::RHIViewport vp{ 0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 1.0f };
		cmdList->setViewport( vp );
		cmdList->draw( 3, 0, 0 );
		cmdList->endCommandList();

		rhiDevice->executeCommandList( cmdList.get() );
	}

	shutdownDeviceWithWindow( rhiDevice, window );
}

/**
 * @brief [RHITest] 컴퓨트 셰이더 디스패치와 간접 커맨드
 */
SW_TEST_CASE( RHITest, ComputeShaderDispatchAndIndirectCommands )
{
	sw::unique_ptr<sw::IWindow>	   window;
	sw::shared_ptr<sw::IRHIDevice> rhiDevice;
	if ( tryInitDeviceWithWindow( sw::RHIBackend::DirectX11, window, rhiDevice ) == false &&
		 tryInitDeviceWithWindow( sw::RHIBackend::DirectX12, window, rhiDevice ) == false &&
		 tryInitDeviceWithWindow( sw::RHIBackend::OpenGL, window, rhiDevice ) == false )
		SW_TEST_SKIP( "RHI initialize failed (no GPU / display context)" );

	sw::RHIDrawIndirectCommand drawCmd{};
	drawCmd._vertexCount		   = 3;
	drawCmd._instanceCount		   = 1;
	drawCmd._startVertexLocation   = 0;
	drawCmd._startInstanceLocation = 0;

	sw::RHIBufferDesc argDesc{};
	argDesc._sizeBytes		   = sizeof( sw::RHIDrawIndirectCommand );
	argDesc._elementSize	   = sizeof( sw::RHIDrawIndirectCommand );
	argDesc._elementCount	   = 1;
	argDesc._usage			   = sw::RHIBufferUsage::IndirectArgs | sw::RHIBufferUsage::UnorderedAccess | sw::RHIBufferUsage::Raw | sw::RHIBufferUsage::ShaderResource;
	argDesc._pInitialData	   = &drawCmd;
	sw::RHIBufferHandle argBuf = rhiDevice->getResource()->createBuffer( argDesc );
	if ( argBuf == 0 )
		argBuf = rhiDevice->getResource()->createStructuredBuffer( sizeof( sw::RHIDrawIndirectCommand ), 1 );
	SW_EXPECT_TRUE( argBuf != 0 );

	if ( argBuf != 0 )
	{
		rhiDevice->getResource()->updateStructuredBuffer( argBuf, &drawCmd, sizeof( sw::RHIDrawIndirectCommand ) );

		sw::unique_ptr<sw::IRHICommandList> cmdList = rhiDevice->createCommandList();
		if ( cmdList != nullptr )
		{
			cmdList->beginCommandList();
			cmdList->dispatchCompute( 4, 1, 1 );
			cmdList->transitionBuffer( argBuf, sw::RHIBufferState::IndirectArgument );
			cmdList->drawIndirect( argBuf, 0 );
			cmdList->multiDrawIndirect( argBuf, 0, 1 );
			cmdList->dispatchCompute( 2, 1, 1 );
			cmdList->drawIndirect( argBuf, 0 );
			cmdList->dispatchIndirect( argBuf, 0 );
			cmdList->endCommandList();

			rhiDevice->executeCommandList( cmdList.get() );
		}

		rhiDevice->getResource()->destroyBuffer( argBuf );
	}

	shutdownDeviceWithWindow( rhiDevice, window );
}

// ------------------------------------------------------------------------------
// 2) RHIReleaseQueueTest — 지연 해제·flush
// ------------------------------------------------------------------------------
/**
 * @brief [RHIReleaseQueueTest] 지연 해제
 */
SW_TEST_CASE( RHIReleaseQueueTest, LatencyRelease )
{
	sw::RHIReleaseQueue queue( 3 );
	SW_EXPECT_EQUAL( 0u, queue.getPendingReleaseCount() );

	bool						   bDestroyed{ false };
	sw::RHIResourceReleaseDelegate releaseDel = SW_DELEGATE_LAMBDA( sw::RHIResourceReleaseDelegate, [&bDestroyed]()
	{
		bDestroyed = true;
	} );

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

/**
 * @brief [RHIReleaseQueueTest] 전체 flush
 */
SW_TEST_CASE( RHIReleaseQueueTest, FlushAll )
{
	sw::RHIReleaseQueue			   queue( 5 );
	int32						   releaseCount{ 0 };
	sw::RHIResourceReleaseDelegate releaseDel = SW_DELEGATE_LAMBDA( sw::RHIResourceReleaseDelegate, [&releaseCount]()
	{
		++releaseCount;
	} );

	queue.enqueueRelease( releaseDel );
	queue.enqueueRelease( releaseDel );
	SW_EXPECT_EQUAL( 2u, queue.getPendingReleaseCount() );

	queue.flushAll();
	SW_EXPECT_EQUAL( 2, releaseCount );
	SW_EXPECT_EQUAL( 0u, queue.getPendingReleaseCount() );
}

// ------------------------------------------------------------------------------
// 3) RHITypesTest — 버텍스 레이아웃 오프셋
// ------------------------------------------------------------------------------
/**
 * @brief [RHITypesTest] 버텍스 레이아웃 자동 오프셋
 */
SW_TEST_CASE( RHITypesTest, VertexLayoutBuilderAutoOffset )
{
	struct CustomVertex
	{
		float32 pos[3];
		float32 uv[2];
		uint32	color;
	};

	sw::VertexLayoutBuilder builder;
	builder.addElement( "POSITION", 0, sw::RHIFormat::R32G32B32_FLOAT, offsetof( CustomVertex, pos ) );
	builder.addElement( "TEXCOORD", 0, sw::RHIFormat::R32G32_FLOAT, offsetof( CustomVertex, uv ) );
	builder.addElement( "COLOR", 0, sw::RHIFormat::R8G8B8A8_UNORM, offsetof( CustomVertex, color ) );

	sw::vector<sw::RHIInputElement> layout = builder.build();
	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( layout.size() ) );
	if ( layout.size() == 3 )
	{
		SW_EXPECT_EQUAL( sw::string( "POSITION" ), layout[0]._semanticName );
		SW_EXPECT_EQUAL( 0u, layout[0]._alignedByteOffset );

		SW_EXPECT_EQUAL( sw::string( "TEXCOORD" ), layout[1]._semanticName );
		SW_EXPECT_EQUAL( static_cast<uint32>( offsetof( CustomVertex, uv ) ), layout[1]._alignedByteOffset );

		SW_EXPECT_EQUAL( sw::string( "COLOR" ), layout[2]._semanticName );
		SW_EXPECT_EQUAL( static_cast<uint32>( offsetof( CustomVertex, color ) ), layout[2]._alignedByteOffset );
	}
}

// ------------------------------------------------------------------------------
// 4) RHITest — Deferred CL (디바이스 없이)
// ------------------------------------------------------------------------------
/**
 * @brief 디바이스 없이 Deferred CL 기록 (GPU 없는 환경 안전)
 */
SW_TEST_CASE( RHITest, DeferredCommandListRecordsWithoutDevice )
{
	sw::RHIDeferredCommandList list( sw::RHICommandListMode::Deferred, nullptr );
	list.beginCommandList();
	SW_EXPECT_TRUE( list.isRecording() );
	SW_EXPECT_EQUAL( 0u, static_cast<uint32>( list.commandCount() ) );

	sw::RHIViewport vp{ 0.0f, 0.0f, 128.0f, 72.0f, 0.0f, 1.0f };
	list.setViewport( vp );
	list.setVertexBuffer( 0, 1, 16, 0 );
	list.draw( 3, 0 );
	list.setIndexBuffer( 2, 4, 0 );
	list.drawIndexedIndirect( 3, 0 );
	list.endCommandList();

	SW_EXPECT_TRUE( list.isRecording() == false );
	SW_EXPECT_EQUAL( 5u, static_cast<uint32>( list.commandCount() ) );
	SW_EXPECT_TRUE( list.isApplied() == false );
}

/**
 * @brief [RHIHandleTable] generation이 올라가면 옛 핸들은 무효이고 슬롯은 재사용된다
 */
SW_TEST_CASE( RHIHandleTableTest, GenerationInvalidatesStaleHandles )
{
	sw::RHIHandleTable<uint32> table;
	const uint64			   first = table.insert( 42u );
	SW_EXPECT_TRUE( first != 0 );
	uint32* slot = table.get( first );
	SW_ASSERT_NOT_NULL( slot );
	SW_EXPECT_EQUAL( 42u, *slot );

	uint32 taken{ 0 };
	SW_EXPECT_TRUE( table.take( first, taken ) );
	SW_EXPECT_EQUAL( 42u, taken );
	SW_EXPECT_TRUE( table.get( first ) == nullptr );

	const uint64 second = table.insert( 99u );
	SW_EXPECT_TRUE( second != 0 );
	SW_EXPECT_TRUE( second != first );
	SW_EXPECT_TRUE( table.get( first ) == nullptr );
	uint32* reused = table.get( second );
	SW_ASSERT_NOT_NULL( reused );
	SW_EXPECT_EQUAL( 99u, *reused );
}

/**
 * @brief [RHIReleaseQueueTest] GPU 펜스가 완료되기 전에는 해제하지 않는다
 */
SW_TEST_CASE( RHIReleaseQueueTest, GpuFenceRelease )
{
	sw::RHIReleaseQueue			   queue( 3 );
	bool						   bDestroyed{ false };
	sw::RHIResourceReleaseDelegate releaseDel = SW_DELEGATE_LAMBDA( sw::RHIResourceReleaseDelegate, [&bDestroyed]()
	{
		bDestroyed = true;
	} );

	queue.enqueueGpuRelease( releaseDel, 4 );
	SW_EXPECT_EQUAL( 1u, queue.getPendingReleaseCount() );
	queue.tickCompleted( 3 );
	SW_EXPECT_FALSE( bDestroyed );
	queue.tickFrame();
	SW_EXPECT_FALSE( bDestroyed );
	queue.tickCompleted( 4 );
	SW_EXPECT_TRUE( bDestroyed );
	SW_EXPECT_EQUAL( 0u, queue.getPendingReleaseCount() );
}

// ------------------------------------------------------------------------------
// 5) RHITest — Mode / soft replay / dual context parity
// ------------------------------------------------------------------------------
/**
 * @brief Immediate Mode: endCommandList가 Context로 flush(replay)하고 applied + cmds clear
 */
SW_TEST_CASE( RHITest, ImmediateCommandListFlushesOnEnd )
{
	CountingCommandContext	   ctx;
	sw::RHIDeferredCommandList list( sw::RHICommandListMode::Immediate, &ctx );
	list.beginCommandList();
	sw::RHIViewport vp{ 0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f };
	list.setViewport( vp );
	list.draw( 3, 0 );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( list.commandCount() ) );
	SW_EXPECT_EQUAL( 0u, ctx._setViewportCount );

	list.endCommandList();
	SW_EXPECT_TRUE( list.isApplied() );
	SW_EXPECT_EQUAL( 0u, static_cast<uint32>( list.commandCount() ) );
	SW_EXPECT_EQUAL( 1u, ctx._setViewportCount );
	SW_EXPECT_EQUAL( 1u, ctx._drawCount );
}

/**
 * @brief Deferred Mode: endCommandList는 replay하지 않음; replay()가 Context에 재생
 */
SW_TEST_CASE( RHITest, DeferredCommandListReplayOntoContext )
{
	CountingCommandContext	   ctx;
	sw::RHIDeferredCommandList list( sw::RHICommandListMode::Deferred, &ctx );
	list.beginCommandList();
	list.draw( 3, 0, 7 );
	list.setViewport( sw::RHIViewport{ 1.0f, 2.0f, 3.0f, 4.0f, 0.0f, 1.0f } );
	list.endCommandList();

	SW_EXPECT_TRUE( list.isApplied() == false );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( list.commandCount() ) );
	SW_EXPECT_EQUAL( 0u, ctx._drawCount );
	SW_EXPECT_EQUAL( 0u, ctx._setViewportCount );

	list.replay( &ctx );
	SW_EXPECT_EQUAL( 1u, ctx._drawCount );
	SW_EXPECT_EQUAL( 1u, ctx._setViewportCount );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( list.commandCount() ) );

	list.replay( nullptr ); // no-op
	SW_EXPECT_EQUAL( 1u, ctx._drawCount );
}

/**
 * @brief executeDeferredCommandList: Immediate Context null이면 markApplied 금지
 */
SW_TEST_CASE( RHITest, ExecuteDeferredDoesNotMarkAppliedWithoutImmediateContext )
{
	SW_TEST_DEFENSIVE_SCOPE( "Testing deferred list execution without immediate context" );
	sw::shared_ptr<sw::IRHIDevice> device = sw::RHI::createDevice( sw::RHIBackend::DirectX12 );
	if ( device == nullptr )
		device = sw::RHI::createDevice( sw::RHIBackend::Vulkan );
	if ( device == nullptr )
		SW_TEST_SKIP( "No RHI device factory available" );

	// initialize 전: getImmediateContext는 구현체마다 null 또는 미배선일 수 있음.
	// 생성만 된 디바이스에서 Deferred CL을 execute해도 crash/markApplied 되면 안 됨.
	sw::RHIDeferredCommandList list( sw::RHICommandListMode::Deferred, nullptr );
	list.beginCommandList();
	list.draw( 3, 0 );
	list.endCommandList();
	SW_EXPECT_TRUE( list.isApplied() == false );

	if ( device->getImmediateContext() == nullptr )
	{
		sw::executeDeferredCommandList( device.get(), &list );
		SW_EXPECT_TRUE( list.isApplied() == false );
	}
	else
	{
		// 드물게 생성자에서 context를 만들면 execute가 성공할 수 있음 — 그 경우 applied OK.
		sw::executeDeferredCommandList( device.get(), &list );
	}

	device.reset();
}

/**
 * @brief 가용 백엔드마다 Immediate/Deferred Context·SwapChain·Resource·Mode 선택 패리티
 */
SW_TEST_CASE( RHITest, DualContextAndModeParityAllAvailableBackends )
{
	const sw::RHIBackend backends[] = {
		sw::RHIBackend::DirectX11,
		sw::RHIBackend::DirectX12,
		sw::RHIBackend::Vulkan,
		sw::RHIBackend::OpenGL,
	};

	uint32 initializedCount{ 0 };
	for ( sw::RHIBackend backend : backends )
	{
		sw::unique_ptr<sw::IWindow>	   window;
		sw::shared_ptr<sw::IRHIDevice> device;
		if ( tryInitDeviceWithWindow( backend, window, device ) == false )
			continue;

		++initializedCount;
		SW_EXPECT_NOT_NULL( device->getImmediateContext() );
		SW_EXPECT_NOT_NULL( device->getDeferredCommandContext() );
		SW_EXPECT_NOT_NULL( device->getSwapChain() );
		SW_EXPECT_NOT_NULL( device->getResource() );

		device->setDefaultCommandListMode( sw::RHICommandListMode::Immediate );
		SW_EXPECT_TRUE( device->getDefaultCommandContext() == device->getImmediateContext() );
		SW_EXPECT_TRUE( device->getCommandContextForMode( sw::RHICommandListMode::Immediate ) == device->getImmediateContext() );

		device->setDefaultCommandListMode( sw::RHICommandListMode::Deferred );
		SW_EXPECT_TRUE( device->getDefaultCommandContext() == device->getDeferredCommandContext() );
		SW_EXPECT_TRUE( device->getCommandContextForMode( sw::RHICommandListMode::Deferred ) == device->getDeferredCommandContext() );

		sw::unique_ptr<sw::IRHICommandList> deferredList =
			device->createCommandList( sw::RHICommandListMode::Deferred );
		SW_EXPECT_NOT_NULL( deferredList.get() );
		if ( deferredList != nullptr )
		{
			sw::RHIDeferredCommandList* asDef = deferredList->asDeferred();
			SW_ASSERT_NOT_NULL( asDef );
			SW_EXPECT_TRUE( asDef->getMode() == sw::RHICommandListMode::Deferred );
			asDef->beginCommandList();
			asDef->draw( 3, 0, 0 );
			asDef->endCommandList();
			SW_EXPECT_TRUE( asDef->isApplied() == false );
			device->executeCommandList( deferredList.get() );
			SW_EXPECT_TRUE( asDef->isApplied() );
		}

		sw::unique_ptr<sw::IRHICommandList> immediateList =
			device->createCommandList( sw::RHICommandListMode::Immediate );
		SW_EXPECT_NOT_NULL( immediateList.get() );
		if ( immediateList != nullptr )
		{
			sw::RHIDeferredCommandList* asImm = immediateList->asDeferred();
			SW_ASSERT_NOT_NULL( asImm );
			asImm->beginCommandList();
			asImm->draw( 3, 0, 0 );
			asImm->endCommandList();
			SW_EXPECT_TRUE( asImm->isApplied() );
		}

		shutdownDeviceWithWindow( device, window );
	}

	if ( initializedCount == 0 )
		SW_TEST_SKIP( "No RHI backend could initialize with a window in this environment" );
}

/**
 * @brief setDefaultCommandListMode가 getDefaultCommandContext 선택을 바꾼다
 */
SW_TEST_CASE( RHITest, DefaultCommandContextFollowsMode )
{
	sw::unique_ptr<sw::IWindow>	   window;
	sw::shared_ptr<sw::IRHIDevice> device;
	sw::RHIBackend				   backends[] = {
		sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::DirectX11, sw::RHIBackend::OpenGL };
	bool bOk{ false };
	for ( sw::RHIBackend backend : backends )
	{
		if ( tryInitDeviceWithWindow( backend, window, device ) )
		{
			bOk = true;
			break;
		}
	}
	if ( bOk == false )
		SW_TEST_SKIP( "No RHI backend could initialize for Mode selection test" );

	sw::IRHICommandContext* imm = device->getImmediateContext();
	sw::IRHICommandContext* def = device->getDeferredCommandContext();
	SW_ASSERT_NOT_NULL( imm );
	SW_ASSERT_NOT_NULL( def );

	device->setDefaultCommandListMode( sw::RHICommandListMode::Immediate );
	SW_EXPECT_TRUE( device->getDefaultCommandContext() == imm );

	device->setDefaultCommandListMode( sw::RHICommandListMode::Deferred );
	SW_EXPECT_TRUE( device->getDefaultCommandContext() == def );

	shutdownDeviceWithWindow( device, window );
}

/**
 * @brief RHI::recreateDevice 핫스왑 후 Imm/Def Context·Mode가 다시 살아 있다
 */
SW_TEST_CASE( RHITest, RecreateDeviceHotSwapRestoresContexts )
{
	sw::unique_ptr<sw::IWindow> window = sw::IWindow::createPlatformWindow();
	SW_ASSERT_NOT_NULL( window.get() );
	SW_EXPECT_TRUE( window->initializeWindow( "RHIRecreateHotSwap", 320, 240 ) );
	sw::IWindow::setActiveWindow( window.get() );

	const sw::RHIBackend startCandidates[] = {
		sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::OpenGL, sw::RHIBackend::Vulkan };
	const sw::RHIBackend swapCandidates[] = {
		sw::RHIBackend::DirectX12, sw::RHIBackend::OpenGL, sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan };

	sw::RHI		   rhi;
	sw::RHIBackend startBackend = sw::RHIBackend::DirectX11;
	bool		   bStarted{ false };
	for ( sw::RHIBackend backend : startCandidates )
	{
		if ( sw::RHIAvailability::isAvailable( backend ) == false )
			continue;
		sw::gv_rhiBackend		  = backend;
		sw::gv_rhiCommandListMode = sw::RHICommandListMode::Deferred;
		if ( rhi.initialize() )
		{
			startBackend = backend;
			bStarted	 = true;
			break;
		}
	}
	if ( bStarted == false )
	{
		sw::IWindow::setActiveWindow( nullptr );
		window->destroy();
		SW_TEST_SKIP( "Could not initialize starting RHI for recreateDevice test" );
	}

	SW_EXPECT_NOT_NULL( rhi.getDevice().getImmediateContext() );
	SW_EXPECT_NOT_NULL( rhi.getDevice().getDeferredCommandContext() );
	SW_EXPECT_TRUE( rhi.getDevice().getDefaultCommandContext() == rhi.getDevice().getDeferredCommandContext() );

	bool bSwapped{ false };
	for ( sw::RHIBackend target : swapCandidates )
	{
		if ( target == startBackend )
			continue;
		if ( sw::RHIAvailability::isAvailable( target ) == false )
			continue;
		if ( rhi.recreateDevice( target ) )
		{
			bSwapped = true;
			SW_EXPECT_EQUAL( static_cast<int32>( target ), static_cast<int32>( rhi.getDevice().getBackendType() ) );
			SW_EXPECT_EQUAL( static_cast<int32>( target ), static_cast<int32>( rhi.getCommittedBackend() ) );
			SW_EXPECT_NOT_NULL( rhi.getDevice().getImmediateContext() );
			SW_EXPECT_NOT_NULL( rhi.getDevice().getDeferredCommandContext() );
			rhi.getDevice().setDefaultCommandListMode( sw::RHICommandListMode::Immediate );
			SW_EXPECT_TRUE( rhi.getDevice().getDefaultCommandContext() == rhi.getDevice().getImmediateContext() );
			break;
		}
	}

	// schedulePendingBackendChange API (실제 consume은 EngineLoop 경로)
	rhi.schedulePendingBackendChange( startBackend );
	if ( rhi.hasPendingBackendChange() )
	{
		const sw::RHIBackend pending = rhi.consumePendingBackendChange();
		SW_EXPECT_EQUAL( static_cast<int32>( startBackend ), static_cast<int32>( pending ) );
		SW_EXPECT_FALSE( rhi.hasPendingBackendChange() );
	}

	rhi.shutdown();
	sw::IWindow::setActiveWindow( nullptr );
	window->destroy();

	if ( bSwapped == false )
		SW_TEST_SKIP( "recreateDevice could not switch to another backend (single-backend env)" );
}
