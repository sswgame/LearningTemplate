#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/FrameArenaAllocator.h"
#include "Core/String/hashed_string.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/Frame/RenderFramePacket.h"
#include "Engine/Graphics/Renderer/Frame/TransientAttachmentPool.h"
#include "Engine/Graphics/Renderer/Graph/RenderGraph.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassAsset.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassTypeInfo.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineAssetCache.h"
#include "Engine/Graphics/Renderer/Scene/GpuScene.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Upload/GpuUploadQueue.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Window/IWindow.h"

#include "TestFramework/TestFramework.h"

// RenderPassTest — 파이프라인 XML · 렌더 그래프 위상 · 검증. GPU 디바이스를 만들지 않는다(nogpu).

SW_TEST_CASE( RenderPassTest, XmlSerializationRoundtrip )
{
    sw::RenderPassAsset passRes;
    sw::RenderPassDesc& desc = passRes.getDesc();
    desc._name               = "UnitTestRenderPass";

    sw::RenderPassAttachment colorAtt{};
    colorAtt._name       = "Color0";
    colorAtt._format     = "R8G8B8A8_UNORM";
    colorAtt._clearColor = sw::float4{ 0.5f, 0.2f, 0.8f, 1.0f };
    colorAtt._bClear     = true;
    desc._listAttachment.push_back( colorAtt );

    sw::string testPath = test::makeTempPath( "test_renderpass_roundtrip.xml" );
    SW_EXPECT_TRUE( passRes.saveToXmlFile( testPath ) );

    sw::RenderPassAsset loadedRes;
    SW_EXPECT_TRUE( loadedRes.loadFromXmlFile( testPath ) );
    SW_EXPECT_EQUAL( sw::string( "UnitTestRenderPass" ), loadedRes.getDesc()._name );
    SW_EXPECT_EQUAL( size_t( 1 ), loadedRes.getDesc()._listAttachment.size() );
    SW_EXPECT_EQUAL( sw::string( "Color0" ), loadedRes.getDesc()._listAttachment[0]._name );
}

/**
 * @brief [RenderPassTest] 단위 큐브 메시 컴포넌트
 */
SW_TEST_CASE( RenderPassTest, UnitCubeMeshComponent )
{
    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
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
    sw::RenderPipelineAsset pipeRes;
    sw::RenderPipelineDesc& desc = pipeRes.getDesc();
    desc._name                   = "UnitTestPipeline";
    desc._shadingModel           = "Forward";

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

    sw::string testPath = test::makeTempPath( "test_renderpipeline_roundtrip.xml" );
    SW_EXPECT_TRUE( pipeRes.saveToXmlFile( testPath ) );

    sw::RenderPipelineAsset loadedRes;
    SW_EXPECT_TRUE( loadedRes.loadFromXmlFile( testPath ) );
    SW_EXPECT_EQUAL( sw::string( "UnitTestPipeline" ), loadedRes.getDesc()._name );
    SW_EXPECT_EQUAL( sw::string( "Forward" ), loadedRes.getDesc()._shadingModel );
    SW_EXPECT_EQUAL( size_t( 1 ), loadedRes.getDesc()._listAttachment.size() );
    SW_EXPECT_EQUAL( size_t( 1 ), loadedRes.getGraphPass().size() );
    SW_EXPECT_EQUAL( sw::string( "Present" ), loadedRes.getGraphPass()[0]._name );
    SW_EXPECT_EQUAL( size_t( 1 ), loadedRes.getDesc()._listRenderPassRef.size() );
}

/**
 * @brief [RenderPassTest] 레거시 RenderPassDesc 루트는 거부
 */
SW_TEST_CASE( RenderPassTest, PipelineRejectsLegacyRenderPassDescRoot )
{
    SW_TEST_DEFENSIVE_SCOPE( "Testing legacy root XML rejection" );
    const sw::string testPath = test::makeTempPath( "test_legacy_pipeline.xml" );
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

    sw::RenderPipelineAsset loaded;
    SW_EXPECT_FALSE( loaded.loadFromXmlFile( testPath ) );
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

    // DepthPass/ShadowPass는 서로 입출력이 없어 같은 레벨(0)에 묶이고, 둘 다에 의존하는
    // ForwardPass는 다음 레벨(1)로 분리돼야 한다 — executeParallel이 이 구조로 안전하게
    // 병렬 기록할 수 있는지의 근거.
    const sw::vector<sw::vector<sw::hashed_string>>& levels = graph.getExecutionLevels();
    SW_ASSERT_EQUAL( size_t( 2 ), levels.size() );
    SW_EXPECT_EQUAL( size_t( 2 ), levels[0].size() );
    SW_ASSERT_EQUAL( size_t( 1 ), levels[1].size() );
    SW_EXPECT_TRUE( levels[1][0] == sw::hashed_string( "ForwardPass" ) );

    taskManager.shutdown();
}

/**
 * @brief [RenderPassTest] 완전 직렬 체인은 패스마다 자기 레벨을 받는다(현재 기본 파이프라인 형태).
 */
SW_TEST_CASE( RenderPassTest, RenderGraphLinearChainProducesSinglePassLevels )
{
    sw::RenderGraph graph;
    graph.addPass( sw::hashed_string( "Shadow" ), {}, { sw::hashed_string( "ShadowMap" ) } );
    graph.addPass( sw::hashed_string( "Forward" ), { sw::hashed_string( "ShadowMap" ) }, { sw::hashed_string( "SceneColor" ) } );
    graph.addPass( sw::hashed_string( "Present" ), { sw::hashed_string( "SceneColor" ) }, {} );

    SW_ASSERT_TRUE( graph.compile() );
    const sw::vector<sw::vector<sw::hashed_string>>& levels = graph.getExecutionLevels();
    SW_ASSERT_EQUAL( size_t( 3 ), levels.size() );
    for ( const sw::vector<sw::hashed_string>& level : levels )
        SW_EXPECT_EQUAL( size_t( 1 ), level.size() );
}

