/**
 * @file TestRenderGraphSnapshot.cpp
 * @brief 렌더 그래프 사본(`RenderGraphSnapshot`) 테스트 — 패스 수 · 레벨 · 전이 수가 실행과 같고, 들인 자원의 처음 접근은 전이로 세지 않는다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Graph/RenderGraphSnapshot.h"

using namespace sw;

SW_TEST_CASE( RenderGraphSnapshotTest, PassesLevelsAndTransitionsMatchExecution )
{
    RenderGraph         graph;
    const hashed_string colorBuffer( "ColorBuffer" );
    graph.addPass( hashed_string( "PassA_Write" ), {}, { colorBuffer } );
    graph.addPass( hashed_string( "PassB_Read" ), { colorBuffer }, { hashed_string( "BlurBuffer" ) } );
    graph.addPass( hashed_string( "PassC_ReadSame" ), { colorBuffer }, { hashed_string( "UIBuffer" ) } );
    SW_REQUIRE_TRUE( graph.compile() );

    const RenderGraphSnapshot snapshot = RenderGraphSnapshot::build( graph, "view" );
    SW_EXPECT_EQUAL( string( "view" ), snapshot._graphName );
    SW_REQUIRE_EQUAL( size_t( 3 ), snapshot._listPass.size() );
    SW_EXPECT_EQUAL( 2u, snapshot._levelCount );
    SW_EXPECT_EQUAL( 0u, snapshot._listPass[0]._level );
    SW_EXPECT_EQUAL( 1u, snapshot._listPass[1]._level );
    // 실행이 내는 전이와 같다(TestRenderGraph 의 RenderGraphInfersOnlyChangedBarriers): Color 쓰기 · 읽기, Blur · UI 쓰기 — 셋째 패스의 Color 읽기는 이미 그 상태다.
    SW_EXPECT_EQUAL( 4u, snapshot._transitionCount );
    SW_EXPECT_EQUAL( 1u, snapshot._listPass[0]._transitionCount );

    const optional<uint32> colorIndex = snapshot.findResource( "ColorBuffer" );
    SW_REQUIRE_TRUE( colorIndex.has_value() );
    const RenderGraphSnapshotResource& color = snapshot._listResource[*colorIndex];
    SW_EXPECT_EQUAL( 0u, color._firstPass );
    SW_EXPECT_EQUAL( 2u, color._lastPass );
    SW_EXPECT_EQUAL( size_t( 1 ), snapshot._listPass[0]._listAccess.size() );
    SW_EXPECT_TRUE( snapshot._listPass[0]._listAccess[0]._access == RenderGraphAccess::RenderTarget );
    SW_EXPECT_FALSE( snapshot.findResource( "Missing" ).has_value() );
}

SW_TEST_CASE( RenderGraphSnapshotTest, ImportedFirstAccessIsNotATransition )
{
    RenderGraph         graph;
    const hashed_string tiles( "Tiles" );
    graph.importResource( tiles, RenderGraphViewableKind::Buffer, RenderGraphAccess::ShaderRead );
    graph.addPass( hashed_string( "Cull" ), { RenderGraphBufferSRV{ tiles } } );
    graph.addPass( hashed_string( "Build" ), { RenderGraphBufferUAV{ tiles } } );
    SW_REQUIRE_TRUE( graph.compile() );

    const RenderGraphSnapshot snapshot = RenderGraphSnapshot::build( graph, "frame" );
    SW_REQUIRE_EQUAL( size_t( 2 ), snapshot._listPass.size() );
    SW_EXPECT_EQUAL( 0u, snapshot._listPass[0]._transitionCount ); // 들인 처음 접근과 같다
    SW_EXPECT_EQUAL( 1u, snapshot._listPass[1]._transitionCount ); // SRV → UAV
    const optional<uint32> tilesIndex = snapshot.findResource( "Tiles" );
    SW_REQUIRE_TRUE( tilesIndex.has_value() );
    SW_EXPECT_TRUE( snapshot._listResource[*tilesIndex]._bImported != SW_FALSE );
    SW_EXPECT_TRUE( snapshot._listResource[*tilesIndex]._viewable == RenderGraphViewableKind::Buffer );
}
