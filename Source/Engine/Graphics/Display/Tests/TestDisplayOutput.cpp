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

/**
 * @brief [DisplayOutputTest] 출력 종류마다 스왑체인 색 공간 · 백버퍼 포맷 짝이 하나로 정해진다(SDR 은 요청 SDR 포맷 그대로). 결함이면 HDR10 신호를 8 비트 백버퍼에 쓰거나 scRGB 를 PQ 색 공간으로 알려 그림이 깨진다
 */
SW_TEST_CASE( DisplayOutputTest, ColorSpaceDecidesSwapChainFormat )
{
    DisplayOutputDesc desc;
    desc._kind = DisplayOutputKind::SDR;
    SW_EXPECT_TRUE( desc.getColorSpace() == RHIColorSpace::SRGBNonLinear );
    SW_EXPECT_TRUE( getSwapChainFormat( desc.getColorSpace(), RHIFormat::B8G8R8A8_UNORM ) == RHIFormat::B8G8R8A8_UNORM );

    desc._kind = DisplayOutputKind::HDR10;
    SW_EXPECT_TRUE( desc.getColorSpace() == RHIColorSpace::HDR10ST2084 );
    SW_EXPECT_TRUE( getSwapChainFormat( desc.getColorSpace(), RHIFormat::R8G8B8A8_UNORM ) == RHIFormat::R10G10B10A2_UNORM );

    desc._kind = DisplayOutputKind::ScRGB;
    SW_EXPECT_TRUE( desc.getColorSpace() == RHIColorSpace::ExtendedSRGBLinear );
    SW_EXPECT_TRUE( getSwapChainFormat( desc.getColorSpace(), RHIFormat::R8G8B8A8_UNORM ) == RHIFormat::R16G16B16A16_FLOAT );

    // 스왑체인 서술도 같은 함수로 포맷을 낸다 — 색 공간과 포맷을 따로 적을 칸이 없다.
    RHISwapChainDesc swapChainDesc{};
    swapChainDesc._colorSpace = RHIColorSpace::HDR10ST2084;
    SW_EXPECT_TRUE( swapChainDesc.getFormat() == RHIFormat::R10G10B10A2_UNORM );
}

/**
 * @brief [DisplayOutputTest] HDR10 마스터링 값은 출력의 최대 휘도와 종이 흰색에서 온다. 결함이면 TV 가 다른 최대 휘도로 톤맵해 밝은 부분이 잘리거나 눌린다
 */
SW_TEST_CASE( DisplayOutputTest, HDRMetadataFollowsOutputLuminance )
{
    DisplayOutputDesc desc;
    desc._kind                        = DisplayOutputKind::HDR10;
    desc._paperWhiteNits              = 240.0f;
    desc._maxLuminanceNits            = 1400.0f;
    const RHIDisplayMetadata metadata = desc.makeDisplayMetadata();
    SW_EXPECT_EQUAL( 1400.0f, metadata._maxLuminanceNits );
    SW_EXPECT_EQUAL( 240.0f, metadata._maxFrameAverageNits );
}
