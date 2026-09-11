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
 * @brief 프리미티브 등록부: 변경을 알린 것만 다시 만들고, 알린 게 없으면 수집조차 하지 않는다.
 * @details 이 구조의 최악 실패 모드는 "움직였는데 화면이 안 따라오는 것"이다. 등록·해제·더티
 *          신호 중 하나라도 빠지면 여기서 걸린다.
 */
SW_TEST_CASE( RenderPassTest, GpuScenePrimitiveRegistryTracksChanges )
{
    sw::Scene scene( "GpuScenePrimitiveRegistry" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );

    sw::GameObjectManager* objects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( objects );

    sw::shared_ptr<sw::Mesh> cube = sw::Mesh::createUnitCube();
    SW_ASSERT_NOT_NULL( cube.get() );

    auto addMeshAt = [&]( const utf8* pName, float32 x ) -> sw::MeshComponent*
    {
        sw::GameObject* go = objects->createGameObject( sw::hashed_string( pName ) );
        if ( go == nullptr )
            return nullptr;
        sw::MeshComponent* mesh = go->addComponent<sw::MeshComponent>();
        if ( mesh == nullptr )
            return nullptr;
        mesh->setMesh( cube );
        mesh->setLocalPosition( sw::float3( x, 0.0f, -1.0f ) );
        mesh->setVisible( true );
        return mesh;
    };

    sw::MeshComponent* pMeshA = addMeshAt( "RegA", 0.0f );
    sw::MeshComponent* pMeshB = addMeshAt( "RegB", 2.0f );
    SW_ASSERT_NOT_NULL( pMeshA );
    SW_ASSERT_NOT_NULL( pMeshB );

    // 붙는 것만으로 등록부에 들어간다 — 렌더러가 씬을 뒤져 찾지 않는다.
    SW_EXPECT_EQUAL( size_t( 2 ), objects->getPrimitiveRegistry().getAll().size() );

    sw::GpuScene     gpuScene;
    const sw::float3 camPos{ 0.0f, 0.0f, 0.0f };
    gpuScene.buildFromScene( &scene, camPos, nullptr );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( gpuScene.getInstances().size() ) );

    // 1) 움직이면 반영된다.
    pMeshA->setLocalPosition( sw::float3( 7.0f, 0.0f, -1.0f ) );
    SW_EXPECT_TRUE( objects->getPrimitiveRegistry().hasDirty() || objects->getPrimitiveRegistry().getSetGeneration() != 0 );
    gpuScene.buildFromScene( &scene, camPos, nullptr );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( gpuScene.getInstances().size() ) );
    bool bFoundMoved = false;
    for ( const sw::GpuInstance& inst : gpuScene.getInstances() )
    {
        if ( sw::MathUtil::nearEqual( inst._boundsCenter._x, 7.0f ) )
            bFoundMoved = true;
    }
    SW_EXPECT_TRUE( bFoundMoved );

    // 2) 아무도 안 바뀌면 더티가 없다 — 이게 수집을 건너뛰는 근거다.
    SW_EXPECT_TRUE( objects->getPrimitiveRegistry().hasDirty() == false );

    // 3) 가시성을 끄면 빠진다.
    pMeshB->setVisible( false );
    SW_EXPECT_TRUE( objects->getPrimitiveRegistry().hasDirty() );
    gpuScene.buildFromScene( &scene, camPos, nullptr );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( gpuScene.getInstances().size() ) );

    // 4) 컴포넌트를 떼면 등록부에서도 빠진다.
    sw::GameObject* pOwnerB = pMeshB->getOwner();
    SW_ASSERT_NOT_NULL( pOwnerB );
    SW_EXPECT_TRUE( pOwnerB->removeComponent( pMeshB ) );
    SW_EXPECT_EQUAL( size_t( 1 ), objects->getPrimitiveRegistry().getAll().size() );

    gpuScene.buildFromScene( &scene, camPos, nullptr );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( gpuScene.getInstances().size() ) );

    // 5) 오브젝트를 비활성화하면 집합이 바뀐다.
    sw::GameObject* pOwnerA = pMeshA->getOwner();
    SW_ASSERT_NOT_NULL( pOwnerA );
    pOwnerA->setActive( false );
    gpuScene.buildFromScene( &scene, camPos, nullptr );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( gpuScene.getInstances().size() ) );
}

/**
 * @brief 트랜스폼만 바뀌면 배치를 다시 나누지 않지만, 결과는 다시 나눈 것과 같아야 한다.
 * @details 정렬을 건너뛰는 경로라 조용히 틀리기 쉽다. 키가 바뀐 경우와 나란히 확인한다.
 */
SW_TEST_CASE( RenderPassTest, GpuSceneTransformOnlyChangeKeepsBatches )
{
    sw::Scene scene( "GpuSceneTransformOnly" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );

    sw::GameObjectManager* objects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( objects );

    sw::shared_ptr<sw::Mesh> cube = sw::Mesh::createUnitCube();
    SW_ASSERT_NOT_NULL( cube.get() );

    auto addMeshAt = [&]( const utf8* pName, float32 x, float32 z, sw::RHIBlendMode blend ) -> sw::MeshComponent*
    {
        sw::GameObject* go = objects->createGameObject( sw::hashed_string( pName ) );
        if ( go == nullptr )
            return nullptr;
        sw::MeshComponent* mesh = go->addComponent<sw::MeshComponent>();
        if ( mesh == nullptr )
            return nullptr;
        mesh->setMesh( cube );
        mesh->setLocalPosition( sw::float3( x, 0.0f, z ) );
        mesh->setBlendMode( blend );
        mesh->setVisible( true );
        return mesh;
    };

    sw::MeshComponent* pOpaqueA = addMeshAt( "TfOpaqueA", 0.0f, -1.0f, sw::RHIBlendMode::Opaque );
    addMeshAt( "TfOpaqueB", 1.0f, -1.0f, sw::RHIBlendMode::Opaque );
    addMeshAt( "TfTransFar", 0.0f, -10.0f, sw::RHIBlendMode::Transparent );
    sw::MeshComponent* pTransNear = addMeshAt( "TfTransNear", 0.0f, -2.0f, sw::RHIBlendMode::Transparent );
    SW_ASSERT_NOT_NULL( pOpaqueA );
    SW_ASSERT_NOT_NULL( pTransNear );

    sw::GpuScene     gpuScene;
    const sw::float3 camPos{ 0.0f, 0.0f, 0.0f };
    gpuScene.buildFromScene( &scene, camPos, nullptr );

    const uint32 opaqueBatchCount      = static_cast<uint32>( gpuScene.getOpaqueBatches().size() );
    const uint32 transparentBatchCount = static_cast<uint32>( gpuScene.getTransparentBatches().size() );
    SW_ASSERT_EQUAL( 1u, transparentBatchCount );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( gpuScene.getInstances().size() ) );

    // 1) 트랜스폼만 변경 — 배치 구성은 그대로여야 하고, 위치는 반영돼야 한다.
    pOpaqueA->setLocalPosition( sw::float3( 5.0f, 0.0f, -1.0f ) );
    gpuScene.buildFromScene( &scene, camPos, nullptr );

    SW_EXPECT_EQUAL( opaqueBatchCount, static_cast<uint32>( gpuScene.getOpaqueBatches().size() ) );
    SW_EXPECT_EQUAL( transparentBatchCount, static_cast<uint32>( gpuScene.getTransparentBatches().size() ) );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( gpuScene.getInstances().size() ) );
    bool bMovedApplied = false;
    for ( const sw::GpuInstance& inst : gpuScene.getInstances() )
    {
        if ( sw::MathUtil::nearEqual( inst._boundsCenter._x, 5.0f ) )
            bMovedApplied = true;
    }
    SW_EXPECT_TRUE( bMovedApplied );

    // transparent 는 트랜스폼이 바뀌면 다시 정렬돼야 한다 (먼 것 → 가까운 것).
    {
        const sw::vector<sw::GpuInstance>& instances = gpuScene.getInstances();
        const sw::GpuMeshBatch&            trBatch   = gpuScene.getTransparentBatches()[0];
        SW_ASSERT_EQUAL( 2u, trBatch._instanceCount );
        const float32 z0 = instances[trBatch._instanceBase]._boundsCenter._z;
        const float32 z1 = instances[trBatch._instanceBase + 1]._boundsCenter._z;
        SW_EXPECT_TRUE( ( z0 * z0 ) >= ( z1 * z1 ) );
    }

    // 2) 배치 키가 바뀌면(블렌드 모드) 다시 나눠야 한다 — 빠른 경로로 새면 안 된다.
    pTransNear->setBlendMode( sw::RHIBlendMode::Opaque );
    gpuScene.buildFromScene( &scene, camPos, nullptr );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( gpuScene.getInstances().size() ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( gpuScene.getTransparentBatches().size() ) );
    SW_EXPECT_EQUAL( 1u, gpuScene.getTransparentBatches()[0]._instanceCount );
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
SW_TEST_CASE( RenderPassGpuTest, FrameRendererInitializeAndExecuteSmoke )
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
    device->beginFrame( clear );
    SW_EXPECT_TRUE( renderer.execute( device.get(), nullptr, &scene ) );
    device->endFrame( false, false );
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
 * @brief [RenderPassTest] 셰이더 핫리로드가 PSO 를 **실제로 다시 만드는지** 검증.
 * @details PSO 는 바이트코드를 구워 넣은 객체다. onShaderRecompiled 가 바인딩 레이아웃만
 *          새로 만들던 시절에는 셰이더를 고쳐도 화면이 시작 시 컴파일된 그대로였다 —
 *          로그는 "Recompilation Succeeded" 를 찍는데 그림은 안 바뀌니 눈치채기 어려웠다.
 *          LiveShaderTest 는 등록과 리로드 큐만 보므로 이 경로를 잡지 못한다.
 *
 *          네 백엔드 모두 PSO 를 RHIHandleTable(generation 팩드)로 발급하므로, 다시 만들면
 *          핸들 값이 반드시 달라진다. 재생성 여부를 핸들로 판정하는 근거다.
 */
SW_TEST_CASE( RenderPassGpuTest, ShaderRecompileRebuildsPipelineStates )
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
        SW_TEST_SKIP( "No RHI backend for shader recompile test" );

    sw::FrameRenderer renderer;
    SW_EXPECT_TRUE( renderer.initialize( device.get() ) );
    SW_EXPECT_TRUE( renderer.isReady() );

    const sw::RHIPipelineStateHandle beforeForward = renderer.getEnginePso( sw::RenderPassType::ForwardOpaque );
    SW_EXPECT_TRUE_MSG( beforeForward != 0, "리로드 전 ForwardOpaque PSO 가 있어야 한다" );

    sw::ShaderCompileResult result{};
    result._bSuccess = true;
    renderer.onShaderRecompiled( "engine/shaders/forwardlit.hlsl", result );

    const sw::RHIPipelineStateHandle afterForward = renderer.getEnginePso( sw::RenderPassType::ForwardOpaque );
    SW_EXPECT_TRUE_MSG( afterForward != 0, "리로드 후 ForwardOpaque PSO 가 다시 만들어져야 한다" );
    SW_EXPECT_TRUE_MSG( afterForward != beforeForward,
                        "PSO 핸들이 그대로다 — 레이아웃만 갱신하고 파이프라인은 예전 바이트코드를 들고 있다" );

    // 재생성 뒤에도 여전히 그릴 수 있어야 한다 (레이아웃·폴백 버퍼·콜백이 같이 재구축됐는지).
    sw::Scene scene( "ShaderRecompileScene" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    sw::shared_ptr<sw::Mesh> cube = sw::Mesh::createUnitCube();
    sw::GameObject*          pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( "Cube" ) );
    SW_ASSERT_NOT_NULL( pObj );
    sw::MeshComponent* pMesh = pObj->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    pMesh->setMesh( cube );

    const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
    device->beginFrame( clear );
    SW_EXPECT_TRUE_MSG( renderer.execute( device.get(), nullptr, &scene ), "PSO 재생성 후 프레임 실행" );
    device->endFrame( false, false );
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
 * @brief executePacket()이 프레임마다 GpuScene GPU 버퍼를 재생성하지 않고 재사용하는지 검증.
 * @details GT/RT 소유권 분리(exportCpuSnapshot/adoptCpuSnapshot) 회귀 테스트 — 고치기 전에는
 *          FrameRenderer::_gpuScene이 매 프레임 통째로 덮어써져서 인스턴스 버퍼 핸들이 매번 바뀌었다
 *          (직전 프레임 버퍼/디스크립터는 releaseGpu() 없이 버려지는 누수였음).
 */
