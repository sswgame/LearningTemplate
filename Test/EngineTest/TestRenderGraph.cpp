#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Renderer/Graph/RenderGraph.h"

#include "EngineTest/RHIFakeDevice.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief 패스마다 몇 번 돌았는지 세고, 받은 가짜 리스트에 자기 이름을 적는 콜백을 만듭니다. */
    sw::RenderGraphPassExecuteFn makeCountingPass( sw::atomic<int32>& callCount )
    {
        return SW_DELEGATE_LAMBDA( sw::RenderGraphPassExecuteFn, [&callCount]( const sw::RenderGraphPassContext& ctx )
        {
            callCount.fetch_add( 1 );
            if ( ctx._pCmdList != nullptr )
                static_cast<test::FakeRHICommandList*>( ctx._pCmdList )->_passName = ctx._passName;
        } );
    }
} // namespace

// Engine_Renderer — RenderGraph 가 배리어를 **바뀐 것만** 추론하는지, 읽고-쓰는 자원의 수명을 맞추는지.
// ------------------------------------------------------------------------------
// 9-1) RenderGraph 배리어 추론 — 실제로 바뀌는 전이만 나오는지
// ------------------------------------------------------------------------------
/**
 * @brief [RenderGraphTest] 그래프가 상태를 들고 있다가 **바뀌는 전이만** 내는지 (GPU 불필요).
 * @details 레벨이 읽고 쓰는 자원 **이름을 전부** 넘기면 같은 자원을 세 패스가 읽을 때 읽기 전이를 세 번 걸고,
 *          이미 그 상태인 것도 다시 건다. 전이 자체는 백엔드가 걸러 주지만(DX12 는 상태가 같으면 배리어를 안 쏜다)
 *          그건 백엔드마다 사정이 다르고, 무엇보다 "누가 상태를 아는가" 가 흐려진다 — 배리어를 병렬 기록 스레드가
 *          정하는 구조는 깨지기 쉽다.
 *
 *          그래서 그래프가 정본이다. 여기서는 그 추론만 따로 본다(커맨드 리스트 없이).
 */