/**
 * @brief [RenderPassTest] 파이프라인 XML 의 포맷 표기가 리플렉션으로 해석되는가 — 안 되면 **왜 안 되는지까지** 적는다
 * @details `RHIFormat` 이 리플렉션으로 해석되지 않으면 첨부 포맷이 모두 "알 수 없는 포맷" 이 되고, 그러면 깊이 판정이
 *          무너져 `SceneDepth` 가 SourceColor 역할로 잡히며 입력 계약까지 연쇄로 틀어진다 — 아래 `ShippedPipelinesValidateClean` 이
 *          여러 건으로 진다. 툴체인(clang · libclang 판)에 따라서만 나타날 수 있어 로컬에서는 재현되지 않을 수 있다.
 *
 *          그래서 이 케이스는 **연쇄가 시작되는 한 지점만** 보고, 졌을 때 어디가 끊겼는지 메시지에 담는다:
 *          `typeFqn` 이 무엇으로 읽혔는지 · FQN 으로 찾히는지 · 짧은 이름으로 찾히는지 · 이름표가 몇 개인지 ·
 *          등록된 enum 이 모두 몇 개인지. 같은 경로를 쓰는 `RenderPassType` 이 대조군이다 — 그쪽이 되고 이쪽이
 *          안 되면 리플렉션 전체가 아니라 이 헤더 하나의 문제다.
 */
SW_TEST_CASE( RenderPassTest, PipelineFormatNamesResolveThroughReflection )
{
    const sw::TypeRegistry& registry = sw::engine::getTypeRegistry();

    uint32 registeredEnumCount = 0;
    registry.forEachEnum( [&registeredEnumCount]( const sw::EnumInfo& )
    { ++registeredEnumCount; } );

    const sw::hashed_string formatFqn = sw::typeFqn<sw::RHIFormat>();
    const sw::EnumInfo*     pByFqn    = registry.findEnum( formatFqn );
    const sw::EnumInfo*     pByLeaf   = registry.findEnum( sw::hashed_string( "RHIFormat" ) );
    const sw::EnumInfo*     pFound    = ( pByFqn != nullptr ) ? pByFqn : pByLeaf;

    const sw::string diagnosis =
        sw::string( "typeFqn='" ) + ( formatFqn.c_str() != nullptr ? formatFqn.c_str() : "(null)" ) +
        "' byFqn=" + ( pByFqn != nullptr ? "1" : "0" ) +
        " byLeaf=" + ( pByLeaf != nullptr ? "1" : "0" ) +
        " names=" + sw::to_string( pFound != nullptr ? static_cast<uint64>( pFound->_mapNameToValue.size() ) : uint64( 0 ) ) +
        " enums=" + sw::to_string( registeredEnumCount );

    sw::RHIFormat parsedFormat{};
    const bool    bFormatParsed = registry.enumFromString( sw::string_view( "R8G8B8A8_UNORM" ), parsedFormat );
    SW_EXPECT_TRUE_MSG( bFormatParsed,
                        ( sw::string( "RHIFormat 이 리플렉션으로 해석되지 않습니다 — " ) + diagnosis ).c_str() );
    if ( bFormatParsed )
        SW_EXPECT_TRUE( parsedFormat == sw::RHIFormat::R8G8B8A8_UNORM );

    sw::RenderPassType parsedPassType{};
    SW_EXPECT_TRUE_MSG( registry.enumFromString( sw::string_view( "Present" ), parsedPassType ),
                        ( sw::string( "대조군 RenderPassType 도 해석되지 않습니다 — " ) + diagnosis ).c_str() );
}

/**
 * @brief 엔진이 실제로 배포하는 파이프라인 XML 들이 스스로 모순이 없는지.
 * @details 모두 검증 0건이어야 한다. 여기가 깨지면 런타임에 포맷이 어긋나 조용히 잘못 그리거나 GPU 가 죽는다.
 *          패스 수는 GPU 타임스탬프가 재는 수(`FrameRendererUtil::kGpuTimedPassCapacity`) 이하여야 한다 — 넘는 패스는 프로파일에서 빠진다.
 */
SW_TEST_CASE( RenderPassTest, ShippedPipelinesValidateClean )
{
    const std::string_view arrPipeline[] = {
        "engine/pipeline/forwardpipeline.xml",
        "engine/pipeline/deferredpipeline.xml",
        "engine/pipeline/forwardprepasspipeline.xml",
        "engine/pipeline/forwardpipelinestaged.xml",
        "engine/pipeline/forwardtoonpipeline.xml",
    };
    for ( std::string_view path : arrPipeline )
    {
        sw::RenderPipelineAsset res;
        SW_ASSERT_TRUE( res.loadFromXmlFile( path ) );
        SW_EXPECT_EQUAL( 0u, res.validate( path ) );
        // 모든 패스 타입이 해석돼야 한다 — Invalid 가 남아 있으면 PSO 가 기본 포맷으로 만들어진다.
        for ( const sw::RenderGraphPassDesc& pass : res.getGraphPass() )
            SW_EXPECT_TRUE( sw::isPipelinePassType( pass._resolvedType ) );
        SW_EXPECT_EQUAL( 0u, sw::FrameRendererUtil::countUntimedGpuPass( res.getGraphPass().size() ) );
    }
}

