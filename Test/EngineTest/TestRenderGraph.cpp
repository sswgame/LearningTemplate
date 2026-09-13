#include "pch.h"

#include "Engine/Graphics/Renderer/Graph/RenderGraph.h"

#include "TestFramework/TestFramework.h"

// Engine_Renderer — RenderGraph 가 배리어를 **바뀐 것만** 추론하는지, 읽고-쓰는 자원의 수명을 맞추는지.
// ------------------------------------------------------------------------------
// 9-1) RenderGraph 배리어 추론 — 실제로 바뀌는 전이만 나오는지
// ------------------------------------------------------------------------------
/**
 * @brief [RenderGraphTest] 그래프가 상태를 들고 있다가 **바뀌는 전이만** 내는지 (GPU 불필요).
 * @details 예전엔 웨이브가 읽고 쓰는 자원 **이름을 전부** 넘겼다. 그래서 같은 자원을 세 패스가 읽으면
 *          읽기 전이를 세 번 걸었고, 이미 그 상태인 것도 다시 걸었다. 전이 자체는 백엔드가 걸러 주지만
 *          (DX12 는 상태가 같으면 배리어를 안 쏜다) 그건 백엔드마다 사정이 다른 이야기고, 무엇보다
 *          "누가 상태를 아는가" 가 흐려진다 — 배리어를 병렬 기록 스레드가 정하던 구조는 실제로 여러 번 깨졌다.
 *
 *          그래서 그래프가 정본이 된다. 여기서는 그 추론만 따로 본다(커맨드 리스트 없이).
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

    sw::vector<sw::RenderGraphBarrier> listSeen;
    graph.setWavePrologue( sw::RenderGraphWavePrologueFn( [&listSeen]( const sw::RenderGraphWaveContext& waveCtx )
    {
        if ( waveCtx._pListBarrier == nullptr )
            return;
        for ( const sw::RenderGraphBarrier& barrier : *waveCtx._pListBarrier )
        {
            listSeen.push_back( barrier );
        }
    } ) );

    auto countFor = [&listSeen]( sw::hashed_string resource ) -> uint32
    {
        uint32 count{ 0 };
        for ( const sw::RenderGraphBarrier& barrier : listSeen )
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
    // 이름을 그대로 넘기던 시절엔 여기서 두 번 나왔다.
    SW_EXPECT_EQUAL( uint32( 2 ), countFor( colorBuffer ) );
    SW_EXPECT_EQUAL( uint32( 1 ), countFor( blurBuffer ) );
    SW_EXPECT_EQUAL( uint32( 1 ), countFor( uiBuffer ) );

    // 첫 전이는 Undefined 에서 시작하고, 그다음이 쓰기 → 읽기다.
    SW_ASSERT_TRUE( listSeen.size() >= 2 );
    SW_EXPECT_TRUE( listSeen[0]._resource == colorBuffer );
    SW_EXPECT_TRUE( listSeen[0]._before == sw::RenderGraphResourceState::Undefined );
    SW_EXPECT_TRUE( listSeen[0]._after == sw::RenderGraphResourceState::Write );

    // --- 두 번째 프레임: **똑같이 나와야 한다.** 그래프는 프레임 시작에 일부러 잊는다.
    // 전이는 그래프 밖에서도 일어나므로(선언 안 한 텍스처를 registerPassTexture 로 걸거나 리드백이
    // 상태를 되돌린다) 지난 프레임의 믿음을 이어 가면 필요한 배리어를 건너뛴다. 여기서 버는 것은
    // 프레임 사이가 아니라 **한 프레임 안의 중복**이다 — 그게 위의 PassC 가 아무것도 안 내는 이유다.
    const size_t firstFrameCount = listSeen.size();
    listSeen.clear();
    SW_ASSERT_TRUE( graph.execute( context ) );

    SW_EXPECT_EQUAL( uint32( 2 ), countFor( colorBuffer ) );
    SW_EXPECT_EQUAL( uint32( 1 ), countFor( blurBuffer ) );
    SW_EXPECT_EQUAL( uint32( 1 ), countFor( uiBuffer ) );
    SW_EXPECT_TRUE_MSG( listSeen.size() == firstFrameCount,
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

    // 수명은 compile() 이 계산해 둔다 — 부를 때마다 다시 세던 함수는 지웠다(아무도 부르지 않았고,
    // 실행 순서가 정해지기 전에 부르면 뜻이 없는 값이 나왔다).
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
