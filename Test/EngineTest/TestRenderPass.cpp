#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/FrameArenaAllocator.h"
#include "Core/String/hashed_string.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHISwapChain.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RenderPass/FrameRenderer.h"
#include "Engine/Graphics/RenderPass/GpuScene.h"
#include "Engine/Graphics/RenderPass/RenderGraph.h"
#include "Engine/Graphics/RenderPass/RenderPassManager.h"
#include "Engine/Graphics/RenderPass/RenderPassResource.h"
#include "Engine/Graphics/RenderPass/RenderPipelineResource.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Window/IWindow.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) RenderPassTest — XML·그래프 위상
// ------------------------------------------------------------------------------
/**
 * @brief [RenderPassTest] XML 직렬화 라운드트립
 */

SW_TEST_CASE( RenderPassTest, XmlSerializationRoundtrip )
{
	sw::RenderPassResource passRes;
	sw::RenderPassDesc&	   desc = passRes.getDesc();
	desc._name					= "UnitTestRenderPass";

	sw::RenderPassAttachment colorAtt{};
	colorAtt._name		 = "Color0";
	colorAtt._format	 = "R8G8B8A8_UNORM";
	colorAtt._clearColor = sw::float4{ 0.5f, 0.2f, 0.8f, 1.0f };
	colorAtt._bClear	 = true;
	desc._listAttachment.push_back( colorAtt );

	sw::string testPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_renderpass_roundtrip.xml" );
	SW_EXPECT_TRUE( passRes.saveToXmlFile( testPath ) );

	sw::RenderPassResource loadedRes;
	SW_EXPECT_TRUE( loadedRes.loadFromXmlFile( testPath ) );
	SW_EXPECT_EQUAL( sw::string( "UnitTestRenderPass" ), loadedRes.getDesc()._name );
	SW_EXPECT_EQUAL( size_t( 1 ), loadedRes.getDesc()._listAttachment.size() );
	SW_EXPECT_EQUAL( sw::string( "Color0" ), loadedRes.getDesc()._listAttachment[0]._name );

	sw::FileUtil::removeFile( testPath );
}

/**
 * @brief [RenderPassTest] 단위 큐브 메시 컴포넌트
 */
SW_TEST_CASE( RenderPassTest, UnitCubeMeshComponent )
{
	sw::shared_ptr<sw::Mesh> cube = sw::Mesh::createUnitCube();
	SW_EXPECT_TRUE( cube != nullptr );
	SW_EXPECT_EQUAL( uint32( 36 ), cube->getVertexCount() );

	sw::GameObjectManager manager;
	sw::GameObject*		  actorPtr = manager.createGameObject( sw::hashed_string( "UnitCubeActor" ) );
	sw::GameObject&		  actor	   = *actorPtr;
	sw::MeshComponent*	  meshComp = actor.addComponent<sw::MeshComponent>();
	SW_EXPECT_TRUE( meshComp != nullptr );
	meshComp->setMesh( cube );
	meshComp->setLocalPosition( sw::float3( 1.0f, 0.0f, -2.0f ) );
	SW_EXPECT_TRUE( meshComp->getMesh() == cube );
	SW_EXPECT_TRUE( meshComp->isVisible() );
}

/**
 * @brief [RenderPassTest] 게임 카메라
 */
SW_TEST_CASE( RenderPassTest, EditorAndGameCameras )
{
	sw::Scene scene( "CameraTestScene" );
	SW_EXPECT_TRUE( scene.ensureDefaultCameras() );

	sw::CameraComponent* gameCam = scene.getActiveGameCamera();
	SW_EXPECT_TRUE( gameCam != nullptr );
	SW_EXPECT_TRUE( gameCam->getRole() == sw::CameraRole::Game );
	SW_EXPECT_TRUE( scene.getObjectManager()->findGameObjectByName( sw::hashed_string( "EditorCamera" ) ) == nullptr );

	const sw::float4x4 vp = gameCam->getViewProjectionMatrix( 16.0f / 9.0f );
	SW_EXPECT_TRUE( sw::MathUtil::abs( vp._11 ) > 1e-6f || sw::MathUtil::abs( vp._22 ) > 1e-6f );
}

/**
 * @brief [RenderPassTest] 파이프라인 XML 라운드트립
 */