SW_TEST_CASE( RenderPassGpuTest, GpuSceneBufferReusedAcrossPackets )
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

        device->beginFrame( clear );
        SW_EXPECT_TRUE( renderer.executePacket( device.get(), packet ) );
        device->endFrame( false, false );

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
SW_TEST_CASE( RenderPassGpuTest, RenderGraphExecuteParallelRunsOnRealDevice )
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
SW_TEST_CASE( RenderPassGpuTest, FrameRendererDeferredPipelineParallelWaves )
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
        device->beginFrame( clear );
        SW_EXPECT_TRUE( renderer.execute( device.get(), nullptr, &scene ) );
        device->endFrame( false, false );
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
 * @brief [GpuSceneTest] 머티리얼 원소 인덱스가 프레임을 넘어 유지되고, 안 쓰이면 회수되는지 (GPU 불필요).
 * @details 언리얼 GPUScene 은 프리미티브·머티리얼에 등록 시점에 **영속 ID** 를 주고 더티한 것만 갱신한다.
 *          예전엔 매 빌드마다 그룹을 지우고 인스턴스마다 인덱스를 다시 부여했다 — O(인스턴스 x 머티리얼) 이고,
 *          같은 머티리얼의 인덱스가 프레임마다 달라져 "바뀐 것만 올린다" 를 할 수 없었다.
 *
 *          여기서 보는 것 둘: (1) 같은 머티리얼은 빌드를 반복해도 같은 인덱스를 갖는다,
 *          (2) 쓰이지 않게 된 원소는 지연 회수돼 자리가 재사용된다(자리를 **옮기지 않고**).
 *          기본 생성한 Material 은 셰이더 경로가 비어 있어 한 그룹에 모인다 — 리소스 없이 원소 로직만 본다.
 */
SW_TEST_CASE( GpuSceneTest, MaterialElementIdsPersistAcrossBuildsAndRetire )
{
    sw::Scene scene( "MaterialElementIdScene" );
    SW_ASSERT_TRUE( scene.ensureDefaultCameras() );

    sw::shared_ptr<sw::Mesh>     mesh      = sw::Mesh::createUnitCube();
    sw::unique_ptr<sw::Material> materialA = sw::make_unique<sw::Material>();
    sw::unique_ptr<sw::Material> materialB = sw::make_unique<sw::Material>();
    SW_ASSERT_TRUE( mesh != nullptr && materialA != nullptr && materialB != nullptr );

    auto addObject = [&]( const utf8* pName, sw::Material* pMaterial, float32 offsetX ) -> sw::GameObject*
    {
        sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( pName ) );
        if ( pObj == nullptr )
            return nullptr;
        sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
        if ( pMeshComp == nullptr )
            return nullptr;
        pMeshComp->setMesh( mesh );
        pMeshComp->setMaterial( pMaterial );
        pMeshComp->setLocalPosition( sw::float3{ offsetX, 0.0f, 0.0f } );
        return pObj;
    };

    sw::GameObject* pObjA = addObject( "MatA", materialA.get(), -1.0f );
    sw::GameObject* pObjB = addObject( "MatB", materialB.get(), 1.0f );
    SW_ASSERT_TRUE( pObjA != nullptr && pObjB != nullptr );

    // 머티리얼 → 원소 인덱스를 읽는 helper. 그룹은 하나뿐이다(둘 다 셰이더 경로가 비어 있다).
    auto findElementIndex = [&]( const sw::GpuScene& gpuScene, const sw::Material* pMaterial ) -> int32
    {
        for ( const sw::GpuMaterialGroup& group : gpuScene.getMaterialGroups() )
        {
            for ( uint32 index = 0; index < group._listEntry.size(); ++index )
            {
                if ( group._listEntry[index].first == pMaterial )
                    return static_cast<int32>( index );
            }
        }
        return -1;
    };

    sw::GpuScene     gpuScene;
    const sw::float3 cameraPos{ 0.0f, 1.2f, 3.2f };
    gpuScene.buildFromScene( &scene, cameraPos, nullptr );
    const int32 firstA = findElementIndex( gpuScene, materialA.get() );
    const int32 firstB = findElementIndex( gpuScene, materialB.get() );
    SW_EXPECT_TRUE_MSG( firstA >= 0 && firstB >= 0, "첫 빌드에서 두 머티리얼이 모두 원소를 받아야 한다" );
    SW_EXPECT_TRUE_MSG( firstA != firstB, "서로 다른 머티리얼은 서로 다른 원소여야 한다" );

    // (1) 여러 번 다시 빌드해도 인덱스가 그대로여야 한다.
    for ( uint32 buildIndex = 0; buildIndex < 4; ++buildIndex )
    {
        gpuScene.buildFromScene( &scene, cameraPos, nullptr );
        SW_EXPECT_TRUE_MSG( findElementIndex( gpuScene, materialA.get() ) == firstA, "머티리얼 A 의 원소 인덱스가 빌드마다 바뀐다" );
        SW_EXPECT_TRUE_MSG( findElementIndex( gpuScene, materialB.get() ) == firstB, "머티리얼 B 의 원소 인덱스가 빌드마다 바뀐다" );
    }

    // (2) B 를 씬에서 빼고 회수 기준을 넘겨 빌드하면 B 의 자리가 비워져야 한다.
    //     회수 시계는 **실제로 원소를 다시 부여한 빌드**에서만 돈다 — 내용이 그대로면 buildFromScene 이
    //     조기 종료하므로(살아 있는 원소를 다시 표시하지 않는다) 그때 시계를 돌리면 멀쩡한 것이 회수된다.
    //     그래서 매번 A 를 조금씩 움직여 실제 리빌드를 일으킨다.
    sw::MeshComponent* pMeshB = pObjB->getComponent<sw::MeshComponent>();
    SW_ASSERT_TRUE( pMeshB != nullptr );
    pMeshB->setVisible( false );
    sw::MeshComponent* pMeshA = pObjA->getComponent<sw::MeshComponent>();
    SW_ASSERT_TRUE( pMeshA != nullptr );
    for ( uint32 buildIndex = 0; buildIndex < sw::GpuMaterialRetireQueue::kRetireFrameDelay + 3; ++buildIndex )
    {
        pMeshA->setLocalPosition( sw::float3{ -1.0f + static_cast<float32>( buildIndex ) * 0.01f, 0.0f, 0.0f } );
        gpuScene.buildFromScene( &scene, cameraPos, nullptr );
    }

    SW_EXPECT_TRUE_MSG( findElementIndex( gpuScene, materialB.get() ) < 0, "안 쓰이게 된 머티리얼 원소가 회수되지 않았다" );
    SW_EXPECT_TRUE_MSG( findElementIndex( gpuScene, materialA.get() ) == firstA, "남아 있는 머티리얼의 인덱스는 회수 뒤에도 그대로여야 한다" );

    // 회수된 자리는 새 머티리얼이 재사용한다 — 자리를 옮기지 않으므로 A 의 인덱스는 여전히 그대로다.
    sw::unique_ptr<sw::Material> materialC = sw::make_unique<sw::Material>();
    SW_ASSERT_TRUE( addObject( "MatC", materialC.get(), 2.0f ) != nullptr );
    gpuScene.buildFromScene( &scene, cameraPos, nullptr );
    SW_EXPECT_TRUE_MSG( findElementIndex( gpuScene, materialC.get() ) == firstB, "회수된 자리를 새 머티리얼이 재사용해야 한다" );
    SW_EXPECT_TRUE_MSG( findElementIndex( gpuScene, materialA.get() ) == firstA, "새 머티리얼이 들어와도 기존 인덱스는 그대로여야 한다" );
}

/**
 * @brief [GpuSceneTest] 배치마다 **자기 머티리얼 원소**를 고르고, 값이 다르면 바이트도 다른지 (GPU 불필요).
 * @details 지금까지의 렌더 검증은 전부 씬 기본 머티리얼 하나였다 — 머티리얼 원소가 하나뿐이라 `materialIndex`
 *          가 늘 0 이었고, "배치마다 올바른 원소를 고르는가" 가 한 번도 검사되지 않았다. 영속 원소 ID 로 바꾼 뒤라
 *          특히 중요하다(인덱스가 프레임을 넘어 유지되고 회수 후 재사용된다).
 *
 *          픽셀로 보려 했지만 조명·톤매핑이 섞여 값이 흔들렸다. 여기서는 **CPU 스냅샷**을 본다 —
 *          두 머티리얼이 서로 다른 원소를 받는가, 그리고 프로퍼티 값이 다르면 패킹된 바이트도 다른가.
 *          백엔드 간 패킹 일치는 ShaderBindingContractTest.ReflectionNamesAreUniformAcrossBackends 가 본다.
 */
SW_TEST_CASE( GpuSceneTest, PerBatchMaterialElementsAreDistinct )
{
    // 머티리얼은 **실제 에셋**을 읽어 색만 바꾼다. XML 을 손으로 지어내면 퍼뮤테이션 선언(_permutations)이
    // 빠져 구워둔 셰이더 변형과 맞지 않는 머티리얼이 만들어진다 — 그러면 검증하려던 것과 다른 걸 재게 된다.
    auto makeMaterial = []( const utf8* pColor ) -> sw::unique_ptr<sw::Material>
    {
        // 반환 대상을 하나로 둔다 — nullptr 과 material 을 섞어 돌려주면 NRVO 가 걸리지 않는다.
        sw::unique_ptr<sw::Material> material = sw::make_unique<sw::Material>();
        if ( material->loadFromFile( "engine/materials/defaultmaterial.material" ) == false ||
             material->setPropertyValue( nullptr, sw::hashed_string( "color" ), pColor ) == false )
            material.reset();
        return material;
    };

    sw::unique_ptr<sw::Material> materialRed  = makeMaterial( "1.0 0.05 0.05 1.0" );
    sw::unique_ptr<sw::Material> materialBlue = makeMaterial( "0.05 0.05 1.0 1.0" );
    SW_ASSERT_TRUE( materialRed != nullptr && materialBlue != nullptr );

    // 프로퍼티 값이 다르면 패킹된 바이트도 달라야 한다 — 같은 셰이더라 레이아웃은 같고 값만 다르다.
    const sw::vector<uint8>& bytesRed  = materialRed->getBuffer();
    const sw::vector<uint8>& bytesBlue = materialBlue->getBuffer();
    SW_EXPECT_TRUE_MSG( bytesRed.empty() == false && bytesBlue.empty() == false, "머티리얼 바이트가 비어 있다" );
    SW_EXPECT_TRUE_MSG( bytesRed.size() == bytesBlue.size(), "같은 셰이더인데 패킹 크기가 다르다" );
    if ( bytesRed.size() == bytesBlue.size() && bytesRed.empty() == false )
    {
        SW_EXPECT_TRUE_MSG( sw::Memory::compare( bytesRed.data(), bytesBlue.data(), bytesRed.size() ) != 0,
                            "color 가 다른데 패킹된 바이트가 같다 — 프로퍼티가 원소에 반영되지 않는다" );
    }

    sw::Scene scene( "PerBatchMaterialScene" );
    SW_ASSERT_TRUE( scene.ensureDefaultCameras() );

    // 메시도 따로 만든다 — 배치 키에 메시가 들어가므로 배치가 갈린다.
    sw::shared_ptr<sw::Mesh> meshA = sw::Mesh::createUnitCube();
    sw::shared_ptr<sw::Mesh> meshB = sw::Mesh::createUnitCube();
    SW_ASSERT_TRUE( meshA != nullptr && meshB != nullptr );

    auto addObject = [&]( const utf8* pName, const sw::shared_ptr<sw::Mesh>& mesh, sw::Material* pMaterial, float32 offsetX )
    {
        sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( pName ) );
        SW_ASSERT_TRUE( pObj != nullptr );
        sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
        SW_ASSERT_TRUE( pMeshComp != nullptr );
        pMeshComp->setMesh( mesh );
        pMeshComp->setMaterial( pMaterial );
        pMeshComp->setLocalPosition( sw::float3{ offsetX, 0.0f, 0.0f } );
    };
    addObject( "CubeRed", meshA, materialRed.get(), -1.1f );
    addObject( "CubeBlue", meshB, materialBlue.get(), 1.1f );

    sw::GpuScene gpuScene;
    gpuScene.buildFromScene( &scene, sw::float3{ 0.0f, 1.2f, 3.2f }, nullptr );

    // 배치가 둘 생기고, 각 배치가 자기 머티리얼의 원소를 가리켜야 한다.
    const sw::vector<sw::GpuMeshBatch>& batches = gpuScene.getOpaqueBatches();
    SW_EXPECT_TRUE_MSG( batches.size() == 2, "메시가 둘이면 배치도 둘이어야 한다" );
    SW_ASSERT_TRUE( batches.size() == 2 );
    SW_EXPECT_TRUE_MSG( batches[0]._materialIndex != batches[1]._materialIndex,
                        "서로 다른 머티리얼인데 같은 원소를 가리킨다 — materialIndex 가 어긋난다" );

    // 그 인덱스가 실제로 그 배치의 머티리얼을 가리키는지 그룹에서 확인한다.
    for ( const sw::GpuMeshBatch& batch : batches )
    {
        SW_ASSERT_TRUE( batch._materialGroup < gpuScene.getMaterialGroups().size() );
        const sw::GpuMaterialGroup& group = gpuScene.getMaterialGroups()[batch._materialGroup];
        SW_ASSERT_TRUE( batch._materialIndex < group._listEntry.size() );
        SW_EXPECT_TRUE_MSG( group._listEntry[batch._materialIndex].first == batch._pMaterial,
                            "배치의 materialIndex 가 다른 머티리얼의 원소를 가리킨다" );
    }

    meshA->releaseGpu();
    meshB->releaseGpu();
}

