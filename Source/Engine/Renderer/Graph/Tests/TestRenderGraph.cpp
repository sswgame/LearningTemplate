/**
 * @file TestRenderGraph.cpp
 * @brief 렌더 그래프(`RenderGraph`) 테스트 — 바뀐 배리어만 추론, 읽고-쓰는 자원의 수명, 레벨 순서, 순환 보고, 병렬 기록(가짜 디바이스)입니다.
 */
#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Task/TaskManager.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Renderer/Graph/FrameBufferTable.h"
#include "Engine/Renderer/Graph/RenderGraph.h"

using namespace sw;

namespace
{
    /** @brief 패스마다 몇 번 돌았는지 세고, 받은 가짜 리스트에 자기 이름을 적는 콜백을 만듭니다. */
    RenderGraphPassExecuteFn makeCountingPass( atomic<int32>& callCount )
    {
        return SW_DELEGATE_LAMBDA( RenderGraphPassExecuteFn, [&callCount]( const RenderGraphPassContext& context )
        {
            callCount.fetch_add( 1 );
            if ( context.getCommandList() != nullptr )
                static_cast<test::FakeRHICommandList*>( context.getCommandList() )->_passName = context._passName;
        } );
    }

    /** @brief A · C 는 레벨 0, A 의 출력을 읽는 B 는 레벨 1 인 그래프를 짓습니다. */
    void addLevelGraph( RenderGraph& graph, atomic<int32>& countA, atomic<int32>& countB, atomic<int32>& countC )
    {
        graph.addPass( hashed_string( "A" ), {}, { hashed_string( "OutA" ) }, makeCountingPass( countA ) );
        graph.addPass( hashed_string( "B" ), { hashed_string( "OutA" ) }, { hashed_string( "OutB" ) }, makeCountingPass( countB ) );
        graph.addPass( hashed_string( "C" ), {}, { hashed_string( "OutC" ) }, makeCountingPass( countC ) );
    }
} // namespace

/**
 * @brief [RenderGraphTest] 그래프가 상태를 들고 있다가 바뀌는 전이만 낸다
 * @details 레벨이 읽고 쓰는 자원 이름을 전부 넘기면 같은 자원을 세 패스가 읽을 때 읽기 전이를 세 번 걸고, 이미 그 상태인 것도 다시 건다.
 *          그래프가 원본이다 — 여기서는 그 추론만 커맨드 리스트 없이 본다.
 */
SW_TEST_CASE( RenderGraphTest, RenderGraphInfersOnlyChangedBarriers )
{
    RenderGraph         graph;
    const hashed_string colorBuffer( "ColorBuffer" );
    const hashed_string blurBuffer( "BlurBuffer" );
    const hashed_string uiBuffer( "UIBuffer" );

    graph.addPass( hashed_string( "PassA_Write" ), {}, { colorBuffer } );
    graph.addPass( hashed_string( "PassB_Read" ), { colorBuffer }, { blurBuffer } );
    graph.addPass( hashed_string( "PassC_ReadSame" ), { colorBuffer }, { uiBuffer } );
    SW_REQUIRE_TRUE( graph.compile() );

    vector<RenderGraphBarrier> listIssuedBarrier;
    graph.setLevelPrologue( RenderGraphLevelPrologueFn( [&listIssuedBarrier]( const RenderGraphLevelContext& levelContext )
    {
        if ( levelContext._pListBarrier == nullptr )
            return;
        for ( const RenderGraphBarrier& barrier : *levelContext._pListBarrier )
        {
            listIssuedBarrier.push_back( barrier );
        }
    } ) );

    auto countFor = [&listIssuedBarrier]( hashed_string resource ) -> uint32
    {
        uint32 count{ 0 };
        for ( const RenderGraphBarrier& barrier : listIssuedBarrier )
        {
            if ( barrier._resource == resource )
                ++count;
        }
        return count;
    };

    // 첫 프레임: 전부 처음 보는 자원이다. PassB 가 ColorBuffer 를 읽기로 바꿔 놓았으므로 PassC 는 낼 것이 없다.
    RenderGraphExecutionContext context;
    SW_REQUIRE_TRUE( graph.execute( context ) );
    SW_EXPECT_EQUAL( 2u, countFor( colorBuffer ) );
    SW_EXPECT_EQUAL( 1u, countFor( blurBuffer ) );
    SW_EXPECT_EQUAL( 1u, countFor( uiBuffer ) );

    // 첫 전이는 Undefined 에서 시작하고, 그다음이 쓰기 → 읽기다.
    SW_REQUIRE_TRUE( listIssuedBarrier.size() >= 2 );
    SW_EXPECT_TRUE( listIssuedBarrier[0]._resource == colorBuffer );
    SW_EXPECT_TRUE( listIssuedBarrier[0]._before == RenderGraphResourceState::Undefined );
    SW_EXPECT_TRUE( listIssuedBarrier[0]._after == RenderGraphResourceState::Write );

    // 두 번째 프레임: 똑같이 나와야 한다. 그래프는 프레임 시작에 일부러 잊는다 — 전이는 그래프 밖에서도 일어나므로
    // 지난 프레임의 믿음을 이어 가면 필요한 배리어를 건너뛴다. 버는 것은 한 프레임 안의 중복이다.
    const size_t firstFrameCount = listIssuedBarrier.size();
    listIssuedBarrier.clear();
    SW_REQUIRE_TRUE( graph.execute( context ) );
    SW_EXPECT_EQUAL( 2u, countFor( colorBuffer ) );
    SW_EXPECT_EQUAL( 1u, countFor( blurBuffer ) );
    SW_EXPECT_EQUAL( 1u, countFor( uiBuffer ) );
    SW_EXPECT_TRUE_MSG( listIssuedBarrier.size() == firstFrameCount, "barriers differ between frames - the graph carries state across frames" );

    // 수명도 컴파일 산출물이다 — ColorBuffer 는 0 번 패스에서 나서 2 번 패스까지 산다.
    const RenderGraphResourceLifetime* pLife = graph.findResourceLifetime( colorBuffer );
    SW_REQUIRE_NOT_NULL( pLife );
    SW_EXPECT_EQUAL( size_t( 0 ), pLife->_firstPassIndex );
    SW_EXPECT_EQUAL( size_t( 2 ), pLife->_lastPassIndex );
    SW_EXPECT_TRUE( pLife->_bWritten );
    SW_EXPECT_TRUE( pLife->_bRead );
}

/**
 * @brief [RenderGraphTest] 읽고-쓰는(Read-Modify-Write) 패스는 선언 순서대로 줄을 서고, 자원 수명은 compile 이 계산해 둔다
 */
SW_TEST_CASE( RenderGraphTest, RenderGraphReadModifyWriteAndLifetimes )
{
    RenderGraph graph;
    graph.addPass( hashed_string( "PassA_Geometry" ), {}, { hashed_string( "ColorBuffer" ) } );
    graph.addPass( hashed_string( "PassB_PostProcess" ), { hashed_string( "ColorBuffer" ) }, { hashed_string( "ColorBuffer" ) } );
    graph.addPass( hashed_string( "PassC_UIOverlay" ), { hashed_string( "ColorBuffer" ) }, { hashed_string( "FinalOutput" ) } );
    SW_REQUIRE_TRUE( graph.compile() );

    const vector<hashed_string>& order = graph.getExecutionOrder();
    SW_REQUIRE_EQUAL( size_t( 3 ), order.size() );
    SW_EXPECT_EQUAL( string( "PassA_Geometry" ), string( order[0].c_str() ) );
    SW_EXPECT_EQUAL( string( "PassB_PostProcess" ), string( order[1].c_str() ) );
    SW_EXPECT_EQUAL( string( "PassC_UIOverlay" ), string( order[2].c_str() ) );

    // 수명은 compile() 이 계산해 둔다 — 실행 순서가 정해지기 전에 세면 뜻이 없는 값이 나온다.
    const RenderGraphResourceLifetime* pLife = graph.findResourceLifetime( hashed_string( "ColorBuffer" ) );
    SW_REQUIRE_NOT_NULL( pLife );
    SW_EXPECT_EQUAL( size_t( 0 ), pLife->_firstPassIndex );
    SW_EXPECT_EQUAL( size_t( 2 ), pLife->_lastPassIndex );
    SW_EXPECT_TRUE( pLife->_bWritten );
    SW_EXPECT_TRUE( pLife->_bRead );
    SW_EXPECT_FALSE( graph.getResourceLifetimes().empty() );
}

/**
 * @brief [RenderGraphTest] 읽은 자원을 뒤에서 다시 쓰는 패스는 그 읽기 뒤에 돈다(Write-after-Read)
 * @details W 가 쓰고 A 가 읽고 B 가 다시 쓸 때 간선이 W→B(쓰기 사슬) · W→A(읽기)뿐이면 A 와 B 가 같은 레벨이 되어 B 가 먼저
 *          줄을 설 수 있다 — A 가 B 의 출력을 읽는다. UI · 오버레이가 블룸이 읽은 SceneColor 를 다시 쓰는 모양이 그렇다.
 */
SW_TEST_CASE( RenderGraphTest, ReaderRunsBeforeTheNextWriterOfItsInput )
{
    RenderGraph         graph;
    const hashed_string sceneColor( "SceneColor" );
    graph.addPass( hashed_string( "W_Geometry" ), {}, { sceneColor } );
    graph.addPass( hashed_string( "A_Bloom" ), { sceneColor }, { hashed_string( "BloomOut" ) } );
    graph.addPass( hashed_string( "B_Overlay" ), {}, { sceneColor } );
    SW_REQUIRE_TRUE( graph.compile() );

    const vector<hashed_string>& order = graph.getExecutionOrder();
    SW_REQUIRE_EQUAL( size_t( 3 ), order.size() );
    SW_EXPECT_EQUAL( string( "W_Geometry" ), string( order[0].c_str() ) );
    SW_EXPECT_EQUAL( string( "A_Bloom" ), string( order[1].c_str() ) );
    SW_EXPECT_EQUAL( string( "B_Overlay" ), string( order[2].c_str() ) );
    SW_EXPECT_EQUAL( size_t( 3 ), graph.getExecutionLevels().size() ); // 같은 레벨이면 병렬 기록에서 둘이 겨룬다

    // 소비자를 먼저 선언해도 된다(앞에 쓰는 이가 없으면 첫 쓰기가 생산자). 그 다음 쓰기만 소비자 뒤로 간다 — 선언 순서로 고르면
    // 생산자 자신을 골라 순환이 된다.
    RenderGraph earlyConsumer;
    earlyConsumer.addPass( hashed_string( "C_Reads" ), { sceneColor }, { hashed_string( "COut" ) } );
    earlyConsumer.addPass( hashed_string( "W1_Writes" ), {}, { sceneColor } );
    earlyConsumer.addPass( hashed_string( "W2_Writes" ), {}, { sceneColor } );
    SW_REQUIRE_TRUE( earlyConsumer.compile() );
    const vector<hashed_string>& earlyOrder = earlyConsumer.getExecutionOrder();
    SW_REQUIRE_EQUAL( size_t( 3 ), earlyOrder.size() );
    SW_EXPECT_EQUAL( string( "W1_Writes" ), string( earlyOrder[0].c_str() ) );
    SW_EXPECT_EQUAL( string( "C_Reads" ), string( earlyOrder[1].c_str() ) );
    SW_EXPECT_EQUAL( string( "W2_Writes" ), string( earlyOrder[2].c_str() ) );
}