SW_TEST_CASE( RenderPassTest, PipelineXmlSerializationRoundtrip )
{
	sw::RenderPipelineResource pipeRes;
	sw::RenderPipelineDesc&	   desc = pipeRes.getDesc();
	desc._name						= "UnitTestPipeline";
	desc._shadingModel				= "Forward";

	sw::RenderPassAttachment colorAtt{};
	colorAtt._name		 = "SceneColor";
	colorAtt._format	 = "R8G8B8A8_UNORM";
	colorAtt._clearColor = sw::float4{ 0.1f, 0.2f, 0.3f, 1.0f };
	colorAtt._bClear	 = true;
	desc._listAttachment.push_back( colorAtt );

	sw::RenderGraphPassDesc pass{};
	pass._name = "Present";
	pass._type = "Present";
	pass._listInput.push_back( "SceneColor" );
	pass._listOutput.push_back( "Swapchain" );
	desc._listPass.push_back( pass );
	desc._listRenderPassRef.push_back( "renderpass/defaultrenderpass.xml" );

	sw::string testPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_renderpipeline_roundtrip.xml" );
	SW_EXPECT_TRUE( pipeRes.saveToXmlFile( testPath ) );

	sw::RenderPipelineResource loadedRes;
	SW_EXPECT_TRUE( loadedRes.loadFromXmlFile( testPath ) );
	SW_EXPECT_EQUAL( sw::string( "UnitTestPipeline" ), loadedRes.getDesc()._name );
	SW_EXPECT_EQUAL( sw::string( "Forward" ), loadedRes.getDesc()._shadingModel );
	SW_EXPECT_EQUAL( size_t( 1 ), loadedRes.getDesc()._listAttachment.size() );
	SW_EXPECT_EQUAL( size_t( 1 ), loadedRes.getGraphPass().size() );
	SW_EXPECT_EQUAL( sw::string( "Present" ), loadedRes.getGraphPass()[0]._name );
	SW_EXPECT_EQUAL( size_t( 1 ), loadedRes.getDesc()._listRenderPassRef.size() );

	sw::FileUtil::removeFile( testPath );
}

/**
 * @brief [RenderPassTest] 레거시 RenderPassDesc 루트는 거부
 */
SW_TEST_CASE( RenderPassTest, PipelineRejectsLegacyRenderPassDescRoot )
{
	SW_TEST_DEFENSIVE_SCOPE( "Testing legacy root XML rejection" );
	const sw::string testPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_legacy_pipeline.xml" );
	{
		std::ofstream out( testPath.c_str() );
		out << R"(<?xml version="1.0" encoding="utf-8"?>
<RenderPassDesc>
	<_name>LegacyForward</_name>
	<_passes>
		<item>
			<_name>Present</_name>
			<_type>Present</_type>
		</item>
	</_passes>
</RenderPassDesc>
)";
	}

	sw::RenderPipelineResource loaded;
	SW_EXPECT_FALSE( loaded.loadFromXmlFile( testPath ) );
	sw::FileUtil::removeFile( testPath );
}

// ------------------------------------------------------------------------------
// 2) RenderGraph — 위상·사이클·컬링·export
// ------------------------------------------------------------------------------
/**
 * @brief [RenderPassTest] 렌더 그래프 컴파일 순서
 */
SW_TEST_CASE( RenderPassTest, RenderGraphCompileOrder )
{
	sw::RenderGraph graph;
	SW_EXPECT_FALSE( graph.compile() );

	// consumer 를 producer 앞에 넣어도 위상 정렬은 Depth 를 먼저 스케줄해야 한다.
	graph.addPass( sw::hashed_string( "Shading" ), { sw::hashed_string( "DepthBuffer" ) }, { sw::hashed_string( "Color" ) } );
	graph.addPass( sw::hashed_string( "Depth" ), {}, { sw::hashed_string( "DepthBuffer" ) } );

	SW_ASSERT_TRUE( graph.compile() );
	SW_EXPECT_EQUAL( 2u, graph.getNodeCount() );
	SW_ASSERT_EQUAL( size_t( 2 ), graph.getExecutionOrder().size() );
	SW_EXPECT_TRUE( graph.getExecutionOrder()[0] == sw::hashed_string( "Depth" ) );
	SW_EXPECT_TRUE( graph.getExecutionOrder()[1] == sw::hashed_string( "Shading" ) );
}

