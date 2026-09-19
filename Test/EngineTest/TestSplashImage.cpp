#include "pch.h"

#include "Engine/Window/ISplashWindow.h"

#include "TestFramework/TestFramework.h"

// 스플래시 이미지 스케일러 — 창을 만들지 않으므로 GPU·디스플레이가 필요 없다(`nogpu`).
//
// **왜 이 스위트가 생겼나.** 리눅스에서 스플래시가 "이상하게" 보였다. 원인은 렌더가 아니라 크기였다 —
// `splash.dds` 는 1376×768 인데 스플래시 창은 480×280 이고, Win32 는 `StretchDIBits` 로 창에 맞춰
// 늘리는데 **X11 의 `XPutImage` 는 1:1 로만 찍는다.** 그래서 원본의 좌상단 480×280 만 보였다.
// 늘리기를 우리 코드가 하게 되었으므로, 그 계산을 여기서 못 박는다.

namespace
{
    /** @brief BGRA 픽셀 하나를 만듭니다. */
    void writeBgraPixel( sw::vector<uint8>& listPixel, size_t index, uint8 b, uint8 g, uint8 r, uint8 a )
    {
        listPixel[index * 4 + 0] = b;
        listPixel[index * 4 + 1] = g;
        listPixel[index * 4 + 2] = r;
        listPixel[index * 4 + 3] = a;
    }
} // namespace

/**
 * @brief [SplashImageTest] 줄이면 덮는 원본 픽셀들의 **평균**이 된다
 * @details 최근접으로 뽑으면 2.9배 축소에서 글자와 로고 가장자리가 부서진다. Win32 가
 *          `SetStretchBltMode(HALFTONE)` 로 시키는 것과 같은 일을 우리가 한다.
 */
SW_TEST_CASE( SplashImageTest, DownscaleAveragesTheCoveredPixels )
{
    // 2×2 → 1×1. 네 픽셀의 평균이 나와야 한다.
    sw::vector<uint8> listSource( 2 * 2 * 4, 0 );
    writeBgraPixel( listSource, 0, 0, 0, 0, 255 );
    writeBgraPixel( listSource, 1, 100, 100, 100, 255 );
    writeBgraPixel( listSource, 2, 200, 200, 200, 255 );
    writeBgraPixel( listSource, 3, 0, 0, 0, 255 );

    sw::vector<uint8> listDest( 1 * 1 * 4, 0xAB );
    sw::ISplashWindow::scaleBgraImage( listSource.data(), 2, 2, listDest.data(), 1, 1 );

    // (0 + 100 + 200 + 0) / 4 = 75
    SW_EXPECT_EQUAL( 75, static_cast<int32>( listDest[0] ) );
    SW_EXPECT_EQUAL( 75, static_cast<int32>( listDest[1] ) );
    SW_EXPECT_EQUAL( 75, static_cast<int32>( listDest[2] ) );
    SW_EXPECT_EQUAL( 255, static_cast<int32>( listDest[3] ) );
}

/**
 * @brief [SplashImageTest] 줄여도 **목적지 전체**가 채워진다
 * @details 이것이 리눅스에서 났던 증상의 축소판이다 — 채우는 범위가 원본 크기를 따라가면 목적지의
 *          일부만 그려지고 나머지는 그대로 남는다(그때는 창의 좌상단만 보였다). 한 픽셀이라도
 *          손대지 않은 곳이 있으면 실패한다.
 */
