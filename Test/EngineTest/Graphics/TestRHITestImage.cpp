#include "pch.h"

#include "EngineTest/RHITestImage.h"

#include "TestFramework/TestFramework.h"

// GPU 케이스가 픽셀을 읽는 규칙(`test::RHITestImage`)을 GPU 없이 지킨다. 이 PC 의 네 백엔드는 `SceneColor` 를 전부 RGBA8 로
// 되읽어서, BGRA 뒤집기를 틀려도 `RenderPassGPUTest` 는 하나도 지지 않는다 — 스왑체인이 BGRA 인 호스트에서만 빨강 · 파랑이
// 바뀐 채로 통과하거나 진다.

namespace
{
    /** @brief 너비 × 높이, 픽셀당 bytesPerPixel 바이트, 빈틈없는 행의 배치. */
    sw::RHITextureMipSpan makeLayout( uint32 width, uint32 height, uint32 bytesPerPixel )
    {
        sw::RHITextureMipSpan layout{};
        layout._width     = width;
        layout._height    = height;
        layout._rowBytes  = width * bytesPerPixel;
        layout._sizeBytes = layout._rowBytes * height;
        return layout;
    }

    /** @brief 반정밀도 한 값을 작은 끝 두 바이트로 덧붙입니다. */
    void appendHalf( sw::vector<uint8>& outBytes, uint16 half )
    {
        outBytes.push_back( static_cast<uint8>( half & 0xFFu ) );
        outBytes.push_back( static_cast<uint8>( half >> 8 ) );
    }
} // namespace

/**
 * @brief [RHITestImageTest] RGBA8 은 그대로, BGRA8 은 뒤집어 RGBA 로 읽는다 — 행 간격도 지킨다
 */
SW_TEST_CASE( RHITestImageTest, EightBitFormatsReadAsRgba )
{
    // 2 x 2 — 픽셀마다 다른 값이라 행 · 열을 잘못 짚으면 드러난다.
    const sw::vector<uint8> bytes = { 10, 20, 30, 40, 11, 21, 31, 41, 12, 22, 32, 42, 13, 23, 33, 43 };

    test::RHITestImage rgba;
    rgba.assign( bytes, makeLayout( 2, 2, 4 ), sw::RHIFormat::R8G8B8A8_UNORM );
    const test::Rgba8 rgbaPixel = rgba.getPixel( 1, 1 );
    SW_EXPECT_EQUAL( 13, static_cast<int32>( rgbaPixel._r ) );
    SW_EXPECT_EQUAL( 23, static_cast<int32>( rgbaPixel._g ) );
    SW_EXPECT_EQUAL( 33, static_cast<int32>( rgbaPixel._b ) );
    SW_EXPECT_EQUAL( 43, static_cast<int32>( rgbaPixel._a ) );

    test::RHITestImage bgra;
    bgra.assign( bytes, makeLayout( 2, 2, 4 ), sw::RHIFormat::B8G8R8A8_UNORM );
    const test::Rgba8 bgraPixel = bgra.getPixel( 0, 1 );
    SW_EXPECT_EQUAL( 32, static_cast<int32>( bgraPixel._r ) );
    SW_EXPECT_EQUAL( 22, static_cast<int32>( bgraPixel._g ) );
    SW_EXPECT_EQUAL( 12, static_cast<int32>( bgraPixel._b ) );
    SW_EXPECT_EQUAL( 42, static_cast<int32>( bgraPixel._a ) );

    // 저장된 그대로의 바이트는 뒤집지 않는다.
    SW_ASSERT_NOT_NULL( bgra.getRawPixel( 0, 1 ) );
    SW_EXPECT_EQUAL( 12, static_cast<int32>( bgra.getRawPixel( 0, 1 )[0] ) );

    // 범위 밖은 0 이고 죽지 않는다.
    SW_EXPECT_EQUAL( 0, static_cast<int32>( rgba.getPixel( 2, 0 )._r ) );
    SW_EXPECT_NULL( rgba.getRawPixel( 0, 2 ) );
}

/**
 * @brief [RHITestImageTest] 반정밀도(RGBA16F)는 [0,1] 로 잘라 0~255 로 읽는다 — 음수 · 무한 · 비정상 수는 0
 */