/**
 * @brief [RenderPassTest] GPU 타임스탬프가 재는 패스 수와 칸 배치가 맞다 — 넘는 패스 수를 세는 함수가 로드 경고의 근거다
 * @details 패스는 인덱스 x 2 쌍을 쓰고 뒤쪽 칸은 프레임 · 컴퓨트 예약이다. 실행 · 보고 · 로드 경고가 모두 `kGpuTimedPassCapacity` 하나로 자른다.
 */
SW_TEST_CASE( RenderPassTest, GpuTimestampPassCapacityMatchesSlotLayout )
{
    constexpr uint32 kCapacity = sw::FrameRendererUtil::kGpuTimedPassCapacity;
    static_assert( kCapacity * 2u <= sw::FrameRendererUtil::kGpuTimestampSlotComputeBegin, "패스 칸이 예약 칸과 겹친다" );
    SW_EXPECT_EQUAL( 14u, kCapacity );
    SW_EXPECT_EQUAL( 0u, sw::FrameRendererUtil::countUntimedGpuPass( 0 ) );
    SW_EXPECT_EQUAL( 0u, sw::FrameRendererUtil::countUntimedGpuPass( kCapacity ) );
    SW_EXPECT_EQUAL( 1u, sw::FrameRendererUtil::countUntimedGpuPass( kCapacity + 1u ) );
    SW_EXPECT_EQUAL( 6u, sw::FrameRendererUtil::countUntimedGpuPass( 20 ) );
}

/**
 * @brief [RenderPassTest] 패스 종류 표(RenderPassTypeInfo)는 RenderPassType 열거자마다 한 줄이고, 줄 순서가 값 순서다
 * @details 표의 크기(`kRenderPassTypeCount`)는 마지막 열거자로 정한다. 그 뒤에 열거자를 더하고 상수를 안 고치면 새 타입이
 *          Invalid 줄로 읽혀 PSO 도 실행도 조용히 빠진다 — 리플렉션이 아는 열거자 수와 대조해 막는다.
 */
SW_TEST_CASE( RenderPassTest, TypeInfoTableCoversEveryEnumValue )
{
    const sw::TypeRegistry& registry = sw::engine::getTypeRegistry();
    const sw::EnumInfo*     pByFqn   = registry.findEnum( sw::typeFqn<sw::RenderPassType>() );
    const sw::EnumInfo*     pEnum    = ( pByFqn != nullptr ) ? pByFqn : registry.findEnum( sw::hashed_string( "RenderPassType" ) );
    SW_ASSERT_TRUE( pEnum != nullptr );

    SW_EXPECT_EQUAL( static_cast<size_t>( sw::kRenderPassTypeCount ), pEnum->_mapValueToName.size() );
    for ( const auto& [value, name] : pEnum->_mapValueToName )
    {
        SW_EXPECT_TRUE_MSG( 0 <= value && value < static_cast<int64>( sw::kRenderPassTypeCount ), name.c_str() );
        const sw::RenderPassType type = static_cast<sw::RenderPassType>( value );
        SW_EXPECT_TRUE_MSG( sw::getRenderPassTypeInfo( type )._type == type, name.c_str() );
    }

    for ( uint32 typeIndex = 1; typeIndex < sw::kRenderPassTypeCount; ++typeIndex )
    {
        const sw::RenderPassType      type  = static_cast<sw::RenderPassType>( typeIndex );
        const sw::RenderPassTypeInfo& info  = sw::getRenderPassTypeInfo( type );
        const sw::string              label = sw::to_string( typeIndex );
        SW_EXPECT_TRUE_MSG( info._pDefaultShader != nullptr, ( "기본 셰이더가 없는 패스 타입 " + label ).c_str() );
        // 파이프라인 패스의 입력은 지오메트리 드로우가 정하거나(메시 패스) 입력 계약이 정한다 — 둘 중 정확히 하나다.
        if ( sw::isPipelinePassType( type ) )
        {
            const bool bDrawsSceneMeshes = info.hasFlag( sw::RenderPassTraitFlag::kDrawsSceneMeshes );
            const bool bHasInputContract = info.hasFlag( sw::RenderPassTraitFlag::kHasInputContract );
            SW_EXPECT_TRUE_MSG( bDrawsSceneMeshes != bHasInputContract, ( "입력을 정하는 쪽이 하나가 아닌 패스 타입 " + label ).c_str() );
        }
    }
}

/**
 * @brief 파이프라인 검증이 실제로 문제를 잡는지 — 잡지 못하는 검증은 없느니만 못하다.
 */