SW_TEST_CASE( RenderGraphTest, RenderGraphInfersOnlyChangedBarriers )
{
    sw::RenderGraph         graph;
    const sw::hashed_string colorBuffer( "ColorBuffer" );
    const sw::hashed_string blurBuffer( "BlurBuffer" );
    const sw::hashed_string uiBuffer( "UiBuffer" );

    graph.addPass( sw::hashed_string( "PassA_Write" ), {}, { colorBuffer } );
    graph.addPass( sw::hashed_string( "PassB_Read" ), { colorBuffer }, { blurBuffer } );
    graph.addPass( sw::hashed_string( "PassC_ReadSame" ), { colorBuffer }, { uiBuffer } );
    SW_ASSERT_TRUE( graph.compile() );

    sw::vector<sw::RenderGraphBarrier> listIssuedBarrier;
    graph.setLevelPrologue( sw::RenderGraphLevelPrologueFn( [&listIssuedBarrier]( const sw::RenderGraphLevelContext& levelCtx )
    {
        if ( levelCtx._pListBarrier == nullptr )
            return;
        for ( const sw::RenderGraphBarrier& barrier : *levelCtx._pListBarrier )
        {
            listIssuedBarrier.push_back( barrier );
        }
    } ) );

    auto countFor = [&listIssuedBarrier]( sw::hashed_string resource ) -> uint32
    {
        uint32 count{ 0 };
        for ( const sw::RenderGraphBarrier& barrier : listIssuedBarrier )
        {
            if ( barrier._resource == resource )
                ++count;
        }
        return count;
    };

    // --- 첫 프레임: 전부 처음 보는 자원이다.
    sw::RenderGraphExecutionContext context;
    SW_ASSERT_TRUE( graph.execute( context ) );

    // 여기가 핵심이다. PassB 가 ColorBuffer 를 읽기로 바꿔 놓았으므로 PassC 는 **낼 것이 없다**.
    // 이름을 그대로 넘기면 여기서 두 번 나온다.
    SW_EXPECT_EQUAL( uint32( 2 ), countFor( colorBuffer ) );
    SW_EXPECT_EQUAL( uint32( 1 ), countFor( blurBuffer ) );
    SW_EXPECT_EQUAL( uint32( 1 ), countFor( uiBuffer ) );

    // 첫 전이는 Undefined 에서 시작하고, 그다음이 쓰기 → 읽기다.
    SW_ASSERT_TRUE( listIssuedBarrier.size() >= 2 );
    SW_EXPECT_TRUE( listIssuedBarrier[0]._resource == colorBuffer );
    SW_EXPECT_TRUE( listIssuedBarrier[0]._before == sw::RenderGraphResourceState::Undefined );
    SW_EXPECT_TRUE( listIssuedBarrier[0]._after == sw::RenderGraphResourceState::Write );

    // --- 두 번째 프레임: **똑같이 나와야 한다.** 그래프는 프레임 시작에 일부러 잊는다.
    // 전이는 그래프 밖에서도 일어나므로(선언 안 한 텍스처를 registerPassTexture 로 걸거나 리드백이
    // 상태를 되돌린다) 지난 프레임의 믿음을 이어 가면 필요한 배리어를 건너뛴다. 여기서 버는 것은
    // 프레임 사이가 아니라 **한 프레임 안의 중복**이다 — 그게 위의 PassC 가 아무것도 안 내는 이유다.
    const size_t firstFrameCount = listIssuedBarrier.size();
    listIssuedBarrier.clear();
    SW_ASSERT_TRUE( graph.execute( context ) );

    SW_EXPECT_EQUAL( uint32( 2 ), countFor( colorBuffer ) );
    SW_EXPECT_EQUAL( uint32( 1 ), countFor( blurBuffer ) );
    SW_EXPECT_EQUAL( uint32( 1 ), countFor( uiBuffer ) );
    SW_EXPECT_TRUE_MSG( listIssuedBarrier.size() == firstFrameCount,
                        "프레임마다 결과가 달라진다 — 그래프가 프레임 사이의 상태를 이어서 보고 있다" );

    // 수명도 컴파일 산출물이다 — ColorBuffer 는 0 번 패스에서 나서 2 번 패스까지 산다.
    const sw::RenderGraphResourceLifetime* pLife = graph.findResourceLifetime( colorBuffer );
    SW_ASSERT_TRUE( pLife != nullptr );
    SW_EXPECT_EQUAL( size_t( 0 ), pLife->_firstPassIndex );
    SW_EXPECT_EQUAL( size_t( 2 ), pLife->_lastPassIndex );
    SW_EXPECT_TRUE( pLife->_bWritten );
    SW_EXPECT_TRUE( pLife->_bRead );
}

// ------------------------------------------------------------------------------
// 9) RenderGraph Read-Modify-Write 및 Resource Lifetime 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( RenderGraphTest, RenderGraphReadModifyWriteAndLifetimes )
{
    sw::RenderGraph graph;
    graph.addPass( sw::hashed_string( "PassA_Geometry" ), {}, { sw::hashed_string( "ColorBuffer" ) } );
    graph.addPass( sw::hashed_string( "PassB_PostProcess" ), { sw::hashed_string( "ColorBuffer" ) }, { sw::hashed_string( "ColorBuffer" ) } );
    graph.addPass( sw::hashed_string( "PassC_UIOverlay" ), { sw::hashed_string( "ColorBuffer" ) }, { sw::hashed_string( "FinalOutput" ) } );

    SW_EXPECT_TRUE( graph.compile() );

    const auto& order = graph.getExecutionOrder();
    SW_ASSERT_EQUAL( size_t( 3 ), order.size() );
    SW_EXPECT_EQUAL( sw::string( "PassA_Geometry" ), sw::string( order[0].c_str() ) );
    SW_EXPECT_EQUAL( sw::string( "PassB_PostProcess" ), sw::string( order[1].c_str() ) );
    SW_EXPECT_EQUAL( sw::string( "PassC_UIOverlay" ), sw::string( order[2].c_str() ) );

    // 수명은 compile() 이 계산해 둔다 — 실행 순서가 정해지기 전에 세면 뜻이 없는 값이 나온다.
    const auto& listLifetimes = graph.getResourceLifetimes();
    SW_EXPECT_TRUE( listLifetimes.empty() == false );

    bool bFoundColorBuffer = false;
    for ( const auto& life : listLifetimes )
    {
        if ( life._name == sw::hashed_string( "ColorBuffer" ) )
        {
            bFoundColorBuffer = true;
            SW_EXPECT_EQUAL( size_t( 0 ), life._firstPassIndex );
            SW_EXPECT_EQUAL( size_t( 2 ), life._lastPassIndex );
            SW_EXPECT_TRUE( life._bWritten );
            SW_EXPECT_TRUE( life._bRead );
        }
    }
    SW_EXPECT_TRUE( bFoundColorBuffer );
}

