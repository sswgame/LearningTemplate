#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Automation/Runner/AutomationImage.h"

using namespace sw;

namespace
{
    struct AutomationImageMetricTestUtil
    {
        /** @brief 화소의 세 채널이 모두 @p value 인 그림입니다. */
        static AutomationImage makeImage( uint32 width, uint32 height, uint8 value )
        {
            AutomationImage image;
            image._width  = width;
            image._height = height;
            image._listRGB.assign( static_cast<size_t>( width ) * height * 3u, value );
            return image;
        }

        static vector<uint8> makePpmBytes( string_view header, size_t pixelByteCount )
        {
            vector<uint8> listByte( header.begin(), header.end() );
            for ( size_t byteIndex = 0; byteIndex < pixelByteCount; ++byteIndex )
            {
                listByte.push_back( static_cast<uint8>( byteIndex ) );
            }
            return listByte;
        }

        static uint8* getPixel( AutomationImage& image, uint32 x, uint32 y ) { return &image._listRGB[( static_cast<size_t>( y ) * image._width + x ) * 3u]; }

        /** @brief 틀린 PPM 은 false 와 이유이고, 출력 그림을 건드리지 않습니다. */
        static void expectParseFails( const vector<uint8>& listByte )
        {
            AutomationImage image;
            image._width  = 7;
            image._height = 9;
            string error;
            SW_EXPECT_FALSE( AutomationImage::parsePpm( listByte.data(), listByte.size(), image, error ) );
            SW_EXPECT_FALSE( error.empty() );
            SW_EXPECT_EQUAL( 7u, image._width );
            SW_EXPECT_EQUAL( 9u, image._height );
        }
    };
    using Util = AutomationImageMetricTestUtil;
} // namespace

/** @brief [AutomationImageMetricTest] 쓴 PPM 을 그대로 읽는다 */
SW_TEST_CASE( AutomationImageMetricTest, PpmRoundTrip )
{
    AutomationImage source = Util::makeImage( 2, 3, 0 );
    for ( size_t byteIndex = 0; byteIndex < source._listRGB.size(); ++byteIndex )
    {
        source._listRGB[byteIndex] = static_cast<uint8>( byteIndex * 10u );
    }
    vector<uint8> listByte;
    AutomationImage::encodePpm( source, listByte );
    const string_view expectedHeader = "P6\n2 3\n255\n";
    SW_REQUIRE_EQUAL( expectedHeader.size() + source._listRGB.size(), listByte.size() );
    SW_EXPECT_TRUE( string_view( reinterpret_cast<const utf8*>( listByte.data() ), expectedHeader.size() ) == expectedHeader );

    AutomationImage image;
    string          error;
    SW_REQUIRE_TRUE( AutomationImage::parsePpm( listByte.data(), listByte.size(), image, error ) );
    SW_EXPECT_EQUAL( 2u, image._width );
    SW_EXPECT_EQUAL( 3u, image._height );
    SW_EXPECT_TRUE( image._listRGB == source._listRGB );
}

/** @brief [AutomationImageMetricTest] PPM 은 신뢰 경계다 — 어느 길이로 잘려도 false 와 이유이고 출력을 건드리지 않는다 */
SW_TEST_CASE( AutomationImageMetricTest, EveryTruncatedLengthIsRejected )
{
    vector<uint8> listByte;
    AutomationImage::encodePpm( Util::makeImage( 2, 2, 7 ), listByte );
    for ( size_t length = 0; length < listByte.size(); ++length )
    {
        Util::expectParseFails( vector<uint8>( listByte.begin(), listByte.begin() + static_cast<ptrdiff_t>( length ) ) );
    }
}