SW_TEST_CASE( SplashImageTest, DownscaleFillsEveryDestinationPixel )
{
    constexpr uint32 kSourceWidth  = 137;
    constexpr uint32 kSourceHeight = 76;
    constexpr uint32 kDestWidth    = 48;
    constexpr uint32 kDestHeight   = 28;

    // 원본은 전부 불투명한 흰색이다 — 평균도 흰색이므로 "손대지 않은 곳" 과 확실히 구분된다.
    sw::vector<uint8> listSource( static_cast<size_t>( kSourceWidth ) * kSourceHeight * 4, 0xFF );
    sw::vector<uint8> listDest( static_cast<size_t>( kDestWidth ) * kDestHeight * 4, 0x00 );

    sw::ISplashWindow::scaleBgraImage( listSource.data(), kSourceWidth, kSourceHeight,
                                       listDest.data(), kDestWidth, kDestHeight );

    size_t untouchedCount = 0;
    for ( size_t byteIndex = 0; byteIndex < listDest.size(); ++byteIndex )
    {
        if ( listDest[byteIndex] != 0xFF )
            ++untouchedCount;
    }
    SW_EXPECT_TRUE_MSG( untouchedCount == 0, "목적지에 손대지 않은 픽셀이 남았습니다 — 창의 일부만 그려집니다" );
    SW_EXPECT_EQUAL( size_t( 0 ), untouchedCount );
}

/**
 * @brief [SplashImageTest] 늘리면 원본 픽셀이 그대로 퍼진다
 * @details 늘릴 때는 목적지 한 픽셀이 원본 한 픽셀만 덮으므로 평균이 곧 그 픽셀이다.
 */
SW_TEST_CASE( SplashImageTest, UpscaleReplicatesSourcePixels )
{
    sw::vector<uint8> listSource( 1 * 1 * 4, 0 );
    writeBgraPixel( listSource, 0, 10, 20, 30, 40 );

    sw::vector<uint8> listDest( 3 * 2 * 4, 0 );
    sw::ISplashWindow::scaleBgraImage( listSource.data(), 1, 1, listDest.data(), 3, 2 );

    for ( size_t pixelIndex = 0; pixelIndex < 3 * 2; ++pixelIndex )
    {
        SW_EXPECT_EQUAL( 10, static_cast<int32>( listDest[pixelIndex * 4 + 0] ) );
        SW_EXPECT_EQUAL( 20, static_cast<int32>( listDest[pixelIndex * 4 + 1] ) );
        SW_EXPECT_EQUAL( 30, static_cast<int32>( listDest[pixelIndex * 4 + 2] ) );
        SW_EXPECT_EQUAL( 40, static_cast<int32>( listDest[pixelIndex * 4 + 3] ) );
    }
}

/**
 * @brief [SplashImageTest] 빈 입력에는 아무것도 하지 않는다
 * @details 스플래시는 이미지 로드가 실패해도 떠야 한다 — 그때 이 함수가 0 으로 나누거나 버퍼 밖을
 *          쓰면 **로고가 없는 것이 아니라 시작 자체가 죽는다.**
 */
SW_TEST_CASE( SplashImageTest, EmptyInputIsLeftAlone )
{
    sw::vector<uint8>       listDest( 2 * 2 * 4, 0x7F );
    const sw::vector<uint8> listExpected = listDest;

    sw::ISplashWindow::scaleBgraImage( nullptr, 4, 4, listDest.data(), 2, 2 );
    sw::vector<uint8> listSource( 4 * 4 * 4, 0xFF );
    sw::ISplashWindow::scaleBgraImage( listSource.data(), 0, 4, listDest.data(), 2, 2 );
    sw::ISplashWindow::scaleBgraImage( listSource.data(), 4, 4, listDest.data(), 0, 2 );

    SW_EXPECT_TRUE_MSG( listDest == listExpected, "빈 입력인데 목적지를 건드렸습니다" );
}

// **실제 에셋으로 재 보는 케이스는 두지 않았다 — 두 번 시도했고 둘 다 이 버그를 구분하지 못했다.**
// 좌상단만 그리는 변이를 넣고 (1) 전체 평균색, (2) 네 분면 평균색을 각각 비교해 봤는데 **둘 다
// 통과했다.** 지금 아트의 좌상단 480×280 이 그림 전체와 색 분포가 비슷해서다. 잡지 못하는 검사를
// 남겨 두면 "실제 그림도 본다" 는 거짓 안심만 생긴다 — 위의 합성 케이스들은 같은 변이에서 실제로
// 진다(`DownscaleAveragesTheCoveredPixels` · `UpscaleReplicatesSourcePixels`).
// 진짜 그림을 보려면 창을 띄워 픽셀을 되읽어야 하고, 그것은 `hostgpu` 쪽 일이다.
