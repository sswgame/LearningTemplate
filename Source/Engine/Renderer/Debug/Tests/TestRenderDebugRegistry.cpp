/**
 * @file TestRenderDebugRegistry.cpp
 * @brief 렌더링 런타임 디버그 우편함(`RenderDebugRegistry`) 테스트 — 프레임 디버거의 멈춤 규칙 · 기록 순서 · 통계, 버퍼 읽기 요청의 바뀜(GPU 없음).
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Debug/RenderDebugRegistry.h"

using namespace sw;

/**
 * @brief [RenderDebugRegistryTest] 멈춤 패스 앞은 모두, 멈춤 패스는 그 번호까지 그리고, 뒤 패스 · 그 번호 뒤는 그리지 않는다 — 그래프에 없는 패스는 그린다
 */
SW_TEST_CASE( RenderDebugRegistryTest, StopRuleCutsAfterTheStopDraw )
{
    SW_EXPECT_FALSE( RenderDebugRegistry::isPastStop( 0u, 50u, 2u, 1u ) );      // 앞 패스
    SW_EXPECT_FALSE( RenderDebugRegistry::isPastStop( 2u, 1u, 2u, 1u ) );       // 멈춤 드로우 자신
    SW_EXPECT_TRUE( RenderDebugRegistry::isPastStop( 2u, 2u, 2u, 1u ) );        // 그 뒤
    SW_EXPECT_TRUE( RenderDebugRegistry::isPastStop( 3u, 0u, 2u, 1u ) );        // 뒤 패스
    SW_EXPECT_FALSE( RenderDebugRegistry::isPastStop( nullopt, 99u, 2u, 1u ) ); // 그래프 밖 패스
}

/**
 * @brief [RenderDebugRegistryTest] 켜진 프레임은 드로우를 실행 순서로 줄 세워 공개하고, 멈춤 뒤 드로우는 건너뜀으로 적으며 통계에서 뺀다
 */
SW_TEST_CASE( RenderDebugRegistryTest, FrameRecordsDrawsInPassOrder )
{
    RenderDebugRegistry registry;
    const hashed_string prepass( "DepthPrepass" );
    const hashed_string gbuffer( "GBuffer" );
    const hashed_string forward( "Forward" );

    FrameDebuggerSettings settings{};
    settings._stopPass = "GBuffer";
    settings._stopDraw = 1;
    settings._bEnabled = SW_TRUE;
    registry.setFrameDebugger( settings );

    registry.beginFrame( { prepass, gbuffer, forward } );
    SW_REQUIRE_TRUE( registry.isRecording() );
    // 병렬 기록처럼 뒤 패스가 먼저 온다 — 공개할 때 실행 순서로 다시 선다.
    SW_EXPECT_FALSE( registry.allowDraw( forward, 7, nullptr, 30, 1, 10 ) );
    SW_EXPECT_TRUE( registry.allowDraw( gbuffer, 5, nullptr, 30, 2, 20 ) );
    SW_EXPECT_TRUE( registry.allowDraw( gbuffer, 5, nullptr, 30, 1, 10 ) );
    SW_EXPECT_FALSE( registry.allowDraw( gbuffer, 5, nullptr, 30, 1, 10 ) );
    SW_EXPECT_TRUE( registry.allowDraw( prepass, 3, nullptr, 30, 1, 10 ) );
    registry.endFrame();
    SW_EXPECT_FALSE( registry.isRecording() );

    SW_EXPECT_EQUAL( uint64{ 1 }, registry.getDrawEventSerial() );
    vector<FrameDebugDrawEvent> listEvent;
    registry.copyDrawEvents( listEvent );
    SW_REQUIRE_EQUAL( size_t{ 5 }, listEvent.size() );
    SW_EXPECT_EQUAL( string( "DepthPrepass" ), listEvent[0]._passName );
    SW_EXPECT_EQUAL( string( "GBuffer" ), listEvent[1]._passName );
    SW_EXPECT_EQUAL( 0u, listEvent[1]._drawIndex );
    SW_EXPECT_EQUAL( 2u, listEvent[3]._drawIndex );
    SW_EXPECT_TRUE( listEvent[3]._bSkipped != SW_FALSE );
    SW_EXPECT_EQUAL( string( "Forward" ), listEvent[4]._passName );

    const RenderFrameStats stats = registry.getStats();
    SW_EXPECT_EQUAL( uint64{ 3 }, stats._drawCount ); // 그린 셋만
    SW_EXPECT_EQUAL( uint64{ 40 }, stats._triangleCount );
}

/**
 * @brief [RenderDebugRegistryTest] 꺼져 있으면 기록하지 않고, 통계는 원할 때만 센다
 */
