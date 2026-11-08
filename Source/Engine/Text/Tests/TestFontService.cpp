// FontServiceTest — 글꼴 카탈로그 · 가족 고르기(굵기 · 가짜 굵게) · 문화권 대체 사슬 · 못 찾은 가족의 경고 한 번. 디바이스 없음(nogpu).
// 실제 글꼴 테스트(CatalogDefaultFamilyOpens)은 저장소 글꼴로만 단언한다 — 시스템 글꼴은 기계마다 달라 보지 않는다(결정 R1).
#include "pch.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Text/FontService.h"
#include "Engine/Text/Tests/FakeFontRasterizer.h"
#include "Engine/Text/TextItemizer.h"

namespace
{
    struct FontServiceTestUtil
    {
        static constexpr const utf8* kLatinPath  = "test/fonts/latin.ttf";
        static constexpr const utf8* kHangulPath = "test/fonts/hangul.ttf";

        /** @brief 라틴 가족(기본) · 한글 가족 둘의 카탈로그입니다. 한글 가족은 공백도 가진다. */
        static sw::FontCatalogDef makeLatinHangulCatalog()
        {
            sw::FontCatalogDef catalog{};
            catalog._defaultFamily = "Latin";
            sw::test::FakeFontServiceFixture::addFamily( catalog, "Latin", kLatinPath );
            sw::test::FakeFontServiceFixture::addFamily( catalog, "Hangul", kHangulPath );
            return catalog;
        }

        static void configureLatinHangul( sw::test::FakeFontRasterizer& rasterizer )
        {
            sw::test::FakeFontFaceConfig latin{};
            latin.addRange( 0x20u, 0x7Eu );
            rasterizer.setFaceConfig( kLatinPath, latin );
            sw::test::FakeFontFaceConfig hangul{};
            hangul.addRange( 0x20u, 0x20u ).addRange( 0xAC00u, 0xD7A3u );
            rasterizer.setFaceConfig( kHangulPath, hangul );
        }
    };
} // namespace

/** @brief [FontServiceTest] 엔진 카탈로그가 읽히고 기본 가족(저장소 라틴 글꼴)이 열린다 — 'A' 는 사슬에서 글리프를 찾는다 */
SW_TEST_CASE( FontServiceTest, CatalogDefaultFamilyOpens )
{
    sw::FontService fontService;
    SW_REQUIRE_TRUE( fontService.initialize( sw::AssetID{ sw::engine::getSubsystem<sw::EngineDefaultAssets>()._fontCatalog.get() } ) );
    SW_EXPECT_TRUE( fontService.isInitialized() );
    const sw::FontFaceChain chain = fontService.getFaceChain( sw::FontSpec{} );
    SW_REQUIRE_TRUE( chain._faceCount >= 1 );
    uint32               glyphIndex = 0;
    const sw::FontFaceID face       = fontService.findFaceForCodepoint( chain, 'A', glyphIndex );
    SW_EXPECT_NOT_EQUAL( sw::kInvalidFontFaceID, face );
    SW_EXPECT_NOT_EQUAL( 0u, glyphIndex );
}

/** @brief [FontServiceTest] 고른 가족에 없는 글자는 사슬의 다음 면에서 찾는다 — 한글 가족 → 라틴 기본 가족 */
SW_TEST_CASE( FontServiceTest, ChainFallsBackForMissingCodepoints )
{
    sw::test::FakeFontServiceFixture fixture;
    FontServiceTestUtil::configureLatinHangul( *fixture._pRasterizer );
    SW_REQUIRE_TRUE( fixture.initialize( FontServiceTestUtil::makeLatinHangulCatalog() ) );

    sw::FontSpec spec{};
    spec._family                  = "Hangul";
    const sw::FontFaceChain chain = fixture._fontService->getFaceChain( spec );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( chain._faceCount ) );

    uint32               glyphIndex = 0;
    const sw::FontFaceID hangulFace = fixture._fontService->findFaceForCodepoint( chain, 0xAC00u, glyphIndex );
    SW_EXPECT_EQUAL( 0xAC00u, glyphIndex );
    const sw::FontFaceID latinFace = fixture._fontService->findFaceForCodepoint( chain, 'A', glyphIndex );
    SW_EXPECT_EQUAL( static_cast<uint32>( 'A' ), glyphIndex );
    SW_EXPECT_EQUAL( chain._arrFace[0], hangulFace );
    SW_EXPECT_EQUAL( chain._arrFace[1], latinFace );
    SW_EXPECT_NOT_EQUAL( hangulFace, latinFace );
}

