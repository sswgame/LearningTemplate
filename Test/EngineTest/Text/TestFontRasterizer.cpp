#include "pch.h"

#include "Core/Container/vector.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Text/IFontRasterizer.h"

#include "TestFramework/TestFramework.h"

// FontRasterizerTest — FreeType 래스터라이저: 면 열기 · 글리프 번호 · 메트릭 · SDF 부호 규약. 디바이스 없음(nogpu).

namespace
{
    struct FontRasterizerTestUtil
    {
        static constexpr const utf8* kFontPath = "engine/fonts/kenney_future.ttf";

        static sw::FontFaceId openEngineFace( sw::IFontRasterizer& rasterizer )
        {
            sw::vector<uint8> bytes;
            if ( sw::ResourceUtil::readBinaryResource( kFontPath, bytes ) == false )
                return sw::kInvalidFontFaceId;
            return rasterizer.loadFace( std::move( bytes ), 0, kFontPath );
        }
    };
} // namespace

/** @brief [FontRasterizerTest] 엔진 글꼴이 열리고 라틴 글리프는 있고 한글 글리프는 없다(대체 사슬이 다음 면으로 가는 근거) */
SW_TEST_CASE( FontRasterizerTest, OpensFaceAndFindsGlyphs )
{
    sw::unique_ptr<sw::IFontRasterizer> rasterizer = sw::IFontRasterizer::createDefault();
    SW_ASSERT_NOT_NULL( rasterizer.get() );
    const sw::FontFaceId face = FontRasterizerTestUtil::openEngineFace( *rasterizer );
    SW_ASSERT_TRUE( face != sw::kInvalidFontFaceId );

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
    const sw::FontFaceId face = FontRasterizerTestUtil::openEngineFace( *rasterizer );
    SW_ASSERT_TRUE( face != sw::kInvalidFontFaceId );

    sw::SdfGlyphBitmap bitmap{};
    SW_ASSERT_TRUE( rasterizer->rasterizeSdf( face, rasterizer->findGlyphIndex( face, 'H' ), sw::SdfRasterParams{}, bitmap ) );
    SW_ASSERT_TRUE( bitmap._width > 12 && bitmap._height > 12 );
    SW_ASSERT_EQUAL( static_cast<size_t>( bitmap._width ) * bitmap._height, bitmap._bytes.size() );
    uint8 maxValue = 0;
    for ( const uint8 value : bitmap._bytes )
        maxValue = value > maxValue ? value : maxValue;
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
    const sw::FontFaceId face = FontRasterizerTestUtil::openEngineFace( *rasterizer );
    SW_ASSERT_TRUE( face != sw::kInvalidFontFaceId );
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
    SW_EXPECT_EQUAL( sw::kInvalidFontFaceId, rasterizer->loadFace( std::move( garbage ), 0, "garbage" ) );
}
