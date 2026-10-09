#include "pch.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Text/FontSystem.h"
#include "Engine/Text/SystemFontLocator.h"

#include "EngineTest/LocalizationTestUtil.h"
#include "EngineTest/Text/FakeFontRasterizer.h"

#include "TestFramework/TestFramework.h"

// FontSystemTest — 글꼴 카탈로그 · 가족 고르기(굵기 · 가짜 굵게) · 문화권 대체 사슬 · 못 찾은 가족의 경고 한 번. 디바이스 없음(nogpu).
// 실제 글꼴 시험(CatalogDefaultFamilyOpens)은 저장소 글꼴로만 단언한다 — 시스템 글꼴은 기계마다 달라 글리프 존재 · 사슬 길이만 본다(결정 R1).

namespace
{
    struct FontSystemTestUtil
    {
        static constexpr const utf8* kLatinPath  = "test/fonts/latin.ttf";
        static constexpr const utf8* kHangulPath = "test/fonts/hangul.ttf";

        /** @brief 라틴 가족(기본) · 한글 가족 둘의 카탈로그입니다. 한글 가족은 공백도 가진다. */
        static sw::FontCatalogDesc makeLatinHangulCatalog()
        {
            sw::FontCatalogDesc catalog{};
            catalog._defaultFamily = "Latin";
            sw::test::FakeFontSystemFixture::addFamily( catalog, "Latin", kLatinPath );
            sw::test::FakeFontSystemFixture::addFamily( catalog, "Hangul", kHangulPath );
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

/** @brief [FontSystemTest] 엔진 카탈로그가 읽히고 기본 가족(저장소 라틴 글꼴)이 열린다 — 'A' 는 사슬에서 글리프를 찾는다 */
SW_TEST_CASE( FontSystemTest, CatalogDefaultFamilyOpens )
{
    sw::FontSystem fontSystem;
    SW_ASSERT_TRUE( fontSystem.initialize( sw::EngineDefaultAssets{}._fontCatalog ) );
    SW_EXPECT_TRUE( fontSystem.isInitialized() );
    const sw::FontFaceChain chain = fontSystem.getFaceChain( sw::FontSpec{} );
    SW_ASSERT_TRUE( chain._faceCount >= 1 );
    uint32               glyphIndex = 0;
    const sw::FontFaceId face       = fontSystem.findFaceForCodepoint( chain, 'A', glyphIndex );
    SW_EXPECT_NOT_EQUAL( sw::kInvalidFontFaceId, face );
    SW_EXPECT_NOT_EQUAL( 0u, glyphIndex );
}

/** @brief [FontSystemTest] 고른 가족에 없는 글자는 사슬의 다음 면에서 찾는다 — 한글 가족 → 라틴 기본 가족 */
SW_TEST_CASE( FontSystemTest, ChainFallsBackForMissingCodepoints )
{
    sw::test::FakeFontSystemFixture fixture;
    FontSystemTestUtil::configureLatinHangul( *fixture._pRasterizer );
    SW_ASSERT_TRUE( fixture.initialize( FontSystemTestUtil::makeLatinHangulCatalog() ) );

    sw::FontSpec spec{};
    spec._family                  = "Hangul";
    const sw::FontFaceChain chain = fixture._fontSystem->getFaceChain( spec );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( chain._faceCount ) );

    uint32               glyphIndex = 0;
    const sw::FontFaceId hangulFace = fixture._fontSystem->findFaceForCodepoint( chain, 0xAC00u, glyphIndex );
    SW_EXPECT_EQUAL( 0xAC00u, glyphIndex );
    const sw::FontFaceId latinFace = fixture._fontSystem->findFaceForCodepoint( chain, 'A', glyphIndex );
    SW_EXPECT_EQUAL( static_cast<uint32>( 'A' ), glyphIndex );
    SW_EXPECT_EQUAL( chain._arrFace[0], hangulFace );
    SW_EXPECT_EQUAL( chain._arrFace[1], latinFace );
    SW_EXPECT_NOT_EQUAL( hangulFace, latinFace );
}

/** @brief [FontSystemTest] 없는 가족 이름은 사슬에서 빠지고 경고는 한 번뿐이다 — 어디에도 없는 글자도 두부 + 경고 한 번 */
SW_TEST_CASE( FontSystemTest, MissingFamilyWarnsOnceAndIsSkipped )
{
    sw::test::FakeFontSystemFixture fixture;
    FontSystemTestUtil::configureLatinHangul( *fixture._pRasterizer );
    SW_ASSERT_TRUE( fixture.initialize( FontSystemTestUtil::makeLatinHangulCatalog() ) );
    SW_TEST_DEFENSIVE_SCOPE( "a missing family and a missing glyph warn once each" );

    sw::FontSpec spec{};
    spec._family                  = "No Such Family";
    const sw::FontFaceChain chain = fixture._fontSystem->getFaceChain( spec );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( chain._faceCount ) ); // 기본 가족만
    SW_EXPECT_EQUAL( 1u, fixture._fontSystem->getWarnedOnceCount() );

