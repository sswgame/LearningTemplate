#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"

#include "Engine/Text/FontService.h"
#include "Engine/Text/IFontRasterizer.h"
#include "Engine/Text/ITextShaper.h"
#include "Engine/Text/TextLayout.h"

#include "EngineTest/Text/FakeFontRasterizer.h"

#include "TestFramework/TestFramework.h"

// ComplexShapingTest — 실제 글꼴 바이트(아랍어 연결형 모양을 GSUB 로 가진 작은 테스트 글꼴)로 셰이퍼를 돌린다. 가짜 래스터라이저에는 바이트가 없어 다른 텍스트 테스트는 단순 셰이퍼를 쓴다.
// 복합 문자 빌드(`SW_ENABLE_COMPLEX_TEXT`)는 연결형 모양이 골라지고, 기본 빌드는 모양 바꾸기 없이 독립형이 나온다 — 두 판의 기대값을 한 테스트에 나란히 적는다(nogpu).
// 글꼴은 `Scripts/generate/GenerateShapingTestFont.py` 가 만든다. 글리프: 1 바 독립(폭 700) · 2 어두(500) · 3 어중(400) · 4 어말(600) · 5 알리프 독립(300) · 6 알리프 어말(350).

namespace
{
    struct ComplexShapingTestUtil
    {
        static constexpr const utf8*             kFontRepoPath    = "Test/EngineTest/Text/Fonts/swshapingtest.ttf";
        static constexpr const utf8*             kFontVirtualPath = "test/fonts/swshapingtest.ttf";
        static constexpr uint32                  kBehIsolated     = 1;
        [[maybe_unused]] static constexpr uint32 kBehInitial      = 2; // 아래 넷은 복합 문자 빌드의 기대에서만 쓴다
        [[maybe_unused]] static constexpr uint32 kBehMedial       = 3;
        [[maybe_unused]] static constexpr uint32 kBehFinal        = 4;
        static constexpr uint32                  kAlefIsolated    = 5;
        [[maybe_unused]] static constexpr uint32 kAlefFinal       = 6;

        /** @brief 작업 폴더(`build/<프리셋>/Bin`)에서 위로 올라가며 테스트 글꼴을 읽습니다. 못 찾으면 false. */
        [[nodiscard]] static bool readFontBytes( sw::vector<uint8>& outBytes )
        {
            sw::string directory = sw::FileUtil::getCurrentPath();
            for ( uint32 depth = 0; depth < 8 && directory.empty() == false; ++depth )
            {
                const sw::string candidate = sw::FileUtil::joinPath( directory, kFontRepoPath );
                if ( sw::FileUtil::readFile( candidate, outBytes ) )
                    return true;
                const sw::string parent = sw::FileUtil::getDirectoryPart( directory );
                if ( parent == directory )
                    break;
                directory = parent;
            }
            return false;
        }

        /** @brief 글꼴을 연 FreeType 래스터라이저와 면 번호를 내고, 그 래스터라이저의 셰이퍼로 @p text 한 런을 셰이핑합니다. */
        static bool shapeRun( sw::string_view text, sw::TextDirection direction, sw::vector<sw::ShapedGlyph>& outListGlyph )
        {
            sw::vector<uint8> bytes;
            if ( readFontBytes( bytes ) == false )
                return false;
            const sw::unique_ptr<sw::IFontRasterizer> rasterizer = sw::IFontRasterizer::createDefault();
            if ( rasterizer == nullptr )
                return false;
            const sw::FontFaceID face = rasterizer->loadFace( std::move( bytes ), 0, kFontVirtualPath );
            if ( face == sw::kInvalidFontFaceID )
                return false;
            const sw::unique_ptr<sw::ITextShaper> shaper = rasterizer->createShaper();
            sw::ShapingRun                        run{};
            run._text      = text;
            run._face      = face;
            run._direction = direction;
            shaper->shape( *rasterizer, run, outListGlyph );
            return true;
        }
    };
} // namespace

/**
 * @brief [ComplexShapingTest] 아랍어 바(ب) 셋은 복합 문자 빌드에서 어두 · 어중 · 어말 모양(글리프 2 · 3 · 4, 전진 0.5 · 0.4 · 0.6)이고, 기본 빌드에서는 독립형(1 · 1 · 1, 0.7)이 셋이다
 * @details 결과는 논리 순서(텍스트 순서)다. 클러스터는 원문 바이트 위치(글자당 2 바이트)다. 변이: HarfBuzz 셰이퍼를 단순 셰이퍼로 바꾸면 복합 문자 빌드의 기대가 진다.
 */
SW_TEST_CASE( ComplexShapingTest, BehJoinsIntoInitialMedialFinalForms )
{
    sw::vector<sw::ShapedGlyph> listGlyph;
    SW_ASSERT_TRUE_MSG( ComplexShapingTestUtil::shapeRun( "\xD8\xA8\xD8\xA8\xD8\xA8", sw::TextDirection::RightToLeft, listGlyph ), "test font Test/EngineTest/Text/Fonts/swshapingtest.ttf not readable" );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listGlyph.size() ) );
    SW_EXPECT_EQUAL( 0u, listGlyph[0]._cluster );
    SW_EXPECT_EQUAL( 2u, listGlyph[1]._cluster );
    SW_EXPECT_EQUAL( 4u, listGlyph[2]._cluster );