/**
 * @brief [GpuSceneTest] 정적 스위치가 다르면 배치가 갈리고, 그 퍼뮤테이션이 배치에 실려 나가는지 (GPU 불필요).
 * @details 배치는 **PSO 하나로** 그린다. 그래서 배치를 묶는 키에 셰이더 퍼뮤테이션이 들어 있지 않으면,
 *          같은 .hlsl 을 쓰지만 정적 스위치가 다른 두 머티리얼이 한 배치로 접히고 한쪽 퍼뮤테이션이
 *          통째로 사라진다. 예전 키는 셰이더 **경로**뿐이라 정확히 그랬다 — 머티리얼이 선언한
 *          MATERIAL_BLEND_TRANSLUCENT 같은 것이 구워지기만 하고 한 번도 걸리지 않았다.
 *
 *          화면으로는 잡기 어렵다(퍼뮤테이션이 빠져도 그림은 그럴듯하게 나온다). 그래서 배치가 갈리는지와
 *          배치가 가리키는 퍼뮤테이션의 define 을 CPU 에서 직접 본다.
 */
SW_TEST_CASE( GpuSceneTest, PermutationSplitsBatchesAcrossMaterials )
{
    // 실제 에셋을 읽어 스위치만 바꾼다 — XML 을 손으로 지으면 _permutations 가 빠져 다른 걸 재게 된다.
    auto makeMaterial = []() -> sw::unique_ptr<sw::Material>
    {
        // 반환 대상을 하나로 둔다 — nullptr 과 material 을 섞어 돌려주면 NRVO 가 걸리지 않는다.
        sw::unique_ptr<sw::Material> material = sw::make_unique<sw::Material>();
        if ( material->loadFromFile( "engine/materials/defaultmaterial.material" ) == false )
            material.reset();
        return material;
    };

    sw::unique_ptr<sw::Material> materialPlain    = makeMaterial();
    sw::unique_ptr<sw::Material> materialSwitched = makeMaterial();
    SW_ASSERT_TRUE( materialPlain != nullptr && materialSwitched != nullptr );

    // 같은 셰이더, 같은 블렌드 모드. 다른 것은 정적 스위치 하나뿐이다.
    materialSwitched->setStaticSwitch( sw::hashed_string( "UseNormalMap" ), true );
    SW_EXPECT_TRUE_MSG( materialPlain->getShaderPath() == materialSwitched->getShaderPath(),
                        "이 테스트는 같은 셰이더를 쓰는 두 머티리얼을 전제로 한다" );
    SW_ASSERT_TRUE( materialPlain->getPermutationHash() != materialSwitched->getPermutationHash() );

    sw::Scene scene( "PermutationSplitScene" );
    SW_ASSERT_TRUE( scene.ensureDefaultCameras() );

    // 메시는 **하나를 공유한다**. 메시가 다르면 어차피 배치가 갈려서 퍼뮤테이션 때문에 갈린 것인지 알 수 없다.
    sw::shared_ptr<sw::Mesh> mesh = sw::Mesh::createUnitCube();
    SW_ASSERT_TRUE( mesh != nullptr );

    auto addObject = [&]( const utf8* pName, sw::Material* pMaterial, float32 offsetX )
    {
        sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( pName ) );
        SW_ASSERT_TRUE( pObj != nullptr );
        sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
        SW_ASSERT_TRUE( pMeshComp != nullptr );
        pMeshComp->setMesh( mesh );
        pMeshComp->setMaterial( pMaterial );
        pMeshComp->setLocalPosition( sw::float3{ offsetX, 0.0f, 0.0f } );
    };
    addObject( "CubePlain", materialPlain.get(), -1.1f );
    addObject( "CubeSwitched", materialSwitched.get(), 1.1f );

    sw::GpuScene gpuScene;
    // 머티리얼을 가로질러 합치는 모드 — 이 모드가 바로 퍼뮤테이션을 뭉개던 자리다.
    gpuScene.setMergeBatchesAcrossMaterials( true );
    gpuScene.buildFromScene( &scene, sw::float3{ 0.0f, 1.2f, 3.2f }, nullptr );

    const sw::vector<sw::GpuMeshBatch>& batches = gpuScene.getOpaqueBatches();
    SW_EXPECT_TRUE_MSG( batches.size() == 2,
                        "퍼뮤테이션이 다른 두 머티리얼이 한 배치로 접혔다 — 배치는 PSO 하나로 그리므로 한쪽이 버려진다" );
    SW_ASSERT_TRUE( batches.size() == 2 );

    SW_EXPECT_TRUE_MSG( batches[0]._shaderPermutation != batches[1]._shaderPermutation,
                        "배치는 갈렸는데 같은 퍼뮤테이션을 가리킨다" );

    // 배치가 가리키는 퍼뮤테이션이 실제로 그 머티리얼의 define 을 들고 있어야 한다.
    bool bFoundSwitched{ false };
    for ( const sw::GpuMeshBatch& batch : batches )
    {
        const sw::GpuShaderPermutation* pPermutation = gpuScene.findShaderPermutation( batch._shaderPermutation );
        SW_ASSERT_TRUE( pPermutation != nullptr );
        SW_EXPECT_TRUE_MSG( pPermutation->_shaderPath.empty() == false, "퍼뮤테이션에 셰이더 경로가 없다" );

        bool bHasNormalMap{ false };
        for ( const sw::string& defineStr : pPermutation->_listDefine )
        {
            if ( defineStr == "MATERIAL_NORMALMAP" )
                bHasNormalMap = true;
        }
        if ( batch._pMaterial == materialSwitched.get() )
        {
            bFoundSwitched = true;
            SW_EXPECT_TRUE_MSG( bHasNormalMap, "스위치를 켠 머티리얼의 배치인데 그 키워드가 퍼뮤테이션에 없다" );
        }
        else
        {
            SW_EXPECT_TRUE_MSG( bHasNormalMap == false, "스위치를 안 켠 머티리얼의 배치에 남의 키워드가 들어 있다" );
        }
    }
    SW_EXPECT_TRUE_MSG( bFoundSwitched, "스위치를 켠 머티리얼의 배치를 찾지 못했다" );

    mesh->releaseGpu();
}

/**
 * @brief [GpuSceneTest] 절두체 평면을 viewProj 에서 제대로 뽑는지 (GPU 불필요).
 * @details 이 계산이 **없어서** GPU 컬링이 켜 놓고도 한 번도 아무것도 거르지 않았다. 상수버퍼의 평면
 *          배열이 0 인 채로 나갔고, 그러면 셰이더의 `dot( 0, center ) + 0 < -radius` 가 항상 거짓이라
 *          모든 인스턴스가 통과한다. 화면은 멀쩡해 보이므로 픽셀로는 잡히지 않는다 — 컬링이 안 될 뿐
 *          그림은 맞기 때문이다. 그래서 평면 자체를 CPU 에서 본다.
 *
 *          셰이더와 같은 판정식(`dot( plane.xyz, center ) + plane.w < -radius` 면 바깥)을 그대로 쓴다.
 */
SW_TEST_CASE( GpuSceneTest, FrustumPlanesFromViewProj )
{
    // 원점을 바라보는 카메라 — 엔진 기본 카메라와 같은 자리에 둔다.
    const sw::float3   eye{ 0.0f, 0.0f, 5.0f };
    const sw::float4x4 view     = sw::float4x4::createLookAt( eye, sw::float3::Zero, sw::float3::Up );
    const sw::float4x4 proj     = sw::float4x4::createPerspectiveFieldOfView( 0.8f, 1.0f, 0.5f, 100.0f );
    const sw::float4x4 viewProj = view * proj;

    // **실제 코드가 쓰는 경로**를 그대로 검증한다 — 뷰가 행렬과 절두체를 함께 갱신한다.
    sw::RenderView renderView{};
    renderView.setViewProjection( viewProj );
    const float32( &arrPlane )[6][4] = renderView._arrFrustumPlane;

    // 평면은 정규화돼 있어야 한다 — 그래야 셰이더가 반지름을 그대로 비교할 수 있다.
    for ( uint32 planeIndex = 0; planeIndex < 6; ++planeIndex )
    {
        const float32 length = sw::MathUtil::sqrt( arrPlane[planeIndex][0] * arrPlane[planeIndex][0] +
                                                   arrPlane[planeIndex][1] * arrPlane[planeIndex][1] +
                                                   arrPlane[planeIndex][2] * arrPlane[planeIndex][2] );
        SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( length - 1.0f ) < 0.001f,
                            ( sw::string( "평면 " ) + sw::to_string( planeIndex ) + " 가 정규화되지 않았다 (길이 " +
                              sw::to_string( length ) + ")" )
                                .c_str() );
    }

    // 셰이더와 같은 판정 — 하나라도 -radius 보다 작으면 바깥이다.
    auto isVisible = [&arrPlane]( const sw::float3& center, float32 radius ) -> bool
    {
        for ( uint32 planeIndex = 0; planeIndex < 6; ++planeIndex )
        {
            const float32 distance = arrPlane[planeIndex][0] * center._x + arrPlane[planeIndex][1] * center._y +
                                     arrPlane[planeIndex][2] * center._z + arrPlane[planeIndex][3];
            if ( distance < -radius )
                return false;
        }
        return true;
    };

    SW_EXPECT_TRUE_MSG( isVisible( sw::float3{ 0.0f, 0.0f, 0.0f }, 0.5f ), "카메라가 보는 원점이 절두체 밖으로 판정됐다" );
    SW_EXPECT_TRUE_MSG( isVisible( eye + sw::float3{ 0.0f, 0.0f, -2.0f }, 0.5f ), "카메라 바로 앞이 절두체 밖으로 판정됐다" );

    // 여기부터가 핵심 — 평면이 0 이면 아래 넷이 전부 "보인다"로 나온다.
    SW_EXPECT_TRUE_MSG( isVisible( eye + sw::float3{ 0.0f, 0.0f, 20.0f }, 0.5f ) == false, "카메라 뒤가 걸러지지 않는다" );
    SW_EXPECT_TRUE_MSG( isVisible( sw::float3{ 0.0f, 0.0f, -500.0f }, 0.5f ) == false, "원평면 너머가 걸러지지 않는다" );
    SW_EXPECT_TRUE_MSG( isVisible( sw::float3{ 200.0f, 0.0f, 0.0f }, 0.5f ) == false, "화면 오른쪽 바깥이 걸러지지 않는다" );
    SW_EXPECT_TRUE_MSG( isVisible( sw::float3{ 0.0f, 200.0f, 0.0f }, 0.5f ) == false, "화면 위쪽 바깥이 걸러지지 않는다" );

    // 반지름이 크면 경계 밖이어도 걸리면 안 된다 (셰이더가 반지름을 그대로 쓰는지).
    SW_EXPECT_TRUE_MSG( isVisible( sw::float3{ 200.0f, 0.0f, 0.0f }, 400.0f ), "반지름이 큰 물체를 잘못 걸렀다" );
}

/**
 * @brief [RenderPassTest] 머티리얼의 퍼뮤테이션이 실제로 그 배치의 PSO 가 되는지 (4 백엔드).
 * @details 머티리얼은 자기 셰이더 변형을 선언한다(유리는 MATERIAL_BLEND_TRANSLUCENT 를 always-define 으로
 *          들고 있다). 그런데 드로우가 **패스 PSO 하나로** 전부 그리면 그 선언은 구워지기만 하고 한 번도
 *          걸리지 않는다. 예전엔 반투명 패스 PSO 에 그 define 을 직접 박아 두어 가려져 있었다 —
 *          "반투명 패스에 들어온 것은 무조건 반투명" 이었고, 머티리얼이 뭘 선언했는지는 상관이 없었다.
 *
 *          픽셀로는 잡기 어렵다. 알파 경로가 컴파일됐는지 여부는 겹치는 곳의 색만 바꾸는데, 그 색은
 *          조명·톤매핑을 타고 흔들린다. 그래서 **드로우가 실제로 고른 PSO 의 디스크립터**를 본다.
 */
