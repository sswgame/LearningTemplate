// Color — 패킹 순서 · 왕복 · 자르기와 반올림, 16진 글자, sRGB ↔ 선형, 알파 · 밝기 가공, saturate, HSV, 허용치 비교.
#include "pch.h"

#include "Core/Math/Color.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

/**
 * @brief [ColorTest] 패킹 이름은 높은 바이트부터 채널 순서로 읽는다 — toRGBA 는 0xRRGGBBAA, toArgb 는 0xAARRGGBB, toAbgr 는 0xAABBGGRR(ImGui IM_COL32)
 */
SW_TEST_CASE( ColorTest, PackedOrderFollowsTheName )
{
    const Color color = Color::fromBytes( 0x11, 0x22, 0x33, 0x44 );
    SW_EXPECT_EQUAL( 0x11223344u, color.toRGBA() );
    SW_EXPECT_EQUAL( 0x44112233u, color.toArgb() );
    SW_EXPECT_EQUAL( 0x44332211u, color.toAbgr() );
}

/**
 * @brief [ColorTest] 세 패킹은 만든 값으로 그대로 돌아온다(바이트 값은 반올림해도 변하지 않는다)
 */
SW_TEST_CASE( ColorTest, PackedValuesRoundTrip )
{
    for ( const uint32 packed : { 0x00000000u, 0xFFFFFFFFu, 0x12345678u, 0x80FF017Fu } )
    {
        SW_EXPECT_EQUAL( packed, Color::fromRGBA( packed ).toRGBA() );
        SW_EXPECT_EQUAL( packed, Color::fromArgb( packed ).toArgb() );
        SW_EXPECT_EQUAL( packed, Color::fromAbgr( packed ).toAbgr() );
    }
    SW_EXPECT_TRUE( Color::fromRGBA( 0xFF8000FFu ) == Color::fromArgb( Color::fromRGBA( 0xFF8000FFu ).toArgb() ) );
}

/**
 * @brief [ColorTest] 패킹은 0~1 밖의 값을 자르고 가장 가까운 바이트로 반올림한다
 */
SW_TEST_CASE( ColorTest, PackingClampsAndRounds )
{
    SW_EXPECT_EQUAL( 0xFF0000FFu, Color( 2.0f, -1.0f, 0.0f, 1.0f ).toRGBA() );
    SW_EXPECT_EQUAL( 0x80808080u, Color( 0.5f, 0.5f, 0.5f, 0.5f ).toRGBA() ); // 127.5 → 128
}

/**
 * @brief [ColorTest] 16진 글자 — #RGB · #RGBA · #RRGGBB · #RRGGBBAA(# 생략 · 대소문자 무관), 틀린 글자는 값을 바꾸지 않고 false
 */
SW_TEST_CASE( ColorTest, HexParsingAcceptsTheFourShapes )
{
    Color color = Color::kBlack;
    SW_EXPECT_TRUE( Color::fromHex( "#F80", color ) );
    SW_EXPECT_EQUAL( 0xFF8800FFu, color.toRGBA() );
    SW_EXPECT_TRUE( Color::fromHex( "f808", color ) );
    SW_EXPECT_EQUAL( 0xFF880088u, color.toRGBA() );
    SW_EXPECT_TRUE( Color::fromHex( "#1a2B3c", color ) );
    SW_EXPECT_EQUAL( 0x1A2B3CFFu, color.toRGBA() );
    SW_EXPECT_TRUE( Color::fromHex( "#1A2B3C4D", color ) );
    SW_EXPECT_EQUAL( 0x1A2B3C4Du, color.toRGBA() );

    const uint32 kept = color.toRGBA();
    for ( const utf8* pBad : { "", "#", "#12", "#12345", "#1234567", "#GGGGGG", "#12 456" } )
    {
        SW_EXPECT_FALSE( Color::fromHex( pBad, color ) );
        SW_EXPECT_EQUAL( kept, color.toRGBA() );
    }
}

/**
 * @brief [ColorTest] toHex 는 #RRGGBBAA 이고 fromHex 로 되돌아온다
 */
SW_TEST_CASE( ColorTest, HexTextRoundTrips )
{
    const Color color = Color::fromRGBA( 0x0A1B2C3Du );
    utf8        text[10]{};
    color.toHex( text );
    SW_EXPECT_EQUAL( string_view( "#0A1B2C3D" ), string_view( text ) );
    Color back;
    SW_EXPECT_TRUE( Color::fromHex( text, back ) );
    SW_EXPECT_TRUE( back == color );
}

/**
 * @brief [ColorTest] sRGB ↔ 선형은 서로 역이고 알파는 그대로다 — 중간 회색의 sRGB 0.5 는 선형 0.2140
 */
SW_TEST_CASE( ColorTest, SrgbConversionRoundTrips )
{
    const Color srgb( 0.5f, 0.25f, 1.0f, 0.3f );
    const Color linear = srgb.toLinear();
    SW_EXPECT_NEAR( 0.2140f, linear._r, 1e-3f );
    SW_EXPECT_NEAR( 0.3f, linear._a, 1e-6f );
    const Color back = linear.toSRGB();
    SW_EXPECT_NEAR( srgb._r, back._r, 1e-4f );
    SW_EXPECT_NEAR( srgb._g, back._g, 1e-4f );
    SW_EXPECT_NEAR( srgb._b, back._b, 1e-4f );
}

/**
 * @brief [ColorTest] 알파 · 보간 · 미리 곱한 알파 · 밝기 가공
 */