/**
 * @brief [RenderGraphTest] 읽은 자원을 뒤에서 다시 쓰는 패스는 그 읽기 뒤에 돈다(Write-after-Read)
 * @details W 가 쓰고 A 가 읽고 B 가 다시 쓸 때 간선이 W→B(쓰기 사슬) · W→A(읽기)뿐이면 A 와 B 가 같은 레벨이 되어 B 가 먼저
 *          줄을 설 수 있다 — A 가 B 의 출력을 읽는다. UI · 오버레이가 블룸이 읽은 SceneColor 를 다시 쓰는 모양이 그렇다.
 */
SW_TEST_CASE( RenderGraphTest, ReaderRunsBeforeTheNextWriterOfItsInput )
{
    sw::RenderGraph         graph;
    const sw::hashed_string sceneColor( "SceneColor" );
    graph.addPass( sw::hashed_string( "W_Geometry" ), {}, { sceneColor } );
    graph.addPass( sw::hashed_string( "A_Bloom" ), { sceneColor }, { sw::hashed_string( "BloomOut" ) } );
    graph.addPass( sw::hashed_string( "B_Overlay" ), {}, { sceneColor } );
    SW_ASSERT_TRUE( graph.compile() );

    const auto& order = graph.getExecutionOrder();
    SW_ASSERT_EQUAL( size_t( 3 ), order.size() );
    SW_EXPECT_STREQ( "W_Geometry", order[0].c_str() );
    SW_EXPECT_STREQ( "A_Bloom", order[1].c_str() );
    SW_EXPECT_STREQ( "B_Overlay", order[2].c_str() );

    // 레벨도 갈린다 — 같은 레벨이면 병렬 기록에서 둘이 겨룬다.
    const auto& listLevel = graph.getExecutionLevels();
    SW_EXPECT_EQUAL( size_t( 3 ), listLevel.size() );

    // 소비자를 먼저 선언해도 된다(앞에 쓰는 이가 없으면 첫 쓰기가 생산자). 그 다음 쓰기만 소비자 뒤로 간다 — 선언 순서로 고르면
    // 생산자 자신을 골라 순환이 된다.
    sw::RenderGraph earlyConsumer;
    earlyConsumer.addPass( sw::hashed_string( "C_Reads" ), { sceneColor }, { sw::hashed_string( "COut" ) } );
    earlyConsumer.addPass( sw::hashed_string( "W1_Writes" ), {}, { sceneColor } );
    earlyConsumer.addPass( sw::hashed_string( "W2_Writes" ), {}, { sceneColor } );
    SW_ASSERT_TRUE( earlyConsumer.compile() );
    const auto& earlyOrder = earlyConsumer.getExecutionOrder();
    SW_ASSERT_EQUAL( size_t( 3 ), earlyOrder.size() );
    SW_EXPECT_STREQ( "W1_Writes", earlyOrder[0].c_str() );
    SW_EXPECT_STREQ( "C_Reads", earlyOrder[1].c_str() );
    SW_EXPECT_STREQ( "W2_Writes", earlyOrder[2].c_str() );
}

