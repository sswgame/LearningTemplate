#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Client/Automation/Runner/AutomationImage.h"

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

        /** @brief 테스트의 측정 문턱 — 정책 데이터의 기본(`Config/Dev/GoldenImage.json`)과 같은 값이다. */
        static constexpr AutomationImageThreshold kMeasure{ 12, 96 };

        static uint8* getPixel( AutomationImage& image, uint32 x, uint32 y ) { return &image._listRGB[( static_cast<size_t>( y ) * image._width + x ) * 3u]; }

        /** @brief 틀린 PPM 은 이유가 있는 오류입니다. */
        static void expectParseFails( const vector<uint8>& listByte )
        {
            const expected<AutomationImage> image = AutomationImage::parsePpm( listByte.data(), listByte.size() );
            SW_REQUIRE_FALSE( image.has_value() );
            SW_EXPECT_FALSE( image.error().message().empty() );
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

    const expected<AutomationImage> image = AutomationImage::parsePpm( listByte.data(), listByte.size() );
    SW_REQUIRE_TRUE( image.has_value() );
    SW_EXPECT_EQUAL( 2u, image->_width );
    SW_EXPECT_EQUAL( 3u, image->_height );
    SW_EXPECT_TRUE( image->_listRGB == source._listRGB );
}

/** @brief [AutomationImageMetricTest] PPM 은 신뢰 경계다 — 어느 길이로 잘려도 이유가 있는 오류다 */
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
    SW_EXPECT_FALSE( AutomationImage::parsePpm( nullptr, 64 ).has_value() );
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
        const expected<AutomationImage> image = AutomationImage::parsePpm( listByte.data(), listByte.size() );
        SW_REQUIRE_TRUE( image.has_value() );
        SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( image->_listRGB.size() ) );
        SW_EXPECT_EQUAL( 1u, static_cast<uint32>( image->_listRGB[0] ) );
    }
}

/** @brief [AutomationImageMetricTest] 머리말이 말한 크기보다 화소가 짧으면 거절하고, 곱이 커도 넘치지 않는다. 뒤에 남는 바이트는 쓰지 않는다 */
SW_TEST_CASE( AutomationImageMetricTest, PixelLengthFollowsTheHeader )
{
    Util::expectParseFails( Util::makePpmBytes( "P6\n2 2\n255\n", 11 ) );
    Util::expectParseFails( Util::makePpmBytes( "P6\n999999999 999999999\n255\n", 3 ) );

    const vector<uint8>             listLong = Util::makePpmBytes( "P6\n1 1\n255\n", 7 );
    const expected<AutomationImage> image    = AutomationImage::parsePpm( listLong.data(), listLong.size() );
    SW_REQUIRE_TRUE( image.has_value() );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( image->_listRGB.size() ) );
}

/** @brief [AutomationImageMetricTest] 머리말의 주석과 여러 공백은 건너뛴다. 주석이 끝나지 않고 파일이 끝나면 틀린 머리말이다 */
SW_TEST_CASE( AutomationImageMetricTest, HeaderCommentsAndWhitespace )
{
    const vector<uint8>             listByte = Util::makePpmBytes( "P6 # made by a tool\n# second line\n\t2  \r\n1 # height\n255\n", 6 );
    const expected<AutomationImage> image    = AutomationImage::parsePpm( listByte.data(), listByte.size() );
    SW_REQUIRE_TRUE( image.has_value() );
    SW_EXPECT_EQUAL( 2u, image->_width );
    SW_EXPECT_EQUAL( 1u, image->_height );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( image->_listRGB[0] ) );
    SW_EXPECT_EQUAL( 5u, static_cast<uint32>( image->_listRGB[5] ) );

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

    const expected<AutomationImage> image = AutomationImage::loadPpm( path );
    SW_REQUIRE_TRUE( image.has_value() );
    SW_EXPECT_EQUAL( 9u, static_cast<uint32>( image->_listRGB[2] ) );

    const string                    missing  = test::makeScratchPath( "missing.ppm" );
    const expected<AutomationImage> notThere = AutomationImage::loadPpm( missing );
    SW_REQUIRE_FALSE( notThere.has_value() );
    SW_EXPECT_TRUE( notThere.error().detail().find( "missing.ppm" ) != string_view::npos );
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
    region._x0                              = 0.5f;
    region._y0                              = 0.5f;
    const expected<AutomationImage> reduced = AutomationImage::reduce( image, region );
    SW_REQUIRE_TRUE( reduced.has_value() );
    SW_REQUIRE_EQUAL( 2u, reduced->_width );
    SW_REQUIRE_EQUAL( 1u, reduced->_height );
    SW_EXPECT_EQUAL( 100u, static_cast<uint32>( reduced->_listRGB[0] ) );
    SW_EXPECT_EQUAL( 200u, static_cast<uint32>( reduced->_listRGB[4] ) );
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

    const expected<AutomationImage> reduced = AutomationImage::reduce( image, AutomationImageRegion{} );
    SW_REQUIRE_TRUE( reduced.has_value() );
    SW_REQUIRE_EQUAL( AutomationImage::kGoldenMaxWidth, reduced->_width );
    SW_REQUIRE_EQUAL( 2u, reduced->_height );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( reduced->_listRGB[0] ) );
    SW_EXPECT_EQUAL( 100u, static_cast<uint32>( reduced->_listRGB[4] ) );
}