SW_TEST_CASE( RenderPassTest, PipelineValidationCatchesInconsistencies )
{
    // 0) 같은 이름의 패스 두 번 — 이름은 패스의 열쇠라 뒤의 것이 조용히 버려진다(그래프는 하나만 받는다)
    {
        sw::RenderPipelineAsset res;
        sw::RenderPipelineDesc& desc = res.getDesc();
        sw::RenderGraphPassDesc pass{};
        pass._name = "Twice";
        pass._type = "Present";
        pass._listOutput.push_back( "Swapchain" );
        desc._listPass.push_back( pass );
        desc._listPass.push_back( pass );
        SW_EXPECT_TRUE( res.validate( "unit-test" ) > 0u );
    }

    // 1) 알 수 없는 패스 타입
    {
        sw::RenderPipelineAsset res;
        sw::RenderPipelineDesc& desc = res.getDesc();
        sw::RenderGraphPassDesc pass{};
        pass._name = "Bad";
        pass._type = "NoSuchPassType";
        desc._listPass.push_back( pass );
        SW_EXPECT_TRUE( res.validate( "unit-test" ) > 0u );
        SW_EXPECT_TRUE( desc._listPass[0]._resolvedType == sw::RenderPassType::Invalid );
    }

    // 2) 선언되지 않은 첨부를 입출력으로 참조
    {
        sw::RenderPipelineAsset res;
        sw::RenderPipelineDesc& desc = res.getDesc();
        sw::RenderGraphPassDesc pass{};
        pass._name = "Dangling";
        pass._type = "ForwardOpaque";
        pass._listOutput.push_back( "NotDeclared" );
        desc._listPass.push_back( pass );
        SW_EXPECT_TRUE( res.validate( "unit-test" ) > 0u );
    }

    // 3) Swapchain 은 첨부로 선언하지 않는 예약어라 통과해야 한다
    {
        sw::RenderPipelineAsset res;
        sw::RenderPipelineDesc& desc = res.getDesc();
        sw::RenderGraphPassDesc pass{};
        pass._name = "Blit";
        pass._type = "Present";
        pass._listOutput.push_back( "Swapchain" );
        desc._listPass.push_back( pass );
        SW_EXPECT_EQUAL( 0u, res.validate( "unit-test" ) );
    }

    // 4) 알 수 없는 첨부 포맷
    {
        sw::RenderPipelineAsset  res;
        sw::RenderPipelineDesc&  desc = res.getDesc();
        sw::RenderPassAttachment att{};
        att._name   = "Weird";
        att._format = "R99G99_NOPE";
        desc._listAttachment.push_back( att );
        SW_EXPECT_TRUE( res.validate( "unit-test" ) > 0u );
    }
    // 4-2) 첨부 나눗수: 1 · 2 · 4 만 받고, 한 패스의 출력(컬러 · 깊이)은 같은 나눗수여야 한다.
    {
        auto makeAttachment = []( const utf8* pName, const utf8* pFormat, uint32 divisor )
        {
            sw::RenderPassAttachment att{};
            att._name              = pName;
            att._format            = pFormat;
            att._resolutionDivisor = divisor;
            return att;
        };
        for ( uint32 divisor : { 0u, 3u, 8u } )
        {
            sw::RenderPipelineAsset res;
            res.getDesc()._listAttachment.push_back( makeAttachment( "Odd", "R8G8B8A8_UNORM", divisor ) );
            SW_EXPECT_TRUE_MSG( res.validate( "unit-test" ) == 1u, sw::to_string( divisor ).c_str() );
        }
        auto makeHalfPass = [&]( sw::RenderPipelineAsset& res, uint32 depthDivisor )
        {
            res.getDesc()._listAttachment.push_back( makeAttachment( "HalfColor", "R8G8B8A8_UNORM", 2 ) );
            res.getDesc()._listAttachment.push_back( makeAttachment( "Depth", "D24_UNORM_S8_UINT", depthDivisor ) );
            sw::RenderGraphPassDesc pass{};
            pass._name            = "HalfOpaque";
            pass._type            = "ForwardOpaque";
            pass._depthAttachment = "Depth";
            pass._listOutput.push_back( "HalfColor" );
            pass._listOutput.push_back( "Depth" );
            res.getDesc()._listPass.push_back( pass );
        };
        {
            sw::RenderPipelineAsset res;
            makeHalfPass( res, 2 );
            SW_EXPECT_EQUAL( 0u, res.validate( "unit-test" ) );
        }
        {
            sw::RenderPipelineAsset res;
            makeHalfPass( res, 1 );
            SW_EXPECT_TRUE( res.validate( "unit-test" ) >= 1u );
        }
        // 크기는 올림이고 최소 1 이다 — 홀수 창에서 반해상도가 한 줄 모자라지 않는다.
        SW_EXPECT_EQUAL( 641u, sw::TransientAttachmentPool::computeScaledExtent( 1281u, 2u ) );
        SW_EXPECT_EQUAL( 320u, sw::TransientAttachmentPool::computeScaledExtent( 1280u, 4u ) );
        SW_EXPECT_EQUAL( 1u, sw::TransientAttachmentPool::computeScaledExtent( 1u, 4u ) );
        SW_EXPECT_EQUAL( 720u, sw::TransientAttachmentPool::computeScaledExtent( 720u, 1u ) );
    }
    // 4-1) 이름은 RHIFormat 이지만 첨부가 될 수 없는 포맷 — 각각 오류 하나. 렌더 가능한 포맷은 통과한다.
    for ( const utf8* pFormat : { "Unknown", "BC1_UNORM", "BC7_UNORM", "R32G32B32_FLOAT" } )
    {
        sw::RenderPipelineAsset  res;
        sw::RenderPassAttachment att{};
        att._name   = "NotRenderable";
        att._format = pFormat;
        res.getDesc()._listAttachment.push_back( att );
        SW_EXPECT_TRUE_MSG( res.validate( "unit-test" ) == 1u, pFormat );
    }
    for ( const utf8* pFormat : { "R8G8B8A8_UNORM", "R16G16B16A16_FLOAT", "R32_FLOAT", "R32G32_FLOAT", "D24_UNORM_S8_UINT" } )
    {
        sw::RenderPipelineAsset  res;
        sw::RenderPassAttachment att{};
        att._name   = "Renderable";
        att._format = pFormat;
        res.getDesc()._listAttachment.push_back( att );
        SW_EXPECT_TRUE_MSG( res.validate( "unit-test" ) == 0u, pFormat );
    }

    // 5) 이름은 정본 하나로 통일돼 있다 — 다른 표기(`Shading`, `PostBloom`)는 오류로 잡힌다.
    //    이름을 바꾸면 XML 을 새 이름으로 다시 쓴다(ValueAlias 는 실제 게임 데이터가 생긴 뒤의 창구다).
    {
        auto removedTypeIsRejected = []( const utf8* pRemovedType ) -> bool
        {
            sw::RenderPipelineAsset res;
            sw::RenderPipelineDesc& desc = res.getDesc();
            sw::RenderGraphPassDesc pass{};
            pass._name = "Removed";
            pass._type = pRemovedType;
            desc._listPass.push_back( pass );
            res.validate( "unit-test" );
            // 여기서 보는 것은 **이름 해석**뿐이다 — 입력 계약 위반(입력 없는 Tonemap)은 다른 케이스가 본다.
            return desc._listPass[0]._resolvedType == sw::RenderPassType::Invalid;
        };
        SW_EXPECT_TRUE( removedTypeIsRejected( "Shading" ) );
        SW_EXPECT_TRUE( removedTypeIsRejected( "PostBloom" ) );
        SW_EXPECT_TRUE( removedTypeIsRejected( "HBAO" ) );
        // MRT 없는 G버퍼의 단독 PSO 슬롯은 없다 — 네 백엔드가 모두 MRT 를 보장한다.
        SW_EXPECT_TRUE( removedTypeIsRejected( "GBufferAlbedo" ) );
        SW_EXPECT_TRUE( removedTypeIsRejected( "GBufferNormal" ) );
        // 철자 대소문자는 리플렉션이 무시하므로 "ToneMap" 은 "Tonemap" 으로 읽힌다 — 의도된 관용이다.
        SW_EXPECT_TRUE( removedTypeIsRejected( "ToneMap" ) == false );
    }

    // 6) 엔진 내부 PSO 슬롯은 XML 패스 타입으로 쓸 수 없다. executePass 에 실행 코드가 없다 — XML 이 받아 주면 그 패스는
    //    매 프레임 경고만 남기고 아무것도 그리지 않는다.
    for ( const utf8* pInternalType : { "GpuCull", "ForwardOpaqueNoDepthWrite" } )
    {
        sw::RenderPipelineAsset res;
        sw::RenderPipelineDesc& desc = res.getDesc();
        sw::RenderGraphPassDesc pass{};
        pass._name = "Internal";
        pass._type = pInternalType;
        desc._listPass.push_back( pass );
        SW_EXPECT_TRUE_MSG( res.validate( "unit-test" ) > 0u, pInternalType );
    }

    // 7) 풀스크린 패스의 입력은 그 타입의 계약과 맞아야 한다 — "선언만 있고 아무도 안 읽는 입력" 이 오류다.
    //    디퍼드 XML 이 Bloom 의 입력으로 AOColor 를 적어 두고도 Bloom 이 그것을 걸지 않는 것이 이 검사가 잡는 병이다.
    {
        auto makeDesc = []( sw::RenderPipelineAsset& res )
        {
            sw::RenderPipelineDesc& desc          = res.getDesc();
            auto                    addAttachment = [&desc]( const utf8* pName, const utf8* pFormat )
            {
                sw::RenderPassAttachment att{};
                att._name   = pName;
                att._format = pFormat;
                desc._listAttachment.push_back( att );
            };
            addAttachment( "LitColor", "R16G16B16A16_FLOAT" );
            addAttachment( "AOColor", "R8G8B8A8_UNORM" );
            addAttachment( "BloomColor", "R16G16B16A16_FLOAT" );
            addAttachment( "GBufferNormal", "R16G16B16A16_FLOAT" );
            addAttachment( "SceneDepth", "D24_UNORM_S8_UINT" );
        };
        auto addPass = []( sw::RenderPipelineAsset& res, const utf8* pType, std::initializer_list<const utf8*> listInput, const utf8* pOutput )
        {
            sw::RenderGraphPassDesc pass{};
            pass._name = pType;
            pass._type = pType;
            for ( const utf8* pInput : listInput )
                pass._listInput.push_back( pInput );
            pass._listOutput.push_back( pOutput );
            res.getDesc()._listPass.push_back( pass );
        };

        // 계약대로: Bloom 이 컬러 하나 + AO 를 읽는다. 역할은 로드 시점에 해석돼 있어야 한다.
        {
            sw::RenderPipelineAsset res;
            makeDesc( res );
            addPass( res, "Bloom", { "LitColor", "AOColor" }, "BloomColor" );
            SW_EXPECT_EQUAL( 0u, res.validate( "unit-test" ) );
            const sw::RenderGraphPassDesc& pass = res.getGraphPass()[0];
            SW_ASSERT_TRUE( pass._listResolvedInput.size() == 2 );
            SW_EXPECT_TRUE( static_cast<sw::RenderPassInputRole>( pass._listResolvedInput[0]._role ) == sw::RenderPassInputRole::SourceColor );
            SW_EXPECT_TRUE( static_cast<sw::RenderPassInputRole>( pass._listResolvedInput[1]._role ) == sw::RenderPassInputRole::AmbientOcclusion );
            SW_ASSERT_TRUE( pass._listResolvedOutput.size() == 1 );
            SW_EXPECT_TRUE( pass._listResolvedOutput[0].view() == "BloomColor" );
        }
        // Tonemap 은 G버퍼 노멀을 읽지 않는다 — 선언만 있고 바인딩되지 않는 입력.
        {
            sw::RenderPipelineAsset res;
            makeDesc( res );
            addPass( res, "Tonemap", { "LitColor", "GBufferNormal" }, "BloomColor" );
            SW_EXPECT_EQUAL( 1u, res.validate( "unit-test" ) );
        }
        // SSAO 는 깊이가 필수다.
        {
            sw::RenderPipelineAsset res;
            makeDesc( res );
            addPass( res, "SSAO", { "GBufferNormal" }, "AOColor" );
            SW_EXPECT_EQUAL( 1u, res.validate( "unit-test" ) );
        }
        // 가공할 컬러가 둘이면 셰이더가 어느 것을 읽을지 정할 수 없다.
        {
            sw::RenderPipelineAsset res;
            makeDesc( res );
            addPass( res, "Bloom", { "LitColor", "BloomColor" }, "AOColor" );
            SW_EXPECT_EQUAL( 1u, res.validate( "unit-test" ) );
        }
    }
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
    sw::RenderPipelineAsset pipeRes;
    sw::RenderPipelineDesc& desc = pipeRes.getDesc();
    desc._name                   = "UnitTestAllStagesPipeline";
    desc._shadingModel           = "Deferred";

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

    const sw::string testPath = test::makeTempPath( "test_pipeline_all_stages.xml" );
    SW_EXPECT_TRUE( pipeRes.saveToXmlFile( testPath ) );

    sw::RenderPipelineAsset loadedRes;
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
    sw::RenderPipelineAsset pipeRes;
    sw::RenderPipelineDesc& desc = pipeRes.getDesc();
    desc._name                   = "UnitTestCompactPipeline";
    desc._shadingModel           = "Forward";

    sw::RenderGraphPassDesc pass{};
    pass._name             = "CompactPass";
    pass._type             = "ForwardOpaque";
    pass._shaderPath       = "engine/shaders/forwardlit.hlsl";
    pass._vertexEntryPoint = "VSMain";
    pass._pixelEntryPoint  = "PSMain";
    // _computeEntryPoint, _geometryEntryPoint 등은 비어 있음
    desc._listPass.push_back( pass );

    const sw::string testPath = test::makeTempPath( "test_pipeline_compact.xml" );
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
    sw::RenderPipelineAsset loadedRes;
    SW_EXPECT_TRUE( loadedRes.loadFromXmlFile( testPath ) );
    const sw::RenderGraphPassDesc& loadedPass = loadedRes.getGraphPass()[0];
    SW_EXPECT_TRUE( loadedPass._geometryEntryPoint.empty() );
    SW_EXPECT_TRUE( loadedPass._hullEntryPoint.empty() );
    SW_EXPECT_TRUE( loadedPass._domainEntryPoint.empty() );
    SW_EXPECT_TRUE( loadedPass._meshEntryPoint.empty() );
    SW_EXPECT_TRUE( loadedPass._amplificationEntryPoint.empty() );
    SW_EXPECT_TRUE( loadedPass._computeEntryPoint.empty() );
}

