/**
 * @file TestRenderDebuggerViewModel.cpp
 * @brief 렌더 디버거 창 모델 테스트 — 그래프 배치(노드 수 = 패스 수, 열 = 레벨, 쓴 → 읽은 링크), 버퍼 표 풀이, ViewModel 의 프레임 디버거 단계 · 그래프 · 버퍼 흐름(가짜 렌더 스레드).
 */
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/RenderDebugger/Model/BufferViewLayout.h"
#include "Editor/Panels/RenderDebugger/Model/RenderDebuggerViewModel.h"
#include "Editor/Panels/RenderDebugger/Model/RenderGraphLayout.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief A 가 Color 를 쓰고 B · C 가 읽는 그래프의 사본입니다(레벨 2). */
    RenderGraphSnapshot makeThreePassSnapshot()
    {
        RenderGraph graph;
        graph.addPass( hashed_string( "A" ), {}, { hashed_string( "Color" ) } );
        graph.addPass( hashed_string( "B" ), { hashed_string( "Color" ) }, { hashed_string( "Blur" ) } );
        graph.addPass( hashed_string( "C" ), { hashed_string( "Color" ) }, { hashed_string( "UI" ) } );
        (void)graph.compile();
        return RenderGraphSnapshot::build( graph, "view" );
    }
} // namespace

SW_TEST_CASE( RenderGraphLayoutTest, NodesMatchPassesAndColumnsMatchLevels )
{
    const RenderGraphSnapshot snapshot = makeThreePassSnapshot();
    SW_REQUIRE_EQUAL( size_t{ 3 }, snapshot._listPass.size() );
    const RenderGraphLayout layout = RenderGraphLayout::make( snapshot );
    SW_EXPECT_EQUAL( snapshot._listPass.size(), layout._listNode.size() );
    SW_EXPECT_EQUAL( snapshot._levelCount, layout._columnCount );
    SW_EXPECT_EQUAL( 0u, layout._listNode[0]._column );
    SW_EXPECT_EQUAL( 1u, layout._listNode[1]._column );
    SW_EXPECT_EQUAL( 1u, layout._listNode[2]._row ); // B 와 C 는 같은 레벨의 두 행
    SW_EXPECT_NEAR( RenderGraphLayout::kColumnSpacing, layout._listNode[1]._position._x, 1.0e-4f );
    // 쓴 A → 읽은 B · C, 두 링크
    SW_REQUIRE_EQUAL( size_t{ 2 }, layout._listLink.size() );
    SW_EXPECT_EQUAL( 0u, layout._listLink[0]._fromPass );
    SW_EXPECT_EQUAL( 1u, layout._listLink[0]._toPass );
    SW_EXPECT_EQUAL( 2u, layout._listLink[1]._toPass );
    // id 는 노드 · 핀 · 링크가 겹치지 않는다
    SW_EXPECT_TRUE( RenderGraphLayout::getOutputPinID( 0 ) != RenderGraphLayout::getInputPinID( 1 ) );
    SW_EXPECT_TRUE( RenderGraphLayout::getLinkID( 1 ) < RenderGraphLayout::getNodeID( 0 ) );
}

