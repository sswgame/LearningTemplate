// DisplayEncoding — HDR 출력 인코딩의 CPU 거울: PQ(ST 2084) 표 값 · 왕복 · 자르기, Rec.709 → BT.2020 원색 변환(nogpu).
#include "pch.h"

#include "Core/Math/DisplayEncoding.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

/**
 * @brief [DisplayEncodingTest] PQ 는 ST 2084 표 값을 낸다 — 0 nits 는 거의 0, 100 nits 0.5081, 1000 nits 0.7518, 10000 nits 1. 결함이면 HDR10 화면의 밝기가 모두 어긋난다
 */
SW_TEST_CASE( DisplayEncodingTest, PQMatchesTheST2084Table )
{
    SW_EXPECT_NEAR( 0.0f, DisplayEncoding::encodePQ( 0.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.5081f, DisplayEncoding::encodePQ( 100.0f ), 1.0e-4f );
    SW_EXPECT_NEAR( 0.7518f, DisplayEncoding::encodePQ( 1000.0f ), 1.0e-4f );
    SW_EXPECT_NEAR( 1.0f, DisplayEncoding::encodePQ( DisplayEncoding::kPQMaxNits ), 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, DisplayEncoding::encodePQ( 20000.0f ), 1.0e-6f ); // 10000 위는 자른다
    SW_EXPECT_NEAR( 0.0f, DisplayEncoding::encodePQ( -5.0f ), 1.0e-6f );
}

/**
 * @brief [DisplayEncodingTest] 디코딩은 인코딩의 역이다 — 1 ~ 10000 nits 왕복이 0.1 % 안. 되읽은 백버퍼 검사가 이 함수로 휘도를 푼다
 */
SW_TEST_CASE( DisplayEncodingTest, PQRoundTrips )
{
    for ( const float32 nits : { 1.0f, 80.0f, 200.0f, 1000.0f, 4000.0f, 10000.0f } )
    {
        const float32 decoded = DisplayEncoding::decodePQ( DisplayEncoding::encodePQ( nits ) );
        SW_EXPECT_NEAR( nits, decoded, nits * 1.0e-3f );
    }
}

/**
 * @brief [DisplayEncodingTest] Rec.709 → BT.2020 은 흰색을 그대로 두고 순색 빨강을 BT.2020 의 (0.627, 0.069, 0.016) 으로 옮긴다(ITU-R BT.2087). 결함이면 HDR10 의 색이 바랜다
 */
SW_TEST_CASE( DisplayEncodingTest, Rec709ToRec2020KeepsWhite )
{
    const float3 white = DisplayEncoding::convertRec709ToRec2020( float3{ 1.0f, 1.0f, 1.0f } );
    SW_EXPECT_NEAR( 1.0f, white._x, 1.0e-5f );
    SW_EXPECT_NEAR( 1.0f, white._y, 1.0e-5f );
    SW_EXPECT_NEAR( 1.0f, white._z, 1.0e-5f );
    const float3 red = DisplayEncoding::convertRec709ToRec2020( float3{ 1.0f, 0.0f, 0.0f } );
    SW_EXPECT_NEAR( 0.627404f, red._x, 1.0e-5f );
    SW_EXPECT_NEAR( 0.069097f, red._y, 1.0e-5f );
    SW_EXPECT_NEAR( 0.016392f, red._z, 1.0e-5f );
}