/** @brief [FontServiceTest] 없는 가족 이름은 사슬에서 빠지고 경고는 한 번뿐이다 — 어디에도 없는 글자도 두부 + 경고 한 번 */
SW_TEST_CASE( FontServiceTest, MissingFamilyWarnsOnceAndIsSkipped )
{
    sw::test::FakeFontServiceFixture fixture;
    FontServiceTestUtil::configureLatinHangul( *fixture._pRasterizer );
    SW_REQUIRE_TRUE( fixture.initialize( FontServiceTestUtil::makeLatinHangulCatalog() ) );
    SW_EXPECT_LOG( "Text.FontFamilyMissing" ); // 없는 가족과 없는 글리프가 한 번씩 경고한다
    SW_EXPECT_LOG( "Text.GlyphMissing" );

    sw::FontSpec spec{};
    spec._family                  = "No Such Family";
    const sw::FontFaceChain chain = fixture._fontService->getFaceChain( spec );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( chain._faceCount ) ); // 기본 가족만
    SW_EXPECT_EQUAL( 1u, fixture._fontService->getWarnedOnceCount() );

    spec._weight = sw::FontWeight::Bold;
    (void)fixture._fontService->getFaceChain( spec );
    fixture._fontService->invalidateFaceChains();
    (void)fixture._fontService->getFaceChain( spec );
    SW_EXPECT_EQUAL( 1u, fixture._fontService->getWarnedOnceCount() );

    uint32               glyphIndex = 7;
    const sw::FontFaceID face       = fixture._fontService->findFaceForCodepoint( chain, 0x0627u, glyphIndex ); // 아랍 알리프 — 어느 면에도 없다
    SW_EXPECT_EQUAL( chain._arrFace[0], face );
    SW_EXPECT_EQUAL( 0u, glyphIndex );
    (void)fixture._fontService->findFaceForCodepoint( chain, 0x0627u, glyphIndex ); // 면을 미리 열거나 경고 횟수만 본다(위 설명)
    SW_EXPECT_EQUAL( 2u, fixture._fontService->getWarnedOnceCount() );
}

/**
 * @brief [FontServiceTest] 카탈로그의 시스템 가족이 이 기계에 설치되지 않은 것은 경고가 아니다 — 사슬에서 빠지고 Info 한 줄이다
 * @details 현지화 대체 목록의 시스템 글꼴(Noto Sans 등)은 기계마다 설치가 달라, 없을 때마다 깨끗한 에디터 실행이 Warning 을 남겼다(패널 점검 D25).
 */
SW_TEST_CASE( FontServiceTest, MissingSystemFallbackIsNotAWarning )
{
    sw::test::FakeFontServiceFixture fixture;
    FontServiceTestUtil::configureLatinHangul( *fixture._pRasterizer );
    sw::FontCatalogDef      catalog = FontServiceTestUtil::makeLatinHangulCatalog();
    sw::SystemFontFamilyDef systemFamily{};
    systemFamily._name = "Sw Not Installed Sans";
    systemFamily._listPlatformFace.push_back( sw::SystemFontFaceDef{ sw::build::kPlatformName, "SwNotInstalledSans-Regular.ttf", {}, 0 } );
    catalog._listSystemFamily.push_back( systemFamily );
    SW_REQUIRE_TRUE( fixture.initialize( catalog ) );

    // 경고가 나오면 케이스의 로그 게이트가 실패로 센다(이 케이스는 아무 id 도 적지 않는다). 카탈로그에 없는 가족의 경고는 MissingFamilyWarnsOnceAndIsSkipped 가 본다.
    sw::FontSpec spec{};
    spec._family                  = "Sw Not Installed Sans";
    const sw::FontFaceChain chain = fixture._fontService->getFaceChain( spec );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( chain._faceCount ) ); // 기본 가족만
}