/**
 * @brief [RenderPassTest] 렌더 그래프 사이클 감지
 */
SW_TEST_CASE( RenderPassTest, RenderGraphCycleDetect )
{
	SW_TEST_DEFENSIVE_SCOPE( "Testing render graph cycle detection" );
	sw::RenderGraph graph;
	graph.addPass( sw::hashed_string( "A" ), { sw::hashed_string( "BOut" ) }, { sw::hashed_string( "AOut" ) } );
	graph.addPass( sw::hashed_string( "B" ), { sw::hashed_string( "AOut" ) }, { sw::hashed_string( "BOut" ) } );

	SW_EXPECT_FALSE( graph.compile() );
	SW_EXPECT_EQUAL( size_t( 0 ), graph.getExecutionOrder().size() );
}

/**
 * @brief [RenderPassTest] 미사용 패스 컬링
 */
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

	const sw::vector<sw::hashed_string>& order = graph.getExecutionOrder();
	SW_EXPECT_EQUAL( size_t( 2 ), order.size() );
	for ( const sw::hashed_string& pass : order )
	{
		SW_EXPECT_TRUE( pass != sw::hashed_string( "DebugOverlay" ) );
	}
}

/**
 * @brief [RenderPassTest] Mermaid/DOT export
 */
SW_TEST_CASE( RenderPassTest, RenderGraphMermaidAndDotExport )
{
	sw::RenderGraph graph;
	graph.addPass( sw::hashed_string( "PassA" ), {}, { sw::hashed_string( "RT0" ) } );
	graph.addPass( sw::hashed_string( "PassB" ), { sw::hashed_string( "RT0" ) }, { sw::hashed_string( "RT1" ) } );
	SW_ASSERT_TRUE( graph.compile() );

	const sw::string mermaid = graph.exportToMermaid();
	SW_EXPECT_TRUE_MSG( mermaid.find( "graph TD" ) != sw::string::npos, "Mermaid export should start with graph TD" );
	SW_EXPECT_TRUE( mermaid.find( "PassA" ) != sw::string::npos );
	SW_EXPECT_TRUE( mermaid.find( "RT0" ) != sw::string::npos );

	const sw::string dot = graph.exportToDot();
	SW_EXPECT_TRUE_MSG( dot.find( "digraph" ) != sw::string::npos, "DOT export should declare a digraph" );
	SW_EXPECT_TRUE( dot.find( "PassB" ) != sw::string::npos );

	graph.clear();
	SW_EXPECT_EQUAL( 0u, graph.getNodeCount() );
}

/**
 * @brief [RenderPassTest] 실행 콜백
 */
SW_TEST_CASE( RenderPassTest, RenderGraphExecuteCallbacks )
{
	sw::RenderGraph		   graph;
	sw::vector<sw::string> executed;

	auto makeCb = [&executed]( const utf8* pName ) -> sw::RenderGraphPassExecuteFn
	{
		return sw::RenderGraphPassExecuteFn(
			SW_DELEGATE_LAMBDA( sw::RenderGraphPassExecuteFn, [&executed, pName]( const sw::RenderGraphPassContext& ctx )
		{
			SW_EXPECT_STREQ( pName, ctx._passName.c_str() );
			executed.push_back( pName );
		} ) );
	};

	graph.addPass( sw::hashed_string( "Shading" ), { sw::hashed_string( "DepthBuffer" ) }, { sw::hashed_string( "Color" ) },
				   makeCb( "Shading" ) );
	graph.addPass( sw::hashed_string( "Depth" ), {}, { sw::hashed_string( "DepthBuffer" ) }, makeCb( "Depth" ) );

	sw::RenderGraphExecutionContext context;
	SW_ASSERT_TRUE( graph.execute( context ) );
	SW_ASSERT_EQUAL( size_t( 2 ), executed.size() );
	SW_EXPECT_STREQ( "Depth", executed[0].c_str() );
	SW_EXPECT_STREQ( "Shading", executed[1].c_str() );
	SW_EXPECT_TRUE( context._lastTransitionCount >= 2u );
}

