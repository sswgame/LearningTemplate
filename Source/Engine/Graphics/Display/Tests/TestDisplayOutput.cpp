// DisplayOutput — 화면 출력 장치 값을 능력에 맞춰 고른다: SDR 은 늘 받고, 받지 않는 HDR 요청은 로그 한 줄과 SDR(휘도 값은 그대로)(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Display/DisplayOutput.h"

using namespace sw;

/**
 * @brief [DisplayOutputTest] 능력 안의 요청은 그대로, 밖의 HDR 요청은 SDR 로 떨어지되 종이 흰색 · 최대 휘도는 남는다. 결함이면 HDR 이 없는 화면에서 PQ 신호를 SDR 백버퍼에 써 그림이 하얗게 뜬다
 */
SW_TEST_CASE( DisplayOutputTest, ResolveFallsBackToSDROutsideCapabilities )
{
    DisplayOutputDesc requested;
    requested._kind             = DisplayOutputKind::HDR10;
    requested._paperWhiteNits   = 250.0f;
    requested._maxLuminanceNits = 1500.0f;

    const uint8             hdr10Only = makeDisplayOutputBit( DisplayOutputKind::HDR10 );
    const DisplayOutputDesc kept      = DisplayOutputDesc::resolve( requested, hdr10Only );
    SW_EXPECT_TRUE( kept._kind == DisplayOutputKind::HDR10 );

    SW_EXPECT_LOG( "Display.OutputUnsupported" );
    const DisplayOutputDesc fallen = DisplayOutputDesc::resolve( requested, makeDisplayOutputBit( DisplayOutputKind::ScRGB ) );
    SW_EXPECT_TRUE( fallen._kind == DisplayOutputKind::SDR );
    SW_EXPECT_EQUAL( 250.0f, fallen._paperWhiteNits );
    SW_EXPECT_EQUAL( 1500.0f, fallen._maxLuminanceNits );

    DisplayOutputDesc sdr;
    SW_EXPECT_TRUE( DisplayOutputDesc::resolve( sdr, 0 )._kind == DisplayOutputKind::SDR ); // SDR 은 능력 비트 없이도 받는다
}