/** @brief [AutomationImageMetricTest] 바이트가 없거나 매직이 다르면 PPM 이 아니다(P3 · P5 도 거절한다) */
SW_TEST_CASE( AutomationImageMetricTest, WrongMagicIsRejected )
{
    AutomationImage image;
    string          error;
    SW_EXPECT_FALSE( AutomationImage::parsePpm( nullptr, 64, image, error ) );
    SW_EXPECT_FALSE( error.empty() );
    Util::expectParseFails( Util::makePpmBytes( "P3\n1 1\n255\n", 3 ) );
    Util::expectParseFails( Util::makePpmBytes( "P5\n1 1\n255\n", 3 ) );
    Util::expectParseFails( Util::makePpmBytes( "p6\n1 1\n255\n", 3 ) );
}

/** @brief [AutomationImageMetricTest] 머리말 수는 세 개가 있어야 하고 크기 0 · 16 비트 최대값은 거절한다 */
SW_TEST_CASE( AutomationImageMetricTest, BrokenHeaderNumbersAreRejected )
{
    Util::expectParseFails( Util::makePpmBytes( "P6\n0 1\n255\n", 3 ) );
    Util::expectParseFails( Util::makePpmBytes( "P6\n1 0\n255\n", 3 ) );
    Util::expectParseFails( Util::makePpmBytes( "P6\n1 1\n65535\n", 6 ) );
    Util::expectParseFails( Util::makePpmBytes( "P6\n1 1\n254\n", 3 ) );
    Util::expectParseFails( Util::makePpmBytes( "P6\n-1 1\n255\n", 3 ) );
    Util::expectParseFails( Util::makePpmBytes( "P6\nwide 1\n255\n", 3 ) );
    Util::expectParseFails( Util::makePpmBytes( "P6\n1\n255\n", 3 ) );
}

/** @brief [AutomationImageMetricTest] 최대값 뒤에는 공백 하나가 와야 한다 — 다른 바이트면 머리말이 어디서 끝나는지 알 수 없다 */
SW_TEST_CASE( AutomationImageMetricTest, HeaderMustEndWithWhitespace )
{
    Util::expectParseFails( Util::makePpmBytes( "P6\n1 1\n255X", 3 ) );
    const uint8 arrTerminator[] = { ' ', '\t', '\r', '\n' };
    for ( const uint8 terminator : arrTerminator )
    {
        vector<uint8> listByte = Util::makePpmBytes( "P6\n1 1\n255", 0 );
        listByte.push_back( terminator );
        listByte.push_back( 1 );
        listByte.push_back( 2 );
        listByte.push_back( 3 );
        AutomationImage image;
        string          error;
        SW_REQUIRE_TRUE( AutomationImage::parsePpm( listByte.data(), listByte.size(), image, error ) );
        SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( image._listRGB.size() ) );
        SW_EXPECT_EQUAL( 1u, static_cast<uint32>( image._listRGB[0] ) );
    }
}

/** @brief [AutomationImageMetricTest] 머리말이 말한 크기보다 화소가 짧으면 거절하고, 곱이 커도 넘치지 않는다. 뒤에 남는 바이트는 쓰지 않는다 */
SW_TEST_CASE( AutomationImageMetricTest, PixelLengthFollowsTheHeader )
{
    Util::expectParseFails( Util::makePpmBytes( "P6\n2 2\n255\n", 11 ) );
    Util::expectParseFails( Util::makePpmBytes( "P6\n999999999 999999999\n255\n", 3 ) );

    const vector<uint8> listLong = Util::makePpmBytes( "P6\n1 1\n255\n", 7 );
    AutomationImage     image;
    string              error;
    SW_REQUIRE_TRUE( AutomationImage::parsePpm( listLong.data(), listLong.size(), image, error ) );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( image._listRGB.size() ) );
}