/**
 * @brief [RenderPassTest] 병렬 렌더 그래프 실행 (TaskManager + executeParallel)
 */
SW_TEST_CASE( RenderPassTest, RenderGraphExecuteParallel )
{
	sw::TaskManager taskManager;
	SW_ASSERT_TRUE( taskManager.initialize( 2 ) );

	sw::RenderGraph	   graph;
	sw::atomic<uint32> executeCount{ 0 };

	auto makeParallelCb = [&executeCount]( const utf8* pExpectedName ) -> sw::RenderGraphPassExecuteFn
	{
		return sw::RenderGraphPassExecuteFn(
			SW_DELEGATE_LAMBDA( sw::RenderGraphPassExecuteFn, [&executeCount, pExpectedName]( const sw::RenderGraphPassContext& ctx )
		{
			SW_EXPECT_STREQ( pExpectedName, ctx._passName.c_str() );
			executeCount.fetch_add( 1, std::memory_order_relaxed );
		} ) );
	};

	graph.addPass( sw::hashed_string( "DepthPass" ), {}, { sw::hashed_string( "DepthBuffer" ) }, makeParallelCb( "DepthPass" ) );
	graph.addPass( sw::hashed_string( "ShadowPass" ), {}, { sw::hashed_string( "ShadowMap" ) }, makeParallelCb( "ShadowPass" ) );
	graph.addPass( sw::hashed_string( "ForwardPass" ), { sw::hashed_string( "DepthBuffer" ), sw::hashed_string( "ShadowMap" ) }, { sw::hashed_string( "SceneColor" ) }, makeParallelCb( "ForwardPass" ) );

	sw::RenderGraphExecutionContext context;
	SW_ASSERT_TRUE( graph.execute( context ) );
	SW_EXPECT_EQUAL( 3u, executeCount.load() );
	SW_EXPECT_EQUAL( 3u, graph.getNodeCount() );

	taskManager.shutdown();
}

/**
 * @brief GpuScene: opaque 머지, transparent 연속 머지 + back-to-front, 빌드 캐시
 */
SW_TEST_CASE( RenderPassTest, GpuSceneBuildBatchesAndSortTransparent )
{
	sw::Scene scene( "GpuSceneBatchTest" );
	SW_EXPECT_TRUE( scene.ensureDefaultCameras() );

	sw::GameObjectManager* objects = scene.getObjectManager();
	SW_ASSERT_NOT_NULL( objects );

	sw::shared_ptr<sw::Mesh> cube = sw::Mesh::createUnitCube();
	SW_ASSERT_NOT_NULL( cube.get() );

	auto addMeshAt = [&]( const utf8* pName, float32 x, float32 z, sw::RHIBlendMode blend )
	{
		sw::GameObject* go = objects->createGameObject( sw::hashed_string( pName ) );
		SW_ASSERT_NOT_NULL( go );
		sw::MeshComponent* mesh = go->addComponent<sw::MeshComponent>();
		SW_ASSERT_NOT_NULL( mesh );
		mesh->setMesh( cube );
		mesh->setLocalPosition( sw::float3( x, 0.0f, z ) );
		mesh->setBlendMode( blend );
		mesh->setVisible( true );
	};

	addMeshAt( "OpaqueA", 0.0f, -1.0f, sw::RHIBlendMode::Opaque );
	addMeshAt( "OpaqueB", 1.0f, -1.0f, sw::RHIBlendMode::Opaque );
	addMeshAt( "TransparentFar", 0.0f, -10.0f, sw::RHIBlendMode::Transparent );
	addMeshAt( "TransparentNear", 0.0f, -2.0f, sw::RHIBlendMode::Transparent );

	sw::GpuScene	 gpuScene;
	const sw::float3 camPos{ 0.0f, 0.0f, 0.0f };
	gpuScene.buildFromScene( &scene, camPos, nullptr );

	SW_EXPECT_TRUE( gpuScene.getOpaqueBatches().empty() == false );
	SW_EXPECT_EQUAL( 4u, static_cast<uint32>( gpuScene.getInstances().size() ) );
	// 동일 mesh/mat transparent 2개는 정렬 후 연속 머지 → 배치 1, instanceCount 2
	SW_ASSERT_EQUAL( 1u, static_cast<uint32>( gpuScene.getTransparentBatches().size() ) );
	SW_EXPECT_EQUAL( 2u, gpuScene.getTransparentBatches()[0]._instanceCount );

	const sw::vector<sw::GpuInstance>& instances = gpuScene.getInstances();
	const sw::GpuMeshBatch&			   trBatch	 = gpuScene.getTransparentBatches()[0];
	const float32					   z0		 = instances[trBatch._instanceBase]._boundsCenter._z;
	const float32					   z1		 = instances[trBatch._instanceBase + 1]._boundsCenter._z;
	const float32					   d0		 = z0 * z0;
	const float32					   d1		 = z1 * z1;
	SW_EXPECT_TRUE( d0 >= d1 ); // far then near within merged batch

	// 동일 내용·카메라 → CPU dirty 없이 early-out
	SW_EXPECT_TRUE( gpuScene.isCpuSnapshotDirty() );
	gpuScene.buildFromScene( &scene, camPos, nullptr );
	// early-out 시 dirty 플래그는 이전 값 유지(업로드 전이면 여전히 dirty)
	SW_EXPECT_EQUAL( 1u, static_cast<uint32>( gpuScene.getTransparentBatches().size() ) );

	// 카메라만 이동 → transparent 재정렬 경로
	const sw::float3 camMoved{ 0.0f, 0.0f, 5.0f };
	gpuScene.buildFromScene( &scene, camMoved, nullptr );
	SW_EXPECT_TRUE( gpuScene.isCpuSnapshotDirty() );
	SW_EXPECT_EQUAL( 1u, static_cast<uint32>( gpuScene.getTransparentBatches().size() ) );
}