SW_TEST_CASE( RenderPassGpuTest, MaterialPermutationDrivesBatchPso )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    auto hasDefine = []( const sw::RHIPipelineStateDesc& desc, const utf8* pDefine ) -> bool
    {
        for ( const sw::string& defineStr : desc._listShaderDefine )
        {
            if ( defineStr == pDefine )
                return true;
        }
        return false;
    };

    uint32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        // 실제 에셋을 쓴다 — 손으로 지은 XML 은 _permutations 가 빠져 검증하려던 것과 다른 걸 재게 된다.
        sw::unique_ptr<sw::Material> materialGlass = sw::make_unique<sw::Material>();
        if ( bOk )
            bOk = materialGlass->loadFromFile( "engine/materials/glassmaterial.material" );

        sw::Scene scene( "MaterialPermutationPsoScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        sw::shared_ptr<sw::Mesh> mesh;
        if ( bOk )
        {
            mesh = sw::Mesh::createUnitCube();
            bOk  = mesh != nullptr;
        }
        if ( bOk )
        {
            sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( "GlassCube" ) );
            bOk                  = pObj != nullptr;
            if ( bOk )
            {
                sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
                bOk                          = pMeshComp != nullptr;
                if ( bOk )
                {
                    pMeshComp->setMesh( mesh );
                    pMeshComp->setMaterial( materialGlass.get() );
                }
            }
        }

        if ( bOk )
        {
            // 한 프레임을 돌려야 배치가 서고 ensureMaterialPsos 가 퍼뮤테이션 PSO 를 만든다.
            const sw::float4 clear{ 0.0f, 0.0f, 0.0f, 1.0f };
            device->beginFrame( clear );
            bOk = renderer.execute( device.get(), nullptr, &scene );
            device->endFrame( false, false );
            device->waitIdle();
        }

        const sw::string label = sw::string( device->getBackendName() );
        if ( bOk )
        {
            const sw::vector<sw::GpuMeshBatch>& batches = renderer.getGpuScene().getTransparentBatches();
            SW_EXPECT_TRUE_MSG( batches.empty() == false,
                                ( label + ": 반투명 머티리얼인데 반투명 배치가 없다" ).c_str() );

            const sw::RHIPipelineStateHandle passPso = renderer.getEnginePso( sw::RenderPassType::Transparent );
            if ( batches.empty() == false && passPso != 0 )
            {
                // 패스 PSO 자체에는 이제 그 define 이 없다 — 반투명 패스가 정하는 것은 블렌드·뎁스지 셰이더가 아니다.
                sw::RHIPipelineStateDesc passDesc{};
                if ( renderer.findPsoDesc( passPso, passDesc ) )
                {
                    SW_EXPECT_TRUE_MSG( hasDefine( passDesc, "MATERIAL_BLEND_TRANSLUCENT" ) == false,
                                        ( label + ": 반투명 패스 PSO 에 퍼뮤테이션이 박혀 있다 — 머티리얼이 뭘 선언하든 상관없어진다" )
                                            .c_str() );
                    SW_EXPECT_TRUE_MSG( passDesc._bEnableBlend != 0,
                                        ( label + ": 반투명 패스인데 블렌드가 꺼져 있다" ).c_str() );
                }

                // 배치가 고르는 PSO 는 패스 PSO 와 **달라야** 하고, 그 안에 머티리얼의 define 이 있어야 한다.
                const sw::RHIPipelineStateHandle batchPso = renderer.psoForBatch( passPso, batches[0] );
                SW_EXPECT_TRUE_MSG( batchPso != passPso,
                                    ( label + ": 유리 배치가 패스 PSO 를 그대로 쓴다 — 머티리얼 퍼뮤테이션이 안 걸렸다" ).c_str() );

                sw::RHIPipelineStateDesc batchDesc{};
                if ( renderer.findPsoDesc( batchPso, batchDesc ) )
                {
                    SW_EXPECT_TRUE_MSG( hasDefine( batchDesc, "MATERIAL_BLEND_TRANSLUCENT" ),
                                        ( label + ": 배치 PSO 에 MATERIAL_BLEND_TRANSLUCENT 가 없다 — 알파 경로가 컴파일되지 않는다" )
                                            .c_str() );
                    // 렌더 상태는 **패스가 정한다** — 머티리얼이 블렌드를 끄거나 켤 수는 없다.
                    SW_EXPECT_TRUE_MSG( batchDesc._bEnableBlend == passDesc._bEnableBlend,
                                        ( label + ": 퍼뮤테이션 변형이 패스의 블렌드 상태를 바꿨다" ).c_str() );
                }
                else
                {
                    SW_EXPECT_TRUE_MSG( false, ( label + ": 배치 PSO 의 디스크립터를 찾을 수 없다" ).c_str() );
                }

                // 그림자 패스는 자기 지오메트리 셰이더가 정본이다 — 머티리얼 셰이더로 갈아타면 안 된다.
                const sw::RHIPipelineStateHandle shadowPso = renderer.getEnginePso( sw::RenderPassType::Shadow );
                sw::RHIPipelineStateDesc         shadowDesc{};
                if ( shadowPso != 0 && renderer.findPsoDesc( shadowPso, shadowDesc ) )
                {
                    const sw::RHIPipelineStateHandle shadowBatchPso = renderer.psoForBatch( shadowPso, batches[0] );
                    sw::RHIPipelineStateDesc         shadowBatchDesc{};
                    if ( renderer.findPsoDesc( shadowBatchPso, shadowBatchDesc ) )
                    {
                        SW_EXPECT_TRUE_MSG( shadowBatchDesc._vertexShaderPath == shadowDesc._vertexShaderPath,
                                            ( label + ": 그림자 패스가 머티리얼 셰이더로 갈아탔다" ).c_str() );
                        // 컬러 출력이 없는 패스는 픽셀 스테이지가 없고, 머티리얼 변형도 그대로 물려받아야 한다.
                        // 여기에 PS 가 남으면 (shadowdepth · PSMain · 머티리얼 define) 조합을 베이커는 굽지 않으므로
                        // Shipping 이 매니페스트 미스를 낸다 — 실제로 났던 [Error] 다.
                        SW_EXPECT_TRUE_MSG( shadowDesc._numRenderTargets == 0 && shadowDesc._pixelShaderPath.empty(),
                                            ( label + ": 그림자 패스 PSO 에 픽셀 스테이지가 있다" ).c_str() );
                        SW_EXPECT_TRUE_MSG( shadowBatchDesc._pixelShaderPath.empty() && shadowBatchDesc._pixelEntryPoint.empty(),
                                            ( label + ": 그림자 패스의 머티리얼 변형에 픽셀 스테이지가 있다" ).c_str() );
                    }
                }
            }
        }
        else
        {
            SW_EXPECT_TRUE_MSG( false, ( label + ": 프레임 실행 실패" ).c_str() );
        }

        renderer.shutdown();
        if ( mesh != nullptr )
            mesh->releaseGpu();
    }

    // 형제 여덟(카메라 컬링·투명 정렬·뷰 모드 등)과 같은 규칙으로 빠진다. 예전엔 여기만 단언이라
    // **디스플레이가 없는 CI 러너에서 이 테스트 하나만 졌다** — X11 디스플레이가 없으면 창이 안 열려
    // 네 백엔드가 전부 초기화에 실패하고, 그건 결함이 아니라 그 환경에 GPU 가 없다는 뜻이다.
    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend available for material permutation PSO test" );
}

/**
 * @brief [RenderPassTest] 메인 패스가 **카메라 절두체**로 컬링되는지 — 라이트 절두체가 아니라 (4 백엔드).
 * @details 컬링은 뷰마다 돈다(메인 카메라 / 그림자 라이트). 그런데 상수버퍼를 **하나만** 두고 두 뷰가
 *          나눠 쓰면, 두 번째 업로드가 첫 번째 디스패치가 읽을 내용을 덮어쓴다 — CPU 는 디스패치 사이에
 *          쓰지만 GPU 는 제출 뒤에 읽기 때문이다. 실제로 그렇게 돼서 메인 뷰가 **그림자 라이트의 좁은
 *          직교 절두체**로 걸러졌고, 화면에서 격자의 절반이 사라졌다.
 *
 *          그래서 큐브를 **라이트 상자 밖, 카메라 시야 안**에 둔다. 라이트는 원점 근처 2.2 폭 상자만
 *          비추므로 x = ±2.5 는 확실히 밖이고, 카메라는 z 를 뒤로 물리면 그만큼 넓게 본다.
 *          뷰를 잘못 쓰면 이 큐브들이 통째로 사라진다.
 */
SW_TEST_CASE( RenderPassGpuTest, MainPassCullsWithCameraFrustumNotLight )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    // 라이트 직교 상자는 원점 중심 폭 2.22 — 반폭 1.11 에 바운드 반지름 0.87 을 더해도 2.5 는 밖이다.
    constexpr float32 kSideX = 2.5f;
    // 카메라(0, 1.2, 3.2)에서 뒤로 물려 시야 폭을 넓힌다 — 그래야 ±2.5 가 화면 안에 들어온다.
    constexpr float32 kDepthZ = -6.0f;

    uint32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "CameraFrustumCullScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        sw::shared_ptr<sw::Mesh> sharedMesh;
        if ( bOk )
        {
            sharedMesh = sw::Mesh::createUnitCube();
            bOk        = sharedMesh != nullptr;
        }
        if ( bOk )
        {
            const float32 arrX[] = { -kSideX, kSideX };
            for ( uint32 sideIndex = 0; sideIndex < 2 && bOk; ++sideIndex )
            {
                sw::string      name = sw::string( "FarCube" ) + sw::to_string( sideIndex );
                sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( name.c_str(), static_cast<uint32>( name.size() ) ) );
                bOk                  = pObj != nullptr;
                if ( bOk == false )
                    break;
                sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
                bOk                          = pMeshComp != nullptr;
                if ( bOk == false )
                    break;
                pMeshComp->setMesh( sharedMesh );
                pMeshComp->setLocalPosition( sw::float3{ arrX[sideIndex], 0.0f, kDepthZ } );
            }
        }

        if ( bOk )
        {
            const sw::float4 clear{ 0.0f, 0.0f, 0.0f, 1.0f };
            device->beginFrame( clear );
            bOk = renderer.execute( device.get(), nullptr, &scene );
            device->endFrame( false, false );
            device->waitIdle();
        }

        sw::vector<uint8>     bytes;
        sw::RHITextureMipSpan layout{};
        sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
        if ( bOk && renderer.readbackTransient( "SceneColor", bytes, layout, format ) )
        {
            const bool   bBgra   = format == sw::RHIFormat::B8G8R8A8_UNORM;
            const uint8* pCorner = bytes.data();
            const int32  bgR     = bBgra ? pCorner[2] : pCorner[0];
            const int32  bgG     = pCorner[1];
            const int32  bgB     = bBgra ? pCorner[0] : pCorner[2];

            uint32 arrDrawn[2]{};
            for ( uint32 y = 0; y < layout._height; ++y )
            {
                const uint8* pRow = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes;
                for ( uint32 x = 0; x < layout._width; ++x )
                {
                    const uint8* pPixel = pRow + static_cast<size_t>( x ) * 4;
                    const int32  r      = bBgra ? pPixel[2] : pPixel[0];
                    const int32  g      = pPixel[1];
                    const int32  b      = bBgra ? pPixel[0] : pPixel[2];
                    if ( sw::MathUtil::abs( r - bgR ) + sw::MathUtil::abs( g - bgG ) + sw::MathUtil::abs( b - bgB ) < 24 )
                        continue;
                    ++arrDrawn[( x < layout._width / 2 ) ? 0u : 1u];
                }
            }

            const sw::string label    = sw::string( device->getBackendName() );
            const uint32     minDrawn = ( layout._width * layout._height ) / 3000;
            SW_EXPECT_TRUE_MSG( arrDrawn[0] > minDrawn && arrDrawn[1] > minDrawn,
                                ( label + ": 라이트 상자 밖의 큐브가 사라졌다 (좌 " + sw::to_string( arrDrawn[0] ) + ", 우 " +
                                  sw::to_string( arrDrawn[1] ) + ", 최소 " + sw::to_string( minDrawn ) +
                                  ") — 메인 패스가 카메라가 아니라 라이트 절두체로 걸러지고 있다" )
                                    .c_str() );
        }
        SW_EXPECT_TRUE_MSG( bOk, device->getBackendName() );

        if ( sharedMesh != nullptr )
            sharedMesh->releaseGpu();
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend available for camera frustum cull test" );
}

/**
 * @brief [RenderPassTest] 한 배치 안의 투명 인스턴스가 백엔드마다 같은 순서로 섞이는지 (4 백엔드).
 * @details 컬링이 압축을 하면 자리 번호가 원자 연산의 **완료 순서**로 정해진다. 투명은 그 순서가 곧
 *          블렌딩 순서라 그대로 두면 그림이 틀린다. 그래서 컬링 뒤에 instancesort 가 깊이순으로 되돌린다.
 *
 *          DX11 은 간접 인자 제약으로 컬링을 아예 돌리지 않아 **CPU 가 정렬한 순서 그대로** 그린다.
 *          그래서 이 테스트에서 DX11 은 정답지 노릇을 한다 — 나머지 셋(압축 + GPU 정렬)이 DX11 과 같은
 *          그림을 내면 정렬이 순서를 제대로 되돌린 것이다.
 *
 *          씬은 **완전히 정적**이어야 한다(회전 시드 없음, 시간에 의존하는 것 없음). 안 그러면 백엔드마다
 *          측정 시각이 달라 비교 자체가 성립하지 않는다.
 */
