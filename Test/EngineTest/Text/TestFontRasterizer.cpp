#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Resource/ResourceIO.h"
#include "Engine/Text/IFontRasterizer.h"

#include "TestFramework/TestFramework.h"

// FontRasterizerTest — FreeType 래스터라이저: 면 열기 · 글리프 번호 · 메트릭 · SDF 부호 규약. 디바이스 없음(nogpu).

namespace
{
    struct FontRasterizerTestUtil
    {
        static constexpr const utf8* kFontPath = "engine/fonts/kenney_future.ttf";

        static sw::FontFaceID openEngineFace( sw::IFontRasterizer& rasterizer )
        {
            sw::vector<uint8> bytes;
            if ( sw::ResourceIO::readBinaryResource( sw::ResourceID{ kFontPath }, bytes ) == false )
                return sw::kInvalidFontFaceID;
            return rasterizer.loadFace( std::move( bytes ), 0, kFontPath );
        }
    };
} // namespace

/** @brief [FontRasterizerTest] 엔진 글꼴이 열리고 라틴 글리프는 있고 한글 글리프는 없다(대체 사슬이 다음 면으로 가는 근거) */
SW_TEST_CASE( FontRasterizerTest, OpensFaceAndFindsGlyphs )
{
    sw::unique_ptr<sw::IFontRasterizer> rasterizer = sw::IFontRasterizer::createDefault();
    SW_ASSERT_NOT_NULL( rasterizer.get() );
    const sw::FontFaceID face = FontRasterizerTestUtil::openEngineFace( *rasterizer );
    SW_ASSERT_TRUE( face != sw::kInvalidFontFaceID );

    SW_EXPECT_NOT_EQUAL( 0u, rasterizer->findGlyphIndex( face, 'A' ) );
    SW_EXPECT_EQUAL( 0u, rasterizer->findGlyphIndex( face, 0xAC00u ) ); // '가'

    sw::FontFaceMetrics faceMetrics{};
    SW_ASSERT_TRUE( rasterizer->findFaceMetrics( face, faceMetrics ) );
    SW_EXPECT_TRUE( 0.5f < faceMetrics._ascender && faceMetrics._ascender < 1.5f );
    SW_EXPECT_TRUE( faceMetrics._descender < 0.0f );

    sw::GlyphMetrics glyphMetrics{};
    SW_ASSERT_TRUE( rasterizer->findGlyphMetrics( face, rasterizer->findGlyphIndex( face, 'A' ), glyphMetrics ) );
    SW_EXPECT_TRUE( 0.2f < glyphMetrics._advance && glyphMetrics._advance < 1.5f );
    SW_EXPECT_NOT_NULL( rasterizer->findFaceBytes( face ) );

    rasterizer->unloadFace( face );
    SW_EXPECT_NULL( rasterizer->findFaceBytes( face ) );
    SW_EXPECT_EQUAL( 0u, rasterizer->findGlyphIndex( face, 'A' ) );
}

/** @brief [FontRasterizerTest] SDF 는 안쪽이 밝고(128 위) 먼 바깥(모서리)이 어둡다 — 셰이더의 부호 규약이다 */
SW_TEST_CASE( FontRasterizerTest, SdfIsBrightInsideTheGlyph )
{
    sw::unique_ptr<sw::IFontRasterizer> rasterizer = sw::IFontRasterizer::createDefault();
    SW_ASSERT_NOT_NULL( rasterizer.get() );
    const sw::FontFaceID face = FontRasterizerTestUtil::openEngineFace( *rasterizer );
    SW_ASSERT_TRUE( face != sw::kInvalidFontFaceID );

    sw::SdfGlyphBitmap bitmap{};
    SW_ASSERT_TRUE( rasterizer->rasterizeSdf( face, rasterizer->findGlyphIndex( face, 'H' ), sw::SdfRasterParams{}, bitmap ) );
    SW_ASSERT_TRUE( bitmap._width > 12 && bitmap._height > 12 );
    SW_ASSERT_EQUAL( static_cast<size_t>( bitmap._width ) * bitmap._height, bitmap._bytes.size() );
    uint8 maxValue = 0;
    for ( const uint8 value : bitmap._bytes )
    {
        maxValue = value > maxValue ? value : maxValue;
    }
    SW_EXPECT_TRUE( maxValue >= 200 );             // 획 한가운데
    SW_EXPECT_TRUE( bitmap._bytes.front() <= 40 ); // 왼쪽 위 모서리는 윤곽에서 spread 만큼 바깥
    SW_EXPECT_TRUE( bitmap._bytes.back() <= 40 );
    SW_EXPECT_TRUE( bitmap._advancePx > 0.0f );
}