    spec._weight = sw::FontWeight::Bold;
    (void)fixture._fontSystem->getFaceChain( spec );
    fixture._fontSystem->invalidateFaceChains();
    (void)fixture._fontSystem->getFaceChain( spec );
    SW_EXPECT_EQUAL( 1u, fixture._fontSystem->getWarnedOnceCount() );

    uint32               glyphIndex = 7;
    const sw::FontFaceId face       = fixture._fontSystem->findFaceForCodepoint( chain, 0x0627u, glyphIndex ); // 아랍 알리프 — 어느 면에도 없다
    SW_EXPECT_EQUAL( chain._arrFace[0], face );
    SW_EXPECT_EQUAL( 0u, glyphIndex );
    (void)fixture._fontSystem->findFaceForCodepoint( chain, 0x0627u, glyphIndex );
    SW_EXPECT_EQUAL( 2u, fixture._fontSystem->getWarnedOnceCount() );
}

/**
 * @brief [FontSystemTest] 카탈로그의 시스템 가족이 이 기계에 설치되지 않은 것은 경고가 아니다 — 사슬에서 빠지고 Info 한 줄, 카탈로그에 없는 가족만 경고다
 * @details 현지화 대체 목록의 시스템 글꼴(Noto Sans 등)은 기계마다 설치가 달라, 없을 때마다 깨끗한 에디터 실행이 Warning 을 남겼다(패널 점검 D25).
 */
SW_TEST_CASE( FontSystemTest, MissingSystemFallbackIsNotAWarning )
{
    sw::test::FakeFontSystemFixture fixture;
    FontSystemTestUtil::configureLatinHangul( *fixture._pRasterizer );
    sw::FontCatalogDesc      catalog = FontSystemTestUtil::makeLatinHangulCatalog();
    sw::SystemFontFamilyDesc systemFamily{};
    systemFamily._name           = "Sw Not Installed Sans";
    systemFamily._windowsRegular = "SwNotInstalledSans-Regular.ttf";
    systemFamily._linuxRegular   = "SwNotInstalledSans-Regular.ttf";
    catalog._listSystemFamily.push_back( systemFamily );
    SW_ASSERT_TRUE( fixture.initialize( catalog ) );

    test::ScopedLogCollector logs;
    sw::FontSpec             spec{};
    spec._family                  = "Sw Not Installed Sans";
    const sw::FontFaceChain chain = fixture._fontSystem->getFaceChain( spec );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( chain._faceCount ) ); // 기본 가족만
    SW_EXPECT_TRUE_MSG( logs.countContaining( "Sw Not Installed Sans" ) == 0, logs.joined().c_str() );

    // 카탈로그에 없는 가족은 데이터 잘못이라 그대로 경고한다.
    SW_TEST_DEFENSIVE_SCOPE( "a family missing from the catalog warns" );
    spec._family = "No Such Family";
    (void)fixture._fontSystem->getFaceChain( spec );
    SW_EXPECT_EQUAL( 1u, logs.countContaining( "No Such Family" ) );
}

