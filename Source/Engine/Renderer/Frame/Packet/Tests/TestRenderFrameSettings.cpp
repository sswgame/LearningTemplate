#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
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
    const string previous = pInfo->getValueAsString();
    SW_REQUIRE_TRUE( variables.setValueFromString( "gv_drawMerge", "true" ) );

    RenderFramePacket framePacket;
    framePacket._settings = RenderFrameSettings::capture(); // 이 프레임의 패킷(렌더 스레드가 그린다)
    SW_EXPECT_EQUAL( uint8{ SW_TRUE }, framePacket._settings._bDrawMerge );

    // 콘솔이 렌더 스레드가 그리는 도중에 바꾼다 — 그리는 프레임의 사본은 그대로다(패스 · 기록 워커가 모두 같은 값을 본다).
    SW_REQUIRE_TRUE( variables.setValueFromString( "gv_drawMerge", "false" ) );
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

    SW_REQUIRE_TRUE( variables.setValueFromString( "gv_drawMerge", previous ) );
}

/**
 * @brief [RenderFrameSettingsTest] 사본은 렌더 쪽이 쓸 수 있는 범위로 잘라 뜬다 — 그림자 맵 한 변은 1 이상, 병렬 기록 레벨 너비는 2 이상, RCAS 선명도는 0 이상
 */
SW_TEST_CASE( RenderFrameSettingsTest, CaptureClampsToUsableRanges )
{
    GlobalVariableManager& variables       = engine::getSubsystem<GlobalVariableManager>();
    constexpr size_t       kCount          = 3;
    const utf8* const      arrName[kCount] = { "gv_shadowMapResolution", "gv_renderGraphParallelMinWidth", "gv_upscalerSharpness" };
    string                 arrPrevious[kCount];
    for ( size_t index = 0; index < kCount; ++index )
    {
        GlobalVariableInfo* pInfo = variables.findVariable( arrName[index] );
        SW_REQUIRE_NOT_NULL( pInfo );
        arrPrevious[index] = pInfo->getValueAsString();
    }
    SW_REQUIRE_TRUE( variables.setValueFromString( "gv_shadowMapResolution", "0" ) );
    SW_REQUIRE_TRUE( variables.setValueFromString( "gv_renderGraphParallelMinWidth", "1" ) );
    SW_REQUIRE_TRUE( variables.setValueFromString( "gv_upscalerSharpness", "-1" ) );

    const RenderFrameSettings settings = RenderFrameSettings::capture();
    SW_EXPECT_EQUAL( 1u, settings._shadowMapResolution );
    SW_EXPECT_EQUAL( 2u, settings._renderGraphParallelMinWidth );
    SW_EXPECT_NEAR( 0.0f, settings._upscalerSharpness, 1.0e-6f );

    for ( size_t index = 0; index < kCount; ++index )
        SW_REQUIRE_TRUE( variables.setValueFromString( arrName[index], arrPrevious[index] ) );
}