/**
 * @brief [RenderGraphTest] 같은 이름의 패스는 두 번째를 버린다 — 한 패스가 두 번 돌고 다른 하나가 안 도는 일이 없다
 */
SW_TEST_CASE( RenderGraphTest, DuplicatePassNameIsRejected )
{
    SW_EXPECT_LOG( "Renderer.GraphDuplicatePass" );
    RenderGraph graph;
    uint32      firstCount{ 0 };
    uint32      secondCount{ 0 };
    SW_EXPECT_TRUE( graph.addPass( hashed_string( "Dup" ), {}, { hashed_string( "Out" ) },
                                   SW_DELEGATE_LAMBDA( RenderGraphPassExecuteFn, [&firstCount]( const RenderGraphPassContext& )
    { ++firstCount; } ) ) );
    SW_EXPECT_FALSE( graph.addPass( hashed_string( "Dup" ), {}, { hashed_string( "Out2" ) },
                                    SW_DELEGATE_LAMBDA( RenderGraphPassExecuteFn, [&secondCount]( const RenderGraphPassContext& )
    { ++secondCount; } ) ) );
    SW_REQUIRE_TRUE( graph.compile() );
    SW_EXPECT_EQUAL( size_t( 1 ), graph.getExecutionOrder().size() );

    RenderGraphExecutionContext context;
    SW_REQUIRE_TRUE( graph.execute( context ) );
    SW_EXPECT_EQUAL( 1u, firstCount );
    SW_EXPECT_EQUAL( 0u, secondCount );
}

/**
 * @brief [RenderGraphTest] 순환이면 서로 기다리는 패스를 이름으로 말한다 — "몇 개 중 몇 개" 가 아니라
 * @details A 가 X 를 읽고 Y 를 쓰고, B 가 Y 를 읽고 X 를 쓰면 둘은 서로를 기다린다. 경고가 개수만 말하면 파이프라인 XML 의 입출력을 손으로 따라가야 한다.
 */
SW_TEST_CASE( RenderGraphTest, CycleNamesThePassesThatWaitOnEachOther )
{
    SW_EXPECT_LOG( "Renderer.GraphCycle" );
    RenderGraph         graph;
    const hashed_string resourceX( "CycleX" );
    const hashed_string resourceY( "CycleY" );
    graph.addPass( hashed_string( "Free" ), {}, { hashed_string( "FreeOut" ) } );
    graph.addPass( hashed_string( "LoopA" ), { resourceX }, { resourceY } );
    graph.addPass( hashed_string( "LoopB" ), { resourceY }, { resourceX } );

    SW_EXPECT_FALSE( graph.compile() );
    SW_EXPECT_TRUE( graph.describeCompiledOrder().find( "not compiled" ) != string::npos );
}

/**
 * @brief [RenderGraphTest] 컴파일된 순서를 레벨 · 패스 · 읽고 쓰는 자원으로 적는다(개발 명령 `render.graph` 가 답하는 글)
 */
SW_TEST_CASE( RenderGraphTest, DescribeCompiledOrderListsLevelsAndResources )
{
    RenderGraph         graph;
    const hashed_string sceneColor( "SceneColor" );
    graph.addPass( hashed_string( "Geometry" ), {}, { sceneColor } );
    graph.addPass( hashed_string( "Bloom" ), { sceneColor }, { hashed_string( "BloomOut" ) } );
    SW_REQUIRE_TRUE( graph.compile() );

    const string text = graph.describeCompiledOrder();
    for ( const utf8* pExpected : { "2 passes in 2 levels", "level 0", "Geometry  reads [] writes [SceneColor]", "level 1", "Bloom  reads [SceneColor] writes [BloomOut]" } )
    {
        StringBuilder<constant::buffer::kMax1024> message;
        message.appendFormat( "missing: %#\n%#", pExpected, text );
        SW_EXPECT_TRUE_MSG( text.find( pExpected ) != string::npos, message.view() );
    }
}

/**
 * @brief [RenderGraphTest] 병렬 기록은 패스마다 한 번 기록하고, 레벨 순서로 닫힌 리스트를 제출한다(가짜 디바이스 — GPU 불필요)
 */
SW_TEST_CASE( RenderGraphTest, ParallelExecutionRecordsEachPassOnceInLevelOrder )
{
    atomic<int32> countA{ 0 };
    atomic<int32> countB{ 0 };
    atomic<int32> countC{ 0 };
    RenderGraph   graph;
    addLevelGraph( graph, countA, countB, countC );
    SW_REQUIRE_TRUE( graph.compile() );

    test::FakeRHIDevice         device;
    RenderGraphExecutionContext context;
    SW_REQUIRE_TRUE( graph.executeParallel( context, engine::getSubsystem<TaskManager>(), device ) );

    SW_EXPECT_EQUAL( 1, countA.load() );
    SW_EXPECT_EQUAL( 1, countB.load() );
    SW_EXPECT_EQUAL( 1, countC.load() );
    SW_REQUIRE_EQUAL( size_t( 3 ), device._listExecuted.size() );
    SW_EXPECT_TRUE( device._listExecuted[2]->_passName == hashed_string( "B" ) ); // 레벨 0 의 둘 다음
    for ( const test::FakeRHICommandList* pList : device._listExecuted )
    {
        SW_EXPECT_EQUAL( 1u, pList->_beginCount );
        SW_EXPECT_EQUAL( 1u, pList->_endCount );
        SW_EXPECT_FALSE( pList->_bOpen );
    }
}

/**
 * @brief [RenderGraphTest] 패스 하나짜리 레벨이 이어지면 리스트 하나에 이어 기록해 한 번 제출하고, 패스가 둘인 레벨을 만나면 그 리스트를 먼저 제출한다
 * @details 레벨마다 리스트를 열고 태스크로 넘기고 제출하는 고정 비용이 패스 본문보다 컸다(`RenderGraph::executeParallel`). 제출 순서가 곧 GPU 순서다.
 */
SW_TEST_CASE( RenderGraphTest, ParallelExecutionChainsSinglePassLevelsIntoOneList )
{
    atomic<int32> countA{ 0 };
    atomic<int32> countB{ 0 };
    atomic<int32> countC{ 0 };
    atomic<int32> countD{ 0 };
    atomic<int32> countE{ 0 };
    RenderGraph   graph;
    // 레벨 0 { A } → 레벨 1 { B } → 레벨 2 { C, D } → 레벨 3 { E }
    graph.addPass( hashed_string( "A" ), {}, { hashed_string( "OutA" ) }, makeCountingPass( countA ) );
    graph.addPass( hashed_string( "B" ), { hashed_string( "OutA" ) }, { hashed_string( "OutB" ) }, makeCountingPass( countB ) );
    graph.addPass( hashed_string( "C" ), { hashed_string( "OutB" ) }, { hashed_string( "OutC" ) }, makeCountingPass( countC ) );
    graph.addPass( hashed_string( "D" ), { hashed_string( "OutB" ) }, { hashed_string( "OutD" ) }, makeCountingPass( countD ) );
    graph.addPass( hashed_string( "E" ), { hashed_string( "OutC" ), hashed_string( "OutD" ) }, { hashed_string( "OutE" ) }, makeCountingPass( countE ) );
    SW_REQUIRE_TRUE( graph.compile() );

    test::FakeRHIDevice         device;
    RenderGraphExecutionContext context;
    SW_REQUIRE_TRUE( graph.executeParallel( context, engine::getSubsystem<TaskManager>(), device ) );

    for ( const atomic<int32>* pCount : { &countA, &countB, &countC, &countD, &countE } )
    {
        SW_EXPECT_EQUAL( 1, pCount->load() );
    }
    // A · B 를 이은 리스트 → C · D 의 리스트 둘 → E 의 리스트.
    SW_REQUIRE_EQUAL( size_t( 4 ), device._listExecuted.size() );
    SW_EXPECT_TRUE( device._listExecuted[0]->_passName == hashed_string( "B" ) ); // 같은 리스트에 A 다음 B 를 기록했다
    SW_EXPECT_TRUE( device._listExecuted[3]->_passName == hashed_string( "E" ) );
    for ( const test::FakeRHICommandList* pList : device._listExecuted )
    {
        SW_EXPECT_EQUAL( 1u, pList->_beginCount );
        SW_EXPECT_EQUAL( 1u, pList->_endCount );
        SW_EXPECT_FALSE( pList->_bOpen );
    }
}

/**
 * @brief [RenderGraphTest] 커맨드 리스트를 만들 수 없으면 병렬 기록은 아무것도 기록 · 제출하지 않고 실패한다 — 앞 레벨을 두 번 돌리지 않는다
 * @details 레벨을 돌며 리스트를 만들다 실패할 때 직렬 `execute` 로 넘어가면, 앞 레벨은 이미 기록 · 제출된 뒤라 그 패스들이 두 번 돌고(같은
 *          프레임에 두 번 그린다), 직렬 경로는 리스트 없이 기록한다(부르는 쪽은 프레임 리스트를 이미 닫았다).
 */
SW_TEST_CASE( RenderGraphTest, ParallelExecutionSubmitsNothingWhenACommandListCannotBeMade )
{
    SW_EXPECT_LOG( "Renderer.GraphCommandListUnavailable" );
    atomic<int32> countA{ 0 };
    atomic<int32> countB{ 0 };
    atomic<int32> countC{ 0 };
    RenderGraph   graph;
    addLevelGraph( graph, countA, countB, countC );
    SW_REQUIRE_TRUE( graph.compile() );

    test::FakeRHIDevice device;
    device._maxCreatable = 2; // 레벨 0 의 둘은 만들고, 레벨 1 의 B 에서 실패한다
    RenderGraphExecutionContext context;
    SW_EXPECT_FALSE( graph.executeParallel( context, engine::getSubsystem<TaskManager>(), device ) );
    SW_EXPECT_EQUAL( size_t( 0 ), device._listExecuted.size() );
    SW_EXPECT_EQUAL( 0, countA.load() );
    SW_EXPECT_EQUAL( 0, countB.load() );
    SW_EXPECT_EQUAL( 0, countC.load() );
}