SW_TEST_CASE( RenderPassGpuTest, TransparentOrderMatchesAcrossBackends )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    bool    bHasReference{ false };
    float32 referenceMean[3]{};
    uint32  referenceDrawn{ 0 };
    uint32  attemptedCount{ 0 };

    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "TransparentOrderScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        // 메시와 머티리얼 인스턴스를 **공유**한다 — 그래야 한 배치에 투명 인스턴스가 여럿 들어가고,
        // 배치 안의 정렬이 실제로 검사된다. 따로 주면 배치가 하나씩 갈려 검사할 순서가 없다.
        // 로드하지 않은 씬은 기본 머티리얼이 없다(getMaterial 이 null) — 에셋을 직접 읽고 알파만 낮춘다.
        sw::shared_ptr<sw::Mesh>     sharedMesh;
        sw::unique_ptr<sw::Material> glassMaterial;
        if ( bOk )
        {
            sharedMesh    = sw::Mesh::createUnitCube();
            glassMaterial = sw::make_unique<sw::Material>();
            // 반투명 전용 에셋 — blendMode 와 퍼뮤테이션이 불투명과 다르다. 예전처럼 불투명 에셋에
            // 알파만 낮춰 쓰면 "머티리얼은 불투명인데 블렌딩으로 그린다"는 어긋난 상태를 검증하게 된다.
            bOk = sharedMesh != nullptr && glassMaterial->loadFromFile( "engine/materials/glassmaterial.material" );
        }

        if ( bOk )
        {
            // 카메라(0, 1.2, 3.2)에서 원점을 본다. 깊이를 어긋나게 겹쳐 놓아 순서가 그림을 바꾸게 한다.
            constexpr uint32 kCubeCount = 6;
            for ( uint32 cubeIndex = 0; cubeIndex < kCubeCount && bOk; ++cubeIndex )
            {
                sw::string      name = sw::string( "Glass" ) + sw::to_string( cubeIndex );
                sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( name.c_str(), static_cast<uint32>( name.size() ) ) );
                bOk                  = pObj != nullptr;
                if ( bOk == false )
                    break;
                sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
                bOk                          = pMeshComp != nullptr;
                if ( bOk == false )
                    break;
                pMeshComp->setMesh( sharedMesh );
                pMeshComp->setMaterial( glassMaterial.get() ); // 블렌드 모드는 이 머티리얼이 정한다
                const float32 offset = static_cast<float32>( cubeIndex ) * 0.30f;
                pMeshComp->setLocalPosition( sw::float3{ offset - 0.75f, 0.0f, offset - 0.75f } );
                // **큐브마다 다른 고정 회전**을 준다. 같은 색·같은 알파 레이어를 겹치면 블렌딩이 순서에
                // 무관해져(모든 src 가 같으면 결과가 교환법칙을 따른다) 정렬이 뒤집혀도 그림이 안 변한다.
                // 회전을 달리하면 보이는 면의 정점 색이 달라져 순서가 그림에 남는다. 시간이 아니라
                // 인덱스로 정하므로 백엔드·실행이 달라도 같다.
                const float32 yaw = static_cast<float32>( cubeIndex ) * 37.0f;
                pMeshComp->setLocalRotation( sw::float3{ 17.0f * static_cast<float32>( cubeIndex % 3 ), yaw, 0.0f } );
            }
        }

        if ( bOk )
        {
            const sw::float4 clear{ 0.0f, 0.0f, 0.0f, 1.0f };
            device->beginFrame( clear );
            bOk = renderer.execute( device.get(), nullptr, &scene );
            device->endFrame( false, false );
            device->waitIdle();
        }

        sw::vector<uint8>     bytes;
        sw::RHITextureMipSpan layout{};
        sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
        if ( bOk && renderer.readbackTransient( "SceneColor", bytes, layout, format ) )
        {
            const bool   bBgra   = format == sw::RHIFormat::B8G8R8A8_UNORM;
            const uint8* pCorner = bytes.data();
            const int32  bgR     = bBgra ? pCorner[2] : pCorner[0];
            const int32  bgG     = pCorner[1];
            const int32  bgB     = bBgra ? pCorner[0] : pCorner[2];

            uint64 arrSum[3]{};
            uint32 drawn{ 0 };
            for ( uint32 y = 0; y < layout._height; ++y )
            {
                const uint8* pRow = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes;
                for ( uint32 x = 0; x < layout._width; ++x )
                {
                    const uint8* pPixel = pRow + static_cast<size_t>( x ) * 4;
                    const int32  r      = bBgra ? pPixel[2] : pPixel[0];
                    const int32  g      = pPixel[1];
                    const int32  b      = bBgra ? pPixel[0] : pPixel[2];
                    if ( sw::MathUtil::abs( r - bgR ) + sw::MathUtil::abs( g - bgG ) + sw::MathUtil::abs( b - bgB ) < 24 )
                        continue;
                    arrSum[0] += static_cast<uint64>( r );
                    arrSum[1] += static_cast<uint64>( g );
                    arrSum[2] += static_cast<uint64>( b );
                    ++drawn;
                }
            }

            const sw::string label = sw::string( device->getBackendName() );
            SW_EXPECT_TRUE_MSG( drawn > ( layout._width * layout._height ) / 400,
                                ( label + ": 투명 큐브가 그려지지 않았다 (" + sw::to_string( drawn ) + " px)" ).c_str() );
            if ( drawn > 0 )
            {
                const float32 mean[3] = { static_cast<float32>( arrSum[0] ) / static_cast<float32>( drawn ),
                                          static_cast<float32>( arrSum[1] ) / static_cast<float32>( drawn ),
                                          static_cast<float32>( arrSum[2] ) / static_cast<float32>( drawn ) };
                if ( bHasReference == false )
                {
                    bHasReference    = true;
                    referenceMean[0] = mean[0];
                    referenceMean[1] = mean[1];
                    referenceMean[2] = mean[2];
                    referenceDrawn   = drawn;
                }
                else
                {
                    // 정적 씬이라 백엔드끼리 그림이 같아야 한다. 블렌딩 순서가 어긋나면 겹친 자리의
                    // 색이 달라져 평균이 움직인다.
                    for ( uint32 channel = 0; channel < 3; ++channel )
                    {
                        const float32 diff = sw::MathUtil::abs( mean[channel] - referenceMean[channel] );
                        SW_EXPECT_TRUE_MSG( diff < 6.0f,
                                            ( label + ": 투명 블렌딩 결과가 기준 백엔드와 다르다 (채널 " +
                                              sw::to_string( channel ) + ", " + sw::to_string( mean[channel] ) + " vs " +
                                              sw::to_string( referenceMean[channel] ) + ") — 배치 안 정렬 순서가 어긋난다" )
                                                .c_str() );
                    }
                    const int32 drawnDiff = static_cast<int32>( drawn ) - static_cast<int32>( referenceDrawn );
                    SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( drawnDiff ) < static_cast<int32>( referenceDrawn / 8 + 64 ),
                                        ( label + ": 그려진 픽셀 수가 기준과 크게 다르다 (" + sw::to_string( drawn ) + " vs " +
                                          sw::to_string( referenceDrawn ) + ")" )
                                            .c_str() );
                }
            }
        }
        SW_EXPECT_TRUE_MSG( bOk, device->getBackendName() );

        if ( sharedMesh != nullptr )
            sharedMesh->releaseGpu();
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend available for transparent order test" );
}

/**
 * @brief [RenderPassTest] 컴퓨트가 만든 드로우 커맨드가 **보이는 인스턴스만** 고르는지 (4 백엔드).
 * @details 컬링 컴퓨트는 배치의 개수를 줄이는 데서 끝나지 않고, 살아남은 인스턴스 번호를 압축 목록
 *          (g_SwVisibleInstanceIds)에 적는다. 정점 셰이더는 그 목록으로 자기 인스턴스를 찾는다 —
 *          언리얼 FInstanceCullingContext 와 같은 구조다.
 *
 *          개수만 줄이던 예전 방식은 "배치 앞쪽 N 개"를 그렸다. 한 배치 안에서 앞이 안 보이고 뒤가
 *          보이면 **보이는 쪽이 사라지고 안 보이는 쪽이 그려졌다**. 개수만으로는 무엇을 그릴지 고를 수가
 *          없기 때문이다.
 *
 *          그래서 **메시 하나를 여럿이 공유해 한 배치에 인스턴스를 여러 개** 만들고, 그중 절반을 카메라
 *          뒤로 보낸다. 화면에 남아야 할 둘이 좌우에 제대로 찍히는지 본다.
 */
SW_TEST_CASE( RenderPassGpuTest, GpuGeneratedCommandsDrawOnlyVisibleInstances )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    // 기본 카메라는 (0, 1.2, 3.2) 에서 원점을 본다 — -Z 를 보므로 월드 +X 는 화면 왼쪽이다.
    constexpr float32 kSideOffset = 1.2f;
    // 카메라 뒤(+Z 쪽 멀리)로 보내 절두체 밖에 둔다.
    constexpr float32 kBehindCameraZ = 40.0f;

    uint32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "GpuCullVisibleScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        // **메시 하나를 모두가 공유한다** — 배치 키에 메시가 들어가므로 배치가 하나로 묶이고, 그래야
        // 한 배치 안에서 일부만 컬링되는 상황이 만들어진다(배치마다 인스턴스가 하나면 검사할 게 없다).
        sw::shared_ptr<sw::Mesh> sharedMesh;
        if ( bOk )
        {
            sharedMesh = sw::Mesh::createUnitCube();
            bOk        = sharedMesh != nullptr;
        }

        if ( bOk )
        {
            // 앞의 넷은 카메라 뒤(안 보임), 뒤의 둘은 화면 좌우(보임).
            const sw::float3 arrPosition[] = {
                sw::float3{       -3.0f, 0.0f, kBehindCameraZ},
                sw::float3{       -1.0f, 0.0f, kBehindCameraZ},
                sw::float3{        1.0f, 0.0f, kBehindCameraZ},
                sw::float3{        3.0f, 0.0f, kBehindCameraZ},
                sw::float3{-kSideOffset, 0.0f,           0.0f},
                sw::float3{ kSideOffset, 0.0f,           0.0f},
            };
            constexpr uint32 kObjectCount = static_cast<uint32>( sizeof( arrPosition ) / sizeof( arrPosition[0] ) );
            for ( uint32 objectIndex = 0; objectIndex < kObjectCount && bOk; ++objectIndex )
            {
                sw::string      name = sw::string( "CullCube" ) + sw::to_string( objectIndex );
                sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( name.c_str(), static_cast<uint32>( name.size() ) ) );
                bOk                  = pObj != nullptr;
                if ( bOk )
                {
                    sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
                    bOk                          = pMeshComp != nullptr;
                    if ( bOk )
                    {
                        pMeshComp->setMesh( sharedMesh );
                        pMeshComp->setLocalPosition( arrPosition[objectIndex] );
                    }
                }
            }
        }

        if ( bOk )
        {
            const sw::float4 clear{ 0.0f, 0.0f, 0.0f, 1.0f };
            device->beginFrame( clear );
            bOk = renderer.execute( device.get(), nullptr, &scene );
            device->endFrame( false, false );
            device->waitIdle();
        }

        sw::vector<uint8>     bytes;
        sw::RHITextureMipSpan layout{};
        sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
        if ( bOk && renderer.readbackTransient( "SceneColor", bytes, layout, format ) )
        {
            const bool   bBgra   = format == sw::RHIFormat::B8G8R8A8_UNORM;
            const uint8* pCorner = bytes.data();
            const int32  bgR     = bBgra ? pCorner[2] : pCorner[0];
            const int32  bgG     = pCorner[1];
            const int32  bgB     = bBgra ? pCorner[0] : pCorner[2];

            uint32 arrDrawn[2]{};
            for ( uint32 y = 0; y < layout._height; ++y )
            {
                const uint8* pRow = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes;
                for ( uint32 x = 0; x < layout._width; ++x )
                {
                    const uint8* pPixel = pRow + static_cast<size_t>( x ) * 4;
                    const int32  r      = bBgra ? pPixel[2] : pPixel[0];
                    const int32  g      = pPixel[1];
                    const int32  b      = bBgra ? pPixel[0] : pPixel[2];
                    if ( sw::MathUtil::abs( r - bgR ) + sw::MathUtil::abs( g - bgG ) + sw::MathUtil::abs( b - bgB ) < 24 )
                        continue;
                    ++arrDrawn[( x < layout._width / 2 ) ? 0u : 1u];
                }
            }

            const sw::string label    = sw::string( device->getBackendName() );
            const uint32     minDrawn = ( layout._width * layout._height ) / 400;
            SW_EXPECT_TRUE_MSG( arrDrawn[0] > minDrawn && arrDrawn[1] > minDrawn,
                                ( label + ": 보이는 큐브 둘이 화면 좌우에 남지 않았다 (좌 " + sw::to_string( arrDrawn[0] ) +
                                  ", 우 " + sw::to_string( arrDrawn[1] ) + ", 최소 " + sw::to_string( minDrawn ) +
                                  ") — 컬링이 보이는 인스턴스를 고르지 못한다" )
                                    .c_str() );
        }
        SW_EXPECT_TRUE_MSG( bOk, device->getBackendName() );

        if ( sharedMesh != nullptr )
            sharedMesh->releaseGpu();
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend available for GPU-generated command test" );
}

/**
 * @brief [RenderPassTest] 배치마다 자기 머티리얼 **색**으로 그려지는지 (4 백엔드).
 * @details GpuSceneTest.PerBatchMaterialElementsAreDistinct 는 CPU 쪽 원소 선택까지만 본다. 여기서는 그
 *          원소가 실제로 셰이더까지 도달하는지를 픽셀로 본다 — 붉은 머티리얼과 푸른 머티리얼을 좌우에 두고
 *          그린다.
 *
 *          판정은 "붉은 픽셀 수" 가 아니라 **그려진 픽셀의 평균 (R - B)** 로 한다. 절대 색은 조명·톤매핑·
 *          백엔드 색공간에 따라 흔들리지만, 같은 조명을 받는 두 큐브 사이의 R-B 대소는 흔들리지 않는다.
 *          픽셀 수로 세었을 때는 백엔드마다 값이 널뛰어 판정이 되지 않았다.
 */