/**
 * @brief [RenderPassTest] 지오메트리 패스의 컬러 타깃은 선언에서 온다 — 컬러 출력만 선언 순서대로, 컬러 출력이 없으면 검증 오류
 * @details 실행(FrameRenderer)은 `_listResolvedColorOutput` 을 그대로 건다(GBuffer 는 [0] 알베도, [1] 노멀). 이름을 코드에 박으면
 *          (SceneColor · GBufferAlbedo …) 다른 이름을 쓰는 파이프라인에서 없는 첨부(핸들 0 = 백버퍼)를 연다. 실제 그림은
 *          `RenderPassGpuTest.RenamedAttachmentsRenderTheSameImage` 가 본다.
 */
SW_TEST_CASE( RenderPassTest, GeometryPassColorTargetsComeFromTheDeclaration )
{
    SW_TEST_SUPPRESS_LOGS();

    auto makePipeline = []( sw::RenderPipelineAsset& res, const utf8* pType, std::initializer_list<const utf8*> listOutput )
    {
        sw::RenderPipelineDesc& desc          = res.getDesc();
        auto                    addAttachment = [&desc]( const utf8* pName, const utf8* pFormat )
        {
            sw::RenderPassAttachment att{};
            att._name   = pName;
            att._format = pFormat;
            desc._listAttachment.push_back( att );
        };
        addAttachment( "MainAlbedo", "R8G8B8A8_UNORM" );
        addAttachment( "MainNormal", "R16G16B16A16_FLOAT" );
        addAttachment( "MainDepth", "D24_UNORM_S8_UINT" );

        sw::RenderGraphPassDesc pass{};
        pass._name            = pType;
        pass._type            = pType;
        pass._depthAttachment = "MainDepth";
        for ( const utf8* pOutput : listOutput )
            pass._listOutput.push_back( pOutput );
        desc._listPass.push_back( pass );
    };

    // 출력에 뎁스가 섞여 있어도 컬러만, 선언 순서대로 — 이름은 정본(GBufferAlbedo …)이 아니어도 된다.
    {
        sw::RenderPipelineAsset res;
        makePipeline( res, "GBuffer", { "MainDepth", "MainAlbedo", "MainNormal" } );
        SW_EXPECT_EQUAL( 0u, res.validate( "unit-test" ) );
        const sw::vector<sw::RenderGraphPassDesc::ResolvedAttachment>& listColor = res.getGraphPass()[0]._listResolvedColorOutput;
        SW_ASSERT_EQUAL( size_t( 2 ), listColor.size() );
        SW_EXPECT_TRUE( listColor[0]._attachment.view() == "MainAlbedo" );
        SW_EXPECT_TRUE( listColor[1]._attachment.view() == "MainNormal" );
    }
    // G버퍼는 알베도 · 노멀 두 컬러를 한 MRT 패스로 쓴다 — 하나뿐이면 Lighting · SSAO 가 읽을 노멀이 없다(MRT 없는 단독 패스 경로는 없다).
    {
        sw::RenderPipelineAsset res;
        makePipeline( res, "GBuffer", { "MainAlbedo", "MainDepth" } );
        SW_EXPECT_EQUAL( 1u, res.validate( "unit-test" ) );
    }
    // 뎁스만 내는 ForwardOpaque 는 그릴 컬러가 없다 — 검증 오류(SceneColor 를 짐작해 열지 않는다).
    {
        sw::RenderPipelineAsset res;
        makePipeline( res, "ForwardOpaque", { "MainDepth" } );
        SW_EXPECT_EQUAL( 1u, res.validate( "unit-test" ) );
        SW_EXPECT_TRUE( res.getGraphPass()[0]._listResolvedColorOutput.empty() );
    }
    // 뎁스 전용 패스는 컬러가 없어도 된다.
    {
        sw::RenderPipelineAsset res;
        makePipeline( res, "DepthPrepass", { "MainDepth" } );
        SW_EXPECT_EQUAL( 0u, res.validate( "unit-test" ) );
    }
}