/**
 * @brief [RenderGraphTest] 태스크를 넣지 못하면(풀이 내리는 중) 병렬 기록은 그 패스를 렌더 스레드에서 기록한다 — 기록 없이 제출하지 않는다
 * @details 그냥 넘어가면 레벨의 첫 리스트는 배리어만 든 채 열린 채로, 나머지는 지난 프레임의 명령 그대로 제출된다.
 */
SW_TEST_CASE( RenderGraphTest, ParallelExecutionRecordsInlineWhenTasksCannotBeQueued )
{
    SW_EXPECT_LOG( "Task.EmplaceRefused" );
    atomic<int32> countA{ 0 };
    atomic<int32> countB{ 0 };
    atomic<int32> countC{ 0 };
    RenderGraph   graph;
    addLevelGraph( graph, countA, countB, countC );
    SW_REQUIRE_TRUE( graph.compile() );

    // 내린 풀 — emplaceTask 가 무효 핸들을 돌려준다.
    TaskManager stoppedTaskManager;
    SW_REQUIRE_TRUE( stoppedTaskManager.initialize( 1 ) );
    stoppedTaskManager.shutdown();

    test::FakeRHIDevice         device;
    RenderGraphExecutionContext context;
    SW_REQUIRE_TRUE( graph.executeParallel( context, stoppedTaskManager, device ) );
    SW_EXPECT_EQUAL( 1, countA.load() );
    SW_EXPECT_EQUAL( 1, countB.load() );
    SW_EXPECT_EQUAL( 1, countC.load() );
    SW_REQUIRE_EQUAL( size_t( 3 ), device._listExecuted.size() );
    for ( const test::FakeRHICommandList* pList : device._listExecuted )
    {
        SW_EXPECT_EQUAL( 1u, pList->_beginCount );
        SW_EXPECT_EQUAL( 1u, pList->_endCount );
        SW_EXPECT_FALSE( pList->_bOpen );
    }
}

/**
 * @brief [RenderGraphTest] 같은 출력(Swapchain)을 쓰는 두 패스는 선언 순서대로 다른 레벨에 놓인다 — Canvas 가 Present 앞으로 가거나 같은 레벨에서 겨루지 않는다
 */
SW_TEST_CASE( RenderGraphTest, TwoSwapchainWritersKeepDeclaredOrder )
{
    RenderGraph         graph;
    const hashed_string sceneColor( "SceneColor" );
    const hashed_string swapchain( "Swapchain" );
    graph.addPass( hashed_string( "Geometry" ), {}, { sceneColor } );
    graph.addPass( hashed_string( "Present" ), { sceneColor }, { swapchain } );
    graph.addPass( hashed_string( "Canvas" ), {}, { swapchain } );
    SW_REQUIRE_TRUE( graph.compile() );

    const vector<hashed_string>& order = graph.getExecutionOrder();
    SW_REQUIRE_EQUAL( size_t( 3 ), order.size() );
    SW_EXPECT_EQUAL( string( "Geometry" ), string( order[0].c_str() ) );
    SW_EXPECT_EQUAL( string( "Present" ), string( order[1].c_str() ) );
    SW_EXPECT_EQUAL( string( "Canvas" ), string( order[2].c_str() ) );
    SW_EXPECT_EQUAL( size_t( 3 ), graph.getExecutionLevels().size() );
}

/**
 * @brief [RenderGraphTest] 프레임을 넘는 자원(Persistent · External)은 수명이 그래프 전체라 메모리를 나눌 후보가 아니고, 트랜지언트는 쓰는 패스부터 읽는 패스까지다
 * @details 히스토리를 트랜지언트처럼 첫 · 마지막 사용 사이만 산다고 보면 앞뒤 빈 구간에 다른 자원이 같은 메모리를 쓰고 다음 프레임이 깨진 히스토리를 읽는다.
 */
SW_TEST_CASE( RenderGraphTest, PersistentAndExternalResourcesLiveForTheWholeGraph )
{
    RenderGraph         graph;
    const hashed_string history( "DepthHistory" );
    const hashed_string lightBuffer( "LightBuffer" );
    const hashed_string scratch( "Scratch" );
    graph.declareResource( history, RenderGraphResourceKind::Persistent );
    graph.declareResource( lightBuffer, RenderGraphResourceKind::External );
    graph.addPass( hashed_string( "A" ), {}, { hashed_string( "OutA" ) } );
    graph.addPass( hashed_string( "B" ), { hashed_string( "OutA" ) }, { scratch } );
    graph.addPass( hashed_string( "C" ), { scratch, history }, { hashed_string( "OutC" ) } ); // 히스토리를 쓰기 전에 읽는다 — 지난 프레임 값
    graph.addPass( hashed_string( "D" ), { hashed_string( "OutC" ), lightBuffer }, { history } );
    SW_REQUIRE_TRUE( graph.compile() );

    const RenderGraphResourceLifetime* pHistory = graph.findResourceLifetime( history );
    SW_REQUIRE_TRUE( pHistory != nullptr );
    SW_EXPECT_TRUE( pHistory->_kind == RenderGraphResourceKind::Persistent );
    SW_EXPECT_EQUAL( size_t( 0 ), pHistory->_firstPassIndex );
    SW_EXPECT_EQUAL( size_t( 3 ), pHistory->_lastPassIndex );

    const RenderGraphResourceLifetime* pLight = graph.findResourceLifetime( lightBuffer );
    SW_REQUIRE_TRUE( pLight != nullptr );
    SW_EXPECT_TRUE( pLight->_kind == RenderGraphResourceKind::External );
    SW_EXPECT_EQUAL( size_t( 0 ), pLight->_firstPassIndex );
    SW_EXPECT_EQUAL( size_t( 3 ), pLight->_lastPassIndex );

    const RenderGraphResourceLifetime* pScratch = graph.findResourceLifetime( scratch );
    SW_REQUIRE_TRUE( pScratch != nullptr );
    SW_EXPECT_TRUE( pScratch->_kind == RenderGraphResourceKind::Transient );
    SW_EXPECT_EQUAL( size_t( 1 ), pScratch->_firstPassIndex );
    SW_EXPECT_EQUAL( size_t( 2 ), pScratch->_lastPassIndex );

    // 선언은 clear 와 함께 잊는다 — 다음 파이프라인의 같은 이름은 다시 선언해야 한다.
    graph.clear();
    SW_EXPECT_TRUE( graph.getResourceKind( history ) == RenderGraphResourceKind::Transient );
}

namespace
{
    /**
     * @brief 프레임 그래프 모양의 큐 시험 그래프입니다. 실행 순서는 Depth, Light, Anim, Occl, Fog, Cull 이다(레벨 0: Depth · Light · Anim, 1: Occl · Fog, 2: Cull).
     * @details Light · Occl 이 AsyncCompute 다. Fog 는 Light 를, Cull 은 Occl 을 읽는다. Anim 은 어느 묶음에도 기대지 않아 컴퓨트와 겹친다.
     */
    void addQueueGraph( RenderGraph& graph, atomic<int32>& callCount )
    {
        graph.addPass( hashed_string( "Depth" ), {}, { hashed_string( "DepthHistory" ) }, makeCountingPass( callCount ) );
        graph.addPass( hashed_string( "Light" ), {}, { hashed_string( "LightGrid" ) }, makeCountingPass( callCount ), RHIQueueKind::AsyncCompute );
        graph.addPass( hashed_string( "Anim" ), {}, { hashed_string( "InstanceBuffer" ) }, makeCountingPass( callCount ) );
        graph.addPass( hashed_string( "Occl" ), { hashed_string( "DepthHistory" ) }, { hashed_string( "OcclusionTiles" ) }, makeCountingPass( callCount ), RHIQueueKind::AsyncCompute );
        graph.addPass( hashed_string( "Fog" ), { hashed_string( "LightGrid" ) }, { hashed_string( "FogVolume" ) }, makeCountingPass( callCount ) );
        graph.addPass( hashed_string( "Cull" ), { hashed_string( "InstanceBuffer" ), hashed_string( "OcclusionTiles" ) }, { hashed_string( "CullResults" ) }, makeCountingPass( callCount ) );
    }
} // namespace

/**
 * @brief [RenderGraphTest] 비동기 큐 계획 — 이어진 비동기 패스가 묶음이고, 그래픽스 패스는 자기가 읽는 묶음만 앞에서 조인한다(기대지 않는 그래픽스는 겹친다)
 */
SW_TEST_CASE( RenderGraphTest, AsyncComputePlanForksAndJoinsOnDependencies )
{
    RenderGraph   graph;
    atomic<int32> callCount{ 0 };
    addQueueGraph( graph, callCount );
    graph.setAsyncComputeSupported( true );
    SW_REQUIRE_TRUE( graph.compile() );

    const vector<hashed_string>& order = graph.getExecutionOrder();
    SW_REQUIRE_EQUAL( size_t( 6 ), order.size() );
    SW_EXPECT_EQUAL( string( "Light" ), string( order[1].c_str() ) );
    SW_EXPECT_EQUAL( string( "Occl" ), string( order[3].c_str() ) );

    const vector<RenderGraphQueueStep>& steps = graph.getQueueSteps();
    SW_REQUIRE_EQUAL( size_t( 6 ), steps.size() );
    SW_EXPECT_EQUAL( uint32( 2 ), graph.getAsyncBatchCount() );
    SW_EXPECT_TRUE( steps[0]._queue == RHIQueueKind::Graphics );
    SW_EXPECT_TRUE( steps[1]._queue == RHIQueueKind::AsyncCompute && steps[1]._bBeginsBatch != 0 && steps[1]._bEndsBatch != 0 && steps[1]._batch == 0 );
    SW_EXPECT_TRUE( steps[2]._listJoinBatch.empty() ); // Anim 은 컴퓨트와 겹친다
    SW_EXPECT_TRUE( steps[3]._queue == RHIQueueKind::AsyncCompute && steps[3]._batch == 1 );
    SW_REQUIRE_EQUAL( size_t( 1 ), steps[4]._listJoinBatch.size() ); // Fog 가 Light 묶음을 기다린다
    SW_EXPECT_EQUAL( uint32( 0 ), steps[4]._listJoinBatch[0] );
    SW_REQUIRE_EQUAL( size_t( 1 ), steps[5]._listJoinBatch.size() ); // Cull 이 Occl 묶음을 기다린다
    SW_EXPECT_EQUAL( uint32( 1 ), steps[5]._listJoinBatch[0] );
    SW_EXPECT_TRUE( graph.getFinalJoinBatches().empty() );
}