/** @brief [FontSystemTest] 굵기는 가장 가까운 면을 고르고, 굵은 면이 없으면 일반 면 + 가짜 굵게 · 기운 면이 없으면 가짜 기울임 */
SW_TEST_CASE( FontSystemTest, WeightPicksNearestFaceAndMarksFauxBold )
{
    sw::test::FakeFontSystemFixture fixture;
    sw::FontCatalogDesc             catalog = FontSystemTestUtil::makeLatinHangulCatalog();
    sw::FontFaceDesc                boldFace{};
    boldFace._path   = "test/fonts/latin_bold.ttf";
    boldFace._weight = sw::FontWeight::Bold;
    catalog._listFamily[0]._listFace.push_back( boldFace );
    SW_ASSERT_TRUE( fixture.initialize( catalog ) );

    sw::FontSpec hangulRegular{};
    hangulRegular._family                = "Hangul";
    sw::FontSpec hangulBold              = hangulRegular;
    hangulBold._weight                   = sw::FontWeight::Bold;
    const sw::FontFaceChain regularChain = fixture._fontSystem->getFaceChain( hangulRegular );
    const sw::FontFaceChain boldChain    = fixture._fontSystem->getFaceChain( hangulBold );
    SW_EXPECT_EQUAL( regularChain._arrFace[0], boldChain._arrFace[0] ); // 한글 가족엔 Regular 하나
    SW_EXPECT_EQUAL( SW_FALSE, regularChain._bFauxBold );
    SW_EXPECT_EQUAL( SW_TRUE, boldChain._bFauxBold );

    sw::FontSpec latinRegular{};
    latinRegular._family                      = "Latin";
    sw::FontSpec latinSemi                    = latinRegular;
    latinSemi._weight                         = sw::FontWeight::SemiBold;
    sw::FontSpec latinItalic                  = latinRegular;
    latinItalic._slant                        = sw::FontSlant::Italic;
    const sw::FontFaceChain latinRegularChain = fixture._fontSystem->getFaceChain( latinRegular );
    const sw::FontFaceChain latinSemiChain    = fixture._fontSystem->getFaceChain( latinSemi );
    const sw::FontFaceChain latinItalicChain  = fixture._fontSystem->getFaceChain( latinItalic );
    SW_EXPECT_NOT_EQUAL( latinRegularChain._arrFace[0], latinSemiChain._arrFace[0] ); // SemiBold(600) 은 Bold(700) 이 더 가깝다
    SW_EXPECT_EQUAL( SW_FALSE, latinSemiChain._bFauxBold );
    SW_EXPECT_EQUAL( latinRegularChain._arrFace[0], latinItalicChain._arrFace[0] );
    SW_EXPECT_EQUAL( SW_TRUE, latinItalicChain._bFauxItalic );
    SW_EXPECT_EQUAL( 3u, fixture._pRasterizer->getLoadCount() ); // 라틴 일반 · 라틴 굵게 · 한글 — 같은 파일은 한 번만 연다
}

/** @brief [FontSystemTest] 문화권 대체 가족이 사슬에 들어가고, 문화권을 바꾸고 사슬을 비우면 새 문화권의 가족으로 다시 만든다 */
SW_TEST_CASE( FontSystemTest, LanguageChangeRebuildsChain )
{
    using sw::test::LocalizationTestUtil;
    const sw::string folder      = test::makeTempDirectory( "font_system_chain" );
    const sw::string projectPath = LocalizationTestUtil::writeProject( folder, "en", R"([ "ko", "ar" ])" );
    LocalizationTestUtil::writeSourceTable( folder, "en", R"("ui.title": { "source": "Title" })" );
    LocalizationTestUtil::writeTranslation( folder, "ko", R"("ui.title": { "text": "제목" })" );
    LocalizationTestUtil::writeTranslation( folder, "ar", R"("ui.title": { "text": "عنوان" })" );
    sw::LocalizationManager localization;
    SW_ASSERT_TRUE( LocalizationTestUtil::loadEngineCultures( localization ) );
    SW_ASSERT_TRUE( localization.mountProject( projectPath, sw::LocalizationScope::Game ) );
    SW_ASSERT_TRUE( localization.setCurrentLanguage( "ko" ) );

    // 문화권 표의 대체 가족 이름(ko: Noto Sans KR · ar: Noto Sans Arabic)을 저장소 가족으로 둔다 — 시스템 글꼴 없이 사슬만 본다.
    sw::test::FakeFontSystemFixture fixture( &localization );
    sw::FontCatalogDesc             catalog = FontSystemTestUtil::makeLatinHangulCatalog();
    sw::test::FakeFontSystemFixture::addFamily( catalog, "Noto Sans KR", "test/fonts/notosanskr.ttf" );
    sw::test::FakeFontSystemFixture::addFamily( catalog, "Noto Sans Arabic", "test/fonts/notosansarabic.ttf" );
    SW_TEST_DEFENSIVE_SCOPE( "culture fallback families that are not in the test catalog warn once" );
    SW_ASSERT_TRUE( fixture.initialize( catalog ) );

    const sw::FontFaceChain koreanChain = fixture._fontSystem->getFaceChain( sw::FontSpec{} );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( koreanChain._faceCount ) ); // 기본(라틴)이 고른 가족이자 끝 — Noto Sans KR 이 사이에
    const sw::FontFaceId koreanFace = koreanChain._arrFace[1];

    SW_ASSERT_TRUE( localization.setCurrentLanguage( "ar" ) );
    fixture._fontSystem->invalidateFaceChains();
    const sw::FontFaceChain arabicChain = fixture._fontSystem->getFaceChain( sw::FontSpec{} );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( arabicChain._faceCount ) );
    SW_EXPECT_NOT_EQUAL( koreanFace, arabicChain._arrFace[1] );
    for ( uint32 index = 0; index < arabicChain._faceCount; ++index )
    {
        SW_EXPECT_NOT_EQUAL( koreanFace, arabicChain._arrFace[index] );
    }
}