SW_TEST_CASE( RenderPassGpuTest, PerBatchMaterialColorsReachShader )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    // 기본 카메라는 -Z 를 본다 — 월드 +X 가 화면 **왼쪽**으로 간다. 그래서 붉은 큐브를 -X 에 두면
    // 화면 오른쪽이 붉어진다.
    constexpr float32 kSideOffset = 1.2f;

    uint32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "PerBatchMaterialColorScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        // 머티리얼은 **실제 에셋**을 읽어 색만 바꾼다 — 손으로 지은 XML 은 퍼뮤테이션 선언이 빠져 구워둔
        // 셰이더 변형과 맞지 않는다(그러면 드로우가 통째로 사라져 검증이 무의미해진다).
        // initialize 가 아니라 loadFromFile 을 쓴다 — initialize 는 텍스처 에셋 해석까지 하므로 에셋
        // 시스템이 없는 테스트 프로세스에서는 못 쓴다. GPUScene 은 머티리얼 상수버퍼가 아니라
        // getBuffer() 를 구조버퍼 원소로 패킹하므로 여기까지면 충분하다.
        auto makeMaterial = []( const utf8* pColor ) -> sw::unique_ptr<sw::Material>
        {
            // 반환 대상을 하나로 둔다 — nullptr 과 material 을 섞어 돌려주면 NRVO 가 걸리지 않는다.
            sw::unique_ptr<sw::Material> material = sw::make_unique<sw::Material>();
            if ( material->loadFromFile( "engine/materials/defaultmaterial.material" ) == false ||
                 material->setPropertyValue( nullptr, sw::hashed_string( "color" ), pColor ) == false )
                material.reset();
            return material;
        };

        sw::unique_ptr<sw::Material> materialRed;
        sw::unique_ptr<sw::Material> materialBlue;
        if ( bOk )
        {
            materialRed  = makeMaterial( "1.0 0.02 0.02 1.0" );
            materialBlue = makeMaterial( "0.02 0.02 1.0 1.0" );
            bOk          = materialRed != nullptr && materialBlue != nullptr;
            SW_EXPECT_TRUE_MSG( bOk, "테스트 머티리얼 준비" );
        }

        // 메시도 따로 만든다 — 배치 키에 메시가 들어가므로 배치가 갈려 드로우가 둘이 된다.
        sw::shared_ptr<sw::Mesh> meshRed;
        sw::shared_ptr<sw::Mesh> meshBlue;
        if ( bOk )
        {
            meshRed  = sw::Mesh::createUnitCube();
            meshBlue = sw::Mesh::createUnitCube();
            bOk      = meshRed != nullptr && meshBlue != nullptr;
        }
        if ( bOk )
        {
            const sw::shared_ptr<sw::Mesh> arrMesh[2]     = { meshRed, meshBlue };
            sw::Material*                  arrMaterial[2] = { materialRed.get(), materialBlue.get() };
            const float32                  arrOffsetX[2]  = { -kSideOffset, kSideOffset };
            const utf8*                    arrName[2]     = { "CubeRed", "CubeBlue" };
            for ( uint32 sideIndex = 0; sideIndex < 2 && bOk; ++sideIndex )
            {
                sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( arrName[sideIndex] ) );
                bOk                  = pObj != nullptr;
                if ( bOk )
                {
                    sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
                    bOk                          = pMeshComp != nullptr;
                    if ( bOk )
                    {
                        pMeshComp->setMesh( arrMesh[sideIndex] );
                        pMeshComp->setMaterial( arrMaterial[sideIndex] );
                        pMeshComp->setLocalPosition( sw::float3{ arrOffsetX[sideIndex], 0.0f, 0.0f } );
                    }
                }
            }
        }

        if ( bOk )
        {
            const sw::float4 clear{ 0.0f, 0.0f, 0.0f, 1.0f };
            device->beginFrame( clear );
            bOk = renderer.execute( device.get(), nullptr, &scene );
            device->endFrame( false, false );
            device->waitIdle();
        }

        sw::vector<uint8>     bytes;
        sw::RHITextureMipSpan layout{};
        sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
        if ( bOk && renderer.readbackTransient( "SceneColor", bytes, layout, format ) )
        {
            // 그려진 픽셀(검은 배경이 아닌 곳)만 모아 좌/우 절반의 평균 (R - B) 를 낸다.
            int64  arrSumDiff[2]{};
            uint32 arrDrawn[2]{};

            // 배경은 검지 않다 — 패스 리소스가 정한 클리어 색과 톤매핑이 섞여 회색빛이 깔린다. 그래서
            // "검지 않은 픽셀" 이 아니라 **모서리 픽셀과 확연히 다른 픽셀** 을 큐브로 본다.
            const bool   bBgra   = format == sw::RHIFormat::B8G8R8A8_UNORM;
            const uint8* pCorner = bytes.data();
            const int32  bgR     = bBgra ? pCorner[2] : pCorner[0];
            const int32  bgG     = pCorner[1];
            const int32  bgB     = bBgra ? pCorner[0] : pCorner[2];
            for ( uint32 y = 0; y < layout._height; ++y )
            {
                const uint8* pRow = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes;
                for ( uint32 x = 0; x < layout._width; ++x )
                {
                    const uint8* pPixel = pRow + static_cast<size_t>( x ) * 4;
                    const int32  r      = bBgra ? pPixel[2] : pPixel[0];
                    const int32  g      = pPixel[1];
                    const int32  b      = bBgra ? pPixel[0] : pPixel[2];
                    if ( sw::MathUtil::abs( r - bgR ) + sw::MathUtil::abs( g - bgG ) + sw::MathUtil::abs( b - bgB ) < 24 )
                        continue; // 배경
                    const uint32 side = ( x < layout._width / 2 ) ? 0u : 1u;
                    arrSumDiff[side] += ( r - b );
                    ++arrDrawn[side];
                }
            }

            const sw::string label    = sw::string( device->getBackendName() );
            const uint32     minDrawn = ( layout._width * layout._height ) / 400;
            const bool       bEnough  = arrDrawn[0] > minDrawn && arrDrawn[1] > minDrawn;
            SW_EXPECT_TRUE_MSG( bEnough, ( label + ": 큐브가 화면 양쪽에 그려지지 않았다 (좌 " + sw::to_string( arrDrawn[0] ) +
                                           ", 우 " + sw::to_string( arrDrawn[1] ) + ", 최소 " + sw::to_string( minDrawn ) + ")" )
                                             .c_str() );
            if ( bEnough )
            {
                const int64 leftDiff  = arrSumDiff[0] / static_cast<int64>( arrDrawn[0] );
                const int64 rightDiff = arrSumDiff[1] / static_cast<int64>( arrDrawn[1] );
                SW_EXPECT_TRUE_MSG( rightDiff > leftDiff + 16,
                                    ( label + ": 좌우가 같은 색으로 그려졌다 (좌 R-B " + sw::to_string( leftDiff ) +
                                      ", 우 R-B " + sw::to_string( rightDiff ) +
                                      ") — 배치가 자기 머티리얼 원소를 못 읽는다" )
                                        .c_str() );
            }
        }
        SW_EXPECT_TRUE_MSG( bOk, device->getBackendName() );

        if ( meshRed != nullptr )
            meshRed->releaseGpu();
        if ( meshBlue != nullptr )
            meshBlue->releaseGpu();
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend available for per-batch material color test" );
}

/**
 * @brief [RenderPassTest] 한 패스에 드로우가 둘일 때 배치마다 다른 상수가 유지되는지 (4 백엔드).
 * @details 배치 키에 메시 포인터가 들어가므로 **메시가 다르면 배치가 갈린다**. 그러면 한 패스가 드로우를
 *          두 번 하는데, 패스 상수버퍼는 `acquirePassCb` 가 패스당 하나만 잡고 `bindGraphics` 는 드로우마다
 *          거기에 덮어쓴다. GPU 는 제출 뒤에 읽으므로 두 드로우가 **마지막 배치의 `g_InstanceBase`** 를 보게 되고,
 *          앞 배치의 메시가 뒤 배치의 인스턴스 자리에 그려진다(= 한쪽이 비어 보인다).
 *
 *          지금까지 이 경로가 한 번도 검증되지 않았다 — 벤치 씬도 패리티 테스트도 메시를 하나만 쓴다.
 *          그래서 같은 큐브를 **두 번 따로 만들어** 포인터를 다르게 하고(기하는 동일해 가시성 변수를 없앤다)
 *          좌우로 떨어뜨린 뒤, 화면 좌우 양쪽에 모두 그려졌는지 본다.
 */
SW_TEST_CASE( RenderPassGpuTest, MultiBatchPassKeepsPerBatchConstants )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    /// @brief 두 큐브를 카메라가 보는 원점에서 좌우로 이만큼 떼어 놓는다.
    constexpr float32 kSideOffset = 1.1f;

    uint32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "MultiBatchScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        // 같은 기하지만 **다른 Mesh 객체** 두 개 — 배치 키가 갈려 한 패스에서 드로우가 둘이 된다.
        sw::shared_ptr<sw::Mesh> meshLeft;
        sw::shared_ptr<sw::Mesh> meshRight;
        if ( bOk )
        {
            meshLeft  = sw::Mesh::createUnitCube();
            meshRight = sw::Mesh::createUnitCube();
            bOk       = meshLeft != nullptr && meshRight != nullptr && meshLeft != meshRight;
        }
        if ( bOk )
        {
            const sw::shared_ptr<sw::Mesh> arrMesh[2]   = { meshLeft, meshRight };
            const float32                  arrOffset[2] = { -kSideOffset, kSideOffset };
            for ( uint32 sideIndex = 0; sideIndex < 2 && bOk; ++sideIndex )
            {
                sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( sideIndex == 0 ? "CubeLeft" : "CubeRight" ) );
                bOk                  = pObj != nullptr;
                if ( bOk )
                {
                    sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
                    bOk                          = pMeshComp != nullptr;
                    if ( bOk )
                    {
                        pMeshComp->setMesh( arrMesh[sideIndex] );
                        pMeshComp->setLocalPosition( sw::float3{ arrOffset[sideIndex], 0.0f, 0.0f } );
                    }
                }
            }
        }

        if ( bOk )
        {
            const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
            device->beginFrame( clear );
            bOk = renderer.execute( device.get(), nullptr, &scene );
            device->endFrame( false, false );
            device->waitIdle();
        }
        SW_EXPECT_TRUE_MSG( bOk, device->getBackendName() );

        if ( bOk )
        {
            sw::vector<uint8>     bytes;
            sw::RHITextureMipSpan layout{};
            sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
            if ( renderer.readbackTransient( "SceneColor", bytes, layout, format ) )
            {
                // 화면을 좌/우로 나눠 각각 그려진 픽셀을 센다. 한 배치가 다른 배치의 인스턴스를 읽으면
                // 두 큐브가 같은 자리에 겹쳐 그려져 한쪽이 비어 버린다.
                uint32 arrSideCount[2]{};
                for ( uint32 y = 0; y < layout._height; ++y )
                {
                    const uint8* pRow = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes;
                    for ( uint32 x = 0; x < layout._width; ++x )
                    {
                        const uint8* pPixel = pRow + static_cast<size_t>( x ) * 4;
                        const uint8  r      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[2] : pPixel[0];
                        const uint8  b      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[0] : pPixel[2];
                        if ( r > 40 || pPixel[1] > 48 || b > 56 || r < 22 || pPixel[1] < 28 || b < 36 )
                            ++arrSideCount[x < layout._width / 2 ? 0 : 1];
                    }
                }

                const sw::string label      = sw::string( device->getBackendName() );
                const uint32     minPerSide = ( layout._width * layout._height ) / 400;
                SW_EXPECT_TRUE_MSG( arrSideCount[0] > minPerSide,
                                    ( label + ": 왼쪽 큐브가 없다 (left " + sw::to_string( arrSideCount[0] ) + ", right " +
                                      sw::to_string( arrSideCount[1] ) + ") — 배치마다 다른 상수가 유지되지 않는다" )
                                        .c_str() );
                SW_EXPECT_TRUE_MSG( arrSideCount[1] > minPerSide,
                                    ( label + ": 오른쪽 큐브가 없다 (left " + sw::to_string( arrSideCount[0] ) + ", right " +
                                      sw::to_string( arrSideCount[1] ) + ") — 배치마다 다른 상수가 유지되지 않는다" )
                                        .c_str() );
            }
        }

        if ( meshLeft != nullptr )
            meshLeft->releaseGpu();
        if ( meshRight != nullptr )
            meshRight->releaseGpu();
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend available for multi-batch pass test" );
}

/**
 * @brief 인스턴스 애니메이션 컴퓨트가 돈 뒤에도 인스턴스 버퍼를 정점 셰이더가 읽을 수 있어야 한다.
 * @details 이 프리패스는 인스턴스 버퍼를 **UAV 로 쓴 다음** 같은 프레임에 정점 셰이더가 SRV 로 읽는다.
 *          D3D11 은 같은 리소스를 출력과 입력에 동시에 걸 수 없어서, UAV 를 안 떼면 런타임이 SRV 를
 *          조용히 NULL 로 강제한다 — 경고만 나오고 화면에서는 전부 사라진다. 실제로 DX11 앱이 그랬다.
 *
 *          패리티 테스트가 이걸 못 잡았던 이유는 그 씬에 spinSeed 를 세운 인스턴스가 하나도 없어서
 *          디스패치 자체가 건너뛰어졌기 때문이다. 여기서는 **반드시 세운다**.
 *
 *          회전각은 시간에 따라 달라지므로 백엔드 사이 픽셀 수를 비교하지 않는다. 각 백엔드가
 *          "무언가를 그렸는지" 만 본다 — 이 버그의 증상이 정확히 "아무것도 안 그린다" 였다.
 */
