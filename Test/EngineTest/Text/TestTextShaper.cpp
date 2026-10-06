#include "pch.h"

#include "Core/Container/vector.h"

#include "Engine/Text/FontSystem.h"
#include "Engine/Text/SimpleTextShaper.h"
#include "Engine/Text/TextItemizer.h"

#include "EngineTest/Text/FakeFontRasterizer.h"

#include "TestFramework/TestFramework.h"

// TextShaperTest — 단순 셰이퍼(cmap + 커닝 · 클러스터 = 바이트 위치)와 런 나누기(면 · 방향). 가짜 래스터라이저(nogpu).

namespace
{
    struct TextShaperTestUtil
    {
        static constexpr const utf8* kLatinPath  = "test/fonts/latin.ttf";
        static constexpr const utf8* kHangulPath = "test/fonts/hangul.ttf";
        static constexpr const utf8* kAllPath    = "test/fonts/all.ttf";

        /** @brief 모든 코드 포인트를 가진 가족 하나("All")의 카탈로그로 시작합니다. */
        static bool initializeAllCoverage( sw::test::FakeFontSystemFixture& fixture )
        {
            sw::FontCatalogDesc catalog{};
            catalog._defaultFamily = "All";
            sw::test::FakeFontSystemFixture::addFamily( catalog, "All", kAllPath );
            return fixture.initialize( catalog );
        }

        /** @brief 라틴(고른 가족) · 한글(기본 가족) 카탈로그로 시작합니다. 둘 다 공백을 가진다. */
        static bool initializeLatinHangul( sw::test::FakeFontSystemFixture& fixture )
        {
            sw::test::FakeFontFaceConfig latin{};
            latin.addRange( 0x20u, 0x7Eu );
            fixture._pRasterizer->setFaceConfig( kLatinPath, latin );
            sw::test::FakeFontFaceConfig hangul{};
            hangul.addRange( 0x20u, 0x20u ).addRange( 0xAC00u, 0xD7A3u );
            fixture._pRasterizer->setFaceConfig( kHangulPath, hangul );
            sw::FontCatalogDesc catalog{};
            catalog._defaultFamily = "Hangul";
            sw::test::FakeFontSystemFixture::addFamily( catalog, "Latin", kLatinPath );
            sw::test::FakeFontSystemFixture::addFamily( catalog, "Hangul", kHangulPath );
            return fixture.initialize( catalog );
        }

        static sw::string_view runText( const sw::ShapingRun& run ) { return run._text; }
    };
} // namespace

/** @brief [TextShaperTest] 클러스터는 원문 바이트 위치다 — "A가B" 는 0 · 1 · 4, 런의 시작 바이트를 더한다 */
SW_TEST_CASE( TextShaperTest, ClustersAreByteOffsets )
{
    sw::test::FakeFontSystemFixture fixture;
    SW_ASSERT_TRUE( TextShaperTestUtil::initializeAllCoverage( fixture ) );
    const sw::FontFaceChain chain = fixture._fontSystem->getFaceChain( sw::FontSpec{} );

    sw::SimpleTextShaper        shaper;
    sw::vector<sw::ShapedGlyph> listGlyph;
    sw::ShapingRun              run{};
    run._text = "A\xEA\xB0\x80"
                "B"; // "A가B"
    run._face = chain._arrFace[0];
    shaper.shape( fixture._fontSystem->getRasterizer(), run, listGlyph );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listGlyph.size() ) );
    SW_EXPECT_EQUAL( 0u, listGlyph[0]._cluster );
    SW_EXPECT_EQUAL( 1u, listGlyph[1]._cluster );
    SW_EXPECT_EQUAL( 4u, listGlyph[2]._cluster );
    SW_EXPECT_EQUAL( 0xAC00u, listGlyph[1]._codepoint );

    run._byteOffset = 10;
    listGlyph.clear();
    shaper.shape( fixture._fontSystem->getRasterizer(), run, listGlyph );
    SW_EXPECT_EQUAL( 14u, listGlyph[2]._cluster );
}