/**
 * @brief [FontSystemTest] 한국어 문화권이면 시스템 대체 가족(맑은 고딕 · Noto Sans KR)이 '가' 를 맡는다 — 시스템 글꼴이 없는 기계(CI 리눅스)는 건너뛴다
 * @details 시스템 글꼴은 기계마다 달라 글리프 모양이 아니라 "글리프가 있다 · 기본 라틴 면이 아니다" 만 본다(결정 R1).
 */
SW_TEST_CASE( FontSystemTest, SystemFallbackCoversHangulWhenInstalled )
{
    const bool bHasKoreanSystemFont = sw::SystemFontLocator::findSystemFontFile( "malgun.ttf" ).empty() == false ||
                                      sw::SystemFontLocator::findSystemFontFile( "NotoSansKR-Regular.ttf" ).empty() == false ||
                                      sw::SystemFontLocator::findSystemFontFile( "NotoSansCJK-Regular.ttc" ).empty() == false;
    if ( bHasKoreanSystemFont == false )
        SW_TEST_SKIP( "no Korean system font on this machine" );

    using sw::test::LocalizationTestUtil;
    const sw::string folder      = test::makeTempDirectory( "font_system_hangul" );
    const sw::string projectPath = LocalizationTestUtil::writeProject( folder, "en", R"([ "ko" ])" );
    LocalizationTestUtil::writeSourceTable( folder, "en", R"("ui.title": { "source": "Title" })" );
    LocalizationTestUtil::writeTranslation( folder, "ko", R"("ui.title": { "text": "제목" })" );
    sw::LocalizationManager localization;
    SW_ASSERT_TRUE( LocalizationTestUtil::loadEngineCultures( localization ) );
    SW_ASSERT_TRUE( localization.mountProject( projectPath, sw::LocalizationScope::Game ) );
    SW_ASSERT_TRUE( localization.setCurrentLanguage( "ko" ) );

    sw::FontSystem fontSystem( sw::IFontRasterizer::createDefault(), &localization );
    SW_TEST_DEFENSIVE_SCOPE( "fallback families not installed on this machine warn once" );
    SW_ASSERT_TRUE( fontSystem.initialize( sw::EngineDefaultAssets{}._fontCatalog ) );
    const sw::FontFaceChain chain = fontSystem.getFaceChain( sw::FontSpec{} );
    SW_ASSERT_TRUE( chain._faceCount >= 2 );
    uint32               glyphIndex = 0;
    const sw::FontFaceId face       = fontSystem.findFaceForCodepoint( chain, 0xAC00u, glyphIndex );
    SW_EXPECT_NOT_EQUAL( 0u, glyphIndex );
    SW_EXPECT_NOT_EQUAL( chain._arrFace[0], face );
}
