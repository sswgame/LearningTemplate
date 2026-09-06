#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/FrameArenaAllocator.h"
#include "Core/String/hashed_string.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHISwapChain.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/RenderFramePacket.h"
#include "Engine/Graphics/Renderer/Graph/RenderGraph.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassManager.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassResource.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineResource.h"
#include "Engine/Graphics/Renderer/Scene/GpuScene.h"
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
    sw::RenderPassDesc&    desc = passRes.getDesc();
    desc._name                  = "UnitTestRenderPass";

    sw::RenderPassAttachment colorAtt{};
    colorAtt._name       = "Color0";
    colorAtt._format     = "R8G8B8A8_UNORM";
    colorAtt._clearColor = sw::float4{ 0.5f, 0.2f, 0.8f, 1.0f };
    colorAtt._bClear     = true;
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
    sw::GameObject*       actorPtr = manager.createGameObject( sw::hashed_string( "UnitCubeActor" ) );
    sw::GameObject&       actor    = *actorPtr;
    sw::MeshComponent*    meshComp = actor.addComponent<sw::MeshComponent>();
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
    sw::RenderPipelineDesc&    desc = pipeRes.getDesc();
    desc._name                      = "UnitTestPipeline";
    desc._shadingModel              = "Forward";

    sw::RenderPassAttachment colorAtt{};
    colorAtt._name       = "SceneColor";
    colorAtt._format     = "R8G8B8A8_UNORM";
    colorAtt._clearColor = sw::float4{ 0.1f, 0.2f, 0.3f, 1.0f };
    colorAtt._bClear     = true;
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
    sw::RenderGraph        graph;
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

    sw::RenderGraph    graph;
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

    // DepthPass/ShadowPass는 서로 입출력이 없어 같은 웨이브(레벨 0)에 묶이고, 둘 다에 의존하는
    // ForwardPass는 다음 웨이브(레벨 1)로 분리돼야 한다 — executeParallel이 이 구조로 안전하게
    // 병렬 기록할 수 있는지의 근거.
    const sw::vector<sw::vector<sw::hashed_string>>& waves = graph.getExecutionWaves();
    SW_ASSERT_EQUAL( size_t( 2 ), waves.size() );
    SW_EXPECT_EQUAL( size_t( 2 ), waves[0].size() );
    SW_ASSERT_EQUAL( size_t( 1 ), waves[1].size() );
    SW_EXPECT_TRUE( waves[1][0] == sw::hashed_string( "ForwardPass" ) );

    taskManager.shutdown();
}

/**
 * @brief [RenderPassTest] 완전 직렬 체인은 패스마다 자기 웨이브를 받는다(현재 기본 파이프라인 형태).
 */