#if defined( SW_TEXT_COMPLEX_SHAPING )
    SW_EXPECT_EQUAL( ComplexShapingTestUtil::kBehInitial, listGlyph[0]._glyphIndex );
    SW_EXPECT_EQUAL( ComplexShapingTestUtil::kBehMedial, listGlyph[1]._glyphIndex );
    SW_EXPECT_EQUAL( ComplexShapingTestUtil::kBehFinal, listGlyph[2]._glyphIndex );
    SW_EXPECT_NEAR_EQUAL( 0.5f, listGlyph[0]._advance, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.4f, listGlyph[1]._advance, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.6f, listGlyph[2]._advance, 1e-4f );
#else
    for ( const sw::ShapedGlyph& glyph : listGlyph )
    {
        SW_EXPECT_EQUAL( ComplexShapingTestUtil::kBehIsolated, glyph._glyphIndex );
        SW_EXPECT_NEAR_EQUAL( 0.7f, glyph._advance, 1e-4f );
    }
#endif
}

/** @brief [ComplexShapingTest] 바 + 알리프(با)는 바가 어두형, 알리프가 어말형이 되고(복합 문자 빌드), 낱글자 바는 독립형이다 — 홀로 선 글자는 어느 빌드든 같다 */
SW_TEST_CASE( ComplexShapingTest, AlefAfterBehIsFinalAndLoneLetterIsIsolated )
{
    sw::vector<sw::ShapedGlyph> listPair;
    SW_ASSERT_TRUE_MSG( ComplexShapingTestUtil::shapeRun( "\xD8\xA8\xD8\xA7", sw::TextDirection::RightToLeft, listPair ), "test font not readable" );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listPair.size() ) );
#if defined( SW_TEXT_COMPLEX_SHAPING )
    SW_EXPECT_EQUAL( ComplexShapingTestUtil::kBehInitial, listPair[0]._glyphIndex );
    SW_EXPECT_EQUAL( ComplexShapingTestUtil::kAlefFinal, listPair[1]._glyphIndex );
#else
    SW_EXPECT_EQUAL( ComplexShapingTestUtil::kBehIsolated, listPair[0]._glyphIndex );
    SW_EXPECT_EQUAL( ComplexShapingTestUtil::kAlefIsolated, listPair[1]._glyphIndex );
#endif

    sw::vector<sw::ShapedGlyph> listLone;
    SW_ASSERT_TRUE_MSG( ComplexShapingTestUtil::shapeRun( "\xD8\xA8", sw::TextDirection::RightToLeft, listLone ), "test font not readable" );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listLone.size() ) );
    SW_EXPECT_EQUAL( ComplexShapingTestUtil::kBehIsolated, listLone[0]._glyphIndex );
}

/**
 * @brief [ComplexShapingTest] 배치는 연결형 모양을 눈에 보이는 순서로 놓는다 — RTL 문단 "بب" 는 왼쪽부터 어말(전진 6) · 어두(전진 5)이고 둘째 글리프는 x = 6 이다(기본 빌드는 독립형 둘, x = 7)
 * @details 크기 10. 가짜가 아닌 FreeType 래스터라이저의 `FontService` 로 돌려 래스터라이저가 고른 셰이퍼가 `TextLayoutEngine` 까지 이어지는지 본다.
 */
SW_TEST_CASE( ComplexShapingTest, LayoutPlacesShapedFormsInVisualOrder )
{
    using Util = ComplexShapingTestUtil;
    sw::vector<uint8> bytes;
    SW_ASSERT_TRUE_MSG( Util::readFontBytes( bytes ), "test font Test/EngineTest/Text/Fonts/swshapingtest.ttf not readable" );
    sw::FontService fontService( sw::IFontRasterizer::createDefault(), sw::engine::findSubsystem<sw::LocalizationManager>() );
    fontService.registerMemoryFontFile( Util::kFontVirtualPath, std::move( bytes ) );
    sw::FontCatalogDesc catalog{};
    catalog._defaultFamily = "Shaping";
    sw::test::FakeFontServiceFixture::addFamily( catalog, "Shaping", Util::kFontVirtualPath );
    SW_ASSERT_TRUE( fontService.initializeFromCatalog( catalog ) );

    sw::TextLayoutEngine layout{ fontService };
    sw::TextLayoutStyle  style{};
    style._fontSize           = 10.0f;
    style._paragraphDirection = sw::TextDirection::RightToLeft;
    sw::TextLayoutResult result{};
    layout.layout( "\xD8\xA8\xD8\xA8", style, 0.0f, result );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( result._listGlyph.size() ) );
#if defined( SW_TEXT_COMPLEX_SHAPING )
    SW_EXPECT_EQUAL( Util::kBehFinal, result._listGlyph[0]._glyphIndex );
    SW_EXPECT_EQUAL( Util::kBehInitial, result._listGlyph[1]._glyphIndex );
    SW_EXPECT_NEAR_EQUAL( 0.0f, result._listGlyph[0]._origin._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 6.0f, result._listGlyph[1]._origin._x, 1e-3f );
#else
    SW_EXPECT_EQUAL( Util::kBehIsolated, result._listGlyph[0]._glyphIndex );
    SW_EXPECT_EQUAL( Util::kBehIsolated, result._listGlyph[1]._glyphIndex );
    SW_EXPECT_NEAR_EQUAL( 7.0f, result._listGlyph[1]._origin._x, 1e-3f );
#endif
}