/**
 * @brief 서로 다른 MaterialInstance 키는 transparent 머지되지 않는다
 */
SW_TEST_CASE( RenderPassTest, GpuSceneTransparentDifferentKeysStaySeparate )
{
	sw::Scene scene( "GpuSceneTransparentKeys" );
	SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
	sw::GameObjectManager* objects = scene.getObjectManager();
	SW_ASSERT_NOT_NULL( objects );

	sw::shared_ptr<sw::Mesh> cube = sw::Mesh::createUnitCube();
	SW_ASSERT_NOT_NULL( cube.get() );

	sw::Material master;
	SW_EXPECT_TRUE( master.loadFromFile( "engine/materials/defaultmaterial.material" ) );
	sw::shared_ptr<sw::MaterialInstance> a = sw::make_shared<sw::MaterialInstance>( &master );
	sw::shared_ptr<sw::MaterialInstance> b = sw::make_shared<sw::MaterialInstance>( &master );

	{
		sw::GameObject*	   go = objects->createGameObject( sw::hashed_string( "T0" ) );
		sw::MeshComponent* mc = go->addComponent<sw::MeshComponent>();
		mc->setMesh( cube );
		mc->setLocalPosition( sw::float3( 0.0f, 0.0f, -10.0f ) );
		mc->setBlendMode( sw::RHIBlendMode::Transparent );
		mc->setMaterialInstance( a );
	}
	{
		sw::GameObject*	   go = objects->createGameObject( sw::hashed_string( "T1" ) );
		sw::MeshComponent* mc = go->addComponent<sw::MeshComponent>();
		mc->setMesh( cube );
		mc->setLocalPosition( sw::float3( 0.0f, 0.0f, -2.0f ) );
		mc->setBlendMode( sw::RHIBlendMode::Transparent );
		mc->setMaterialInstance( b );
	}

	sw::GpuScene	 gpuScene;
	const sw::float3 cam{ 0.0f, 0.0f, 0.0f };
	gpuScene.buildFromScene( &scene, cam, nullptr );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( gpuScene.getTransparentBatches().size() ) );
}