SW_TEST_CASE( RenderPassTest, RenderGraphLinearChainProducesSinglePassWaves )
{
    sw::RenderGraph graph;
    graph.addPass( sw::hashed_string( "Shadow" ), {}, { sw::hashed_string( "ShadowMap" ) } );
    graph.addPass( sw::hashed_string( "Forward" ), { sw::hashed_string( "ShadowMap" ) }, { sw::hashed_string( "SceneColor" ) } );
    graph.addPass( sw::hashed_string( "Present" ), { sw::hashed_string( "SceneColor" ) }, {} );

    SW_ASSERT_TRUE( graph.compile() );
    const sw::vector<sw::vector<sw::hashed_string>>& waves = graph.getExecutionWaves();
    SW_ASSERT_EQUAL( size_t( 3 ), waves.size() );
    for ( const sw::vector<sw::hashed_string>& wave : waves )
        SW_EXPECT_EQUAL( size_t( 1 ), wave.size() );
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

    sw::GpuScene     gpuScene;
    const sw::float3 camPos{ 0.0f, 0.0f, 0.0f };
    gpuScene.buildFromScene( &scene, camPos, nullptr );

    SW_EXPECT_TRUE( gpuScene.getOpaqueBatches().empty() == false );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( gpuScene.getInstances().size() ) );
    // 동일 mesh/mat transparent 2개는 정렬 후 연속 머지 → 배치 1, instanceCount 2
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( gpuScene.getTransparentBatches().size() ) );
    SW_EXPECT_EQUAL( 2u, gpuScene.getTransparentBatches()[0]._instanceCount );

    const sw::vector<sw::GpuInstance>& instances = gpuScene.getInstances();
    const sw::GpuMeshBatch&            trBatch   = gpuScene.getTransparentBatches()[0];
    const float32                      z0        = instances[trBatch._instanceBase]._boundsCenter._z;
    const float32                      z1        = instances[trBatch._instanceBase + 1]._boundsCenter._z;
    const float32                      d0        = z0 * z0;
    const float32                      d1        = z1 * z1;
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
        sw::GameObject*    go = objects->createGameObject( sw::hashed_string( "T0" ) );
        sw::MeshComponent* mc = go->addComponent<sw::MeshComponent>();
        mc->setMesh( cube );
        mc->setLocalPosition( sw::float3( 0.0f, 0.0f, -10.0f ) );
        mc->setBlendMode( sw::RHIBlendMode::Transparent );
        mc->setMaterialInstance( a );
    }
    {
        sw::GameObject*    go = objects->createGameObject( sw::hashed_string( "T1" ) );
        sw::MeshComponent* mc = go->addComponent<sw::MeshComponent>();
        mc->setMesh( cube );
        mc->setLocalPosition( sw::float3( 0.0f, 0.0f, -2.0f ) );
        mc->setBlendMode( sw::RHIBlendMode::Transparent );
        mc->setMaterialInstance( b );
    }

    sw::GpuScene     gpuScene;
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
    sw::unique_ptr<sw::IWindow>    window;
    sw::shared_ptr<sw::IRHIDevice> device;
    const sw::RHIBackend           backends[] = {
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
    sw::GameObject*          go   = scene.getObjectManager()->createGameObject( sw::hashed_string( "Cube" ) );
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
 * @brief executePacket()이 프레임마다 GpuScene GPU 버퍼를 재생성하지 않고 재사용하는지 검증.
 * @details GT/RT 소유권 분리(exportCpuSnapshot/adoptCpuSnapshot) 회귀 테스트 — 고치기 전에는
 *          FrameRenderer::_gpuScene이 매 프레임 통째로 덮어써져서 인스턴스 버퍼 핸들이 매번 바뀌었다
 *          (직전 프레임 버퍼/디스크립터는 releaseGpu() 없이 버려지는 누수였음).
 */
SW_TEST_CASE( RenderPassTest, GpuSceneBufferReusedAcrossPackets )
{
    sw::unique_ptr<sw::IWindow>    window;
    sw::shared_ptr<sw::IRHIDevice> device;
    const sw::RHIBackend           backends[] = {
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
        SW_TEST_SKIP( "No RHI backend for GpuScene buffer reuse test" );

    sw::FrameRenderer renderer;
    SW_EXPECT_TRUE( renderer.initialize( device.get() ) );

    sw::Scene scene( "GpuSceneReuseScene" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    sw::shared_ptr<sw::Mesh> cube = sw::Mesh::createUnitCube();
    sw::GameObject*          go   = scene.getObjectManager()->createGameObject( sw::hashed_string( "Cube" ) );
    SW_ASSERT_NOT_NULL( go );
    sw::MeshComponent* mesh = go->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( mesh );
    mesh->setMesh( cube );

    sw::GpuScene        gtGpuScene; // EngineLoop::_gtGpuScene 역할 — 여기서는 테스트 로컬로 흉내
    sw::float4          clear{ 0.02f, 0.02f, 0.05f, 1.0f };
    sw::RHIBufferHandle instanceBufferAfterFrame1{ 0 };

    for ( uint32 frameIndex = 0; frameIndex < 8; ++frameIndex )
    {
        sw::RenderFramePacket packet{};
        packet._bValid = 1;
        gtGpuScene.buildFromScene( &scene, packet._cameraPos, nullptr );
        gtGpuScene.exportCpuSnapshot( packet._gpuScene );

        device->getSwapChain()->beginFrame( clear );
        SW_EXPECT_TRUE( renderer.executePacket( device.get(), packet ) );
        device->getSwapChain()->endFrame( false, false );

        const sw::RHIBufferHandle instanceBuffer = renderer.getGpuScene().getInstanceBuffer();
        SW_EXPECT_TRUE( instanceBuffer != 0 );
        if ( frameIndex == 0 )
            instanceBufferAfterFrame1 = instanceBuffer;
        else
            SW_EXPECT_EQUAL( instanceBufferAfterFrame1, instanceBuffer );
    }

    device->waitIdle();
    if ( cube != nullptr )
        cube->releaseGpu();

    renderer.shutdown();
    device->shutdown();
    device.reset();
    window->destroy();
    window.reset();
}

/**
 * @brief 실제 RHI 디바이스로 RenderGraph::executeParallel을 웨이브 단위로 끝까지 실행해 본다.
 * @details DX12만 _bParallelCommandRecording=1이라 실제로 병렬 경로(패스별 독립 Deferred
 *          커맨드리스트 + TaskManager 스테이지)를 타고, 다른 백엔드는 이 테스트 대상이 아니다.
 *          독립 브랜치(DepthPass/ShadowPass) + 합류 패스(ForwardPass) 구조로 웨이브 경계를 넘나드는
 *          제출 순서(웨이브마다 먼저 제출 후 다음 웨이브)까지 실제로 동작하는지 확인한다.
 */
SW_TEST_CASE( RenderPassTest, RenderGraphExecuteParallelRunsOnRealDevice )
{
    sw::unique_ptr<sw::IWindow>    window;
    sw::shared_ptr<sw::IRHIDevice> device;
    if ( tryInitDeviceForFrameRenderer( sw::RHIBackend::DirectX12, window, device ) == false )
        SW_TEST_SKIP( "No DX12 backend for RenderGraph::executeParallel test" );

    sw::TaskManager taskManager;
    SW_ASSERT_TRUE( taskManager.initialize( 2 ) );

    sw::RenderGraph    graph;
    sw::atomic<uint32> executeCount{ 0 };

    auto makeCb = [&executeCount]( const utf8* pExpectedName ) -> sw::RenderGraphPassExecuteFn
    {
        return sw::RenderGraphPassExecuteFn(
            SW_DELEGATE_LAMBDA( sw::RenderGraphPassExecuteFn, [&executeCount, pExpectedName]( const sw::RenderGraphPassContext& ctx )
        {
            SW_EXPECT_STREQ( pExpectedName, ctx._passName.c_str() );
            SW_EXPECT_TRUE( ctx._pCmdList != nullptr );
            executeCount.fetch_add( 1, std::memory_order_relaxed );
        } ) );
    };

    graph.addPass( sw::hashed_string( "DepthPass" ), {}, { sw::hashed_string( "DepthBuffer" ) }, makeCb( "DepthPass" ) );
    graph.addPass( sw::hashed_string( "ShadowPass" ), {}, { sw::hashed_string( "ShadowMap" ) }, makeCb( "ShadowPass" ) );
    graph.addPass( sw::hashed_string( "ForwardPass" ), { sw::hashed_string( "DepthBuffer" ), sw::hashed_string( "ShadowMap" ) }, { sw::hashed_string( "SceneColor" ) }, makeCb( "ForwardPass" ) );

    sw::RenderGraphExecutionContext context;
    SW_ASSERT_TRUE( graph.executeParallel( context, &taskManager, device.get() ) );
    SW_EXPECT_EQUAL( 3u, executeCount.load() );

    device->waitIdle();
    taskManager.shutdown();
    device->shutdown();
    device.reset();
    window->destroy();
    window.reset();
}

/**
 * @brief 엔진이 실제로 배포하는 파이프라인 XML 들이 스스로 모순이 없는지.
 * @details forward/deferred 둘 다 검증 0건이어야 한다. 여기가 깨지면 런타임에 포맷이 어긋나
 *          조용히 잘못 그리거나 GPU 가 죽는다(`ae7fb078` 이 그 사례였다).
 */
SW_TEST_CASE( RenderPassTest, ShippedPipelinesValidateClean )
{
    const std::string_view arrPipeline[] = {
        "engine/pipeline/forwardpipeline.xml",
        "engine/pipeline/deferredpipeline.xml",
    };
    for ( std::string_view path : arrPipeline )
    {
        sw::RenderPipelineResource res;
        SW_ASSERT_TRUE( res.loadFromXmlFile( path ) );
        SW_EXPECT_EQUAL( 0u, res.validate( path ) );
        // 모든 패스 타입이 해석돼야 한다 — Invalid 가 남아 있으면 PSO 가 기본 포맷으로 만들어진다.
        for ( const sw::RenderGraphPassDesc& pass : res.getGraphPass() )
            SW_EXPECT_TRUE( sw::isPipelinePassType( pass._resolvedType ) );
    }
}

/**
 * @brief 파이프라인 검증이 실제로 문제를 잡는지 — 잡지 못하는 검증은 없느니만 못하다.
 */
SW_TEST_CASE( RenderPassTest, PipelineValidationCatchesInconsistencies )
{
    // 1) 알 수 없는 패스 타입
    {
        sw::RenderPipelineResource res;
        sw::RenderPipelineDesc&    desc = res.getDesc();
        sw::RenderGraphPassDesc    pass{};
        pass._name = "Bad";
        pass._type = "NoSuchPassType";
        desc._listPass.push_back( pass );
        SW_EXPECT_TRUE( res.validate( "unit-test" ) > 0u );
        SW_EXPECT_TRUE( desc._listPass[0]._resolvedType == sw::RenderPassType::Invalid );
    }

    // 2) 선언되지 않은 첨부를 입출력으로 참조
    {
        sw::RenderPipelineResource res;
        sw::RenderPipelineDesc&    desc = res.getDesc();
        sw::RenderGraphPassDesc    pass{};
        pass._name = "Dangling";
        pass._type = "ForwardOpaque";
        pass._listOutput.push_back( "NotDeclared" );
        desc._listPass.push_back( pass );
        SW_EXPECT_TRUE( res.validate( "unit-test" ) > 0u );
    }

    // 3) Swapchain 은 첨부로 선언하지 않는 예약어라 통과해야 한다
    {
        sw::RenderPipelineResource res;
        sw::RenderPipelineDesc&    desc = res.getDesc();
        sw::RenderGraphPassDesc    pass{};
        pass._name = "Blit";
        pass._type = "Present";
        pass._listOutput.push_back( "Swapchain" );
        desc._listPass.push_back( pass );
        SW_EXPECT_EQUAL( 0u, res.validate( "unit-test" ) );
    }

    // 4) 알 수 없는 첨부 포맷
    {
        sw::RenderPipelineResource res;
        sw::RenderPipelineDesc&    desc = res.getDesc();
        sw::RenderPassAttachment   att{};
        att._name   = "Weird";
        att._format = "R99G99_NOPE";
        desc._listAttachment.push_back( att );
        SW_EXPECT_TRUE( res.validate( "unit-test" ) > 0u );
    }

    // 5) 이름은 정본 하나로 통일돼 있다 — 예전 표기(`Shading`, `PostBloom`)는 이제 오류로 잡힌다.
    //    다시 이름을 바꿔야 하면 ENUM( ValueAlias = "Old:New" ) 로 호환을 열어 주면 된다.
    {
        auto retiredIsRejected = []( const utf8* pRetired ) -> bool
        {
            sw::RenderPipelineResource res;
            sw::RenderPipelineDesc&    desc = res.getDesc();
            sw::RenderGraphPassDesc    pass{};
            pass._name = "Retired";
            pass._type = pRetired;
            desc._listPass.push_back( pass );
            return res.validate( "unit-test" ) > 0u;
        };
        SW_EXPECT_TRUE( retiredIsRejected( "Shading" ) );
        SW_EXPECT_TRUE( retiredIsRejected( "PostBloom" ) );
        SW_EXPECT_TRUE( retiredIsRejected( "HBAO" ) );
        // 철자 대소문자는 리플렉션이 무시하므로 "ToneMap" 은 "Tonemap" 으로 읽힌다 — 의도된 관용이다.
        SW_EXPECT_TRUE( retiredIsRejected( "ToneMap" ) == false );
    }

    // 6) 엔진 내부 PSO 슬롯은 XML 패스 타입으로 쓸 수 없다.
    {
        sw::RenderPipelineResource res;
        sw::RenderPipelineDesc&    desc = res.getDesc();
        sw::RenderGraphPassDesc    pass{};
        pass._name = "Internal";
        pass._type = "GpuCull";
        desc._listPass.push_back( pass );
        SW_EXPECT_TRUE( res.validate( "unit-test" ) > 0u );
    }
}

/**
 * @brief Deferred 파이프라인을 실제 디바이스에서 돌린다 — 같은 웨이브의 패스가 병렬로 기록되는 유일한 구성.
 * @details forwardpipeline 은 Shadow→ForwardOpaque→…→Present 완전 체인이라 웨이브가 전부 1개다.
 *          즉 병렬 기록 경로가 있어도 실제로 동시에 도는 패스가 없었고, 그래서 패스 콜백이 만지는
 *          FrameRenderer 공유 상태(_listClearedThisFrame, 프레임 래치 플래그)의 레이스가 드러나지
 *          않았다. deferredpipeline 은 웨이브0 = {Shadow, GBuffer}, 이후 {Transparent, SSAO} 가
 *          동시에 기록된다. 메시가 있어야 드로우 경로까지 들어가므로 큐브를 넣고 여러 프레임 돌린다.
 *
 *          이 테스트를 처음 넣었을 때 곧바로 DX12 GPU 행(3번째 프레임에서 fence wait timeout →
 *          DEVICE_HUNG → 크래시)을 잡아냈다. 원인은 파이프라인 XML 이 선언한 포맷과 PSO/보조
 *          텍스처가 어긋난 것이었다(Shading 별칭 미해석, 풀스크린 PSO 의 뎁스 포맷, TAA 히스토리
 *          포맷 하드코딩). 검증 레이어 오류가 0 인지도 같이 봐야 의미가 있다.
 */
SW_TEST_CASE( RenderPassTest, FrameRendererDeferredPipelineParallelWaves )
{
    sw::unique_ptr<sw::IWindow>    window;
    sw::shared_ptr<sw::IRHIDevice> device;
    const sw::RHIBackend           backends[] = { sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan };
    bool                           bOk{ false };
    for ( sw::RHIBackend backend : backends )
    {
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) )
        {
            bOk = true;
            break;
        }
    }
    if ( bOk == false )
        SW_TEST_SKIP( "No parallel-recording backend for deferred pipeline test" );

    sw::TaskManager taskManager;
    SW_ASSERT_TRUE( taskManager.initialize( 4 ) );

    sw::FrameRenderer renderer;
    SW_ASSERT_TRUE( renderer.initialize( device.get(), &taskManager, "engine/pipeline/deferredpipeline.xml" ) );
    SW_EXPECT_TRUE( renderer.isReady() );

    sw::Scene scene( "DeferredParallelScene" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    sw::shared_ptr<sw::Mesh> cube = sw::Mesh::createUnitCube();
    for ( uint32 objectIndex = 0; objectIndex < 4; ++objectIndex )
    {
        sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( "DeferredCube" ) );
        SW_ASSERT_NOT_NULL( pObj );
        sw::MeshComponent* pMesh = pObj->addComponent<sw::MeshComponent>();
        SW_ASSERT_NOT_NULL( pMesh );
        pMesh->setMesh( cube );
        // 인스턴스를 서로 다른 월드 행렬로 흩어 놓아야 드로우 루프의 월드 갱신 분기까지 탄다.
        pMesh->setLocalPosition( sw::float3{ static_cast<float32>( objectIndex ) * 1.5f, 0.0f, 0.0f } );
    }

    // 여러 프레임 돌린다 — 레이스는 한 프레임만으로는 잘 드러나지 않는다.
    const sw::float4 clear = { 0.02f, 0.02f, 0.05f, 1.0f };
    for ( uint32 frameIndex = 0; frameIndex < 8; ++frameIndex )
    {
        device->getSwapChain()->beginFrame( clear );
        SW_EXPECT_TRUE( renderer.execute( device.get(), nullptr, &scene ) );
        device->getSwapChain()->endFrame( false, false );
    }
    device->waitIdle();

    // 드로우 경로까지 실제로 들어갔는지 — GpuScene 이 비어 있으면 이 테스트는 클리어만 검증한 셈이다.
    SW_EXPECT_TRUE( renderer.getGpuScene().getInstances().empty() == false );

    if ( cube != nullptr )
        cube->releaseGpu();
    renderer.shutdown();
    taskManager.shutdown();
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
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;

        ++attemptedCount;

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "FrameRendererParityScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        sw::shared_ptr<sw::Mesh> cube;
        if ( bOk )
        {
            cube               = sw::Mesh::createUnitCube();
            sw::GameObject* go = scene.getObjectManager()->createGameObject( sw::hashed_string( "Cube" ) );
            bOk                = go != nullptr;
            if ( bOk )
            {
                sw::MeshComponent* meshComp = go->addComponent<sw::MeshComponent>();
                bOk                         = meshComp != nullptr;
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
    uint32          passAExecuted = 0;
    uint32          passBExecuted = 0;

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

/**
 * @brief [RenderPassTest] 8대 전체 스테이지 진입점을 명시한 파이프라인 XML 직렬화/역직렬화 라운드트립 검증
 */
SW_TEST_CASE( RenderPassTest, PipelineExtendedStagesRoundtrip )
{
    sw::RenderPipelineResource pipeRes;
    sw::RenderPipelineDesc&    desc = pipeRes.getDesc();
    desc._name                      = "UnitTestAllStagesPipeline";
    desc._shadingModel              = "Deferred";

    sw::RenderGraphPassDesc pass{};
    pass._name                    = "MegaShaderPass";
    pass._type                    = "CustomGraphics";
    pass._shaderPath              = "engine/shaders/custom.hlsl";
    pass._vertexEntryPoint        = "VSMainCustom";
    pass._pixelEntryPoint         = "PSMainCustom";
    pass._computeEntryPoint       = "CSMainCustom";
    pass._geometryEntryPoint      = "GSMainCustom";
    pass._hullEntryPoint          = "HSMainCustom";
    pass._domainEntryPoint        = "DSMainCustom";
    pass._meshEntryPoint          = "MSMainCustom";
    pass._amplificationEntryPoint = "ASMainCustom";
    pass._listInput.push_back( "DepthBuffer" );
    pass._listOutput.push_back( "HDRColor" );

    desc._listPass.push_back( pass );

    const sw::string testPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_pipeline_all_stages.xml" );
    SW_EXPECT_TRUE( pipeRes.saveToXmlFile( testPath ) );

    sw::RenderPipelineResource loadedRes;
    SW_EXPECT_TRUE( loadedRes.loadFromXmlFile( testPath ) );
    SW_EXPECT_EQUAL( size_t( 1 ), loadedRes.getGraphPass().size() );

    const sw::RenderGraphPassDesc& loadedPass = loadedRes.getGraphPass()[0];
    SW_EXPECT_EQUAL( sw::string( "MegaShaderPass" ), loadedPass._name );
    SW_EXPECT_EQUAL( sw::string( "VSMainCustom" ), loadedPass._vertexEntryPoint );
    SW_EXPECT_EQUAL( sw::string( "PSMainCustom" ), loadedPass._pixelEntryPoint );
    SW_EXPECT_EQUAL( sw::string( "CSMainCustom" ), loadedPass._computeEntryPoint );
    SW_EXPECT_EQUAL( sw::string( "GSMainCustom" ), loadedPass._geometryEntryPoint );
    SW_EXPECT_EQUAL( sw::string( "HSMainCustom" ), loadedPass._hullEntryPoint );
    SW_EXPECT_EQUAL( sw::string( "DSMainCustom" ), loadedPass._domainEntryPoint );
    SW_EXPECT_EQUAL( sw::string( "MSMainCustom" ), loadedPass._meshEntryPoint );
    SW_EXPECT_EQUAL( sw::string( "ASMainCustom" ), loadedPass._amplificationEntryPoint );

    sw::FileUtil::removeFile( testPath );
}

/**
 * @brief [RenderPassTest] 미지정 확장 스테이지 진입점의 XML 직렬화 압축성(빈 태그 배제) 검증
 */
SW_TEST_CASE( RenderPassTest, PipelineEmptyStagesCompactness )
{
    sw::RenderPipelineResource pipeRes;
    sw::RenderPipelineDesc&    desc = pipeRes.getDesc();
    desc._name                      = "UnitTestCompactPipeline";
    desc._shadingModel              = "Forward";

    sw::RenderGraphPassDesc pass{};
    pass._name             = "CompactPass";
    pass._type             = "ForwardOpaque";
    pass._shaderPath       = "engine/shaders/forwardlit.hlsl";
    pass._vertexEntryPoint = "VSMain";
    pass._pixelEntryPoint  = "PSMain";
    // _computeEntryPoint, _geometryEntryPoint 등은 비어 있음
    desc._listPass.push_back( pass );

    const sw::string testPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_pipeline_compact.xml" );
    SW_EXPECT_TRUE( pipeRes.saveToXmlFile( testPath ) );

    sw::string xmlContent;
    SW_EXPECT_TRUE( sw::FileUtil::readTextFile( testPath, xmlContent ) );

    // 빈 확장 태그가 XML 텍스트에 포함되지 않았는지 검증
    SW_EXPECT_TRUE( xmlContent.find( "_geometryEntryPoint" ) == sw::string::npos );
    SW_EXPECT_TRUE( xmlContent.find( "_hullEntryPoint" ) == sw::string::npos );
    SW_EXPECT_TRUE( xmlContent.find( "_domainEntryPoint" ) == sw::string::npos );
    SW_EXPECT_TRUE( xmlContent.find( "_meshEntryPoint" ) == sw::string::npos );
    SW_EXPECT_TRUE( xmlContent.find( "_amplificationEntryPoint" ) == sw::string::npos );
    SW_EXPECT_TRUE( xmlContent.find( "_computeEntryPoint" ) == sw::string::npos );

    // 다시 로드했을 때 기본 빈 문자열 상태가 안전하게 유지되는지 검증
    sw::RenderPipelineResource loadedRes;
    SW_EXPECT_TRUE( loadedRes.loadFromXmlFile( testPath ) );
    const sw::RenderGraphPassDesc& loadedPass = loadedRes.getGraphPass()[0];
    SW_EXPECT_TRUE( loadedPass._geometryEntryPoint.empty() );
    SW_EXPECT_TRUE( loadedPass._hullEntryPoint.empty() );
    SW_EXPECT_TRUE( loadedPass._domainEntryPoint.empty() );
    SW_EXPECT_TRUE( loadedPass._meshEntryPoint.empty() );
    SW_EXPECT_TRUE( loadedPass._amplificationEntryPoint.empty() );
    SW_EXPECT_TRUE( loadedPass._computeEntryPoint.empty() );

    sw::FileUtil::removeFile( testPath );
}