/** @brief [FontServiceTest] 굵기는 가장 가까운 면을 고르고, 굵은 면이 없으면 일반 면 + 가짜 굵게 · 기운 면이 없으면 가짜 기울임 */
SW_TEST_CASE( FontServiceTest, WeightPicksNearestFaceAndMarksFauxBold )
{
    sw::test::FakeFontServiceFixture fixture;
    sw::FontCatalogDef               catalog = FontServiceTestUtil::makeLatinHangulCatalog();
    sw::FontFaceDef                  boldFace{};
    boldFace._path   = "test/fonts/latin_bold.ttf";
    boldFace._weight = sw::FontWeight::Bold;
    catalog._listFamily[0]._listFace.push_back( boldFace );
    SW_REQUIRE_TRUE( fixture.initialize( catalog ) );

    sw::FontSpec hangulRegular{};
    hangulRegular._family                = "Hangul";
    sw::FontSpec hangulBold              = hangulRegular;
    hangulBold._weight                   = sw::FontWeight::Bold;
    const sw::FontFaceChain regularChain = fixture._fontService->getFaceChain( hangulRegular );
    const sw::FontFaceChain boldChain    = fixture._fontService->getFaceChain( hangulBold );
    SW_EXPECT_EQUAL( regularChain._arrFace[0], boldChain._arrFace[0] ); // 한글 가족엔 Regular 하나
    SW_EXPECT_EQUAL( SW_FALSE, regularChain._bFauxBold );
    SW_EXPECT_EQUAL( SW_TRUE, boldChain._bFauxBold );

    sw::FontSpec latinRegular{};
    latinRegular._family                      = "Latin";
    sw::FontSpec latinSemi                    = latinRegular;
    latinSemi._weight                         = sw::FontWeight::SemiBold;
    sw::FontSpec latinItalic                  = latinRegular;
    latinItalic._slant                        = sw::FontSlant::Italic;
    const sw::FontFaceChain latinRegularChain = fixture._fontService->getFaceChain( latinRegular );
    const sw::FontFaceChain latinSemiChain    = fixture._fontService->getFaceChain( latinSemi );
    const sw::FontFaceChain latinItalicChain  = fixture._fontService->getFaceChain( latinItalic );
    SW_EXPECT_NOT_EQUAL( latinRegularChain._arrFace[0], latinSemiChain._arrFace[0] ); // SemiBold(600) 은 Bold(700) 이 더 가깝다
    SW_EXPECT_EQUAL( SW_FALSE, latinSemiChain._bFauxBold );
    SW_EXPECT_EQUAL( latinRegularChain._arrFace[0], latinItalicChain._arrFace[0] );
    SW_EXPECT_EQUAL( SW_TRUE, latinItalicChain._bFauxItalic );
    SW_EXPECT_EQUAL( 3u, fixture._pRasterizer->getLoadCount() ); // 라틴 일반 · 라틴 굵게 · 한글 — 같은 파일은 한 번만 연다
}