/**
 * @brief [RenderGraphTest] 같은 이름의 패스는 두 번째를 버린다 — 한 패스가 두 번 돌고 다른 하나가 안 도는 일이 없다
 */
SW_TEST_CASE( RenderGraphTest, DuplicatePassNameIsRejected )
{
    sw::RenderGraph graph;
    uint32          firstCount  = 0;
    uint32          secondCount = 0;
    SW_EXPECT_TRUE( graph.addPass( sw::hashed_string( "Dup" ), {}, { sw::hashed_string( "Out" ) },
                                   SW_DELEGATE_LAMBDA( sw::RenderGraphPassExecuteFn, [&firstCount]( const sw::RenderGraphPassContext& )
    { ++firstCount; } ) ) );
    {
        test::ScopedDefensiveTestLog expected( "the same pass name declared twice" );
        SW_EXPECT_FALSE( graph.addPass( sw::hashed_string( "Dup" ), {}, { sw::hashed_string( "Out2" ) },
                                        SW_DELEGATE_LAMBDA( sw::RenderGraphPassExecuteFn, [&secondCount]( const sw::RenderGraphPassContext& )
        { ++secondCount; } ) ) );
    }
    SW_ASSERT_TRUE( graph.compile() );
    SW_EXPECT_EQUAL( size_t( 1 ), graph.getExecutionOrder().size() );

    sw::RenderGraphExecutionContext context;
    SW_ASSERT_TRUE( graph.execute( context ) );
    SW_EXPECT_EQUAL( 1u, firstCount );
    SW_EXPECT_EQUAL( 0u, secondCount );
}

/**
 * @brief [RenderGraphTest] 순환이면 서로 기다리는 패스를 이름으로 말한다 — "몇 개 중 몇 개" 가 아니라
 * @details A 가 X 를 읽고 Y 를 쓰고, B 가 Y 를 읽고 X 를 쓰면 둘은 서로를 기다린다. 경고가 개수(`1/3 active passes scheduled.`)만 말하면
 *          파이프라인 XML 의 입출력을 손으로 따라가야 한다.
 */
SW_TEST_CASE( RenderGraphTest, CycleNamesThePassesThatWaitOnEachOther )
{
    sw::RenderGraph         graph;
    const sw::hashed_string resourceX( "CycleX" );
    const sw::hashed_string resourceY( "CycleY" );
    graph.addPass( sw::hashed_string( "Free" ), {}, { sw::hashed_string( "FreeOut" ) } );
    graph.addPass( sw::hashed_string( "LoopA" ), { resourceX }, { resourceY } );
    graph.addPass( sw::hashed_string( "LoopB" ), { resourceY }, { resourceX } );

    test::ScopedLogCollector logs;
    {
        test::ScopedDefensiveTestLog expected( "a render graph whose passes wait on each other" );
        SW_EXPECT_FALSE( graph.compile() );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "'LoopA' waits on 'LoopB'" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "'LoopB' waits on 'LoopA'" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "'Free'" ) == 0, logs.joined().c_str() );
    SW_EXPECT_TRUE( graph.describeCompiledOrder().find( "not compiled" ) != sw::string::npos );
}

/**
 * @brief [RenderGraphTest] 컴파일된 순서를 레벨 · 패스 · 읽고 쓰는 자원으로 적는다(`-gv_dumpRenderGraph` 가 로그로 남기는 글)
 */