SW_TEST_CASE( ColorTest, ModifiersKeepTheOtherChannels )
{
    const Color color( 0.2f, 0.4f, 0.6f, 0.5f );
    SW_EXPECT_NEAR( 0.9f, color.withAlpha( 0.9f )._a, 1e-6f );
    SW_EXPECT_NEAR( 0.4f, color.withAlpha( 0.9f )._g, 1e-6f );
    SW_EXPECT_NEAR( 0.25f, color.scaleAlpha( 0.5f )._a, 1e-6f );
    SW_EXPECT_NEAR( 0.3f, color.premultiply()._b, 1e-6f );
    SW_EXPECT_NEAR( 0.5f, color.premultiply()._a, 1e-6f );
    SW_EXPECT_NEAR( 0.4f, color.scaleBrightness( 2.0f )._r, 1e-6f );
    SW_EXPECT_NEAR( 0.5f, Color::lerp( Color::kBlack, Color::kWhite, 0.5f )._r, 1e-6f );
    SW_EXPECT_TRUE( Color::fromFloat4( color.toFloat4() ) == color );
}

/**
 * @brief [ColorTest] saturate 는 네 채널을 0~1 로 자르고 범위 안 값은 그대로 둔다
 */
SW_TEST_CASE( ColorTest, SaturateClampsEveryChannel )
{
    const Color clamped = Color( 1.5f, -0.25f, 0.5f, 2.0f ).saturate();
    SW_EXPECT_NEAR( 1.0f, clamped._r, 1e-6f );
    SW_EXPECT_NEAR( 0.0f, clamped._g, 1e-6f );
    SW_EXPECT_NEAR( 0.5f, clamped._b, 1e-6f );
    SW_EXPECT_NEAR( 1.0f, clamped._a, 1e-6f );
    const Color inside( 0.2f, 0.4f, 0.6f, 0.8f );
    SW_EXPECT_TRUE( inside.saturate() == inside );
}

/**
 * @brief [ColorTest] HSV(fromHueSaturationValue) — 색상환의 여섯 꼭짓점이 순수 색이고(색상 1 은 한 바퀴라 빨강), toHueSaturationValue 는 그 역, 무채색은 색상 · 채도 0 이다
 */
SW_TEST_CASE( ColorTest, HueSaturationValueRoundTripsAndHitsPrimaryColors )
{
    SW_EXPECT_TRUE( Color::fromHueSaturationValue( 0.0f, 1.0f, 1.0f ).nearEqual( Color::kRed, 1e-6f ) );
    SW_EXPECT_TRUE( Color::fromHueSaturationValue( 1.0f / 3.0f, 1.0f, 1.0f ).nearEqual( Color::kGreen, 1e-5f ) );
    SW_EXPECT_TRUE( Color::fromHueSaturationValue( 2.0f / 3.0f, 1.0f, 1.0f ).nearEqual( Color::kBlue, 1e-5f ) );
    SW_EXPECT_TRUE( Color::fromHueSaturationValue( 1.0f / 6.0f, 1.0f, 1.0f ).nearEqual( Color::kYellow, 1e-5f ) );
    SW_EXPECT_TRUE( Color::fromHueSaturationValue( 1.0f, 1.0f, 1.0f ).nearEqual( Color::kRed, 1e-6f ) );
    SW_EXPECT_TRUE( Color::fromHueSaturationValue( 0.3f, 0.0f, 0.5f ).nearEqual( Color::kGray, 1e-6f ) );
    SW_EXPECT_NEAR( 0.25f, Color::fromHueSaturationValue( 0.0f, 1.0f, 1.0f, 0.25f )._a, 1e-6f );

    const Color  warm   = Color::fromBytes( 255, 128, 32 );
    const float3 values = warm.toHueSaturationValue();
    SW_EXPECT_TRUE( Color::fromHueSaturationValue( values._x, values._y, values._z ).nearEqual( warm, 1e-5f ) );
    const float3 grayValues = Color::kGray.toHueSaturationValue();
    SW_EXPECT_EQUAL( 0.0f, grayValues._x );
    SW_EXPECT_EQUAL( 0.0f, grayValues._y );
    SW_EXPECT_NEAR( 0.5f, grayValues._z, 1e-6f );
    const float3 redBlueValues = Color( 1.0f, 0.0f, 0.5f ).toHueSaturationValue(); // 빨강이 가장 크고 파랑이 초록보다 크면 색상이 1 쪽(음수를 감는다)
    SW_EXPECT_NEAR( 11.0f / 12.0f, redBlueValues._x, 1e-5f );
}

/**
 * @brief [ColorTest] nearEqual 은 허용치로 비교한다, 패킹은 NaN 채널을 0 으로 담는다(예전에는 clamp 가 NaN 을 넘겨 정수 변환이 정의되지 않았다)
 */
SW_TEST_CASE( ColorTest, NearEqualAndNanPacking )
{
    const Color color( 0.2f, 0.4f, 0.6f, 0.5f );
    SW_EXPECT_TRUE( color.nearEqual( Color( 0.21f, 0.4f, 0.6f, 0.5f ), 0.02f ) );
    SW_EXPECT_FALSE( color.nearEqual( Color( 0.25f, 0.4f, 0.6f, 0.5f ), 0.02f ) );
    SW_EXPECT_EQUAL( 0x00FF00FFu, Color( std::numeric_limits<float32>::quiet_NaN(), 1.0f, 0.0f, 1.0f ).toRGBA() );
}