/** @brief [AutomationImageMetricTest] 아주 납작한 영역도 세로는 한 줄 남는다(0 줄로 나누지 않는다) */
SW_TEST_CASE( AutomationImageMetricTest, ReduceKeepsAtLeastOneRow )
{
    const AutomationImage           image   = Util::makeImage( 1000, 1, 50 );
    const expected<AutomationImage> reduced = AutomationImage::reduce( image, AutomationImageRegion{} );
    SW_REQUIRE_TRUE( reduced.has_value() );
    SW_EXPECT_EQUAL( AutomationImage::kGoldenMaxWidth, reduced->_width );
    SW_EXPECT_EQUAL( 1u, reduced->_height );
    SW_EXPECT_EQUAL( 50u, static_cast<uint32>( reduced->_listRGB[0] ) );
}

/** @brief [AutomationImageMetricTest] 화소 하나도 덮지 않는 영역과 빈 그림은 이유와 함께 거절한다 */
SW_TEST_CASE( AutomationImageMetricTest, ReduceRejectsEmptyCoverage )
{
    const AutomationImage image = Util::makeImage( 10, 10, 0 );
    AutomationImageRegion tiny;
    tiny._x1                                    = 0.02f;
    tiny._y1                                    = 0.02f;
    const expected<AutomationImage> tinyReduced = AutomationImage::reduce( image, tiny );
    SW_REQUIRE_FALSE( tinyReduced.has_value() );
    SW_EXPECT_TRUE( tinyReduced.error().detail() == "10x10" );

    const expected<AutomationImage> emptyReduced = AutomationImage::reduce( AutomationImage{}, AutomationImageRegion{} );
    SW_REQUIRE_FALSE( emptyReduced.has_value() );
    SW_EXPECT_TRUE( emptyReduced.error().detail() == "0x0" );
}

/** @brief [AutomationImageMetricTest] 크기가 다르면 비교하지 않고 다시 뜨라는 이유를 낸다(가로 · 세로 어느 쪽이든) */
SW_TEST_CASE( AutomationImageMetricTest, CompareRejectsDifferentSizes )
{
    const expected<AutomationImageDifference> wider = AutomationImage::compare( Util::makeImage( 2, 2, 0 ), Util::makeImage( 3, 2, 0 ), Util::kMeasure );
    SW_REQUIRE_FALSE( wider.has_value() );
    SW_EXPECT_TRUE( wider.error().detail().find( "2x2" ) != string_view::npos );
    SW_EXPECT_TRUE( wider.error().detail().find( "3x2" ) != string_view::npos );
    const expected<AutomationImageDifference> taller = AutomationImage::compare( Util::makeImage( 2, 2, 0 ), Util::makeImage( 2, 3, 0 ), Util::kMeasure );
    SW_REQUIRE_FALSE( taller.has_value() );
    SW_EXPECT_TRUE( taller.error().detail().find( "2x3" ) != string_view::npos );
}