namespace
{
	bool tryInitDeviceForFrameRenderer( sw::RHIBackend backend, sw::unique_ptr<sw::IWindow>& outWindow,
										sw::shared_ptr<sw::IRHIDevice>& outDevice )
	{
		if ( sw::RHIAvailability::isAvailable( backend ) == false )
			return false;
		outWindow = sw::IWindow::createPlatformWindow();
		if ( outWindow == nullptr || outWindow->initializeWindow( "FrameRendererGolden", 320, 240 ) == false )
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
} // namespace

/**
 * @brief FrameRenderer 파이프라인 로드 + 씬 execute 스모크 (RenderThread와 동일 begin/execute/end)
 */
SW_TEST_CASE( RenderPassTest, FrameRendererInitializeAndExecuteSmoke )
{
	sw::unique_ptr<sw::IWindow>	   window;
	sw::shared_ptr<sw::IRHIDevice> device;
	const sw::RHIBackend		   backends[] = {
		sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL, sw::RHIBackend::DirectX12 };
	bool bOk{ false };
	for ( sw::RHIBackend backend : backends )
	{
		if ( tryInitDeviceForFrameRenderer( backend, window, device ) )
		{
			bOk = true;
			break;
		}
	}
	if ( bOk == false )
		SW_TEST_SKIP( "No RHI backend for FrameRenderer smoke" );

	sw::FrameRenderer renderer;
	SW_EXPECT_TRUE( renderer.initialize( device.get() ) );
	SW_EXPECT_TRUE( renderer.isReady() );
	SW_EXPECT_TRUE( renderer.getGraph().getNodeCount() > 0 );

	sw::Scene scene( "FrameRendererSmokeScene" );
	SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
	sw::shared_ptr<sw::Mesh> cube = sw::Mesh::createUnitCube();
	sw::GameObject*			 go	  = scene.getObjectManager()->createGameObject( sw::hashed_string( "Cube" ) );
	SW_ASSERT_NOT_NULL( go );
	sw::MeshComponent* mesh = go->addComponent<sw::MeshComponent>();
	SW_ASSERT_NOT_NULL( mesh );
	mesh->setMesh( cube );

	sw::float4 clear = { 0.02f, 0.02f, 0.05f, 1.0f };
	device->getSwapChain()->beginFrame( clear );
	SW_EXPECT_TRUE( renderer.execute( device.get(), nullptr, &scene ) );
	device->getSwapChain()->endFrame( false, false );
	device->waitIdle();

	// static Mesh 캐시가 죽은 디바이스를 붙잡지 않도록 디바이스 종료 전에 GPU 해제.
	if ( cube != nullptr )
		cube->releaseGpu();

	renderer.shutdown();
	device->shutdown();
	device.reset();
	window->destroy();
	window.reset();
}

/**
 * @brief FrameRenderer 패리티 스모크 — DX11 / DX12 / Vulkan / OpenGL 각각 begin→execute→end(no present)
 * @details Present 없이 waitIdle까지. 가용 백엔드는 전부 성공해야 한다.
 */
SW_TEST_CASE( RenderPassTest, FrameRendererParityAllBackends )
{
	const sw::RHIBackend backends[] = {
		sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

	uint32 attemptedCount{ 0 };
	uint32 okCount{ 0 };

	for ( sw::RHIBackend backend : backends )
	{
		sw::unique_ptr<sw::IWindow>	   window;
		sw::shared_ptr<sw::IRHIDevice> device;
		if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
			continue;

		++attemptedCount;

		sw::FrameRenderer renderer;
		bool			  bOk = renderer.initialize( device.get() ) && renderer.isReady();

		sw::Scene scene( "FrameRendererParityScene" );
		if ( bOk )
			bOk = scene.ensureDefaultCameras();

		sw::shared_ptr<sw::Mesh> cube;
		if ( bOk )
		{
			cube			   = sw::Mesh::createUnitCube();
			sw::GameObject* go = scene.getObjectManager()->createGameObject( sw::hashed_string( "Cube" ) );
			bOk				   = go != nullptr;
			if ( bOk )
			{
				sw::MeshComponent* meshComp = go->addComponent<sw::MeshComponent>();
				bOk							= meshComp != nullptr;
				if ( bOk )
					meshComp->setMesh( cube );
			}
		}

		if ( bOk )
		{
			const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
			device->getSwapChain()->beginFrame( clear );
			bOk = renderer.execute( device.get(), nullptr, &scene );
			device->getSwapChain()->endFrame( false, false );
			device->waitIdle();
		}

		if ( cube != nullptr )
			cube->releaseGpu();
		renderer.shutdown();
		device->shutdown();
		device.reset();
		window->destroy();
		window.reset();

		if ( bOk )
			++okCount;
		else
			SW_LOG_ERROR( "backend %# failed", static_cast<uint32>( backend ) );
	}

	if ( attemptedCount == 0 )
		SW_TEST_SKIP( "No RHI backend available for FrameRenderer parity" );

	SW_EXPECT_TRUE( okCount >= 1 );
	SW_EXPECT_EQUAL( okCount, attemptedCount );
}

/**
 * @brief [RenderPassTest] RenderGraph 다중 타깃 기반 자동 패스 컬링 및 출력 파라미터 검증
 */
SW_TEST_CASE( RenderPassTest, AutomaticPassCulling )
{
	sw::RenderGraph graph;
	graph.addPass( sw::hashed_string( "GBufferPass" ), {}, { sw::hashed_string( "GBufferAlbedo" ), sw::hashed_string( "GBufferDepth" ) } );
	graph.addPass( sw::hashed_string( "LightingPass" ), { sw::hashed_string( "GBufferAlbedo" ) }, { sw::hashed_string( "SceneColor" ) } );
	graph.addPass( sw::hashed_string( "UnreferencedDebugPass" ), { sw::hashed_string( "GBufferDepth" ) }, { sw::hashed_string( "DebugOverlay" ) } );

	SW_EXPECT_TRUE( graph.compile() );
	SW_EXPECT_EQUAL( size_t( 3 ), graph.getExecutionOrder().size() );

	// Cull with root output = "SceneColor"
	sw::vector<sw::hashed_string> listCulledPasses;
	graph.cullUnreferencedPasses( { sw::hashed_string( "SceneColor" ) }, &listCulledPasses );

	SW_EXPECT_TRUE( graph.isPassCulled( sw::hashed_string( "UnreferencedDebugPass" ) ) );
	SW_EXPECT_FALSE( graph.isPassCulled( sw::hashed_string( "GBufferPass" ) ) );
	SW_EXPECT_FALSE( graph.isPassCulled( sw::hashed_string( "LightingPass" ) ) );
	SW_EXPECT_EQUAL( size_t( 2 ), graph.getExecutionOrder().size() );
	SW_EXPECT_EQUAL( size_t( 1 ), listCulledPasses.size() );
	SW_EXPECT_TRUE( listCulledPasses[0] == sw::hashed_string( "UnreferencedDebugPass" ) );
}

/**
 * @brief [RenderPassTest] RenderGraph 직렬 실행 및 executeParallel의 안전한 폴백 검증
 */
SW_TEST_CASE( RenderPassTest, RenderGraphExecutionAndSerialFallback )
{
	sw::RenderGraph graph;
	uint32			passAExecuted = 0;
	uint32			passBExecuted = 0;

	graph.addPass(
		sw::hashed_string( "PassA" ),
		{},
		{ sw::hashed_string( "ResA" ) },
		SW_DELEGATE_LAMBDA( sw::RenderGraphPassExecuteFn, [&]( const sw::RenderGraphPassContext& )
	{
		++passAExecuted;
	} ) );

	graph.addPass(
		sw::hashed_string( "PassB" ),
		{ sw::hashed_string( "ResA" ) },
		{ sw::hashed_string( "FinalColor" ) },
		SW_DELEGATE_LAMBDA( sw::RenderGraphPassExecuteFn, [&]( const sw::RenderGraphPassContext& )
	{
		++passBExecuted;
	} ) );

	SW_EXPECT_TRUE( graph.compile() );

	sw::RenderGraphExecutionContext context;
	// 1) 기본 직렬 실행 검증
	SW_EXPECT_TRUE( graph.execute( context ) );
	SW_EXPECT_EQUAL( uint32( 1 ), passAExecuted );
	SW_EXPECT_EQUAL( uint32( 1 ), passBExecuted );

	// 2) executeParallel 호출 시 TaskManager나 Device가 없거나 _bParallelCommandRecording이 미지원일 때 안전한 직렬 폴백 검증
	SW_EXPECT_TRUE( graph.executeParallel( context, nullptr, nullptr ) );
	SW_EXPECT_EQUAL( uint32( 2 ), passAExecuted );
	SW_EXPECT_EQUAL( uint32( 2 ), passBExecuted );
}