/** @brief [FontRasterizerTest] 공백은 래스터화가 성공하고 크기 0 · 전진 > 0 이다 */
SW_TEST_CASE( FontRasterizerTest, SpaceHasAdvanceButNoBitmap )
{
    sw::unique_ptr<sw::IFontRasterizer> rasterizer = sw::IFontRasterizer::createDefault();
    SW_ASSERT_NOT_NULL( rasterizer.get() );
    const sw::FontFaceID face = FontRasterizerTestUtil::openEngineFace( *rasterizer );
    SW_ASSERT_TRUE( face != sw::kInvalidFontFaceID );
    sw::SdfGlyphBitmap bitmap{};
    SW_ASSERT_TRUE( rasterizer->rasterizeSdf( face, rasterizer->findGlyphIndex( face, ' ' ), sw::SdfRasterParams{}, bitmap ) );
    SW_EXPECT_EQUAL( 0u, bitmap._width );
    SW_EXPECT_TRUE( bitmap._bytes.empty() );
    SW_EXPECT_TRUE( bitmap._advancePx > 0.0f );
}

/** @brief [FontRasterizerTest] 글꼴이 아닌 바이트는 면을 열지 않고 무효 번호를 준다(오류 로그 한 줄) */
SW_TEST_CASE( FontRasterizerTest, RejectsNonFontBytes )
{
    sw::unique_ptr<sw::IFontRasterizer> rasterizer = sw::IFontRasterizer::createDefault();
    SW_ASSERT_NOT_NULL( rasterizer.get() );
    SW_TEST_DEFENSIVE_SCOPE( "garbage bytes are rejected with an error" );
    sw::vector<uint8> garbage( 64, 0x5Au );
    SW_EXPECT_EQUAL( sw::kInvalidFontFaceID, rasterizer->loadFace( std::move( garbage ), 0, "garbage" ) );
}

/**
 * @brief [FontRasterizerTest] MSDF 는 SDF 와 같은 부호 규약이다 — 'H' 의 획 한가운데는 중앙값이 밝고(200 위) 모서리 바깥은 0 에 가까우며, 비트맵 크기는 SDF 와 몇 픽셀 안쪽으로 맞아 같은 아틀라스 자리 계산을 쓴다
 * @details 알파는 일반 SDF(참 거리)다. 변이: 윤곽 방향을 뒤집지 않으면(TrueType 시계 방향) 안팎이 거꾸로라 획 한가운데가 어두워져 진다.
 */
SW_TEST_CASE( FontRasterizerTest, MsdfIsBrightInsideTheGlyphLikeSdf )
{
    sw::unique_ptr<sw::IFontRasterizer> rasterizer = sw::IFontRasterizer::createDefault();
    SW_ASSERT_NOT_NULL( rasterizer.get() );
    const sw::FontFaceID face = FontRasterizerTestUtil::openEngineFace( *rasterizer );
    SW_ASSERT_TRUE( face != sw::kInvalidFontFaceID );
    const uint32 glyph = rasterizer->findGlyphIndex( face, 'H' );

    sw::SdfGlyphBitmap sdf{};
    sw::SdfGlyphBitmap msdf{};
    SW_ASSERT_TRUE( rasterizer->rasterizeSdf( face, glyph, sw::SdfRasterParams{}, sdf ) );
    SW_ASSERT_TRUE( rasterizer->rasterizeMsdf( face, glyph, sw::SdfRasterParams{}, msdf ) );
    SW_ASSERT_TRUE( msdf._width > 12 && msdf._height > 12 );
    SW_ASSERT_EQUAL( static_cast<size_t>( msdf._width ) * msdf._height * 4, msdf._bytes.size() );
    SW_EXPECT_TRUE( sw::MathUtil::abs( static_cast<int32>( msdf._width ) - static_cast<int32>( sdf._width ) ) <= 2 );
    SW_EXPECT_TRUE( sw::MathUtil::abs( static_cast<int32>( msdf._height ) - static_cast<int32>( sdf._height ) ) <= 2 );
    SW_EXPECT_NEAR_EQUAL( sdf._advancePx, msdf._advancePx, 1e-3f );

    uint8 maxMedian = 0;
    for ( size_t texel = 0; texel < static_cast<size_t>( msdf._width ) * msdf._height; ++texel )
    {
        const uint8* pRgba   = &msdf._bytes[texel * 4];
        const uint8  minimum = sw::MathUtil::min( pRgba[0], sw::MathUtil::min( pRgba[1], pRgba[2] ) );
        const uint8  maximum = sw::MathUtil::max( pRgba[0], sw::MathUtil::max( pRgba[1], pRgba[2] ) );
        const uint8  median  = static_cast<uint8>( pRgba[0] + pRgba[1] + pRgba[2] - minimum - maximum );
        maxMedian            = sw::MathUtil::max( maxMedian, median );
    }
    SW_EXPECT_TRUE( maxMedian >= 200 ); // 획 한가운데
    for ( uint32 channel = 0; channel < 4; ++channel )
    {
        SW_EXPECT_TRUE( msdf._bytes[channel] <= 40 ); // 왼쪽 위 모서리는 윤곽에서 spread 만큼 바깥
    }
}