/** @brief [AutomationImageMetricTest] 머리말의 주석과 여러 공백은 건너뛴다. 주석이 끝나지 않고 파일이 끝나면 틀린 머리말이다 */
SW_TEST_CASE( AutomationImageMetricTest, HeaderCommentsAndWhitespace )
{
    const vector<uint8> listByte = Util::makePpmBytes( "P6 # made by a tool\n# second line\n\t2  \r\n1 # height\n255\n", 6 );
    AutomationImage     image;
    string              error;
    SW_REQUIRE_TRUE( AutomationImage::parsePpm( listByte.data(), listByte.size(), image, error ) );
    SW_EXPECT_EQUAL( 2u, image._width );
    SW_EXPECT_EQUAL( 1u, image._height );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( image._listRGB[0] ) );
    SW_EXPECT_EQUAL( 5u, static_cast<uint32>( image._listRGB[5] ) );

    Util::expectParseFails( Util::makePpmBytes( "P6\n1 1\n# comment without an end", 0 ) );
}

/** @brief [AutomationImageMetricTest] 파일 읽기 — 있으면 내용이고, 없으면 경로를 담은 이유다 */
SW_TEST_CASE( AutomationImageMetricTest, LoadPpmFromFile )
{
    SW_EXPECT_LOG( "File.NotFound" );
    vector<uint8> listByte;
    AutomationImage::encodePpm( Util::makeImage( 1, 1, 9 ), listByte );
    const string path = test::makeScratchPath( "image.ppm" );
    SW_REQUIRE_TRUE( FileUtil::writeFile( path, listByte.data(), listByte.size() ) );

    AutomationImage image;
    string          error;
    SW_REQUIRE_TRUE( AutomationImage::loadPpm( path, image, error ) );
    SW_EXPECT_EQUAL( 9u, static_cast<uint32>( image._listRGB[2] ) );

    const string missing = test::makeScratchPath( "missing.ppm" );
    SW_EXPECT_FALSE( AutomationImage::loadPpm( missing, image, error ) );
    SW_EXPECT_TRUE( error.find( "missing.ppm" ) != string::npos );
}

/** @brief [AutomationImageMetricTest] 영역 글 — 네 수(공백 허용)이고 0..1 안, 앞 값이 뒤 값보다 작아야 한다. 틀리면 출력을 건드리지 않는다 */
SW_TEST_CASE( AutomationImageMetricTest, RegionParsing )
{
    AutomationImageRegion region;
    SW_REQUIRE_TRUE( AutomationImage::tryParseRegion( "0.25, 0, 1, 0.75", region ) );
    SW_EXPECT_NEAR( 0.25f, region._x0, 0.0f );
    SW_EXPECT_NEAR( 0.0f, region._y0, 0.0f );
    SW_EXPECT_NEAR( 1.0f, region._x1, 0.0f );
    SW_EXPECT_NEAR( 0.75f, region._y1, 0.0f );

    const utf8* const arrBad[] = {
        "",
        "0,0,1",
        "0,0,1,1,1",
        "a,0,1,1",
        "-0.1,0,1,1",
        "0,0,1.1,1",
        "0.5,0,0.5,1", // 너비 0
        "0,0.5,1,0.5", // 높이 0
        "1,0,0,1",     // 뒤집힘
        "nan,0,1,1",
        "0,0,inf,1",
    };
    for ( const utf8* pText : arrBad )
    {
        AutomationImageRegion untouched;
        untouched._x0 = 0.125f;
        SW_EXPECT_FALSE_MSG( AutomationImage::tryParseRegion( pText, untouched ), pText );
        SW_EXPECT_NEAR( 0.125f, untouched._x0, 0.0f );
    }
}

/** @brief [AutomationImageMetricTest] 골든 폭보다 작은 영역은 화소 그대로다 — 영역의 위치가 맞는다 */
SW_TEST_CASE( AutomationImageMetricTest, ReduceSmallRegionKeepsPixels )
{
    AutomationImage image            = Util::makeImage( 4, 2, 0 );
    Util::getPixel( image, 2, 1 )[0] = 100;
    Util::getPixel( image, 3, 1 )[1] = 200;

    AutomationImageRegion region;
    region._x0 = 0.5f;
    region._y0 = 0.5f;
    AutomationImage reduced;
    string          error;
    SW_REQUIRE_TRUE( AutomationImage::reduce( image, region, reduced, error ) );
    SW_REQUIRE_EQUAL( 2u, reduced._width );
    SW_REQUIRE_EQUAL( 1u, reduced._height );
    SW_EXPECT_EQUAL( 100u, static_cast<uint32>( reduced._listRGB[0] ) );
    SW_EXPECT_EQUAL( 200u, static_cast<uint32>( reduced._listRGB[4] ) );
}