/**
 * @brief [RenderGraphTest] 비동기 큐가 없는 디바이스는 AsyncCompute 로 선언한 패스도 그래픽스로 계획한다 — 같은 순서, 묶음 · 조인 없음(능력 표가 정한 길)
 */
SW_TEST_CASE( RenderGraphTest, AsyncComputeWithoutCapabilityRunsOnGraphics )
{
    RenderGraph   graph;
    atomic<int32> callCount{ 0 };
    addQueueGraph( graph, callCount );
    SW_REQUIRE_TRUE( graph.compile() ); // 기본은 지원하지 않음
    SW_EXPECT_EQUAL( uint32( 0 ), graph.getAsyncBatchCount() );
    for ( const RenderGraphQueueStep& step : graph.getQueueSteps() )
    {
        SW_EXPECT_TRUE( step._queue == RHIQueueKind::Graphics );
        SW_EXPECT_TRUE( step._listJoinBatch.empty() );
    }
}

/**
 * @brief [RenderGraphTest] 그래프 안에서 아무도 읽지 않는 비동기 묶음은 그래프 끝에서 조인한다 — 결과를 읽는 쪽(뷰 그래프)이 그래프 밖이다
 */
SW_TEST_CASE( RenderGraphTest, UnreadAsyncBatchJoinsAtTheEnd )
{
    RenderGraph graph;
    graph.addPass( hashed_string( "Anim" ), {}, { hashed_string( "InstanceBuffer" ) } );
    graph.addPass( hashed_string( "Probe" ), {}, { hashed_string( "ProbeIrradiance" ) }, {}, RHIQueueKind::AsyncCompute );
    graph.setAsyncComputeSupported( true );
    SW_REQUIRE_TRUE( graph.compile() );
    SW_REQUIRE_EQUAL( size_t( 1 ), graph.getFinalJoinBatches().size() );
    SW_EXPECT_EQUAL( uint32( 0 ), graph.getFinalJoinBatches()[0] );
}

/**
 * @brief [RenderGraphTest] 큐 계획대로 기록하면 포크 앞에서 그래픽스 리스트를 넘기고, 조인 앞에서 넘긴 뒤 기다린다 — 큐 사건 순서와 기록한 리스트를 본다
 */
SW_TEST_CASE( RenderGraphTest, ExecuteOnQueuesPlacesForksAndJoins )
{
    RenderGraph   graph;
    atomic<int32> callCount{ 0 };
    addQueueGraph( graph, callCount );
    graph.setAsyncComputeSupported( true );
    SW_REQUIRE_TRUE( graph.compile() );

    test::FakeRHIDevice device;
    device._bAsyncCompute                 = SW_TRUE;
    unique_ptr<IRHICommandList> pGraphics = device.createCommandList();
    unique_ptr<IRHICommandList> pAsync    = device.createAsyncComputeCommandList();
    RenderGraphExecutionContext context;
    pGraphics->beginCommandList();
    SW_REQUIRE_TRUE( graph.executeOnQueues( context, device, *pGraphics, pAsync.get() ) );
    SW_EXPECT_EQUAL( 6, callCount.load() );
    SW_EXPECT_TRUE( static_cast<test::FakeRHICommandList*>( pGraphics.get() )->_bOpen ); // 열린 채로 돌려준다

    // G F1 G F2 G J1 G J2 — 포크마다 그 앞의 그래픽스를 넘기고, 조인마다 넘긴 뒤 기다린다.
    const char   arrKind[]  = { 'G', 'F', 'G', 'F', 'G', 'J', 'G', 'J' };
    const uint64 arrValue[] = { 0, 1, 0, 2, 0, 1, 0, 2 };
    SW_REQUIRE_EQUAL( size_t( 8 ), device._listQueueEvent.size() );
    for ( size_t eventIndex = 0; eventIndex < device._listQueueEvent.size(); ++eventIndex )
    {
        SW_EXPECT_EQUAL( arrKind[eventIndex], device._listQueueEvent[eventIndex]._kind );
        SW_EXPECT_EQUAL( arrValue[eventIndex], device._listQueueEvent[eventIndex]._value );
    }
    // 비동기 리스트는 묶음마다 한 번 열고 닫았다.
    SW_EXPECT_EQUAL( uint32( 2 ), static_cast<test::FakeRHICommandList*>( pAsync.get() )->_beginCount );
    SW_EXPECT_EQUAL( uint32( 2 ), static_cast<test::FakeRHICommandList*>( pAsync.get() )->_endCount );
}

/**
 * @brief [RenderGraphTest] DOT 내보내기는 패스가 읽는 자원 → 패스, 패스 → 쓰는 자원의 간선을 모두 적는다 — Graphviz 가 그대로 그리는 글이다
 * @details 컴파일 전에도 지금 넣은 패스 구성을 낸다(패스가 왜 그 순서인지 볼 때 쓴다 — 언리얼 RDG 그래프 덤프와 같은 자리).
 */
SW_TEST_CASE( RenderGraphTest, ExportToDotListsEveryResourceEdge )
{
    RenderGraph graph;
    graph.addPass( hashed_string( "Shadow" ), {}, { hashed_string( "ShadowMap" ) } );
    graph.addPass( hashed_string( "Lighting" ), { hashed_string( "ShadowMap" ) }, { hashed_string( "SceneColor" ) } );

    const string dot = graph.exportToDot();
    SW_EXPECT_TRUE( dot.find( "digraph RenderGraph {" ) == 0 );
    SW_EXPECT_TRUE( dot.find( "\"Shadow\" -> \"ShadowMap\";" ) != string::npos );
    SW_EXPECT_TRUE( dot.find( "\"ShadowMap\" -> \"Lighting\";" ) != string::npos );
    SW_EXPECT_TRUE( dot.find( "\"Lighting\" -> \"SceneColor\";" ) != string::npos );
    SW_EXPECT_TRUE( dot.find( "\"SceneColor\" -> " ) == string::npos ); // 아무도 읽지 않는 자원에서 나가는 간선은 없다

    RenderGraph empty;
    SW_EXPECT_EQUAL( string( "digraph RenderGraph {\n}\n" ), empty.exportToDot() );
}

namespace
{
    /** @brief 레벨 콜백이 받은 배리어를 모두 모으게 합니다. */
    void collectIssuedBarriers( RenderGraph& graph, vector<RenderGraphBarrier>& outListBarrier )
    {
        graph.setLevelPrologue( RenderGraphLevelPrologueFn( [&outListBarrier]( const RenderGraphLevelContext& levelContext )
        {
            if ( levelContext._pListBarrier == nullptr )
                return;
            for ( const RenderGraphBarrier& barrier : *levelContext._pListBarrier )
            {
                outListBarrier.push_back( barrier );
            }
        } ) );
    }

    /** @brief @p resource 의 배리어만 고릅니다. */
    vector<RenderGraphBarrier> findBarriersFor( const vector<RenderGraphBarrier>& listBarrier, hashed_string resource )
    {
        vector<RenderGraphBarrier> listFound;
        for ( const RenderGraphBarrier& barrier : listBarrier )
        {
            if ( barrier._resource == resource )
                listFound.push_back( barrier );
        }
        return listFound;
    }
} // namespace

/**
 * @brief [RenderGraphTest] 접근 핸들 — UAV 로 쓴 버퍼를 다음 레벨 둘이 SRV 로 읽으면 전이는 UAV 하나 · SRV 하나뿐이고(같은 레벨 읽기 여럿은 한 번), 간접 인자는 따로 옮긴다.
 *        결함이면 버퍼 전이를 패스 몸통이 손으로 내야 하고(빠뜨리면 DX12 · Vulkan 에서만 지난 내용을 읽는다), 같은 레벨 읽기마다 전이가 겹친다
 */
SW_TEST_CASE( RenderGraphTest, AccessHandlesInferBufferTransitions )
{
    RenderGraph         graph;
    const hashed_string culled( "CulledInstances" );
    const hashed_string args( "DrawArgs" );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Cull" ), { RenderGraphBufferUAV{ culled }, RenderGraphBufferUAV{ args } } ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "ShadeA" ), { RenderGraphBufferSRV{ culled }, RenderGraphIndirectArgs{ args } } ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "ShadeB" ), { RenderGraphBufferSRV{ culled }, RenderGraphRenderTarget{ hashed_string( "Color" ) } } ) );
    SW_REQUIRE_TRUE( graph.compile() );
    SW_REQUIRE_EQUAL( size_t{ 2 }, graph.getExecutionLevels().size() ); // Cull, 그리고 ShadeA · ShadeB 가 한 레벨

    vector<RenderGraphBarrier> listBarrier;
    collectIssuedBarriers( graph, listBarrier );
    RenderGraphExecutionContext context;
    SW_REQUIRE_TRUE( graph.execute( context ) );

    const vector<RenderGraphBarrier> listCulled = findBarriersFor( listBarrier, culled );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listCulled.size() );
    SW_EXPECT_TRUE( listCulled[0]._access == RenderGraphAccess::UnorderedAccess );
    SW_EXPECT_TRUE( listCulled[0]._viewable == RenderGraphViewableKind::Buffer );
    SW_EXPECT_TRUE( listCulled[1]._access == RenderGraphAccess::ShaderRead );
    SW_EXPECT_TRUE( listCulled[1]._before == RenderGraphResourceState::Write );
    const vector<RenderGraphBarrier> listArgs = findBarriersFor( listBarrier, args );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listArgs.size() );
    SW_EXPECT_TRUE( listArgs[1]._access == RenderGraphAccess::IndirectArgs );
    SW_EXPECT_TRUE( context.findAccess( culled ) == RenderGraphAccess::ShaderRead );
}

/**
 * @brief [RenderGraphTest] UAV 를 레벨을 건너 잇따라 쓰면 상태는 그대로 UAV 이고 UAV 배리어 하나가 든다 — HZB 밉 사슬 · 컬링 → 정렬 자리.
 *        결함이면 둘째 디스패치가 첫째의 쓰기를 기다리지 않는다
 */