SW_TEST_CASE( RenderGraphTest, DescribeCompiledOrderListsLevelsAndResources )
{
    sw::RenderGraph         graph;
    const sw::hashed_string sceneColor( "SceneColor" );
    graph.addPass( sw::hashed_string( "Geometry" ), {}, { sceneColor } );
    graph.addPass( sw::hashed_string( "Bloom" ), { sceneColor }, { sw::hashed_string( "BloomOut" ) } );
    SW_ASSERT_TRUE( graph.compile() );

    const sw::string text = graph.describeCompiledOrder();
    for ( const utf8* pExpected : { "2 passes in 2 levels", "level 0", "Geometry  reads [] writes [SceneColor]", "level 1",
                                    "Bloom  reads [SceneColor] writes [BloomOut]" } )
    {
        SW_EXPECT_TRUE_MSG( text.find( pExpected ) != sw::string::npos, ( sw::string( "missing: " ) + pExpected + "\n" + text ).c_str() );
    }
}

/**
 * @brief [RenderGraphTest] 병렬 기록은 패스마다 한 번 기록하고, 레벨 순서로 닫힌 리스트를 제출한다(가짜 디바이스 — GPU 불필요)
 * @details 이 경로는 디바이스가 있어야 돌아서 GPU 시험에서만 지나갔다. A · C 는 레벨 0, A 의 출력을 읽는 B 는 레벨 1 이다.
 */
SW_TEST_CASE( RenderGraphTest, ParallelExecutionRecordsEachPassOnceInLevelOrder )
{
    sw::atomic<int32> countA{ 0 };
    sw::atomic<int32> countB{ 0 };
    sw::atomic<int32> countC{ 0 };
    sw::RenderGraph   graph;
    graph.addPass( sw::hashed_string( "A" ), {}, { sw::hashed_string( "OutA" ) }, makeCountingPass( countA ) );
    graph.addPass( sw::hashed_string( "B" ), { sw::hashed_string( "OutA" ) }, { sw::hashed_string( "OutB" ) }, makeCountingPass( countB ) );
    graph.addPass( sw::hashed_string( "C" ), {}, { sw::hashed_string( "OutC" ) }, makeCountingPass( countC ) );
    SW_ASSERT_TRUE( graph.compile() );

    test::FakeRHIDevice             device;
    sw::RenderGraphExecutionContext context;
    SW_ASSERT_TRUE( graph.executeParallel( context, &sw::engine::getTaskManager(), &device ) );

    SW_EXPECT_EQUAL( 1, countA.load() );
    SW_EXPECT_EQUAL( 1, countB.load() );
    SW_EXPECT_EQUAL( 1, countC.load() );
    SW_ASSERT_EQUAL( size_t( 3 ), device._listExecuted.size() );
    SW_EXPECT_TRUE( device._listExecuted[2]->_passName == sw::hashed_string( "B" ) ); // 레벨 0 의 둘 다음
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
    sw::atomic<int32> countA{ 0 };
    sw::atomic<int32> countB{ 0 };
    sw::atomic<int32> countC{ 0 };
    sw::RenderGraph   graph;
    graph.addPass( sw::hashed_string( "A" ), {}, { sw::hashed_string( "OutA" ) }, makeCountingPass( countA ) );
    graph.addPass( sw::hashed_string( "B" ), { sw::hashed_string( "OutA" ) }, { sw::hashed_string( "OutB" ) }, makeCountingPass( countB ) );
    graph.addPass( sw::hashed_string( "C" ), {}, { sw::hashed_string( "OutC" ) }, makeCountingPass( countC ) );
    SW_ASSERT_TRUE( graph.compile() );

    test::FakeRHIDevice device;
    device._maxCreatable = 2; // 레벨 0 의 둘은 만들고, 레벨 1 의 B 에서 실패한다
    sw::RenderGraphExecutionContext context;
    {
        test::ScopedDefensiveTestLog expected( "a device that cannot make more command lists" );
        SW_EXPECT_FALSE( graph.executeParallel( context, &sw::engine::getTaskManager(), &device ) );
    }
    SW_EXPECT_EQUAL( size_t( 0 ), device._listExecuted.size() );
    SW_EXPECT_EQUAL( 0, countA.load() );
    SW_EXPECT_EQUAL( 0, countB.load() );
    SW_EXPECT_EQUAL( 0, countC.load() );
}