/** @brief [AutomationImageMetricTest] 넓은 영역은 가로를 골든 폭으로 상자 평균해 줄인다(반올림) — 세로는 비율대로 */
SW_TEST_CASE( AutomationImageMetricTest, ReduceWideRegionAverages )
{
    const uint32    sourceWidth = AutomationImage::kGoldenMaxWidth * 2u;
    AutomationImage image       = Util::makeImage( sourceWidth, 4, 0 );
    // 첫 2 × 2 상자의 빨강 합이 3 이다 — 평균 0.75 는 1 로 반올림한다.
    Util::getPixel( image, 0, 0 )[0] = 1;
    Util::getPixel( image, 1, 0 )[0] = 1;
    Util::getPixel( image, 0, 1 )[0] = 1;
    // 둘째 상자는 평균 100 이다.
    for ( uint32 y = 0; y < 2; ++y )
    {
        for ( uint32 x = 2; x < 4; ++x )
        {
            Util::getPixel( image, x, y )[1] = 100;
        }
    }

    AutomationImage reduced;
    string          error;
    SW_REQUIRE_TRUE( AutomationImage::reduce( image, AutomationImageRegion{}, reduced, error ) );
    SW_REQUIRE_EQUAL( AutomationImage::kGoldenMaxWidth, reduced._width );
    SW_REQUIRE_EQUAL( 2u, reduced._height );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( reduced._listRGB[0] ) );
    SW_EXPECT_EQUAL( 100u, static_cast<uint32>( reduced._listRGB[4] ) );
}

/** @brief [AutomationImageMetricTest] 아주 납작한 영역도 세로는 한 줄 남는다(0 줄로 나누지 않는다) */
SW_TEST_CASE( AutomationImageMetricTest, ReduceKeepsAtLeastOneRow )
{
    AutomationImage image = Util::makeImage( 1000, 1, 50 );
    AutomationImage reduced;
    string          error;
    SW_REQUIRE_TRUE( AutomationImage::reduce( image, AutomationImageRegion{}, reduced, error ) );
    SW_EXPECT_EQUAL( AutomationImage::kGoldenMaxWidth, reduced._width );
    SW_EXPECT_EQUAL( 1u, reduced._height );
    SW_EXPECT_EQUAL( 50u, static_cast<uint32>( reduced._listRGB[0] ) );
}

/** @brief [AutomationImageMetricTest] 화소 하나도 덮지 않는 영역과 빈 그림은 이유와 함께 거절한다 */
SW_TEST_CASE( AutomationImageMetricTest, ReduceRejectsEmptyCoverage )
{
    const AutomationImage image = Util::makeImage( 10, 10, 0 );
    AutomationImageRegion tiny;
    tiny._x1 = 0.02f;
    tiny._y1 = 0.02f;
    AutomationImage reduced;
    string          error;
    SW_EXPECT_FALSE( AutomationImage::reduce( image, tiny, reduced, error ) );
    SW_EXPECT_FALSE( error.empty() );

    error.clear();
    SW_EXPECT_FALSE( AutomationImage::reduce( AutomationImage{}, AutomationImageRegion{}, reduced, error ) );
    SW_EXPECT_FALSE( error.empty() );
}