SW_TEST_CASE( RenderPassGpuTest, InstanceAnimationKeepsInstancesReadable )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    uint32 attemptedCount{ 0 };

    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;

        ++attemptedCount;

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "InstanceAnimScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        constexpr uint32         kAnimMeshCount = 3;
        sw::shared_ptr<sw::Mesh> arrMesh[kAnimMeshCount];
        for ( uint32 meshIndex = 0; meshIndex < kAnimMeshCount && bOk; ++meshIndex )
        {
            arrMesh[meshIndex] = sw::Mesh::createUnitCube();
            bOk                = arrMesh[meshIndex] != nullptr;
            if ( bOk == false )
                break;

            sw::string      objectName = sw::string( "SpinCube" ) + sw::to_string( meshIndex );
            sw::GameObject* go         = scene.getObjectManager()->createGameObject( sw::hashed_string( objectName.c_str(), static_cast<uint32>( objectName.size() ) ) );
            bOk                        = go != nullptr;
            if ( bOk )
            {
                sw::MeshComponent* meshComp = go->addComponent<sw::MeshComponent>();
                bOk                         = meshComp != nullptr;
                if ( bOk )
                {
                    meshComp->setMesh( arrMesh[meshIndex] );
                    meshComp->setLocalPosition( sw::float3{ ( static_cast<float32>( meshIndex ) - 1.0f ) * 1.2f, 1.0f, 0.0f } );
                    // **이게 핵심이다.** 0 이 아니어야 dispatchInstanceAnimation 이 실제로 돈다.
                    meshComp->setGpuSpinSeed( meshIndex + 1u );
                }
            }
        }

        // 두 프레임 돌린다 — 첫 프레임에 UAV 로 쓰고 두 번째 프레임이 그걸 SRV 로 읽는 순서까지 태운다.
        for ( uint32 frame = 0; frame < 2 && bOk; ++frame )
        {
            const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
            device->beginFrame( clear );
            bOk = renderer.execute( device.get(), nullptr, &scene );
            device->endFrame( false, false );
            device->waitIdle();
        }

        if ( bOk )
        {
            sw::vector<uint8>     bytes;
            sw::RHITextureMipSpan layout{};
            sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
            const bool            bRead  = renderer.readbackTransient( "SceneColor", bytes, layout, format );
            SW_EXPECT_TRUE_MSG( bRead, "SceneColor readback" );
            if ( bRead )
            {
                const uint32 pixelCount = layout._width * layout._height;
                uint32       drawnCount{ 0 };
                for ( uint32 y = 0; y < layout._height; ++y )
                {
                    const uint8* pRow = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes;
                    for ( uint32 x = 0; x < layout._width; ++x )
                    {
                        const uint8* pPixel = pRow + static_cast<size_t>( x ) * 4;
                        const uint8  r      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[2] : pPixel[0];
                        const uint8  b      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[0] : pPixel[2];
                        if ( r > 40 || pPixel[1] > 48 || b > 56 || r < 22 || pPixel[1] < 28 || b < 36 )
                            ++drawnCount;
                    }
                }
                const sw::string label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( backend ) );
                SW_EXPECT_TRUE_MSG( pixelCount > 0 && drawnCount > pixelCount / 200,
                                    ( label + ": 인스턴스 애니메이션 뒤 SceneColor 가 비었다 (drawn " + sw::to_string( drawnCount ) + "/" +
                                      sw::to_string( pixelCount ) + ") — UAV 를 떼지 않아 정점 셰이더가 인스턴스를 못 읽는지 의심하라" )
                                        .c_str() );
            }
        }

        for ( sw::shared_ptr<sw::Mesh>& mesh : arrMesh )
        {
            if ( mesh != nullptr )
                mesh->releaseGpu();
        }
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();

        SW_EXPECT_TRUE_MSG( bOk, ( sw::string( "backend " ) + sw::to_string( static_cast<uint32>( backend ) ) + " execute" ).c_str() );
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend available for instance animation test" );
}

/**
 * @brief FrameRenderer 패리티 스모크 — DX11 / DX12 / Vulkan / OpenGL 각각 begin→execute→end(no present)
 * @details Present 없이 waitIdle까지. 가용 백엔드는 전부 성공해야 한다.
 */
SW_TEST_CASE( RenderPassGpuTest, FrameRendererParityAllBackends )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    /// @brief 큐브를 원점(카메라가 보는 지점)보다 이만큼 위에 둔다 — 그림을 세로로 비대칭하게 만들어 방향을 검사할 수 있게.
    constexpr float32 kParityCubeHeight = 1.0f;

    uint32  attemptedCount{ 0 };
    uint32  okCount{ 0 };
    bool    bHasReferenceMean{ false };
    float32 referenceMean[3]{};
    uint32  referenceDrawnCount{ 0 };

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

        // 메시를 **여러 개** 만든다 — 배치 키에 메시가 들어가므로 곧 배치 수이고, 배치가 하나뿐이면
        // 인스턴스 시작 오프셋이 늘 0 이라 인다이렉트 드로우의 백엔드 차이를 전혀 재지 못한다
        // (Vulkan 의 gl_InstanceIndex 가 firstInstance 를 포함하는 문제가 그래서 오래 숨어 있었다).
        constexpr uint32         kParityMeshCount = 3;
        sw::shared_ptr<sw::Mesh> arrMesh[kParityMeshCount];
        sw::shared_ptr<sw::Mesh> cube;
        for ( uint32 meshIndex = 0; meshIndex < kParityMeshCount && bOk; ++meshIndex )
        {
            arrMesh[meshIndex] = sw::Mesh::createUnitCube();
            bOk                = arrMesh[meshIndex] != nullptr;
            if ( bOk == false )
                break;

            sw::string      objectName = sw::string( "Cube" ) + sw::to_string( meshIndex );
            sw::GameObject* go         = scene.getObjectManager()->createGameObject( sw::hashed_string( objectName.c_str(), static_cast<uint32>( objectName.size() ) ) );
            bOk                        = go != nullptr;
            if ( bOk )
            {
                sw::MeshComponent* meshComp = go->addComponent<sw::MeshComponent>();
                bOk                         = meshComp != nullptr;
                if ( bOk )
                {
                    meshComp->setMesh( arrMesh[meshIndex] );
                    meshComp->setLocalPosition( sw::float3{ ( static_cast<float32>( meshIndex ) - 1.0f ) * 1.2f, kParityCubeHeight, 0.0f } );
                    // 큐브를 카메라가 보는 원점보다 **위로** 올린다. 원점에 두면 화면 정중앙이라 그림이 세로로 대칭이고,
                    // 그러면 상하 반전을 평균으로도 무게중심으로도 잡을 수 없다 — OpenGL 이 실제로 뒤집혀 있었는데
                    // 이 테스트가 통과하던 이유다(평균·픽셀 수만 봤다).
                }
            }
        }
        cube = arrMesh[0];

        if ( bOk )
        {
            const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
            device->beginFrame( clear );
            bOk = renderer.execute( device.get(), nullptr, &scene );
            device->endFrame( false, false );
            device->waitIdle();
        }

        // 실행 성공만으로는 부족하다 — 실제로 큐브가 찍혔는지, 백엔드끼리 같은 그림인지 SceneColor 픽셀로 본다.
        // (예전엔 여기가 비어 있어서 Vulkan 이 아무것도 안 그리고 GL 이 큐브를 한 자리에 겹쳐 그려도 통과했다.)
        if ( bOk )
        {
            sw::vector<uint8>     bytes;
            sw::RHITextureMipSpan layout{};
            sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
            const bool            bRead  = renderer.readbackTransient( "SceneColor", bytes, layout, format );
            SW_EXPECT_TRUE_MSG( bRead, "SceneColor readback" );
            if ( bRead )
            {
                const uint32 pixelCount = layout._width * layout._height;
                uint64       arrSum[3]{};
                uint32       drawnCount{ 0 };
                uint64       drawnSumY{ 0 };
                for ( uint32 y = 0; y < layout._height; ++y )
                {
                    const uint8* pRow = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes;
                    for ( uint32 x = 0; x < layout._width; ++x )
                    {
                        const uint8* pPixel = pRow + static_cast<size_t>( x ) * 4;
                        const uint8  r      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[2] : pPixel[0];
                        const uint8  b      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[0] : pPixel[2];
                        arrSum[0] += r;
                        arrSum[1] += pPixel[1];
                        arrSum[2] += b;
                        // 파이프라인 클리어 색(0.12, 0.15, 0.18 → 31, 38, 46) 이 아니면 무언가 그려진 픽셀이다.
                        if ( r > 40 || pPixel[1] > 48 || b > 56 || r < 22 || pPixel[1] < 28 || b < 36 )
                        {
                            ++drawnCount;
                            drawnSumY += y;
                        }
                    }
                }
                const sw::string label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( backend ) );
                SW_EXPECT_TRUE_MSG( pixelCount > 0 && drawnCount > pixelCount / 200,
                                    ( label + ": SceneColor 에 큐브가 없다 (drawn " + sw::to_string( drawnCount ) + "/" + sw::to_string( pixelCount ) + ")" ).c_str() );
                // **방향 검사** — 평균과 픽셀 수는 상하 반전에 무관하다. 큐브를 원점 위에 두었으므로 올바른 방향이면
                // 그려진 픽셀의 무게중심이 이미지 위쪽(행 번호가 작은 쪽)에 있어야 한다. 뒤집히면 아래쪽으로 간다.
                // OpenGL 이 glClipControl 없이 좌하단 원점으로 그리던 시절 이 단언이 잡는다.
                if ( drawnCount > 0 )
                {
                    const float32 centroidY = static_cast<float32>( drawnSumY ) / static_cast<float32>( drawnCount );
                    const float32 centerY   = static_cast<float32>( layout._height ) * 0.5f;
                    SW_EXPECT_TRUE_MSG( centroidY < centerY,
                                        ( label + ": 그림이 상하로 뒤집혔다 — 큐브 무게중심 y=" + sw::to_string( static_cast<int32>( centroidY ) ) +
                                          " 가 중앙 " + sw::to_string( static_cast<int32>( centerY ) ) + " 보다 아래다" )
                                            .c_str() );
                }

                float32 arrMean[3]{};
                for ( uint32 channel = 0; channel < 3; ++channel )
                    arrMean[channel] = pixelCount > 0 ? static_cast<float32>( arrSum[channel] ) / static_cast<float32>( pixelCount ) : 0.0f;
                if ( bHasReferenceMean == false )
                {
                    bHasReferenceMean   = true;
                    referenceDrawnCount = drawnCount;
                    for ( uint32 channel = 0; channel < 3; ++channel )
                        referenceMean[channel] = arrMean[channel];
                }
                else
                {
                    for ( uint32 channel = 0; channel < 3; ++channel )
                    {
                        const float32 diff = arrMean[channel] > referenceMean[channel] ? arrMean[channel] - referenceMean[channel] : referenceMean[channel] - arrMean[channel];
                        SW_EXPECT_TRUE_MSG( diff <= 3.0f, ( label + ": SceneColor 평균이 첫 백엔드와 다르다 (채널 " + sw::to_string( channel ) + ")" ).c_str() );
                    }

                    // **그려진 픽셀 수**도 맞춘다. 평균은 화면 전체로 나눈 값이라 큐브 몇 개가 겹쳐 사라져도
                    // 거의 안 움직인다 — 인다이렉트 드로우의 인스턴스 오프셋이 백엔드마다 다르게 먹던 버그가
                    // 그래서 이 테스트를 통과했다. 배치가 여럿일 때 한 백엔드만 큐브를 잃으면 여기서 걸린다.
                    const uint32 lowerBound = referenceDrawnCount - referenceDrawnCount / 8;
                    const uint32 upperBound = referenceDrawnCount + referenceDrawnCount / 8;
                    SW_EXPECT_TRUE_MSG( lowerBound <= drawnCount && drawnCount <= upperBound,
                                        ( label + ": 그려진 픽셀 수가 첫 백엔드와 다르다 (" + sw::to_string( drawnCount ) + " vs " +
                                          sw::to_string( referenceDrawnCount ) + ") — 배치별 인스턴스 오프셋을 의심하라" )
                                            .c_str() );
                }
            }
        }

        for ( sw::shared_ptr<sw::Mesh>& mesh : arrMesh )
        {
            if ( mesh != nullptr )
                mesh->releaseGpu();
        }
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
 * @brief [RenderPassTest] `PROPERTY( SkipIfEmpty )` 가 빈 확장 스테이지를 파일에서 빼는지 검증
 * @details 리플렉션 직렬화는 기본적으로 **모든** PROPERTY 를 쓴다 — 그래야 "파일에 없음" 과
 *          "명시적으로 비어 있음" 이 구분된다. 그 기본값을 유지한 채 선택적 필드만 간결하게
 *          만드는 방법이 `SkipIfEmpty` 다: 생략해도 좋다고 **스키마가 선언한** 필드만 빠지므로
 *          모호해지지 않는다. 아래는 그 선언이 실제로 파일에 반영되는지를 본다.
 */
SW_TEST_CASE( RenderPassTest, PipelineEmptyStagesSkipped )
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

    // 지정한 값은 남고, SkipIfEmpty 를 선언한 빈 필드는 빠져야 한다.
    SW_EXPECT_TRUE( xmlContent.find( "VSMain" ) != sw::string::npos );
    SW_EXPECT_TRUE( xmlContent.find( "PSMain" ) != sw::string::npos );
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

