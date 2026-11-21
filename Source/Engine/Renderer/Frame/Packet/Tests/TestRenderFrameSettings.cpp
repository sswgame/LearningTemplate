#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Testing/ScopedGlobalVariableValue.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/ThreadTestAccess.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/Frame/Packet/RenderFrameSettings.h"

// 렌더 설정 사본 — 콘솔(전역 변수 표)이 게임 스레드에서 값을 바꾸면 그 뒤에 지은 패킷부터 새 값이 들고, 이미 지은 패킷(렌더 스레드가 그리는 프레임)은 끝까지 옛 값이다(nogpu).

using namespace sw;

/**
 * @brief [RenderFrameSettingsTest] 콘솔이 값을 바꾼 프레임의 패킷부터 새 값이 들고, 앞 패킷의 사본은 렌더 쪽이 그 프레임 동안 같은 값으로 본다
 * @details 변이: 패킷이 사본 대신 변수를 가리키면(참조 · 렌더 쪽 직접 읽기) 앞 패킷의 값이 콘솔 변경을 따라가 첫 기대가 진다.
 */
SW_TEST_CASE( RenderFrameSettingsTest, ConsoleChangeReachesTheNextPacketOnly )
{
    GlobalVariableManager& variables = engine::getSubsystem<GlobalVariableManager>();
    GlobalVariableInfo*    pInfo     = variables.findVariable( "gv_drawMerge" );
    SW_REQUIRE_NOT_NULL( pInfo );
    test::ScopedGlobalVariableValue drawMergeValue{ *pInfo, "true" }; // 케이스가 어떻게 끝나도 앞 값으로 되돌린다
    SW_REQUIRE_TRUE( drawMergeValue.isApplied() );

    RenderFramePacket framePacket;
    framePacket._settings = RenderFrameSettings::capture(); // 이 프레임의 패킷(렌더 스레드가 그린다)
    SW_EXPECT_EQUAL( uint8{ SW_TRUE }, framePacket._settings._bDrawMerge );

    // 콘솔이 렌더 스레드가 그리는 도중에 바꾼다 — 그리는 프레임의 사본은 그대로다(패스 · 기록 워커가 모두 같은 값을 본다).
    SW_REQUIRE_TRUE( drawMergeValue.set( "false" ) );
    SW_EXPECT_EQUAL( uint8{ SW_TRUE }, framePacket._settings._bDrawMerge );
    uint8  seenOnRecordWorker = SW_FALSE;
    thread recordWorker( ThreadTestAccess::getKey(), [&framePacket, &seenOnRecordWorker]()
    {
        seenOnRecordWorker = framePacket._settings._bDrawMerge;
    } );
    recordWorker.join();
    SW_EXPECT_EQUAL( uint8{ SW_TRUE }, seenOnRecordWorker );

    // 다음 프레임의 패킷부터 새 값이다.
    RenderFramePacket nextPacket;
    nextPacket._settings = RenderFrameSettings::capture();
    SW_EXPECT_EQUAL( uint8{ SW_FALSE }, nextPacket._settings._bDrawMerge );
}

/**
 * @brief [RenderFrameSettingsTest] 사본은 렌더 쪽이 쓸 수 있는 범위로 잘라 뜬다 — 그림자 맵 한 변은 1 이상, 병렬 기록 레벨 너비는 2 이상, RCAS 선명도는 0 이상
 */
SW_TEST_CASE( RenderFrameSettingsTest, CaptureClampsToUsableRanges )
{
    GlobalVariableManager& variables         = engine::getSubsystem<GlobalVariableManager>();
    GlobalVariableInfo*    pShadowMap        = variables.findVariable( "gv_shadowMapResolution" );
    GlobalVariableInfo*    pParallelMinWidth = variables.findVariable( "gv_renderGraphParallelMinWidth" );
    GlobalVariableInfo*    pSharpness        = variables.findVariable( "gv_upscalerSharpness" );
    SW_REQUIRE_NOT_NULL( pShadowMap );
    SW_REQUIRE_NOT_NULL( pParallelMinWidth );
    SW_REQUIRE_NOT_NULL( pSharpness );
    // 케이스가 어떻게 끝나도 세 값을 앞 값으로 되돌린다.
    const test::ScopedGlobalVariableValue shadowMapValue{ *pShadowMap, "0" };
    const test::ScopedGlobalVariableValue parallelMinWidthValue{ *pParallelMinWidth, "1" };
    const test::ScopedGlobalVariableValue sharpnessValue{ *pSharpness, "-1" };
    SW_REQUIRE_TRUE( shadowMapValue.isApplied() );
    SW_REQUIRE_TRUE( parallelMinWidthValue.isApplied() );
    SW_REQUIRE_TRUE( sharpnessValue.isApplied() );

    const RenderFrameSettings settings = RenderFrameSettings::capture();
    SW_EXPECT_EQUAL( 1u, settings._shadowMapResolution );
    SW_EXPECT_EQUAL( 2u, settings._renderGraphParallelMinWidth );
    SW_EXPECT_NEAR( 0.0f, settings._upscalerSharpness, 1.0e-6f );
}
