#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/FrameArenaAllocator.h"
#include "Core/String/hashed_string.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/Renderer/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/RenderFramePacket.h"
#include "Engine/Graphics/Renderer/Graph/RenderGraph.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassManager.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassResource.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineResource.h"
#include "Engine/Graphics/Renderer/Scene/GpuScene.h"
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
            res.validate( "unit-test" );
            // 여기서 보는 것은 **이름 해석**뿐이다 — 입력 계약 위반(입력 없는 Tonemap)은 다른 케이스가 본다.
            return desc._listPass[0]._resolvedType == sw::RenderPassType::Invalid;
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

    // 7) 풀스크린 패스의 입력은 그 타입의 계약과 맞아야 한다 — "선언만 있고 아무도 안 읽는 입력" 이 오류다.
    //    디퍼드 XML 이 Bloom 의 입력으로 AOColor 를 적어 두고도 Bloom 이 그것을 걸지 않던 것(백로그 1-6)이 이 검사가 잡는 병이다.
    {
        auto makeDesc = []( sw::RenderPipelineResource& res )
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
        auto addPass = []( sw::RenderPipelineResource& res, const utf8* pType, std::initializer_list<const utf8*> listInput, const utf8* pOutput )
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
            sw::RenderPipelineResource res;
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
            sw::RenderPipelineResource res;
            makeDesc( res );
            addPass( res, "Tonemap", { "LitColor", "GBufferNormal" }, "BloomColor" );
            SW_EXPECT_EQUAL( 1u, res.validate( "unit-test" ) );
        }
        // SSAO 는 깊이가 필수다.
        {
            sw::RenderPipelineResource res;
            makeDesc( res );
            addPass( res, "SSAO", { "GBufferNormal" }, "AOColor" );
            SW_EXPECT_EQUAL( 1u, res.validate( "unit-test" ) );
        }
        // 가공할 컬러가 둘이면 셰이더가 어느 것을 읽을지 정할 수 없다.
        {
            sw::RenderPipelineResource res;
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