SW_TEST_CASE( RenderGraphTest, ChainedUnorderedAccessGetsUAVBarrier )
{
    RenderGraph         graph;
    const hashed_string keys( "SortKeys" );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Build" ), { RenderGraphBufferUAV{ keys } } ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Sort" ), { RenderGraphBufferUAV{ keys } } ) );
    SW_REQUIRE_TRUE( graph.compile() );

    vector<RenderGraphBarrier> listBarrier;
    collectIssuedBarriers( graph, listBarrier );
    RenderGraphExecutionContext context;
    SW_REQUIRE_TRUE( graph.execute( context ) );
    const vector<RenderGraphBarrier> listKeys = findBarriersFor( listBarrier, keys );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listKeys.size() );
    SW_EXPECT_EQUAL( uint8{ 0 }, listKeys[0]._bUAVBarrier ); // 처음 보는 자원 — 전이
    SW_EXPECT_EQUAL( uint8{ 1 }, listKeys[1]._bUAVBarrier );
    SW_EXPECT_TRUE( listKeys[1]._access == RenderGraphAccess::UnorderedAccess );
}

/**
 * @brief [RenderGraphTest] 들인 버퍼(`importResource`)는 처음 접근에서 시작해 그 접근으로 끝난다 — 같은 접근으로 읽으면 전이가 없고, 다른 접근이면 첫 사용과 끝에 전이 하나씩이다(언리얼 RegisterExternalBuffer · 끝 접근).
 *        결함이면 그래프 밖 버퍼가 프레임마다 첫 사용에서 쓸데없는 전이를 받거나, 다음 사용자가 끝 상태를 모른다
 */
SW_TEST_CASE( RenderGraphTest, ImportedResourceStartsInItsInitialAccess )
{
    RenderGraph         graph;
    const hashed_string grid( "LightGrid" );
    const hashed_string args( "CullArgs" );
    graph.importResource( grid, RenderGraphViewableKind::Buffer, RenderGraphAccess::ShaderRead );
    graph.importResource( args, RenderGraphViewableKind::Buffer, RenderGraphAccess::ShaderRead );
    SW_EXPECT_TRUE( graph.getResourceKind( grid ) == RenderGraphResourceKind::External );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Lighting" ), { RenderGraphBufferSRV{ grid }, RenderGraphIndirectArgs{ args } } ) );
    SW_REQUIRE_TRUE( graph.compile() );

    vector<RenderGraphBarrier> listBarrier;
    collectIssuedBarriers( graph, listBarrier );
    RenderGraphExecutionContext context;
    SW_REQUIRE_TRUE( graph.execute( context ) );
    SW_EXPECT_EQUAL( size_t{ 0 }, findBarriersFor( listBarrier, grid ).size() ); // 이미 그 접근
    const vector<RenderGraphBarrier> listArgs = findBarriersFor( listBarrier, args );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listArgs.size() );
    SW_EXPECT_TRUE( listArgs[0]._before == RenderGraphResourceState::Read );
    SW_EXPECT_TRUE( listArgs[0]._access == RenderGraphAccess::IndirectArgs );
    SW_EXPECT_TRUE( listArgs[1]._access == RenderGraphAccess::ShaderRead ); // 끝에 들인 접근으로 되돌린다
    SW_EXPECT_TRUE( context.findAccess( grid ) == RenderGraphAccess::ShaderRead );
    SW_EXPECT_TRUE( context.findAccess( args ) == RenderGraphAccess::ShaderRead );

    // 다음 프레임도 들인 접근에서 다시 시작한다.
    listBarrier.clear();
    SW_REQUIRE_TRUE( graph.execute( context ) );
    SW_EXPECT_EQUAL( size_t{ 2 }, findBarriersFor( listBarrier, args ).size() );
}

/**
 * @brief [RenderGraphTest] 들인 텍스처의 첫 사용은 처음 접근과 같아도 전이를 요구한다 — 새로 만든 텍스처의 레이아웃은 백엔드만 안다. UAV 로 이어지는 들인 버퍼의 첫 사용은 UAV 배리어다
 * @details 결함이면 자동 노출 기록처럼 막 만든 텍스처를 DX12 COMMON · Vulkan UNDEFINED 그대로 읽거나, 프레임 그래프가 쓴 차례 표시를 뷰 그래프의 뒤 단계가 쓰기를 기다리지 않고 읽는다
 *          (같은 상태의 버퍼 전이는 백엔드가 건너뛴다). 변이: 텍스처도 심으면 첫 단언이, 버퍼를 심지 않으면 UAV 배리어 단언이 진다.
 */
SW_TEST_CASE( RenderGraphTest, ImportedTextureAlwaysTransitionsAndBufferChainsUAV )
{
    RenderGraph         graph;
    const hashed_string history( "AutoExposureHistory" );
    const hashed_string orders( "OccludedOrders" );
    graph.importResource( history, RenderGraphViewableKind::Texture, RenderGraphAccess::ShaderRead );
    graph.importResource( orders, RenderGraphViewableKind::Buffer, RenderGraphAccess::UnorderedAccess );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Tonemap" ), { RenderGraphTextureSRV{ history }, RenderGraphBufferUAV{ orders } } ) );
    SW_REQUIRE_TRUE( graph.compile() );

    vector<RenderGraphBarrier> listBarrier;
    collectIssuedBarriers( graph, listBarrier );
    RenderGraphExecutionContext context;
    SW_REQUIRE_TRUE( graph.execute( context ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, findBarriersFor( listBarrier, history ).size() );
    const vector<RenderGraphBarrier> listOrders = findBarriersFor( listBarrier, orders );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listOrders.size() );
    SW_EXPECT_EQUAL( uint8{ 1 }, listOrders[0]._bUAVBarrier );
}

/**
 * @brief [RenderGraphTest] 패스 안 전이의 끝 접근이 그래프의 추적 상태가 된다 — 다음 패스는 선언 접근이 아니라 남긴 접근에서 이어지고, 끝 접근이 들인 접근과 같으면 되돌림이 없다(직렬 · 병렬 · 이어 기록 같음)
 * @details 2 단계 가림의 뒤 단계가 HZB 를 셰이더 읽기로 남기고 화면 공간 반사가 다시 UAV 로 짓는 모양이다. 결함이면 그래프는 HZB 가 아직 UAV 라고 믿어 전이 대신 UAV 배리어만 내고,
 *          반사의 디스패치가 셰이더 읽기 상태의 버퍼에 쓴다(DX12 검증 오류). 변이: `applyExitAccesses` 를 빼면 두 실행 모두 UAV 전이 단언이 진다.
 */
SW_TEST_CASE( RenderGraphTest, InPassTransitionBecomesTheTrackedAccess )
{
    const hashed_string hzb( "HZB" );
    RHIStructuredBuffer buffer{};
    buffer._buffer           = RHIBufferHandle{ 11 };
    buffer._elementSize      = 8;
    buffer._capacityElements = 16;
    FrameBufferTable table;
    table.registerBuffer( hzb, 0, buffer, RenderGraphAccess::ShaderRead );

    for ( const bool bParallel : { false, true } )
    {
        RenderGraph graph;
        const auto  buildAndRead = SW_DELEGATE_LAMBDA( RenderGraphPassExecuteFn, [&table, &hzb]( const RenderGraphPassContext& context )
         {
            RenderGraphPassContext passContext = context;
            passContext._pResourceTable        = &table;
            passContext.transition( RenderGraphBufferUAV{ hzb }, RenderGraphAccess::ShaderRead );
        } );
        SW_REQUIRE_TRUE( graph.addPass( hashed_string( "LateOcclusion" ), { RenderGraphBufferUAV{ hzb } }, buildAndRead ) );
        SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Reflection" ), { RenderGraphBufferUAV{ hzb } }, buildAndRead ) );
        graph.importResources( table );
        SW_REQUIRE_TRUE( graph.compile() );
        vector<RenderGraphBarrier> listBarrier;
        collectIssuedBarriers( graph, listBarrier );
        RenderGraphExecutionContext context;
        test::FakeRHIDevice         device;
        test::FakeRHICommandList    cmd( 0 );
        SW_REQUIRE_TRUE( bParallel ? graph.executeParallel( context, engine::getSubsystem<TaskManager>(), device ) : graph.execute( context, &cmd ) );
        const vector<RenderGraphBarrier> listHZB = findBarriersFor( listBarrier, hzb );
        SW_REQUIRE_EQUAL( size_t{ 2 }, listHZB.size() ); // 짓기마다 전이 하나 — 끝 접근이 등록 접근이라 되돌림이 없다
        for ( const RenderGraphBarrier& barrier : listHZB )
        {
            SW_EXPECT_TRUE( barrier._access == RenderGraphAccess::UnorderedAccess );
            SW_EXPECT_EQUAL( uint8{ 0 }, barrier._bUAVBarrier );
        }
        SW_EXPECT_TRUE( context.findAccess( hzb ) == RenderGraphAccess::ShaderRead );
    }
}

/**
 * @brief [RenderGraphTest] 마지막 레벨이 병렬이면 들인 자원의 되돌림은 따로 연 리스트로 마지막에 낸다 — 첨부 등록(`registerAttachment`)은 들이지 않아 되돌리지 않는다
 * @details 결함이면 되돌림 전이가 앞 레벨의 닫힌 리스트에 들어가거나(기록 오류) 사라져 다음 프레임의 사용자가 다른 상태를 본다.
 *          변이: 되돌림 리스트를 제출하지 않으면 제출 수 단언이, 첨부를 들이면 되돌림 수 단언이 진다.
 */
SW_TEST_CASE( RenderGraphTest, ParallelExecutionReturnsImportsAfterAParallelLevel )
{
    const hashed_string grid( "LightGrid" );
    const hashed_string depth( "SceneDepth" );
    RHIStructuredBuffer buffer{};
    buffer._buffer           = RHIBufferHandle{ 21 };
    buffer._elementSize      = 8;
    buffer._capacityElements = 16;
    FrameBufferTable table;
    table.registerBuffer( grid, 0, buffer, RenderGraphAccess::ShaderRead );
    table.registerAttachment( depth, RHITextureHandle{ 22 } );

    atomic<int32> countA{ 0 };
    atomic<int32> countB{ 0 };
    RenderGraph   graph;
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "A" ), { RenderGraphBufferUAV{ grid }, RenderGraphDepthWrite{ depth } }, makeCountingPass( countA ) ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "B" ), { RenderGraphRenderTarget{ hashed_string( "OutB" ) } }, makeCountingPass( countB ) ) );
    graph.importResources( table );
    SW_REQUIRE_TRUE( graph.compile() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, graph.getExecutionLevels().size() );
    vector<RenderGraphBarrier> listBarrier;
    collectIssuedBarriers( graph, listBarrier );
    test::FakeRHIDevice         device;
    RenderGraphExecutionContext context;
    SW_REQUIRE_TRUE( graph.executeParallel( context, engine::getSubsystem<TaskManager>(), device ) );
    SW_EXPECT_EQUAL( size_t( 3 ), device._listExecuted.size() ); // A · B · 되돌림
    const vector<RenderGraphBarrier> listGrid = findBarriersFor( listBarrier, grid );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listGrid.size() );
    SW_EXPECT_TRUE( listGrid[1]._access == RenderGraphAccess::ShaderRead );
    SW_EXPECT_EQUAL( size_t{ 1 }, findBarriersFor( listBarrier, depth ).size() ); // 첨부 — 첫 쓰기만
    for ( const test::FakeRHICommandList* pList : device._listExecuted )
    {
        SW_EXPECT_FALSE( pList->_bOpen );
    }
}