SW_TEST_CASE( RHITestImageTest, HalfFloatChannelsClampToUnitBytes )
{
    sw::vector<uint8> bytes;
    appendHalf( bytes, 0x3C00u ); // 1.0  -> 255
    appendHalf( bytes, 0x3800u ); // 0.5  -> 127
    appendHalf( bytes, 0x4000u ); // 2.0  -> 255(잘린다)
    appendHalf( bytes, 0xBC00u ); // -1.0 -> 0
    appendHalf( bytes, 0x7C00u ); // +inf -> 0
    appendHalf( bytes, 0x0001u ); // 비정상 수 -> 0
    appendHalf( bytes, 0x0000u );
    appendHalf( bytes, 0x0000u );

    test::RHITestImage image;
    image.assign( bytes, makeLayout( 2, 1, 8 ), sw::RHIFormat::R16G16B16A16_FLOAT );
    const test::Rgba8 first  = image.getPixel( 0, 0 );
    const test::Rgba8 second = image.getPixel( 1, 0 );
    SW_EXPECT_EQUAL( 255, static_cast<int32>( first._r ) );
    SW_EXPECT_EQUAL( 127, static_cast<int32>( first._g ) );
    SW_EXPECT_EQUAL( 255, static_cast<int32>( first._b ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( first._a ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( second._r ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( second._g ) );
}

/**
 * @brief [RHITestImageTest] 기본 클리어 배경의 칸과 색 거리
 * @details 기본 클리어 색(0.02, 0.02, 0.05)은 톤매핑을 지나 (31, 38, 46) 근처에 떨어진다. 칸의 경계가 한 칸만 어긋나도
 *          "그려진 픽셀 수" 를 세는 GPU 케이스 여럿이 배경을 그림으로 센다.
 */
SW_TEST_CASE( RHITestImageTest, DefaultClearBackgroundAndColorDistance )
{
    SW_EXPECT_TRUE( test::RHITestImage::isDefaultClearBackground( test::Rgba8{ 31, 38, 46, 255 } ) );
    SW_EXPECT_TRUE( test::RHITestImage::isDefaultClearBackground( test::Rgba8{ 22, 28, 36, 255 } ) );
    SW_EXPECT_TRUE( test::RHITestImage::isDefaultClearBackground( test::Rgba8{ 40, 48, 56, 255 } ) );
    SW_EXPECT_FALSE( test::RHITestImage::isDefaultClearBackground( test::Rgba8{ 41, 38, 46, 255 } ) );
    SW_EXPECT_FALSE( test::RHITestImage::isDefaultClearBackground( test::Rgba8{ 31, 27, 46, 255 } ) );
    SW_EXPECT_FALSE( test::RHITestImage::isDefaultClearBackground( test::Rgba8{ 31, 38, 57, 255 } ) );

    SW_EXPECT_EQUAL( 0u, test::RHITestImage::getColorDistance( test::Rgba8{ 1, 2, 3, 4 }, test::Rgba8{ 1, 2, 3, 99 } ) );
    SW_EXPECT_EQUAL( 30u, test::RHITestImage::getColorDistance( test::Rgba8{ 10, 20, 30, 0 }, test::Rgba8{ 0, 30, 20, 0 } ) );
}

/**
 * @brief [RHITestImageTest] 두 이미지를 화소마다 비교한다 — 기준 거리 이상만 다르다고 세고, 밝아진 화소를 따로 세며, 크기 · 사각형이 어긋나면 비교하지 않는다
 * @details GPU 케이스가 두 판을 평균 색 대신 이 결과로 판정한다. 셈이 틀리면 "다른 화소 0" 을 기대하는 케이스가 모두 함께 틀린다.
 */
SW_TEST_CASE( RHITestImageTest, CompareCountsDifferingAndBrighterPixels )
{
    // 2 x 2 RGBA8. 기준과 비교해 (0,0) 같음 · (1,0) 거리 3(기준 미만) · (0,1) 밝아짐 30 · (1,1) 어두워짐 60.
    const sw::vector<uint8> bytesBase  = { 10, 10, 10, 255, 50, 50, 50, 255, 100, 100, 100, 255, 200, 200, 200, 255 };
    const sw::vector<uint8> bytesOther = { 10, 10, 10, 255, 51, 51, 51, 255, 110, 110, 110, 255, 180, 180, 180, 255 };
    test::RHITestImage      base;
    test::RHITestImage      other;
    base.assign( bytesBase, makeLayout( 2, 2, 4 ), sw::RHIFormat::R8G8B8A8_UNORM );
    other.assign( bytesOther, makeLayout( 2, 2, 4 ), sw::RHIFormat::R8G8B8A8_UNORM );

    const test::RHITestImageDifference difference = test::RHITestImage::compare( base, other, 6 );
    SW_EXPECT_TRUE( difference.isCompared() );
    SW_EXPECT_EQUAL( 4u, difference._pixelCount );
    SW_EXPECT_EQUAL( 2u, difference._differingCount );
    SW_EXPECT_EQUAL( 1u, difference._brighterCount );
    SW_EXPECT_EQUAL( 60u, difference._maxDistance );
    SW_EXPECT_FALSE( difference.isDifferingAtMost( 1 ) );
    SW_EXPECT_TRUE( difference.isDifferingAtMost( 50 ) );

    // 위 행만 — 거리 3 은 기준 미만이라 다른 화소가 없다.
    const test::RHITestImageDifference topRow = test::RHITestImage::compareRegion( base, other, 6, 0, 0, 2, 1 );
    SW_EXPECT_EQUAL( 2u, topRow._pixelCount );
    SW_EXPECT_EQUAL( 0u, topRow._differingCount );
    SW_EXPECT_EQUAL( 3u, topRow._maxDistance );

    // 사각형이 이미지 밖이거나 크기가 다르면 비교하지 않는다(통과로 읽히지 않게 `isDifferingAtMost` 도 거짓).
    SW_EXPECT_FALSE( test::RHITestImage::compareRegion( base, other, 6, 0, 0, 3, 1 ).isCompared() );
    test::RHITestImage wide;
    wide.assign( sw::vector<uint8>( 16, 0 ), makeLayout( 4, 1, 4 ), sw::RHIFormat::R8G8B8A8_UNORM );
    const test::RHITestImageDifference mismatched = test::RHITestImage::compare( base, wide, 6 );
    SW_EXPECT_FALSE( mismatched.isCompared() );
    SW_EXPECT_FALSE( mismatched.isDifferingAtMost( 100 ) );
}