/** @brief [AutomationImageMetricTest] 같은 그림은 차이가 0 이고 빈 그림끼리도 0 이다(0 으로 나누지 않는다) */
SW_TEST_CASE( AutomationImageMetricTest, CompareIdenticalImages )
{
    const expected<AutomationImageDifference> same = AutomationImage::compare( Util::makeImage( 4, 4, 33 ), Util::makeImage( 4, 4, 33 ), Util::kMeasure );
    SW_REQUIRE_TRUE( same.has_value() );
    SW_EXPECT_NEAR( 0.0, same->_differingFraction, 0.0 );
    SW_EXPECT_NEAR( 0.0, same->_meanDistance, 0.0 );

    const expected<AutomationImageDifference> empty = AutomationImage::compare( AutomationImage{}, AutomationImage{}, Util::kMeasure );
    SW_REQUIRE_TRUE( empty.has_value() );
    SW_EXPECT_NEAR( 0.0, empty->_differingFraction, 0.0 );
}

/** @brief [AutomationImageMetricTest] "다른 화소" 의 경계 — 세 채널 차의 합이 하한 미만이면 세지 않고 하한부터 센다. 평균 거리는 모두 더한다 */
SW_TEST_CASE( AutomationImageMetricTest, CompareDifferingThreshold )
{
    const AutomationImage golden = Util::makeImage( 2, 1, 100 );
    const uint32          limit  = Util::kMeasure._differingMinDistance;

    AutomationImage below                                     = golden;
    below._listRGB[0]                                         = static_cast<uint8>( 100u + ( limit - 1u ) );
    const expected<AutomationImageDifference> belowDifference = AutomationImage::compare( below, golden, Util::kMeasure );
    SW_REQUIRE_TRUE( belowDifference.has_value() );
    SW_EXPECT_NEAR( 0.0, belowDifference->_differingFraction, 0.0 );
    SW_EXPECT_NEAR( static_cast<float64>( limit - 1u ) / ( 2.0 * 765.0 ), belowDifference->_meanDistance, 0.0000001 );

    // 채널 셋에 나눠도 합이 같으면 같고, 부호는 보지 않는다.
    AutomationImage spread                                     = golden;
    spread._listRGB[3]                                         = static_cast<uint8>( 100u - 4u );
    spread._listRGB[4]                                         = static_cast<uint8>( 100u + 4u );
    spread._listRGB[5]                                         = static_cast<uint8>( 100u - ( limit - 8u ) );
    const expected<AutomationImageDifference> spreadDifference = AutomationImage::compare( spread, golden, Util::kMeasure );
    SW_REQUIRE_TRUE( spreadDifference.has_value() );
    SW_EXPECT_NEAR( 0.5, spreadDifference->_differingFraction, 0.0 );
    SW_EXPECT_NEAR( static_cast<float64>( limit ) / ( 2.0 * 765.0 ), spreadDifference->_meanDistance, 0.0000001 );

    // 가장 먼 값 — 평균 거리는 1 이다.
    const expected<AutomationImageDifference> farthest = AutomationImage::compare( Util::makeImage( 1, 1, 0 ), Util::makeImage( 1, 1, 255 ), Util::kMeasure );
    SW_REQUIRE_TRUE( farthest.has_value() );
    SW_EXPECT_NEAR( 1.0, farthest->_differingFraction, 0.0 );
    SW_EXPECT_NEAR( 1.0, farthest->_meanDistance, 0.0 );
    SW_EXPECT_EQUAL( 1u, farthest->_largestDeepRegionPixels );
}

/** @brief [AutomationImageMetricTest] 깊은 화소 덩어리는 4 이웃으로 잇는다 — 대각선은 다른 덩어리이고, 가장 큰 덩어리의 화소 수를 낸다 */
SW_TEST_CASE( AutomationImageMetricTest, CompareLargestDeepRegion )
{
    const AutomationImage golden = Util::makeImage( 6, 4, 0 );
    AutomationImage       actual = golden;
    // 가로 셋이 이어진 덩어리 + 대각선으로만 닿은 화소 하나 + 얕은(깊은 문턱 아래) 화소 하나
    for ( uint32 x = 0; x < 3; ++x )
    {
        Util::getPixel( actual, x, 0 )[0] = 255;
    }
    Util::getPixel( actual, 3, 1 )[0]                    = 255;
    Util::getPixel( actual, 5, 3 )[0]                    = static_cast<uint8>( Util::kMeasure._deepMinDistance - 1u );
    const expected<AutomationImageDifference> difference = AutomationImage::compare( actual, golden, Util::kMeasure );
    SW_REQUIRE_TRUE( difference.has_value() );
    SW_EXPECT_EQUAL( 3u, difference->_largestDeepRegionPixels );
    SW_EXPECT_NEAR( 5.0 / 24.0, difference->_differingFraction, 0.0000001 );
}
