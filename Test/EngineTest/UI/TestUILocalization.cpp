#include "pch.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Text/FontSystem.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "EngineTest/LocalizationTestUtil.h"
#include "EngineTest/Text/FakeFontRasterizer.h"

#include "TestFramework/TestFramework.h"

// UILocalizationTest — 현지화 글 바인딩(6-2): 글 위젯의 `_text` 는 키 또는 글 그대로, 언어를 바꾸면(글 판) 다시 풀고 다시 잰다 · 글꼴 대체 사슬도 문화권을 따른다.
// 글꼴은 가짜 래스터라이저(글자마다 0.5 em 전진 — 너비 = 글자 수에 비례)라 기계와 상관없다. 디바이스 없음(nogpu).

namespace
{
    struct UILocalizationTestUtil
    {
        static constexpr float32     kFrameSeconds = 1.0f / 60.0f;
        static constexpr const utf8* kLatinPath    = "test/fonts/latin.ttf";
        static constexpr const utf8* kHangulPath   = "test/fonts/notosanskr.ttf";

        static void runFrame( sw::InputManager& input, sw::UISystem& ui )
        {
            input.beginFrame( kFrameSeconds );
            ui.processInput( kFrameSeconds );
            ui.update( kFrameSeconds, sw::UIViewport{
                                          sw::float2{ 1280.0f, 720.0f }
            } );
            input.endFrame();
        }
    };

    /**
     * @brief 프로젝트(원문 en · 번역 ko) · 엔진 문화권 표 · 가짜 글꼴(라틴 기본 + ko 대체 가족 "Noto Sans KR" — 한글만) · UI 시스템입니다.
     * @details 문화권 표의 ko 대체 가족 이름을 카탈로그 가족으로 둔다 — 시스템 글꼴 없이 사슬만 본다.
     */
    struct UILocalizationFixture
    {
        sw::LocalizationManager         _localization;
        sw::test::FakeFontSystemFixture _fonts;
        sw::InputManager                _input;
        sw::UISystem                    _ui;

        UILocalizationFixture()
            : _localization{}
            , _fonts{ &_localization }
            , _input{}
            , _ui{}
        {
            using sw::test::LocalizationTestUtil;
            const sw::string folder      = test::makeTempDirectory( "ui_localization" );
            const sw::string projectPath = LocalizationTestUtil::writeProject( folder, "en", R"([ "ko" ])" );
            LocalizationTestUtil::writeSourceTable( folder, "en", R"("Menu.Start": { "source": "Start" })" );
            LocalizationTestUtil::writeTranslation( folder, "ko", R"("Menu.Start": { "text": "새 게임을 시작합니다" })" );
            SW_EXPECT_TRUE( LocalizationTestUtil::loadEngineCultures( _localization ) );
            SW_EXPECT_TRUE( _localization.mountProject( projectPath, sw::LocalizationScope::Game ) );
            SW_EXPECT_TRUE( _localization.setCurrentLanguage( "en" ) );

            sw::FontCatalogDesc catalog{};
            catalog._defaultFamily = "Latin";
            sw::test::FakeFontSystemFixture::addFamily( catalog, "Latin", UILocalizationTestUtil::kLatinPath );
            sw::test::FakeFontSystemFixture::addFamily( catalog, "Noto Sans KR", UILocalizationTestUtil::kHangulPath );
            sw::test::FakeFontFaceConfig latin{};
            latin.addRange( 0x20u, 0x24Fu ); // 라틴 확장까지 — 의사 문화권의 악센트 글자도 라틴 면이 그린다
            _fonts._pRasterizer->setFaceConfig( UILocalizationTestUtil::kLatinPath, latin );
            sw::test::FakeFontFaceConfig hangul{};
            hangul.addRange( 0x20u, 0x20u ).addRange( 0xAC00u, 0xD7A3u );
            _fonts._pRasterizer->setFaceConfig( UILocalizationTestUtil::kHangulPath, hangul );
            {
                SW_TEST_DEFENSIVE_SCOPE( "culture fallback families that are not in the test catalog warn once" );
                SW_EXPECT_TRUE( _fonts.initialize( catalog ) );
            }

            SW_EXPECT_TRUE( _input.initialize() );
            SW_EXPECT_TRUE( _ui.initialize( _input, _fonts._fontSystem.get() ) );
            _ui.setLocalization( &_localization );
        }

        ~UILocalizationFixture()
        {
            _ui.shutdown();
            _input.shutdown();
        }

        UILocalizationFixture( const UILocalizationFixture& )            = delete;
        UILocalizationFixture& operator=( const UILocalizationFixture& ) = delete;