/** @brief [TextShaperTest] 커닝은 앞 글리프의 전진에 더한다 — ('A','V') = -0.1 이면 첫 전진 0.4 · 결합 분음은 글리프를 내지 않는다 */
SW_TEST_CASE( TextShaperTest, KerningAddsToPreviousAdvance )
{
    sw::test::FakeFontSystemFixture fixture;
    SW_ASSERT_TRUE( TextShaperTestUtil::initializeAllCoverage( fixture ) );
    fixture._pRasterizer->setKerning( 'A', 'V', -0.1f );
    const sw::FontFaceChain chain = fixture._fontSystem->getFaceChain( sw::FontSpec{} );

    sw::SimpleTextShaper        shaper;
    sw::vector<sw::ShapedGlyph> listGlyph;
    sw::ShapingRun              run{};
    run._text = "AV\xCC\x81"; // "AV" + U+0301(결합 예음)
    run._face = chain._arrFace[0];
    shaper.shape( fixture._fontSystem->getRasterizer(), run, listGlyph );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listGlyph.size() ) );
    SW_EXPECT_NEAR_EQUAL( 0.4f, listGlyph[0]._advance, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( sw::test::FakeFontRasterizer::kGlyphAdvance, listGlyph[1]._advance, 1e-5f );
}

/** @brief [TextShaperTest] 면이 바뀌는 곳에서 런을 끊고, 공백(중립)은 앞 런에 붙는다 — "Hi 안녕 OK" 는 셋 */
SW_TEST_CASE( TextShaperTest, ItemizerSplitsOnFaceCoverage )
{
    sw::test::FakeFontSystemFixture fixture;
    SW_ASSERT_TRUE( TextShaperTestUtil::initializeLatinHangul( fixture ) );
    sw::FontSpec spec{};
    spec._family                  = "Latin";
    const sw::FontFaceChain chain = fixture._fontSystem->getFaceChain( spec );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( chain._faceCount ) );

    const sw::string           text = "Hi \xEC\x95\x88\xEB\x85\x95 OK"; // "Hi 안녕 OK"
    sw::vector<sw::ShapingRun> listRun;
    sw::TextItemizer::itemize( *fixture._fontSystem, chain, text, listRun );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listRun.size() ) );
    SW_EXPECT_TRUE( TextShaperTestUtil::runText( listRun[0] ) == "Hi " );
    SW_EXPECT_TRUE( TextShaperTestUtil::runText( listRun[1] ) == "\xEC\x95\x88\xEB\x85\x95 " );
    SW_EXPECT_TRUE( TextShaperTestUtil::runText( listRun[2] ) == "OK" );
    SW_EXPECT_EQUAL( chain._arrFace[0], listRun[0]._face );
    SW_EXPECT_EQUAL( chain._arrFace[1], listRun[1]._face );
    SW_EXPECT_EQUAL( chain._arrFace[0], listRun[2]._face );
    SW_EXPECT_EQUAL( 3u, listRun[1]._byteOffset );
}

/** @brief [TextShaperTest] 강한 RTL 문자에서 방향이 바뀌고, 숫자 · 공백은 앞 런에 붙는다 — "abc שלום 12" 는 둘, 둘째가 RTL */
SW_TEST_CASE( TextShaperTest, ItemizerMarksRightToLeftRuns )
{
    sw::test::FakeFontSystemFixture fixture;
    SW_ASSERT_TRUE( TextShaperTestUtil::initializeAllCoverage( fixture ) );
    const sw::FontFaceChain chain = fixture._fontSystem->getFaceChain( sw::FontSpec{} );

    const sw::string           text = "abc \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D 12"; // "abc שלום 12"
    sw::vector<sw::ShapingRun> listRun;
    sw::TextItemizer::itemize( *fixture._fontSystem, chain, text, listRun );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listRun.size() ) );
    SW_EXPECT_TRUE( listRun[0]._direction == sw::TextDirection::LeftToRight );
    SW_EXPECT_TRUE( listRun[1]._direction == sw::TextDirection::RightToLeft );
    SW_EXPECT_TRUE( TextShaperTestUtil::runText( listRun[0] ) == "abc " );
    SW_EXPECT_EQUAL( 4u, listRun[1]._byteOffset );
    SW_EXPECT_EQUAL( static_cast<uint32>( text.size() ) - 4u, static_cast<uint32>( listRun[1]._text.size() ) );
    SW_EXPECT_TRUE( sw::TextItemizer::isStrongRightToLeft( 0x05D0u ) );
    SW_EXPECT_FALSE( sw::TextItemizer::isStrongRightToLeft( 0x0661u ) ); // 아랍 숫자는 약하다
}