/**
 * @brief [RenderPassTest] 첨부가 선언한 역할(`_role`)이 이름보다 먼저다 — 이름을 바꾼 G버퍼 · 그림자 맵으로 Lighting 계약이 선다
 * @details 역할을 이름으로만 정하면 `MainAlbedo` · `MainNormal` 은 SourceColor 로 읽혀 Lighting 계약이 깨지고(가공할 컬러가 둘 ·
 *          필수 G버퍼 없음) `SunShadow` 는 SceneDepth 로 읽힌다. 모르는 역할 글은 검증 오류다. 그림은
 *          `RenderPassGpuTest.RenamedGBufferAttachmentsRenderTheSameImage` 가 본다.
 */
SW_TEST_CASE( RenderPassTest, AttachmentRoleIsDeclaredNotNamed )
{
    SW_TEST_SUPPRESS_LOGS();

    auto addAttachment = []( sw::RenderPipelineAsset& res, const utf8* pName, const utf8* pFormat, const utf8* pRole )
    {
        sw::RenderPassAttachment att{};
        att._name   = pName;
        att._format = pFormat;
        att._role   = pRole;
        res.getDesc()._listAttachment.push_back( att );
    };
    auto makeDeferred = [&]( sw::RenderPipelineAsset& res, const utf8* pAlbedoRole )
    {
        addAttachment( res, "MainAlbedo", "R8G8B8A8_UNORM", pAlbedoRole );
        addAttachment( res, "MainNormal", "R16G16B16A16_FLOAT", "GBufferNormal" );
        addAttachment( res, "MainDepth", "D24_UNORM_S8_UINT", "" );
        addAttachment( res, "SunShadow", "D24_UNORM_S8_UINT", "ShadowMap" );
        addAttachment( res, "Lit", "R16G16B16A16_FLOAT", "" );

        sw::RenderGraphPassDesc gbuffer{};
        gbuffer._name            = "GBuffer";
        gbuffer._type            = "GBuffer";
        gbuffer._depthAttachment = "MainDepth";
        for ( const utf8* pOutput : { "MainNormal", "MainAlbedo", "MainDepth" } ) // 노멀을 먼저 — 순서가 아니라 역할로 골라야 한다
            gbuffer._listOutput.push_back( pOutput );
        res.getDesc()._listPass.push_back( gbuffer );

        sw::RenderGraphPassDesc lighting{};
        lighting._name = "Lighting";
        lighting._type = "Lighting";
        for ( const utf8* pInput : { "MainAlbedo", "MainNormal", "MainDepth", "SunShadow" } )
            lighting._listInput.push_back( pInput );
        lighting._listOutput.push_back( "Lit" );
        res.getDesc()._listPass.push_back( lighting );
    };

    {
        sw::RenderPipelineAsset res;
        makeDeferred( res, "GBufferAlbedo" );
        SW_EXPECT_EQUAL( 0u, res.validate( "unit-test" ) );

        const sw::RenderGraphPassDesc& gbuffer = res.getGraphPass()[0];
        SW_ASSERT_EQUAL( size_t( 2 ), gbuffer._listResolvedColorOutput.size() );
        SW_EXPECT_TRUE( static_cast<sw::RenderPassInputRole>( gbuffer._listResolvedColorOutput[0]._role ) == sw::RenderPassInputRole::GBufferNormal );
        SW_EXPECT_TRUE( static_cast<sw::RenderPassInputRole>( gbuffer._listResolvedColorOutput[1]._role ) == sw::RenderPassInputRole::GBufferAlbedo );

        const sw::RenderGraphPassDesc& lighting = res.getGraphPass()[1];
        SW_ASSERT_EQUAL( size_t( 4 ), lighting._listResolvedInput.size() );
        SW_EXPECT_TRUE( static_cast<sw::RenderPassInputRole>( lighting._listResolvedInput[0]._role ) == sw::RenderPassInputRole::GBufferAlbedo );
        SW_EXPECT_TRUE( static_cast<sw::RenderPassInputRole>( lighting._listResolvedInput[1]._role ) == sw::RenderPassInputRole::GBufferNormal );
        SW_EXPECT_TRUE( static_cast<sw::RenderPassInputRole>( lighting._listResolvedInput[2]._role ) == sw::RenderPassInputRole::SceneDepth );
        SW_EXPECT_TRUE( static_cast<sw::RenderPassInputRole>( lighting._listResolvedInput[3]._role ) == sw::RenderPassInputRole::ShadowMap );
    }
    // 모르는 역할 글은 오류다 — 조용히 이름 규칙으로 넘기면 선언한 사람이 왜 안 걸리는지 모른다. 이름 규칙으로 떨어진 알베도는 SourceColor 라
    // Lighting 계약도 함께 깨진다(읽지 않는 SourceColor · 필수 GBufferAlbedo 없음) — 셋이다.
    {
        sw::RenderPipelineAsset res;
        makeDeferred( res, "Albedo" );
        SW_EXPECT_EQUAL( 3u, res.validate( "unit-test" ) );
    }
}