SW_TEST_CASE( RenderDebugRegistryTest, StatsOnlyWhenWanted )
{
    RenderDebugRegistry registry;
    const hashed_string pass( "Forward" );
    registry.beginFrame( { pass } );
    SW_EXPECT_FALSE( registry.isRecording() );
    registry.endFrame();
    SW_EXPECT_EQUAL( uint64{ 0 }, registry.getStats()._serial );

    registry.requestStats();
    registry.beginFrame( { pass } );
    SW_REQUIRE_TRUE( registry.isRecording() );
    SW_EXPECT_TRUE( registry.allowDraw( pass, 1, nullptr, 3, 1, 4 ) );
    registry.endFrame();
    SW_EXPECT_EQUAL( uint64{ 1 }, registry.getStats()._serial );
    SW_EXPECT_EQUAL( uint64{ 4 }, registry.getStats()._triangleCount );
    SW_EXPECT_EQUAL( uint64{ 0 }, registry.getDrawEventSerial() ); // 프레임 디버거가 꺼져 있어 기록은 없다
}

/**
 * @brief [RenderDebugRegistryTest] 처리하지 않은 버퍼 읽기 요청은 새 요청으로 바뀌고, 꺼내면 비어 있다
 */
SW_TEST_CASE( RenderDebugRegistryTest, BufferReadRequestIsReplaced )
{
    RenderDebugRegistry registry;
    (void)registry.requestBufferRead( "HZB", 0 );
    const uint32 serial = registry.requestBufferRead( "OcclusionTiles", 1 );
    string       name;
    uint32       slot        = 0;
    uint32       takenSerial = 0;
    SW_REQUIRE_TRUE( registry.takeBufferReadRequest( name, slot, takenSerial ) );
    SW_EXPECT_EQUAL( string( "OcclusionTiles" ), name );
    SW_EXPECT_EQUAL( 1u, slot );
    SW_EXPECT_EQUAL( serial, takenSerial );
    SW_EXPECT_FALSE( registry.takeBufferReadRequest( name, slot, takenSerial ) );

    RenderBufferContent content{};
    SW_EXPECT_FALSE( registry.copyBufferContent( content ) );
    registry.publishBufferContent( RenderBufferContent{
        RenderBufferEntry{ "OcclusionTiles", 1, 4, 2, {} },
        { 1, 2, 3, 4, 5, 6, 7, 8 },
        serial
    } );
    SW_REQUIRE_TRUE( registry.copyBufferContent( content ) );
    SW_EXPECT_EQUAL( serial, registry.getBufferContentSerial() );
    SW_EXPECT_EQUAL( size_t{ 8 }, content._byte.size() );
}

/**
 * @brief [RenderDebugRegistryTest] 다음 · 이전 단계는 기록 안에서 패스를 건너 옮기고 끝을 넘지 않는다 — 기록에 없는 지점에서는 앞은 처음, 뒤는 끝이다
 */
SW_TEST_CASE( RenderDebugRegistryTest, StepMovesAcrossPassesAndClamps )
{
    vector<FrameDebugDrawEvent> listEvent( 3 );
    listEvent[0]._passName  = "DepthPrepass";
    listEvent[0]._drawIndex = 0;
    listEvent[1]._passName  = "GBuffer";
    listEvent[1]._drawIndex = 0;
    listEvent[2]._passName  = "GBuffer";
    listEvent[2]._drawIndex = 1;

    FrameDebuggerSettings settings{};
    settings = RenderDebugRegistry::stepStop( listEvent, settings, 1 ); // 기록에 없는 지점 → 처음
    SW_EXPECT_EQUAL( string( "DepthPrepass" ), settings._stopPass );
    SW_EXPECT_TRUE( settings._bEnabled != SW_FALSE );
    settings = RenderDebugRegistry::stepStop( listEvent, settings, 1 );
    SW_EXPECT_EQUAL( string( "GBuffer" ), settings._stopPass );
    SW_EXPECT_EQUAL( 0u, settings._stopDraw );
    settings = RenderDebugRegistry::stepStop( listEvent, settings, 5 ); // 끝에 붙는다
    SW_EXPECT_EQUAL( 1u, settings._stopDraw );
    settings = RenderDebugRegistry::stepStop( listEvent, settings, -1 );
    SW_EXPECT_EQUAL( 0u, settings._stopDraw );
    SW_REQUIRE_TRUE( RenderDebugRegistry::findStopEvent( listEvent, settings ).has_value() );
    SW_EXPECT_EQUAL( 1u, *RenderDebugRegistry::findStopEvent( listEvent, settings ) );
}
