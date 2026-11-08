/**
 * @file TestRenderGraph.cpp
 * @brief 렌더 그래프(`RenderGraph`) 테스트 — 바뀐 배리어만 추론, 읽고-쓰는 자원의 수명, 레벨 순서, 순환 보고, 병렬 기록(가짜 디바이스)입니다.
 */
#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Task/TaskManager.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
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
            if ( context._pCmdList != nullptr )
                static_cast<test::FakeRHICommandList*>( context._pCmdList )->_passName = context._passName;
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
 * @brief [RenderGraphTest] 그래프가 상태를 들고 있다가 **바뀌는 전이만** 낸다
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
        SW_EXPECT_TRUE_MSG( text.find( pExpected ) != string::npos, ( string( "missing: " ) + pExpected + "\n" + text ).c_str() );
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
    SW_REQUIRE_TRUE( graph.executeParallel( context, &engine::getSubsystem<TaskManager>(), &device ) );

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
    SW_REQUIRE_TRUE( graph.executeParallel( context, &engine::getSubsystem<TaskManager>(), &device ) );

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
    SW_EXPECT_FALSE( graph.executeParallel( context, &engine::getSubsystem<TaskManager>(), &device ) );
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
    SW_REQUIRE_TRUE( graph.executeParallel( context, &stoppedTaskManager, &device ) );
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
        graph.addPass( hashed_string( "Light" ), {}, { hashed_string( "LightGrid" ) }, makeCountingPass( callCount ), RHIQueueType::AsyncCompute );
        graph.addPass( hashed_string( "Anim" ), {}, { hashed_string( "InstanceBuffer" ) }, makeCountingPass( callCount ) );
        graph.addPass( hashed_string( "Occl" ), { hashed_string( "DepthHistory" ) }, { hashed_string( "OcclusionTiles" ) }, makeCountingPass( callCount ), RHIQueueType::AsyncCompute );
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
    SW_EXPECT_TRUE( steps[0]._queue == RHIQueueType::Graphics );
    SW_EXPECT_TRUE( steps[1]._queue == RHIQueueType::AsyncCompute && steps[1]._bBeginsBatch != 0 && steps[1]._bEndsBatch != 0 && steps[1]._batch == 0 );
    SW_EXPECT_TRUE( steps[2]._listJoinBatch.empty() ); // Anim 은 컴퓨트와 겹친다
    SW_EXPECT_TRUE( steps[3]._queue == RHIQueueType::AsyncCompute && steps[3]._batch == 1 );
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
        SW_EXPECT_TRUE( step._queue == RHIQueueType::Graphics );
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
    graph.addPass( hashed_string( "Probe" ), {}, { hashed_string( "ProbeIrradiance" ) }, {}, RHIQueueType::AsyncCompute );
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