/**
 * @brief [RenderGraphTest] 병렬 기록을 못 하는 디바이스도 `executeParallel` 이 모든 패스를 리스트 하나에 이어 기록해 낸다 — 패스가 리스트 없이 돌지 않는다
 * @details 예전에는 리스트 없는 `execute` 로 빠져 패스 몸통이 nullptr 리스트를 받고 조용히 아무것도 그리지 않았다.
 *          변이: 리스트 없이 부르면 이름을 적은 리스트가 없어 제출 수 단언이 진다.
 */
SW_TEST_CASE( RenderGraphTest, ExecuteParallelWithoutParallelRecordingChainsOneList )
{
    atomic<int32> countA{ 0 };
    atomic<int32> countB{ 0 };
    atomic<int32> countC{ 0 };
    RenderGraph   graph;
    addLevelGraph( graph, countA, countB, countC );
    SW_REQUIRE_TRUE( graph.compile() );
    test::FakeRHIDevice device;
    device._bParallelRecording = SW_FALSE;
    RenderGraphExecutionContext context;
    SW_REQUIRE_TRUE( graph.executeParallel( context, engine::getSubsystem<TaskManager>(), device ) );
    SW_EXPECT_EQUAL( 1, countA.load() );
    SW_EXPECT_EQUAL( 1, countB.load() );
    SW_EXPECT_EQUAL( 1, countC.load() );
    SW_REQUIRE_EQUAL( size_t( 1 ), device._listExecuted.size() );
    SW_EXPECT_TRUE( device._listExecuted[0]->_passName == hashed_string( "B" ) );
    SW_EXPECT_FALSE( device._listExecuted[0]->_bOpen );
}

/**
 * @brief [RenderGraphTest] 같은 레벨의 두 패스가 한 자원을 다른 읽기(셰이더 읽기 · 간접 인자)로 읽으면 컴파일이 거절한다 — 레벨 배리어는 자원마다 하나다.
 *        결함이면 앞 접근만 GPU 에 나가고 추적은 뒤 접근을 믿어 DX12 · Vulkan 검증 층이 상태 불일치를 낸다
 */
SW_TEST_CASE( RenderGraphTest, MixedReadAccessInOneLevelIsRefused )
{
    RenderGraph         graph;
    const hashed_string args( "DrawArgs" );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Fill" ), { RenderGraphBufferUAV{ args } } ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Inspect" ), { RenderGraphBufferSRV{ args } } ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Draw" ), { RenderGraphIndirectArgs{ args } } ) );
    SW_EXPECT_LOG( "Renderer.GraphMixedReadAccess" );
    SW_EXPECT_FALSE( graph.compile() );
}

/**
 * @brief [RenderGraphTest] 비동기 컴퓨트 — 전이는 요구한 패스의 큐로 나가고(지원하지 않는 그래프는 그래픽스), 첨부 접근을 적은 비동기 패스는 거절한다.
 *        결함이면 컴퓨트 큐에 렌더 타깃 상태를 요구해 DX12 · Vulkan 검증 층이 오류를 내거나, 받는 쪽이 어느 큐의 상태로 옮길지 모른다
 */
SW_TEST_CASE( RenderGraphTest, AsyncComputeAccessesFoldToTheQueue )
{
    const hashed_string particles( "Particles" );
    for ( const bool bSupported : { true, false } )
    {
        RenderGraph graph;
        graph.setAsyncComputeSupported( bSupported );
        SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Simulate" ), { RenderGraphBufferUAV{ particles } }, {}, RHIQueueKind::AsyncCompute ) );
        SW_REQUIRE_TRUE( graph.compile() );
        vector<RenderGraphBarrier> listBarrier;
        collectIssuedBarriers( graph, listBarrier );
        RenderGraphExecutionContext context;
        SW_REQUIRE_TRUE( graph.execute( context ) );
        SW_REQUIRE_EQUAL( size_t{ 1 }, listBarrier.size() );
        SW_EXPECT_TRUE( listBarrier[0]._queue == ( bSupported ? RHIQueueKind::AsyncCompute : RHIQueueKind::Graphics ) );
    }

    RenderGraph graph;
    SW_EXPECT_LOG( "Renderer.GraphAsyncAttachment" );
    SW_EXPECT_FALSE( graph.addPass( hashed_string( "Bad" ), { RenderGraphRenderTarget{ hashed_string( "Color" ) } }, {}, RHIQueueKind::AsyncCompute ) );
    SW_EXPECT_LOG( "Renderer.GraphDuplicateAccess" );
    SW_EXPECT_FALSE( graph.addPass( hashed_string( "Twice" ), { RenderGraphBufferSRV{ particles }, RenderGraphBufferUAV{ particles } } ) );
    SW_EXPECT_EQUAL( 0u, graph.getNodeCount() );
}

/**
 * @brief [RenderGraphTest] 직렬 `execute` 도 패스 문맥에 그 리스트와 패스 번호(`addPass` 의 payload)를 싣는다 — 병렬 · 큐 실행과 같은 문맥이다
 * @details 결함이면 직렬 경로(추가 뷰 · 병렬 기록이 없는 백엔드)에서 `uavBarrier` · `transition` 이 리스트가 없어 조용히 아무것도 내지 않고, 받는 쪽이 이름 해시로 서술을 다시 찾는다.
 *          변이: `execute` 가 문맥을 따로 만들며 리스트를 빠뜨리면(예전 사본) 리스트 단언이, payload 를 싣지 않으면 번호 단언이 진다.
 */
SW_TEST_CASE( RenderGraphTest, SerialExecuteCarriesCommandListAndPayload )
{
    RenderGraph              graph;
    test::FakeRHICommandList cmd( 0 );
    vector<uint32>           listPayload;
    uint32                   listMismatchCount = 0;
    const auto               record            = SW_DELEGATE_LAMBDA( RenderGraphPassExecuteFn, [&]( const RenderGraphPassContext& context )
                             {
        listPayload.push_back( context._payload );
        listMismatchCount += context.getCommandList() == &cmd ? 0u : 1u;
    } );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Writer" ), { RenderGraphRenderTarget{ hashed_string( "Color" ) } }, record, RHIQueueKind::Graphics, 7 ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Reader" ), { RenderGraphTextureSRV{ hashed_string( "Color" ) } }, record, RHIQueueKind::Graphics, 3 ) );
    SW_REQUIRE_TRUE( graph.compile() );
    RenderGraphExecutionContext context;
    SW_REQUIRE_TRUE( graph.execute( context, &cmd ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listPayload.size() );
    SW_EXPECT_EQUAL( 7u, listPayload[0] );
    SW_EXPECT_EQUAL( 3u, listPayload[1] );
    SW_EXPECT_EQUAL( 0u, listMismatchCount );
}

/**
 * @brief [RenderGraphTest] 패스 안 왕복을 쓰기로 선언한 자원(깊이 프리패스 뒤 2 단계 가림처럼 장면 깊이를 깊이 쓰기로 적는 패스)은 같은 자원의 앞 · 뒤 쓰기 사이 레벨에 놓인다
 * @details `LateOcclusion` 이 장면 깊이를 깊이 쓰기로 선언하는 까닭 — 앞 쓰기(깊이 프리패스)와 같은 레벨에서 다른 패스가 그 깊이를 읽거나 쓰지 않고, 뒤 쓰기(G버퍼) 앞에 끝난다.
 *          변이: 셰이더 읽기로 선언하면 읽는 패스(데칼)와 같은 레벨에 들어가 데칼 단언이 진다.
 */
SW_TEST_CASE( RenderGraphTest, InPassRoundTripWriteSitsBetweenWriters )
{
    const hashed_string depth( "SceneDepth" );
    RenderGraph         graph;
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "DepthPrepass" ), { RenderGraphDepthWrite{ depth } } ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "LateOcclusion" ), { RenderGraphDepthWrite{ depth }, RenderGraphBufferUAV{ hashed_string( "CullArgs" ) } } ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Decal" ), { RenderGraphTextureSRV{ depth } } ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "GBuffer" ), { RenderGraphDepthWrite{ depth } } ) );
    SW_REQUIRE_TRUE( graph.compile() );
    const vector<vector<hashed_string>>& listLevel = graph.getExecutionLevels();
    SW_REQUIRE_EQUAL( size_t{ 4 }, listLevel.size() );
    SW_EXPECT_TRUE( listLevel[0][0] == hashed_string( "DepthPrepass" ) );
    SW_EXPECT_TRUE( listLevel[1][0] == hashed_string( "LateOcclusion" ) );
    SW_EXPECT_TRUE( listLevel[2][0] == hashed_string( "Decal" ) );
    SW_EXPECT_TRUE( listLevel[3][0] == hashed_string( "GBuffer" ) );
}

namespace
{
    /** @brief 그래프 @p graph 의 레벨 콜백이 받은 전이를 @p outListBarrier 에 모읍니다. */
    void collectBarriers( RenderGraph& graph, vector<RenderGraphBarrier>& outListBarrier )
    {
        graph.setLevelPrologue( RenderGraphLevelPrologueFn( [&outListBarrier]( const RenderGraphLevelContext& levelContext )
        {
            if ( levelContext._pListBarrier == nullptr )
                return;
            outListBarrier.insert( outListBarrier.end(), levelContext._pListBarrier->begin(), levelContext._pListBarrier->end() );
        } ) );
    }

    /** @brief @p listBarrier 에서 자원 @p resource 를 접근 @p access 로 옮기는 전이가 있으면 true 입니다. */
    bool hasBarrier( const vector<RenderGraphBarrier>& listBarrier, hashed_string resource, RenderGraphAccess access )
    {
        for ( const RenderGraphBarrier& barrier : listBarrier )
        {
            if ( barrier._resource == resource && barrier._access == access )
                return true;
        }
        return false;
    }
} // namespace