/** @brief [AutomationImageMetricTest] 크기가 다르면 비교하지 않고 다시 뜨라는 이유를 낸다(가로 · 세로 어느 쪽이든) */
SW_TEST_CASE( AutomationImageMetricTest, CompareRejectsDifferentSizes )
{
    AutomationImageDifference difference;
    string                    error;
    SW_EXPECT_FALSE( AutomationImage::compare( Util::makeImage( 2, 2, 0 ), Util::makeImage( 3, 2, 0 ), difference, error ) );
    SW_EXPECT_TRUE( error.find( "2x2" ) != string::npos );
    SW_EXPECT_TRUE( error.find( "3x2" ) != string::npos );
    error.clear();
    SW_EXPECT_FALSE( AutomationImage::compare( Util::makeImage( 2, 2, 0 ), Util::makeImage( 2, 3, 0 ), difference, error ) );
    SW_EXPECT_FALSE( error.empty() );
}

/** @brief [AutomationImageMetricTest] 같은 그림은 차이가 0 이고 빈 그림끼리도 0 이다(0 으로 나누지 않는다) */
SW_TEST_CASE( AutomationImageMetricTest, CompareIdenticalImages )
{
    AutomationImageDifference difference;
    difference._differingFraction = 1.0;
    difference._meanDistance      = 1.0;
    string error;
    SW_REQUIRE_TRUE( AutomationImage::compare( Util::makeImage( 4, 4, 33 ), Util::makeImage( 4, 4, 33 ), difference, error ) );
    SW_EXPECT_NEAR( 0.0, difference._differingFraction, 0.0 );
    SW_EXPECT_NEAR( 0.0, difference._meanDistance, 0.0 );

    difference._differingFraction = 1.0;
    SW_REQUIRE_TRUE( AutomationImage::compare( AutomationImage{}, AutomationImage{}, difference, error ) );
    SW_EXPECT_NEAR( 0.0, difference._differingFraction, 0.0 );
}

/** @brief [AutomationImageMetricTest] "다른 화소" 의 경계 — 세 채널 차의 합이 하한 미만이면 세지 않고 하한부터 센다. 평균 거리는 모두 더한다 */
SW_TEST_CASE( AutomationImageMetricTest, CompareDifferingThreshold )
{
    const AutomationImage golden = Util::makeImage( 2, 1, 100 );
    const uint32          limit  = AutomationImage::kDifferingMinDistance;

    AutomationImage below = golden;
    below._listRGB[0]     = static_cast<uint8>( 100u + ( limit - 1u ) );
    AutomationImageDifference difference;
    string                    error;
    SW_REQUIRE_TRUE( AutomationImage::compare( below, golden, difference, error ) );
    SW_EXPECT_NEAR( 0.0, difference._differingFraction, 0.0 );
    SW_EXPECT_NEAR( static_cast<float64>( limit - 1u ) / ( 2.0 * 765.0 ), difference._meanDistance, 0.0000001 );

    // 채널 셋에 나눠도 합이 같으면 같고, 부호는 보지 않는다.
    AutomationImage spread = golden;
    spread._listRGB[3]     = static_cast<uint8>( 100u - 4u );
    spread._listRGB[4]     = static_cast<uint8>( 100u + 4u );
    spread._listRGB[5]     = static_cast<uint8>( 100u - ( limit - 8u ) );
    SW_REQUIRE_TRUE( AutomationImage::compare( spread, golden, difference, error ) );
    SW_EXPECT_NEAR( 0.5, difference._differingFraction, 0.0 );
    SW_EXPECT_NEAR( static_cast<float64>( limit ) / ( 2.0 * 765.0 ), difference._meanDistance, 0.0000001 );

    // 가장 먼 값 — 평균 거리는 1 이다.
    AutomationImageDifference farthest;
    SW_REQUIRE_TRUE( AutomationImage::compare( Util::makeImage( 1, 1, 0 ), Util::makeImage( 1, 1, 255 ), farthest, error ) );
    SW_EXPECT_NEAR( 1.0, farthest._differingFraction, 0.0 );
    SW_EXPECT_NEAR( 1.0, farthest._meanDistance, 0.0 );
}