namespace
{
    /** @brief `FrameRenderer::setAnimationTimeOverride` 가 이 구성에 있는가 — 시험 전용 시계 고정이라 배포본에는 없어야 한다. */
    template <typename T, typename = void>
    struct HasAnimationTimeOverride : std::false_type
    {
    };
    template <typename T>
    struct HasAnimationTimeOverride<T, std::void_t<decltype( std::declval<T&>().setAnimationTimeOverride( 0.0f ) )>> : std::true_type
    {
    };
} // namespace

/**
 * @brief [RenderPassTest] 시험 전용 시계 고정(`setAnimationTimeOverride`)은 배포본에서 컴파일되지 않는다
 * @details 시험이 모프 시각을 고정하려고 둔 창구다. 배포본에 남으면 배포본의 애니메이션 시계를 밖에서 바꿀 수 있고 필드 하나만큼 렌더러가 커진다.
 */
SW_TEST_CASE( RenderPassTest, AnimationTimeOverrideIsCompiledOutOfShipping )
{
#if defined( SW_SHIPPING )
    SW_EXPECT_FALSE( HasAnimationTimeOverride<sw::FrameRenderer>::value );
#else
    SW_EXPECT_TRUE( HasAnimationTimeOverride<sw::FrameRenderer>::value );
#endif
}