/**
 * @brief [RenderGraphTest] 몸통이 돌지 않은 선언(HZB UAV · 깊이 사본 렌더 타깃)이 남긴 끝 상태를 다음 프레임의 소비자는 믿지 않고 다시 옮긴다(들이지 않은 그래프 — 등록 조건이 빠진 자원)
 * @details 안전은 "다음 소비자가 선언대로 다시 옮긴다" 에 기댄다: 프레임 그래프는 프레임마다 등록 접근을 새로 심고(HZB 셰이더 읽기,
 *          깊이 사본 RW 텍스처) 짓기(HZB UAV)와 컴퓨트 읽기(깊이 사본 UAV 읽기)마다 전이를 낸다. 뷰 그래프 쪽도 다음 실행은 처음 사용마다 전이를 낸다.
 *          변이: 실행 문맥이 지난 실행의 상태를 이어 가면(초기화를 빼면) 다음 프레임 짓기의 HZB UAV 전이가 사라져 진다.
 */
SW_TEST_CASE( RenderGraphTest, DisabledReflectionLeftoverStateIsRetransitionedByNextConsumers )
{
    const hashed_string hzb( "HZB" );
    const hashed_string history( "DepthHistory" );
    const hashed_string depth( "SceneDepth" );

    // 뷰 그래프 — 뒤 단계가 짓고 셰이더 읽기로 돌린 뒤, 꺼진 반사 패스(몸통 없음)의 선언이 HZB 를 UAV, 깊이 사본을 렌더 타깃으로 옮긴다.
    RenderGraph viewGraph;
    SW_REQUIRE_TRUE( viewGraph.addPass( hashed_string( "DepthPrepass" ), { RenderGraphDepthWrite{ depth } } ) );
    SW_REQUIRE_TRUE( viewGraph.addPass( hashed_string( "LateOcclusion" ), { RenderGraphDepthWrite{ depth }, RenderGraphRenderTarget{ history }, RenderGraphBufferUAV{ hzb } } ) );
    SW_REQUIRE_TRUE( viewGraph.addPass( hashed_string( "ScreenSpaceReflection" ), { RenderGraphTextureSRV{ depth }, RenderGraphRenderTarget{ history }, RenderGraphBufferUAV{ hzb } } ) );
    SW_REQUIRE_TRUE( viewGraph.compile() );
    vector<RenderGraphBarrier> listViewBarrier;
    collectBarriers( viewGraph, listViewBarrier );
    RenderGraphExecutionContext viewContext;
    SW_REQUIRE_TRUE( viewGraph.execute( viewContext ) );
    SW_REQUIRE_TRUE( viewContext.findAccess( hzb ).has_value() );
    SW_EXPECT_TRUE( *viewContext.findAccess( hzb ) == RenderGraphAccess::UnorderedAccess );  // 꺼진 반사의 선언이 남긴 끝 상태
    SW_EXPECT_TRUE( *viewContext.findAccess( history ) == RenderGraphAccess::RenderTarget ); // 같다

    // 다음 프레임의 프레임 그래프 — 등록 접근을 새로 심는다(남은 UAV · 렌더 타깃을 믿지 않는다). 깊이 사본 패스 · 짓기 · 컴퓨트 읽기가 각자 전이를 받는다.
    RenderGraph frameGraph;
    frameGraph.importResource( hzb, RenderGraphViewableKind::Buffer, RenderGraphAccess::ShaderRead );
    frameGraph.importResource( history, RenderGraphViewableKind::Texture, RenderGraphAccess::UnorderedAccess );
    SW_REQUIRE_TRUE( frameGraph.addPass( hashed_string( "DepthHistory" ), { RenderGraphRenderTarget{ history } } ) );
    SW_REQUIRE_TRUE( frameGraph.addPass( hashed_string( "HZBBuild" ), { RenderGraphTextureUAVRead{ history }, RenderGraphBufferUAV{ hzb } } ) );
    SW_REQUIRE_TRUE( frameGraph.addPass( hashed_string( "GPUCull" ), { RenderGraphBufferSRV{ hzb } } ) );
    SW_REQUIRE_TRUE( frameGraph.compile() );
    vector<RenderGraphBarrier> listFrameBarrier;
    collectBarriers( frameGraph, listFrameBarrier );
    RenderGraphExecutionContext frameContext;
    SW_REQUIRE_TRUE( frameGraph.execute( frameContext ) );
    SW_EXPECT_TRUE( hasBarrier( listFrameBarrier, history, RenderGraphAccess::RenderTarget ) );        // 사본 그리기
    SW_EXPECT_TRUE( hasBarrier( listFrameBarrier, history, RenderGraphAccess::UnorderedAccessRead ) ); // 짓기가 RW 텍스처로 읽는다
    SW_EXPECT_TRUE( hasBarrier( listFrameBarrier, hzb, RenderGraphAccess::UnorderedAccess ) );         // 짓기 — 남은 UAV 를 믿지 않는다
    SW_EXPECT_TRUE( hasBarrier( listFrameBarrier, hzb, RenderGraphAccess::ShaderRead ) );              // 컬링이 읽는다

    // 같은 뷰 그래프를 다시 돌려도 처음 사용마다 전이가 난다 — 지난 실행의 끝 상태(UAV)를 잇지 않는다.
    listViewBarrier.clear();
    SW_REQUIRE_TRUE( viewGraph.execute( viewContext ) );
    SW_EXPECT_TRUE( hasBarrier( listViewBarrier, hzb, RenderGraphAccess::UnorderedAccess ) );
    SW_EXPECT_TRUE( hasBarrier( listViewBarrier, history, RenderGraphAccess::RenderTarget ) );
}

/**
 * @brief [RenderGraphTest] 실행 계획 — 빠진 패스는 기록도 전이도 없고, 꺼진 효과의 출력을 읽던 패스는 원본을 읽는 전이를 받는다(직렬 · 병렬 같음). 같은 상태는 계획을 다시 쓴다
 * @details 결함이면 꺼진 블룸이 안 쓰는 타깃에 전이를 내고, 그 패스를 빼기만 하면 원본의 읽기 전이가 사라져 톤맵이 렌더 타깃 상태의 장면 색을 읽는다(DX12 · Vulkan 검증 오류).
 *          변이: `addPlan` 의 이름 바꾸기를 빼면 장면 색 읽기 단언이, 빠진 패스를 남기면 블룸 전이 단언이 진다.
 */
SW_TEST_CASE( RenderGraphTest, PlanOmitsDisabledPassesAndReadsTheForwardedSource )
{
    const hashed_string color( "SceneColor" );
    const hashed_string bloom( "BloomColor" );
    const hashed_string ao( "AOColor" );
    atomic<int32>       lightingCount{ 0 };
    atomic<int32>       bloomCount{ 0 };
    atomic<int32>       tonemapCount{ 0 };
    RenderGraph         graph;
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Lighting" ), { RenderGraphRenderTarget{ color }, RenderGraphRenderTarget{ ao } }, makeCountingPass( lightingCount ), RHIQueueKind::Graphics, 0 ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Bloom" ), { RenderGraphTextureSRV{ color }, RenderGraphRenderTarget{ bloom } }, makeCountingPass( bloomCount ), RHIQueueKind::Graphics, 1 ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Tonemap" ), { RenderGraphTextureSRV{ bloom }, RenderGraphTextureSRV{ ao }, RenderGraphRenderTarget{ hashed_string( "Swapchain" ) } },
                                    makeCountingPass( tonemapCount ), RHIQueueKind::Graphics, 2 ) );
    SW_REQUIRE_TRUE( graph.compile() );

    const vector<uint8> listState{ 0, 1, 0 };
    SW_EXPECT_TRUE( graph.selectPlan( { 0, 0, 0 } ) ); // 전체 계획은 컴파일이 만든다
    SW_REQUIRE_FALSE( graph.selectPlan( listState ) );
    graph.addPlan( listState, {
                                  {bloom,           color},
                                  {   ao, hashed_string{}}
    } );
    SW_EXPECT_TRUE( graph.selectPlan( listState ) ); // 두 번째부터 다시 쓴다

    vector<RenderGraphBarrier> listBarrier; // 그래프의 콜백이 쥔다 — 마지막 실행까지 산다
    collectBarriers( graph, listBarrier );
    for ( const bool bParallel : { false, true } )
    {
        listBarrier.clear();
        RenderGraphExecutionContext context;
        test::FakeRHIDevice         device;
        test::FakeRHICommandList    cmd( 0 );
        SW_REQUIRE_TRUE( bParallel ? graph.executeParallel( context, engine::getSubsystem<TaskManager>(), device ) : graph.execute( context, &cmd ) );
        SW_EXPECT_TRUE( hasBarrier( listBarrier, color, RenderGraphAccess::RenderTarget ) );
        SW_EXPECT_TRUE( hasBarrier( listBarrier, color, RenderGraphAccess::ShaderRead ) ); // 톤맵이 블룸 대신 읽는다
        SW_EXPECT_FALSE( hasBarrier( listBarrier, bloom, RenderGraphAccess::RenderTarget ) );
        SW_EXPECT_FALSE( hasBarrier( listBarrier, bloom, RenderGraphAccess::ShaderRead ) );
        SW_EXPECT_FALSE( hasBarrier( listBarrier, ao, RenderGraphAccess::ShaderRead ) ); // 이번 뷰에 없는 출력 — 읽기가 빠진다
    }
    SW_EXPECT_EQUAL( 2, lightingCount.load() );
    SW_EXPECT_EQUAL( 0, bloomCount.load() );
    SW_EXPECT_EQUAL( 2, tonemapCount.load() );

    // 전체 계획으로 돌아오면 블룸이 다시 돈다.
    SW_REQUIRE_TRUE( graph.selectPlan( { 0, 0, 0 } ) );
    RenderGraphExecutionContext context;
    test::FakeRHICommandList    cmd( 0 );
    SW_REQUIRE_TRUE( graph.execute( context, &cmd ) );
    SW_EXPECT_EQUAL( 1, bloomCount.load() );
}

/**
 * @brief [RenderGraphTest] 이름이 원본으로 바뀌어 한 레벨에서 같은 자원을 다르게 읽게 되면 레벨을 가른다 — 병렬로 기록해도 전이가 패스마다 맞다
 * @details 컴퓨트 패스(UAV 읽기)와 그리기 패스(셰이더 읽기)가 꺼진 효과 뒤에서 같은 원본을 읽는 모양이다. 결함이면 레벨의 전이 하나가 둘 중 하나의 상태만 맞춘다.
 *          변이: 작은 레벨로 가르지 않으면 원본의 UAV 전이가 하나도 나가지 않거나 셰이더 읽기가 빠져 진다.
 */