        /** @brief 글 위젯 하나를 루트로 둔 화면을 올립니다. */
        sw::TextWidget* pushText( const utf8* pKeyOrText )
        {
            sw::unique_ptr<sw::TextWidget> text = sw::make_unique<sw::TextWidget>();
            text->setText( pKeyOrText );
            sw::TextWidget* pText = text.get();
            (void)_ui.pushScreen( sw::make_unique<sw::UIScreen>( sw::UIScreenDesc{}, std::move( text ) ) );
            return pText;
        }
    };
} // namespace

/** @brief [UILocalizationTest] 언어를 바꾸면 이미 그린 글이 새 언어로 바뀌고 다시 잰다(원하는 크기가 바뀐다) */
SW_TEST_CASE( UILocalizationTest, LanguageSwitchUpdatesTextAndLayout )
{
    UILocalizationFixture fixture;
    sw::TextWidget*       pText = fixture.pushText( "Menu.Start" );
    UILocalizationTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_STREQ( "Start", pText->getDisplayText().c_str() );
    const float32 englishWidth = pText->getDesiredSize()._x;
    SW_EXPECT_TRUE( englishWidth > 0.0f );

    SW_ASSERT_TRUE( fixture._localization.setCurrentLanguage( "ko" ) );
    UILocalizationTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_STREQ( "새 게임을 시작합니다", pText->getDisplayText().c_str() );
    SW_EXPECT_STREQ( "Menu.Start", pText->getText().c_str() ); // 칸은 키 그대로
    SW_EXPECT_TRUE_MSG( pText->getDesiredSize()._x > englishWidth * 1.5f, "the longer Korean text is measured again" );
}

/** @brief [UILocalizationTest] 표에 없는 키(또는 원문 글)는 그대로 보인다 — 끄면(사용자가 친 글) 표에 있어도 풀지 않는다 */
SW_TEST_CASE( UILocalizationTest, MissingKeyShowsSourceText )
{
    UILocalizationFixture fixture;
    sw::TextWidget*       pPlain = fixture.pushText( "Paused" );
    sw::TextWidget*       pTyped = fixture.pushText( "Menu.Start" );
    pTyped->setLocalized( false );
    UILocalizationTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_STREQ( "Paused", pPlain->getDisplayText().c_str() );
    SW_EXPECT_STREQ( "Menu.Start", pTyped->getDisplayText().c_str() );
}

/** @brief [UILocalizationTest] 의사 문화권(qps-ploc)은 글을 늘린다 — 늘어난 글로 다시 잰다(긴 번역에 레이아웃이 버티는지 보는 길) */
SW_TEST_CASE( UILocalizationTest, PseudoLocaleExpandsLayout )
{
    UILocalizationFixture fixture;
    sw::TextWidget*       pText = fixture.pushText( "Menu.Start" );
    UILocalizationTestUtil::runFrame( fixture._input, fixture._ui );
    const float32 sourceWidth = pText->getDesiredSize()._x;

    SW_ASSERT_TRUE( fixture._localization.setCurrentLanguage( "qps-ploc" ) );
    UILocalizationTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_TRUE_MSG( pText->getDisplayText() != "Start", pText->getDisplayText().c_str() );
    SW_EXPECT_TRUE_MSG( pText->getDesiredSize()._x > sourceWidth * 1.3f, pText->getDisplayText().c_str() );
}

/** @brief [UILocalizationTest] 글꼴 대체 사슬이 문화권을 따른다 — ko 로 바꾸면 한글이 대체 가족의 글리프로(두부가 아니게) 다시 배치된다 */
SW_TEST_CASE( UILocalizationTest, FontChainChangesWithCulture )
{
    UILocalizationFixture fixture;
    SW_ASSERT_TRUE( fixture._localization.setCurrentLanguage( "ko" ) );
    sw::TextWidget* pText = fixture.pushText( "\xEA\xB0\x80" ); // '가' — 키가 아닌 글 그대로
    UILocalizationTestUtil::runFrame( fixture._input, fixture._ui );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( pText->getLastLayout()._listGlyph.size() ) );
    const sw::FontFaceID koreanFace = pText->getLastLayout()._listGlyph[0]._face;
    SW_EXPECT_EQUAL( 0xAC00u, pText->getLastLayout()._listGlyph[0]._glyphIndex ); // 가짜 면의 글리프 번호 = 코드 포인트

    SW_ASSERT_TRUE( fixture._localization.setCurrentLanguage( "en" ) );
    UILocalizationTestUtil::runFrame( fixture._input, fixture._ui );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( pText->getLastLayout()._listGlyph.size() ) );
    SW_EXPECT_NOT_EQUAL( koreanFace, pText->getLastLayout()._listGlyph[0]._face ); // en 사슬에는 한글 가족이 없다 — 라틴 면의 두부
    SW_EXPECT_EQUAL( 0u, pText->getLastLayout()._listGlyph[0]._glyphIndex );
}