/**
 * @brief 메시 외곽선 패스는 외곽선 스위치를 켠 머티리얼의 배치만 그린다 — 드로우 · 머티리얼 PSO 변형 · 쿠커가 같은 판정(`drawsMaterialInPass`)을 쓴다
 * @details 머티리얼이 없는 배치 · 스위치를 끈 툰 · 다른 셰이더(forwardlit)는 빠지고, 다른 메시 패스는 거르지 않는다. 패스는 앞면 컬링이 기본이고
 *          파이프라인 XML 의 `_type` 으로 쓸 수 있다.
 */
SW_TEST_CASE( RenderPassTest, MeshOutlineDrawsOnlyMaterialsWithOutline )
{
    const sw::RenderPassTypeInfo& info = sw::getRenderPassTypeInfo( sw::RenderPassType::MeshOutline );
    SW_EXPECT_TRUE( sw::isPipelinePassType( sw::RenderPassType::MeshOutline ) );
    SW_EXPECT_TRUE( info.hasFlag( sw::RenderPassTraitFlag::kCullFront ) );
    SW_EXPECT_TRUE( info.hasFlag( sw::RenderPassTraitFlag::kUsesMaterialShader ) );
    SW_EXPECT_FALSE( info.hasFlag( sw::RenderPassTraitFlag::kDrawsTransparentBatch ) );

    sw::shared_ptr<sw::Material> toon = sw::Material::create();
    SW_ASSERT_TRUE( toon->loadFromFile( "engine/materials/toon.material" ) );
    sw::shared_ptr<sw::Material> lit = sw::Material::create();
    SW_ASSERT_TRUE( lit->loadFromFile( "engine/materials/defaultmaterial.material" ) );

    SW_EXPECT_FALSE( sw::FrameRendererUtil::drawsMaterialInPass( sw::RenderPassType::MeshOutline, nullptr ) );
    SW_EXPECT_FALSE( sw::FrameRendererUtil::drawsMaterialInPass( sw::RenderPassType::MeshOutline, &toon->getCachedShaderDefines() ) );
    SW_EXPECT_FALSE( sw::FrameRendererUtil::drawsMaterialInPass( sw::RenderPassType::MeshOutline, &lit->getCachedShaderDefines() ) );
    toon->setStaticSwitch( sw::hashed_string( "Outline" ), true );
    SW_EXPECT_TRUE( sw::FrameRendererUtil::drawsMaterialInPass( sw::RenderPassType::MeshOutline, &toon->getCachedShaderDefines() ) );

    // 다른 메시 패스는 머티리얼로 거르지 않는다(머티리얼이 없는 배치도 그린다).
    SW_EXPECT_TRUE( sw::FrameRendererUtil::drawsMaterialInPass( sw::RenderPassType::ForwardOpaque, nullptr ) );
    SW_EXPECT_TRUE( sw::FrameRendererUtil::drawsMaterialInPass( sw::RenderPassType::Shadow, &lit->getCachedShaderDefines() ) );
}

/**
 * @brief [RenderPassTest] 비어 있는 진입점은 스테이지 표(getShaderStageInfo)의 기본값이 된다 — 진입점 기본값은 표 한 줄이다
 */
SW_TEST_CASE( RenderPassTest, EmptyEntryPointsResolveToStageTable )
{
    const sw::RenderGraphPassDesc pass{};
    SW_EXPECT_TRUE( pass._vertexEntryPoint.empty() );
    SW_EXPECT_TRUE( pass._pixelEntryPoint.empty() );
    SW_EXPECT_EQUAL( sw::string_view( "VSMain" ), sw::resolveEntryPoint( pass._vertexEntryPoint, sw::ShaderStage::Vertex ) );
    SW_EXPECT_EQUAL( sw::string_view( "PSMain" ), sw::resolveEntryPoint( pass._pixelEntryPoint, sw::ShaderStage::Pixel ) );
    SW_EXPECT_EQUAL( sw::string_view( "CSMain" ), sw::resolveEntryPoint( {}, sw::ShaderStage::Compute ) );
    SW_EXPECT_EQUAL( sw::string_view( "MyCS" ), sw::resolveEntryPoint( "MyCS", sw::ShaderStage::Compute ) );
}