SW_TEST_CASE( BufferViewLayoutTest, RowsFollowStrideAndScalar )
{
    // float2 원소 둘(HZB 최소 · 최대 깊이 꼴): (0.25, 1), (0.5, 0.75)
    const float32 arrValue[4]{ 0.25f, 1.0f, 0.5f, 0.75f };
    vector<uint8> byte( sizeof( arrValue ) );
    Memory::copy( byte.data(), arrValue, sizeof( arrValue ) );

    BufferViewLayout layout = BufferViewLayout::make( 8, BufferViewScalar::Float );
    SW_EXPECT_EQUAL( 2u, layout._columnCount );
    SW_EXPECT_EQUAL( 2u, layout.getRowCount( byte.size() ) );
    vector<string> listCell;
    SW_REQUIRE_TRUE( layout.formatRow( byte, 1, listCell ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listCell.size() );
    SW_EXPECT_EQUAL( string( "0.5" ), listCell[0] );
    SW_EXPECT_EQUAL( string( "0.75" ), listCell[1] );
    SW_EXPECT_FALSE( layout.formatRow( byte, 2, listCell ) ); // 바이트 밖

    layout = BufferViewLayout::make( 8, BufferViewScalar::Hex );
    SW_REQUIRE_TRUE( layout.formatRow( byte, 0, listCell ) );
    SW_EXPECT_EQUAL( string( "0x3F800000" ), listCell[1] ); // 1.0f
    SW_EXPECT_EQUAL( 0u, BufferViewLayout::make( 0, BufferViewScalar::Uint ).getRowCount( byte.size() ) );
    SW_EXPECT_EQUAL( BufferViewLayout::kMaxColumnCount, BufferViewLayout::make( 256, BufferViewScalar::Uint )._columnCount );
}

SW_TEST_CASE( RenderDebuggerViewModelTest, FrameDebuggerStepsThroughRecordedDraws )
{
    RenderDebugRegistry     registry;
    RenderDebuggerViewModel viewModel;
    RenderDebuggerSources   sources{};
    sources._pRegistry = &registry;
    (void)viewModel.gather( sources );

    // 켜면 전체 프레임을 기록한다 — 렌더 스레드 한 프레임을 흉내 낸다.
    viewModel.setFrameDebuggerEnabled( true );
    const hashed_string gbuffer( "GBuffer" );
    const auto          renderFrame = [&registry, &gbuffer]()
    {
        registry.beginFrame( { gbuffer } );
        for ( uint32 draw = 0; draw < 3; ++draw )
            (void)registry.allowDraw( gbuffer, 1, nullptr, 6, 1, 2 );
        registry.endFrame();
    };
    renderFrame();
    const RenderDebuggerState& state = viewModel.gather( sources );
    SW_REQUIRE_EQUAL( size_t{ 3 }, state._listEvent.size() );
    SW_EXPECT_FALSE( state._stopEvent.has_value() ); // 전체 프레임

    // 세 번 넘기면 마지막 드로우에 선다(첫 넘김이 첫 드로우).
    viewModel.stepFrameDebugger( 1 );
    viewModel.stepFrameDebugger( 1 );
    viewModel.stepFrameDebugger( 1 );
    renderFrame();
    (void)viewModel.gather( sources );
    SW_REQUIRE_TRUE( state._stopEvent.has_value() );
    SW_EXPECT_EQUAL( 2u, *state._stopEvent );
    SW_EXPECT_EQUAL( string( "GBuffer" ), registry.getFrameDebugger()._stopPass );

    viewModel.selectEvent( 0 );
    renderFrame();
    (void)viewModel.gather( sources );
    SW_REQUIRE_TRUE( state._stopEvent.has_value() );
    SW_EXPECT_EQUAL( 0u, *state._stopEvent );
    SW_EXPECT_TRUE( state._listEvent[1]._bSkipped != SW_FALSE ); // 멈춤 뒤는 건너뛴다

    viewModel.setFrameDebuggerEnabled( false );
    SW_EXPECT_TRUE( registry.getFrameDebugger()._bEnabled == SW_FALSE );
}

SW_TEST_CASE( RenderDebuggerViewModelTest, GraphTabRequestsCaptureAndBufferTabReadsTheSelectedBuffer )
{
    RenderDebugRegistry     registry;
    RenderDebuggerViewModel viewModel;
    RenderDebuggerSources   sources{};
    sources._pRegistry = &registry;
    (void)viewModel.gather( sources );

    viewModel.selectTab( RenderDebuggerTab::RenderGraph );
    SW_REQUIRE_TRUE( registry.takeGraphCaptureRequest() );
    registry.publishGraphs( { makeThreePassSnapshot() } );
    const RenderDebuggerState& state = viewModel.gather( sources );
    SW_EXPECT_EQUAL( size_t{ 3 }, state._graphLayout._listNode.size() );
    SW_EXPECT_EQUAL( size_t{ 3 }, state._listPassMilliseconds.size() ); // 프로파일러가 없으면 음수(재지 않음)
    SW_EXPECT_TRUE( state._listPassMilliseconds[0] < 0.0f );

    viewModel.selectTab( RenderDebuggerTab::Buffers );
    SW_REQUIRE_TRUE( registry.takeBufferListRequest() );
    registry.publishBufferList( {
        RenderBufferEntry{ "OcclusionTiles", 0, 4, 2 }
    } );
    (void)viewModel.gather( sources );
    SW_REQUIRE_EQUAL( size_t{ 1 }, state._listBuffer.size() );
    viewModel.selectBuffer( 0 );
    (void)viewModel.gather( sources );
    SW_EXPECT_TRUE( state._bBufferReading != SW_FALSE );

    string name;
    uint32 slot   = 0;
    uint32 serial = 0;
    SW_REQUIRE_TRUE( registry.takeBufferReadRequest( name, slot, serial ) );
    SW_EXPECT_EQUAL( string( "OcclusionTiles" ), name );
    const float32 arrDepth[2]{ 0.5f, 1.0f };
    vector<uint8> byte( sizeof( arrDepth ) );
    Memory::copy( byte.data(), arrDepth, sizeof( arrDepth ) );
    registry.publishBufferContent( RenderBufferContent{
        RenderBufferEntry{ name, slot, 4, 2 },
        byte, serial
    } );
    (void)viewModel.gather( sources );
    SW_EXPECT_TRUE( state._bBufferReading == SW_FALSE );
    SW_EXPECT_EQUAL( 2u, state._bufferLayout.getRowCount( state._bufferContent._byte.size() ) );
}