SW_TEST_CASE( RenderGraphTest, PlanSplitsLevelWithMixedForwardedReads )
{
    const hashed_string color( "SceneColor" );
    const hashed_string effect( "EffectColor" );
    atomic<int32>       callCount{ 0 };
    RenderGraph         graph;
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Scene" ), { RenderGraphRenderTarget{ color } }, makeCountingPass( callCount ), RHIQueueKind::Graphics, 0 ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Effect" ), { RenderGraphTextureSRV{ color }, RenderGraphRenderTarget{ effect } }, makeCountingPass( callCount ), RHIQueueKind::Graphics, 1 ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Compute" ), { RenderGraphTextureUAVRead{ effect }, RenderGraphTextureUAV{ hashed_string( "OutA" ) } }, makeCountingPass( callCount ),
                                    RHIQueueKind::Graphics, 2 ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Gate" ), { RenderGraphRenderTarget{ hashed_string( "Gate" ) } }, makeCountingPass( callCount ), RHIQueueKind::Graphics, 3 ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Gate2" ), { RenderGraphTextureSRV{ hashed_string( "Gate" ) }, RenderGraphRenderTarget{ hashed_string( "Gate2" ) } }, makeCountingPass( callCount ),
                                    RHIQueueKind::Graphics, 4 ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Draw" ), { RenderGraphTextureSRV{ color }, RenderGraphTextureSRV{ hashed_string( "Gate2" ) }, RenderGraphRenderTarget{ hashed_string( "OutB" ) } },
                                    makeCountingPass( callCount ), RHIQueueKind::Graphics, 5 ) );
    SW_REQUIRE_TRUE( graph.compile() );
    SW_REQUIRE_EQUAL( size_t{ 3 }, graph.getExecutionLevels().size() );
    SW_REQUIRE_EQUAL( size_t{ 2 }, graph.getExecutionLevels()[2].size() ); // Compute · Draw 가 한 레벨

    graph.addPlan( {
                       0, 1, 0, 0, 0, 0
    },
                   { { effect, color } } );
    vector<RenderGraphBarrier> listBarrier;
    collectBarriers( graph, listBarrier );
    test::FakeRHIDevice         device;
    RenderGraphExecutionContext context;
    SW_REQUIRE_TRUE( graph.executeParallel( context, engine::getSubsystem<TaskManager>(), device ) );
    SW_EXPECT_EQUAL( 5, callCount.load() );
    SW_EXPECT_TRUE( hasBarrier( listBarrier, color, RenderGraphAccess::UnorderedAccessRead ) );
    SW_EXPECT_TRUE( hasBarrier( listBarrier, color, RenderGraphAccess::ShaderRead ) );
}

/**
 * @brief [RenderGraphTest] UAV 쓰기 뒤의 UAV 쓰기 · UAV 읽기는 상태가 그대로라도 UAV 배리어를 받고, UAV 읽기끼리는 아무것도 받지 않는다(텍스처 · 버퍼 같음)
 * @details 컴퓨트 패스 둘이 같은 첨부를 이어 쓰거나 읽을 때 앞 쓰기를 기다리는 길이다 — 레벨 콜백이 텍스처면 `uavBarrierTexture`, 버퍼면 `uavBarrier` 를 낸다.
 *          변이: UAV 읽기를 UAV 배리어 조건에서 빼면 둘째 단언이, UAV 읽기끼리 배리어를 내면 셋째 단언이 진다.
 */
SW_TEST_CASE( RenderGraphTest, UnorderedAccessReadWaitsForPriorWrite )
{
    const hashed_string target( "ComputeColor" );
    RenderGraph         graph;
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Writer" ), { RenderGraphTextureUAV{ target } } ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "ReaderA" ), { RenderGraphTextureUAVRead{ target }, RenderGraphTextureUAV{ hashed_string( "OutA" ) } } ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "ReaderB" ), { RenderGraphTextureUAVRead{ target }, RenderGraphTextureUAV{ hashed_string( "OutB" ) } } ) );
    SW_REQUIRE_TRUE( graph.compile() );
    vector<RenderGraphBarrier> listBarrier;
    collectBarriers( graph, listBarrier );
    RenderGraphExecutionContext context;
    SW_REQUIRE_TRUE( graph.execute( context ) );
    uint32 uavBarrierCount = 0;
    uint32 targetCount     = 0;
    for ( const RenderGraphBarrier& barrier : listBarrier )
    {
        if ( barrier._resource != target )
            continue;
        ++targetCount;
        uavBarrierCount += barrier._bUAVBarrier != 0 ? 1u : 0u;
    }
    SW_EXPECT_EQUAL( 2u, targetCount );     // 쓰기 앞 전이 하나 + 읽기 앞 UAV 배리어 하나(두 읽기는 한 레벨이라 한 번)
    SW_EXPECT_EQUAL( 1u, uavBarrierCount ); // 쓰기 → 읽기
    // 두 읽기는 같은 레벨이다(쓰기가 아니라 간선이 없다).
    SW_REQUIRE_EQUAL( size_t{ 2 }, graph.getExecutionLevels().size() );
    SW_EXPECT_EQUAL( size_t{ 2 }, graph.getExecutionLevels()[1].size() );
}

/**
 * @brief [RenderGraphTest] 그래프가 처음 병렬로 도는 실행도 `allocateBuffers` 가 마련한 버퍼를 그대로 쓴다 — 표에 적은 번호가 풀리지 않는다
 * @details 첫 병렬 실행은 커맨드 리스트를 처음 만드는 "디바이스 바뀜" 이라 예전에는 버퍼 풀까지 놓았다(디퍼드 전환 뒤 타일 분류가 풀린 UAV 를 걸었다).
 *          변이: 리스트를 버릴 때 풀까지 놓으면 실행 뒤 칸이 0 이다.
 */
SW_TEST_CASE( RenderGraphTest, FirstParallelRunKeepsTheAllocatedBuffers )
{
    const hashed_string buffer( "TransientParallel" );
    RenderGraph         graph;
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Write" ), { RenderGraphBufferUAV{ buffer } } ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "Read" ), { RenderGraphBufferSRV{ buffer }, RenderGraphTextureUAV{ hashed_string( "Out" ) } } ) );
    SW_REQUIRE_TRUE( graph.compile() );
    test::FakeRHIBufferFactory factory;
    factory._bufferSRV = RHIBufferSRVIndex{ 1 };
    test::FakeRHIDevice device;
    device._pResourceFactory = &factory;
    FrameBufferTable table;
    graph.importResources( table );
    graph.createBuffer( buffer, RenderGraphBufferDesc{ sizeof( uint32 ), 64, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource } );
    graph.allocateBuffers( device, table );
    SW_REQUIRE_EQUAL( 1u, graph.getBufferSlotCount() );
    RenderGraphExecutionContext context;
    SW_REQUIRE_TRUE( graph.executeParallel( context, engine::getSubsystem<TaskManager>(), device ) );
    SW_EXPECT_EQUAL( 1u, graph.getBufferSlotCount() );
    SW_EXPECT_EQUAL( 1u, factory._structuredCreateCount );
    graph.releaseDeviceResources();
}

/**
 * @brief [RenderGraphTest] 그래프가 만든 버퍼는 레벨 구간이 겹치지 않으면 한 버퍼를 나눠 쓰고, 다음 실행(다음 뷰)도 같은 버퍼를 다시 쓴다
 * @details 사슬 Write A → Read A(Write B) → Read B(Write C) → Read C 에서 A · B, B · C 는 한 레벨에서 겹치고 A · C 는 겹치지 않는다.
 *          변이: 겹침을 보지 않으면 칸이 하나, 나눠 쓰지 않으면 셋이다. 등록을 들이면 첫 사용의 전이가 사라진다.
 */
SW_TEST_CASE( RenderGraphTest, CreatedBuffersShareOneBufferWhenLevelsDoNotOverlap )
{
    const hashed_string bufferA( "TransientA" );
    const hashed_string bufferB( "TransientB" );
    const hashed_string bufferC( "TransientC" );
    RenderGraph         graph;
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "WriteA" ), { RenderGraphBufferUAV{ bufferA } } ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "WriteB" ), { RenderGraphBufferSRV{ bufferA }, RenderGraphBufferUAV{ bufferB } } ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "WriteC" ), { RenderGraphBufferSRV{ bufferB }, RenderGraphBufferUAV{ bufferC } } ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "ReadC" ), { RenderGraphBufferSRV{ bufferC }, RenderGraphTextureUAV{ hashed_string( "Out" ) } } ) );
    SW_REQUIRE_TRUE( graph.compile() );
    vector<RenderGraphBarrier> listBarrier;
    collectIssuedBarriers( graph, listBarrier );

    test::FakeRHIBufferFactory factory;
    factory._bufferSRV = RHIBufferSRVIndex{ 1 };
    test::FakeRHIDevice device;
    device._pResourceFactory = &factory;
    constexpr RenderGraphBufferDesc kDesc{ sizeof( uint32 ), 64, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource };
    RHIBufferHandle                 firstRunA{};
    for ( uint32 run = 0; run < 2; ++run )
    {
        FrameBufferTable table;
        graph.importResources( table );
        for ( const hashed_string& name : { bufferC, bufferA, bufferB, hashed_string( "NoPassDeclaresIt" ) } )
        {
            graph.createBuffer( name, kDesc );
        }
        graph.allocateBuffers( device, table );
        SW_EXPECT_EQUAL( 2u, graph.getBufferSlotCount() );
        SW_EXPECT_EQUAL( 2u, factory._structuredCreateCount );
        SW_EXPECT_TRUE( table.findBuffer( bufferA, 0 ) == table.findBuffer( bufferC, 0 ) );
        SW_EXPECT_TRUE( table.findBuffer( bufferA, 0 ) != table.findBuffer( bufferB, 0 ) );
        SW_EXPECT_TRUE( table.findEntry( hashed_string( "NoPassDeclaresIt" ), 0 ) == nullptr );
        if ( run == 1 )
            SW_EXPECT_TRUE( table.findBuffer( bufferA, 0 ) == firstRunA );
        firstRunA = table.findBuffer( bufferA, 0 );
        listBarrier.clear();
        RenderGraphExecutionContext context;
        SW_REQUIRE_TRUE( graph.execute( context ) );
        const vector<RenderGraphBarrier> listA = findBarriersFor( listBarrier, bufferA );
        SW_REQUIRE_EQUAL( size_t{ 2 }, listA.size() ); // 쓰기 앞 · 읽기 앞 — 들이지 않아 되돌림이 없다
        SW_EXPECT_TRUE( listA[0]._access == RenderGraphAccess::UnorderedAccess );
    }
    graph.releaseDeviceResources();
    SW_EXPECT_EQUAL( 0u, graph.getBufferSlotCount() );
}