/** @brief [FontServiceTest] 문화권 대체 가족이 사슬에 들어가고, 문화권을 바꾸고 사슬을 비우면 새 문화권의 가족으로 다시 만든다 */
SW_TEST_CASE( FontServiceTest, LanguageChangeRebuildsChain )
{
    // 사슬은 문화권 표의 대체 목록만 읽는다 — 번역 프로젝트는 필요 없다.
    sw::LocalizationManager localization;
    SW_REQUIRE_TRUE( localization.loadCultureTable( sw::AssetID{ "engine/localization/engine.cultures.xml" } ) );
    SW_REQUIRE_TRUE( localization.setCurrentLanguage( "ko" ) );

    // 문화권 표의 대체 가족 이름(ko: Noto Sans KR · ar: Noto Sans Arabic)을 저장소 가족으로 둔다 — 시스템 글꼴 없이 사슬만 본다.
    sw::test::FakeFontServiceFixture fixture( &localization );
    sw::FontCatalogDef               catalog = FontServiceTestUtil::makeLatinHangulCatalog();
    sw::test::FakeFontServiceFixture::addFamily( catalog, "Noto Sans KR", "test/fonts/notosanskr.ttf" );
    sw::test::FakeFontServiceFixture::addFamily( catalog, "Noto Sans Arabic", "test/fonts/notosansarabic.ttf" );
    SW_EXPECT_LOG( "Text.FontFamilyMissing" ); // 테스트 카탈로그에 없는 문화권 대체 가족(Malgun Gothic · Noto Sans …)이 한 번씩 경고한다
    SW_REQUIRE_TRUE( fixture.initialize( catalog ) );

    const sw::FontFaceChain koreanChain = fixture._fontService->getFaceChain( sw::FontSpec{} );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( koreanChain._faceCount ) ); // 기본(라틴)이 고른 가족이자 끝 — Noto Sans KR 이 사이에
    const sw::FontFaceID koreanFace = koreanChain._arrFace[1];

    SW_REQUIRE_TRUE( localization.setCurrentLanguage( "ar" ) );
    fixture._fontService->invalidateFaceChains();
    const sw::FontFaceChain arabicChain = fixture._fontService->getFaceChain( sw::FontSpec{} );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( arabicChain._faceCount ) );
    SW_EXPECT_NOT_EQUAL( koreanFace, arabicChain._arrFace[1] );
    for ( uint32 index = 0; index < arabicChain._faceCount; ++index )
    {
        SW_EXPECT_NOT_EQUAL( koreanFace, arabicChain._arrFace[index] );
    }
}

/** @brief [FontServiceTest] 형식 문자(방향 · 폭 없는 이음 · 이체 선택자)는 글꼴에 글리프가 없어도 빠졌다고 알리지 않는다. 결합 부호는 보이는 글자라 알린다 */
SW_TEST_CASE( FontServiceTest, FormatControlsAreNotMissingGlyphs )
{
    SW_EXPECT_TRUE( sw::TextItemizer::isFormatControl( 0x202Eu ) );
    SW_EXPECT_TRUE( sw::TextItemizer::isFormatControl( 0x200Du ) );
    SW_EXPECT_TRUE( sw::TextItemizer::isFormatControl( 0xFE0Fu ) );
    SW_EXPECT_TRUE( sw::TextItemizer::isFormatControl( 0xFEFFu ) );
    SW_EXPECT_FALSE( sw::TextItemizer::isFormatControl( 0x0301u ) );
    SW_EXPECT_TRUE( sw::TextItemizer::isZeroWidth( 0x0301u ) );
    SW_EXPECT_FALSE( sw::TextItemizer::isZeroWidth( 0x0041u ) );

    sw::test::FakeFontServiceFixture fixture;
    FontServiceTestUtil::configureLatinHangul( *fixture._pRasterizer );
    SW_REQUIRE_TRUE( fixture.initialize( FontServiceTestUtil::makeLatinHangulCatalog() ) );
    sw::FontSpec spec{};
    spec._family                  = FontServiceTestUtil::makeLatinHangulCatalog()._defaultFamily;
    const sw::FontFaceChain chain = fixture._fontService->getFaceChain( spec );
    uint32                  glyphIndex{ 0 };
    (void)fixture._fontService->findFaceForCodepoint( chain, 0x202Eu, glyphIndex );
    SW_EXPECT_EQUAL( 0u, fixture._fontService->getWarnedOnceCount() );
}