/**
 * @brief [RenderPassTest] 뷰 모드(Lit/Unlit/Wireframe)가 **PSO 를 실제로 가르는지** 검증.
 * @details 이 기능이 조용히 죽는 방식은 하나다 — 값은 바뀌는데 드로우가 고르는 PSO 는 그대로인 것.
 *          예전 툴바 콤보가 정확히 그 상태였다(값만 바뀌고 화면은 그대로). 그래서 여기서는 화면이
 *          아니라 **드로우가 고른 PSO 의 디스크립터**를 본다 — 픽셀 비교는 인스턴스 애니메이션이
 *          벽시계로 도는 탓에 프레임마다 달라져 판정 근거가 못 된다.
 *
 *          함께 보는 것: 그림자 패스는 뷰 모드를 **받지 않아야** 한다(와이어프레임 그림자를 구우면
 *          그림자가 선 몇 개로 남는다), 모드를 되돌리면 캐시에서 같은 PSO 가 다시 나와야 한다.
 */
SW_TEST_CASE( RenderPassGpuTest, ViewModeSelectsDistinctPipelineStates )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    auto hasDefine = []( const sw::RHIPipelineStateDesc& desc, const utf8* pDefine ) -> bool
    {
        for ( const sw::string& defineStr : desc._listShaderDefine )
        {
            if ( defineStr == pDefine )
                return true;
        }
        return false;
    };

    uint32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;

        const sw::string  label = sw::string( device->getBackendName() );
        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "ViewModeScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        sw::shared_ptr<sw::Mesh> mesh;
        if ( bOk )
        {
            mesh = sw::Mesh::createUnitCube();
            bOk  = mesh != nullptr;
        }
        if ( bOk )
        {
            sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( "ViewModeCube" ) );
            bOk                  = pObj != nullptr;
            if ( bOk )
            {
                sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
                bOk                          = pMeshComp != nullptr;
                if ( bOk )
                    pMeshComp->setMesh( mesh );
            }
        }

        // 모드를 바꾸고 한 프레임 돌리면 그 모드의 PSO 변형이 만들어진다. 그 뒤 드로우가 고를 PSO 를
        // 조회한다 — drawGpuBatches 가 배치마다 부르는 것과 같은 함수다.
        auto renderOneFrame = [&]() -> bool
        {
            const sw::float4 clear{ 0.0f, 0.0f, 0.0f, 1.0f };
            device->beginFrame( clear );
            const bool bFrameOk = renderer.execute( device.get(), nullptr, &scene );
            device->endFrame( false, false );
            device->waitIdle();
            return bFrameOk;
        };

        if ( bOk )
            bOk = renderOneFrame();

        if ( bOk )
        {
            const sw::vector<sw::GpuMeshBatch>& batches = renderer.getGpuScene().getOpaqueBatches();
            SW_EXPECT_TRUE_MSG( batches.empty() == false, ( label + ": 불투명 배치가 없다" ).c_str() );

            const sw::RHIPipelineStateHandle passPso   = renderer.getEnginePso( sw::RenderPassType::ForwardOpaque );
            const sw::RHIPipelineStateHandle shadowPso = renderer.getEnginePso( sw::RenderPassType::Shadow );
            if ( batches.empty() == false && passPso != 0 )
            {
                const sw::GpuMeshBatch& batch = batches[0];

                // ── Lit: 패스 PSO 그대로, Solid ──────────────────────────────
                const sw::RHIPipelineStateHandle litPso = renderer.psoForBatch( passPso, batch );
                sw::RHIPipelineStateDesc         litDesc{};
                SW_EXPECT_TRUE_MSG( renderer.findPsoDesc( litPso, litDesc ),
                                    ( label + ": Lit PSO 의 디스크립터를 찾을 수 없다" ).c_str() );
                SW_EXPECT_TRUE_MSG( litDesc._fillMode == sw::RHIFillMode::Solid,
                                    ( label + ": Lit 인데 채우기 모드가 Solid 가 아니다" ).c_str() );
                SW_EXPECT_TRUE_MSG( hasDefine( litDesc, "SW_VIEWMODE_UNLIT=1" ) == false,
                                    ( label + ": Lit 인데 Unlit define 이 들어 있다" ).c_str() );

                // ── Wireframe ────────────────────────────────────────────────
                renderer.setViewMode( sw::RenderViewMode::Wireframe );
                SW_EXPECT_TRUE_MSG( renderOneFrame(), ( label + ": 와이어프레임 프레임 실행 실패" ).c_str() );

                const sw::RHIPipelineStateHandle wirePso = renderer.psoForBatch( passPso, batch );
                SW_EXPECT_TRUE_MSG( wirePso != litPso,
                                    ( label + ": 와이어프레임인데 드로우가 Lit 과 같은 PSO 를 고른다 — 모드가 화면에 닿지 않는다" )
                                        .c_str() );
                sw::RHIPipelineStateDesc wireDesc{};
                if ( renderer.findPsoDesc( wirePso, wireDesc ) )
                {
                    SW_EXPECT_TRUE_MSG( wireDesc._fillMode == sw::RHIFillMode::Wireframe,
                                        ( label + ": 와이어프레임 PSO 의 채우기 모드가 Wireframe 이 아니다" ).c_str() );
                    SW_EXPECT_TRUE_MSG( wireDesc._cullMode == sw::RHICullMode::None,
                                        ( label + ": 와이어프레임인데 컬링이 남아 뒷면 선이 사라진다" ).c_str() );
                    // 렌더 상태 중 **패스가 정하는 것**은 그대로여야 한다.
                    SW_EXPECT_TRUE_MSG( wireDesc._bEnableDepthTest == litDesc._bEnableDepthTest,
                                        ( label + ": 뷰 모드 변형이 패스의 뎁스 테스트를 바꿨다" ).c_str() );
                }
                else
                {
                    SW_EXPECT_TRUE_MSG( false, ( label + ": 와이어프레임 PSO 의 디스크립터를 찾을 수 없다" ).c_str() );
                }

                // 그림자 패스는 뷰 모드를 받지 않는다.
                sw::RHIPipelineStateDesc shadowDesc{};
                if ( shadowPso != 0 && renderer.findPsoDesc( renderer.psoForBatch( shadowPso, batch ), shadowDesc ) )
                {
                    SW_EXPECT_TRUE_MSG( shadowDesc._fillMode == sw::RHIFillMode::Solid,
                                        ( label + ": 그림자 패스가 와이어프레임으로 구워진다" ).c_str() );
                }

                // ── Unlit ────────────────────────────────────────────────────
                renderer.setViewMode( sw::RenderViewMode::Unlit );
                SW_EXPECT_TRUE_MSG( renderOneFrame(), ( label + ": Unlit 프레임 실행 실패" ).c_str() );

                const sw::RHIPipelineStateHandle unlitPso = renderer.psoForBatch( passPso, batch );
                SW_EXPECT_TRUE_MSG( unlitPso != litPso && unlitPso != wirePso,
                                    ( label + ": Unlit 이 다른 모드와 같은 PSO 를 고른다" ).c_str() );
                sw::RHIPipelineStateDesc unlitDesc{};
                if ( renderer.findPsoDesc( unlitPso, unlitDesc ) )
                {
                    SW_EXPECT_TRUE_MSG( hasDefine( unlitDesc, "SW_VIEWMODE_UNLIT=1" ),
                                        ( label + ": Unlit PSO 에 define 이 없다 — 조명이 그대로 컴파일된다" ).c_str() );
                    SW_EXPECT_TRUE_MSG( unlitDesc._fillMode == sw::RHIFillMode::Solid,
                                        ( label + ": Unlit 인데 채우기 모드가 Solid 가 아니다" ).c_str() );
                }
                else
                {
                    SW_EXPECT_TRUE_MSG( false, ( label + ": Unlit PSO 의 디스크립터를 찾을 수 없다" ).c_str() );
                }

                // ── 되돌리기: 캐시에서 같은 PSO 가 나와야 한다 ────────────────
                renderer.setViewMode( sw::RenderViewMode::Lit );
                SW_EXPECT_TRUE_MSG( renderOneFrame(), ( label + ": Lit 복귀 프레임 실행 실패" ).c_str() );
                SW_EXPECT_TRUE_MSG( renderer.psoForBatch( passPso, batch ) == litPso,
                                    ( label + ": Lit 로 돌아왔는데 다른 PSO 가 나온다 — 캐시가 모드를 구분하지 못한다" ).c_str() );
            }
        }
        else
        {
            SW_EXPECT_TRUE_MSG( false, ( label + ": 뷰 모드 씬 준비 실패" ).c_str() );
        }

        if ( mesh != nullptr )
            mesh->releaseGpu();
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend for view mode test" );
}

/**
 * @brief [GpuSceneTest] CPU 스냅샷이 **퍼뮤테이션 표까지** 건너오는지 검증.
 * @details 배치의 `_shaderPermutation` 은 `GpuScene::getShaderPermutations()` 의 **인덱스**다. 표를
 *          함께 보내지 않으면 받는 쪽에서 `findShaderPermutation` 이 늘 nullptr 을 돌려주고, 배치는
 *          퍼뮤테이션이 없는 것처럼 보인다 — 실제로 그랬다. 그 결과 패킷 경로(= 실제 앱과 에디터가
 *          쓰는 경로)에서는 머티리얼 퍼뮤테이션이 **하나도** 걸리지 않았고, 유리 머티리얼의
 *          `MATERIAL_BLEND_TRANSLUCENT` 도 화면에 닿은 적이 없었다.
 *
 *          `MaterialPermutationDrivesBatchPso` 는 동기 `execute()` 경로만 태우므로 이 결함을 볼 수
 *          없었다 — 두 경로를 가르는 것이 이 테스트의 존재 이유다. GPU 가 필요 없다.
 */
SW_TEST_CASE( GpuSceneTest, CpuSnapshotCarriesShaderPermutations )
{
    sw::unique_ptr<sw::Material> materialGlass = sw::make_unique<sw::Material>();
    SW_ASSERT_TRUE( materialGlass->loadFromFile( "engine/materials/glassmaterial.material" ) );

    sw::Scene scene( "SnapshotPermutationScene" );
    SW_ASSERT_TRUE( scene.ensureDefaultCameras() );

    sw::shared_ptr<sw::Mesh> mesh = sw::Mesh::createUnitCube();
    SW_ASSERT_NOT_NULL( mesh.get() );

    sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( "GlassCube" ) );
    SW_ASSERT_NOT_NULL( pObj );
    sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMeshComp );
    pMeshComp->setMesh( mesh );
    pMeshComp->setMaterial( materialGlass.get() );

    // 게임 스레드 쪽 GpuScene — 여기가 퍼뮤테이션 표의 정본이다.
    sw::GpuScene gtScene;
    gtScene.buildFromScene( &scene, sw::float3{ 0.0f, 0.0f, 0.0f }, nullptr );

    const sw::vector<sw::GpuMeshBatch>& gtBatches = gtScene.getTransparentBatches();
    SW_ASSERT_TRUE( gtBatches.empty() == false ); // 반투명 머티리얼인데 반투명 배치가 없으면 전제가 깨진 것이다
    const uint32 permutationIndex = gtBatches[0]._shaderPermutation;
    SW_ASSERT_TRUE( permutationIndex != sw::GpuScene::kInvalidShaderPermutation ); // 배치에 퍼뮤테이션이 붙어야 한다

    const sw::GpuShaderPermutation* pGtPermutation = gtScene.findShaderPermutation( permutationIndex );
    SW_ASSERT_NOT_NULL( pGtPermutation );

    // 패킷을 거쳐 렌더 스레드 쪽 GpuScene 으로 옮긴다 (EngineLoop 가 매 프레임 하는 그대로).
    sw::GpuScene packetScene;
    gtScene.exportCpuSnapshot( packetScene );
    sw::GpuScene rtScene;
    rtScene.adoptCpuSnapshot( std::move( packetScene ) );

    const sw::vector<sw::GpuMeshBatch>& rtBatches = rtScene.getTransparentBatches();
    SW_EXPECT_TRUE_MSG( rtBatches.empty() == false, "스냅샷에 반투명 배치가 없다" );
    if ( rtBatches.empty() == false )
    {
        SW_EXPECT_TRUE_MSG( rtBatches[0]._shaderPermutation == permutationIndex,
                            "스냅샷의 배치가 다른 퍼뮤테이션 인덱스를 가리킨다" );

        const sw::GpuShaderPermutation* pRtPermutation = rtScene.findShaderPermutation( permutationIndex );
        // 여기가 결함의 자리다 — 표가 안 오면 받는 쪽에서 머티리얼 퍼뮤테이션이 통째로 사라진다.
        SW_EXPECT_NOT_NULL( pRtPermutation );
        if ( pRtPermutation != nullptr )
        {
            SW_EXPECT_TRUE_MSG( pRtPermutation->_hash == pGtPermutation->_hash,
                                "스냅샷의 퍼뮤테이션 해시가 원본과 다르다" );
            SW_EXPECT_TRUE_MSG( pRtPermutation->_listDefine.size() == pGtPermutation->_listDefine.size(),
                                "스냅샷의 퍼뮤테이션 define 개수가 원본과 다르다" );
        }
    }

    // 정본은 GT 에 남아 있어야 한다 — 빼앗아 가면 다음 프레임의 인덱스가 0 부터 다시 매겨진다.
    SW_EXPECT_NOT_NULL( gtScene.findShaderPermutation( permutationIndex ) );

    if ( mesh != nullptr )
        mesh->releaseGpu();
}
