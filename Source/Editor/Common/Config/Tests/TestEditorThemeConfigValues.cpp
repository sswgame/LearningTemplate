// EditorThemeConfig — 고른 테마 찾기, 사용자 덮어쓰기, 환경설정 값과의 왕복. 파일과 ImGui 없이 손으로 만든 벌로 본다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Config/EditorThemeConfig.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct EditorThemeConfigTestUtil
    {
        static constexpr float32 kTolerance = 1e-5f;

        /** @brief 토큰 값이 모두 다른 벌입니다 — 어느 칸을 썼는지 값으로 가려진다. */
        static EditorThemeConfig makeTheme( const utf8* pName )
        {
            EditorThemeConfig theme{};
            theme._name                                           = pName;
            theme._displayName                                    = pName;
            theme._mapColor[hashed_string( "Base.Accent" )]       = Color{ 0.1f, 0.2f, 0.3f, 1.0f };
            theme._mapMetric[hashed_string( "Radius.Window" )]    = 11.0f;
            theme._mapMetric[hashed_string( "Radius.Popup" )]     = 12.0f;
            theme._mapMetric[hashed_string( "Radius.Frame" )]     = 13.0f;
            theme._mapMetric[hashed_string( "Radius.Scrollbar" )] = 14.0f;
            theme._mapMetric[hashed_string( "Radius.Grab" )]      = 15.0f;
            theme._mapMetric[hashed_string( "Radius.Tab" )]       = 16.0f;
            return theme;
        }

        static float32 metric( const EditorThemeConfig& theme, const utf8* pKey )
        {
            const float32* pValue = theme.findMetric( hashed_string( pKey ) );
            SW_EXPECT_TRUE( pValue != nullptr );
            return pValue != nullptr ? *pValue : -100.0f;
        }

        static const Color& accent( const EditorThemeConfig& theme ) { return *theme.findColor( hashed_string( "Base.Accent" ) ); } // `makeTheme` 이 늘 넣는다
    };
} // namespace

SW_TEST_CASE( EditorThemeConfigTest, FindsByNameIgnoringCase )
{
    const vector<EditorThemeConfig> listTheme{ EditorThemeConfigTestUtil::makeTheme( "dark" ), EditorThemeConfigTestUtil::makeTheme( "light" ) };
    const EditorThemeConfig*        pLight = EditorThemeConfig::findByName( listTheme, "LIGHT" );
    SW_REQUIRE_NOT_NULL( pLight );
    SW_EXPECT_EQUAL( string( "light" ), pLight->_name );
    SW_EXPECT_TRUE( EditorThemeConfig::findByName( listTheme, "lightish" ) == nullptr );
    SW_EXPECT_TRUE( EditorThemeConfig::findByName( listTheme, "" ) == nullptr );
}

// 없는 이름을 다른 벌로 바꿔 끼우지 않는다 — 고친 이름이 무시된 것을 아무도 모른다.
SW_TEST_CASE( EditorThemeConfigTest, MissingSavedNameLeavesTheOutputAlone )
{
    const vector<EditorThemeConfig> listTheme{ EditorThemeConfigTestUtil::makeTheme( "dark" ) };
    EditorConfig                    saved{};
    saved._themeName           = "gone";
    EditorThemeConfig outTheme = EditorThemeConfigTestUtil::makeTheme( "current" );
    SW_EXPECT_FALSE( EditorThemeConfig::buildFromSaved( listTheme, saved, outTheme ) );
    SW_EXPECT_EQUAL( string( "current" ), outTheme._name );
}

SW_TEST_CASE( EditorThemeConfigTest, SavedDefaultsKeepTheTokens )
{
    const vector<EditorThemeConfig> listTheme{ EditorThemeConfigTestUtil::makeTheme( "dark" ) };
    EditorConfig                    saved{};
    saved._themeName = "DARK";
    EditorThemeConfig theme{};
    SW_REQUIRE_TRUE( EditorThemeConfig::buildFromSaved( listTheme, saved, theme ) );
    SW_EXPECT_EQUAL( string( "dark" ), theme._name );
    SW_EXPECT_TRUE( theme._bAccentOverridden == SW_FALSE );
    SW_EXPECT_TRUE( theme._bRadiusOverridden == SW_FALSE );
    SW_EXPECT_NEAR( 0.1f, EditorThemeConfigTestUtil::accent( theme )._r, EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 14.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Scrollbar" ), EditorThemeConfigTestUtil::kTolerance );
}

// 색 하나를 이루지 못하는 채널은 쓰지 않는다.
SW_TEST_CASE( EditorThemeConfigTest, AccentNeedsAllThreeChannels )
{
    const vector<EditorThemeConfig> listTheme{ EditorThemeConfigTestUtil::makeTheme( "dark" ) };
    EditorConfig                    saved{};
    saved._themeAccentR = 0.9f;
    saved._themeAccentG = 0.8f;
    saved._themeName    = "dark";
    EditorThemeConfig theme{};
    SW_REQUIRE_TRUE( EditorThemeConfig::buildFromSaved( listTheme, saved, theme ) );
    SW_EXPECT_TRUE( theme._bAccentOverridden == SW_FALSE );
    SW_EXPECT_NEAR( 0.1f, EditorThemeConfigTestUtil::accent( theme )._r, EditorThemeConfigTestUtil::kTolerance );

    saved._themeAccentB = 0.0f; // 0 은 값이다
    SW_REQUIRE_TRUE( EditorThemeConfig::buildFromSaved( listTheme, saved, theme ) );
    SW_EXPECT_TRUE( theme._bAccentOverridden == SW_TRUE );
    SW_EXPECT_NEAR( 0.9f, EditorThemeConfigTestUtil::accent( theme )._r, EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 0.0f, EditorThemeConfigTestUtil::accent( theme )._b, EditorThemeConfigTestUtil::kTolerance );
}

// 결함: 창 둥글기만 적힌 설정은 입력 필드 · 탭 둥글기를 -1 로 넣어 스타일에 음수가 갔고, 창 둥글기가 없으면 나머지 값을 통째로 버렸다.
SW_TEST_CASE( EditorThemeConfigTest, EachSavedRadiusOverridesOnlyItsOwnTokens )
{
    const vector<EditorThemeConfig> listTheme{ EditorThemeConfigTestUtil::makeTheme( "dark" ) };
    EditorConfig                    saved{};
    saved._themeName           = "dark";
    saved._themeWindowRounding = 3.0f;
    EditorThemeConfig theme{};
    SW_REQUIRE_TRUE( EditorThemeConfig::buildFromSaved( listTheme, saved, theme ) );
    SW_EXPECT_TRUE( theme._bRadiusOverridden == SW_TRUE );
    SW_EXPECT_NEAR( 3.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Window" ), EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 3.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Popup" ), EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 13.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Frame" ), EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 14.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Scrollbar" ), EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 16.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Tab" ), EditorThemeConfigTestUtil::kTolerance );

    saved._themeWindowRounding = EditorConfig::kNotOverridden;
    saved._themeFrameRounding  = 4.0f;
    SW_REQUIRE_TRUE( EditorThemeConfig::buildFromSaved( listTheme, saved, theme ) );
    SW_EXPECT_NEAR( 11.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Window" ), EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 4.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Frame" ), EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 8.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Scrollbar" ), EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 4.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Grab" ), EditorThemeConfigTestUtil::kTolerance );

    saved._themeFrameRounding = EditorConfig::kNotOverridden;
    saved._themeTabRounding   = 0.0f;
    SW_REQUIRE_TRUE( EditorThemeConfig::buildFromSaved( listTheme, saved, theme ) );
    SW_EXPECT_TRUE( theme._bRadiusOverridden == SW_TRUE );
    SW_EXPECT_NEAR( 0.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Tab" ), EditorThemeConfigTestUtil::kTolerance );
}

SW_TEST_CASE( EditorThemeConfigTest, RadiusOverrideDerivesTheOtherCorners )
{
    EditorThemeConfig theme = EditorThemeConfigTestUtil::makeTheme( "dark" );
    EditorThemeConfig::overrideRadius( 2.0f, 3.0f, 4.0f, theme );
    SW_EXPECT_TRUE( theme._bRadiusOverridden == SW_TRUE );
    SW_EXPECT_NEAR( 2.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Window" ), EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 2.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Popup" ), EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 3.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Frame" ), EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 6.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Scrollbar" ), EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 3.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Grab" ), EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 4.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Tab" ), EditorThemeConfigTestUtil::kTolerance );
}

// 결함: 슬라이더에 손으로 친 음수가 그대로 스타일과 설정에 갔고, 저장 값에서는 "덮어쓰지 않음" 과 같아 다음 기동에 사라졌다.
SW_TEST_CASE( EditorThemeConfigTest, TypedNegativeRadiusStaysAnOverrideOfZero )
{
    EditorThemeConfig theme = EditorThemeConfigTestUtil::makeTheme( "dark" );
    EditorThemeConfig::overrideRadius( -5.0f, -1.0f, -2.0f, theme );
    SW_EXPECT_NEAR( 0.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Window" ), EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 0.0f, EditorThemeConfigTestUtil::metric( theme, "Radius.Scrollbar" ), EditorThemeConfigTestUtil::kTolerance );

    EditorConfig saved{};
    EditorThemeConfig::writeSaved( theme, saved );
    SW_EXPECT_NEAR( 0.0f, saved._themeWindowRounding, EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 0.0f, saved._themeFrameRounding, EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 0.0f, saved._themeTabRounding, EditorThemeConfigTestUtil::kTolerance );
}

// 덮어쓰지 않은 값은 저장하지 않는다 — 토큰 파일이 바뀌면 그 값을 따른다.
SW_TEST_CASE( EditorThemeConfigTest, UntouchedThemeSavesOnlyItsName )
{
    EditorConfig saved{};
    saved._themeAccentR        = 0.5f;
    saved._themeWindowRounding = 5.0f;
    EditorThemeConfig::writeSaved( EditorThemeConfigTestUtil::makeTheme( "light" ), saved );
    SW_EXPECT_EQUAL( string( "light" ), saved._themeName );
    SW_EXPECT_NEAR( EditorConfig::kNotOverridden, saved._themeAccentR, EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( EditorConfig::kNotOverridden, saved._themeAccentG, EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( EditorConfig::kNotOverridden, saved._themeAccentB, EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( EditorConfig::kNotOverridden, saved._themeWindowRounding, EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( EditorConfig::kNotOverridden, saved._themeFrameRounding, EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( EditorConfig::kNotOverridden, saved._themeTabRounding, EditorThemeConfigTestUtil::kTolerance );
}

SW_TEST_CASE( EditorThemeConfigTest, OverridesSurviveASaveAndLoad )
{
    const vector<EditorThemeConfig> listTheme{ EditorThemeConfigTestUtil::makeTheme( "dark" ) };
    EditorThemeConfig               edited = listTheme[0];
    EditorThemeConfig::overrideAccent( Color{ 0.7f, 0.6f, 0.5f, 1.0f }, edited );
    EditorThemeConfig::overrideRadius( 2.0f, 3.0f, 4.0f, edited );

    EditorConfig saved{};
    EditorThemeConfig::writeSaved( edited, saved );

    EditorThemeConfig loaded{};
    SW_REQUIRE_TRUE( EditorThemeConfig::buildFromSaved( listTheme, saved, loaded ) );
    SW_EXPECT_TRUE( loaded._bAccentOverridden == SW_TRUE );
    SW_EXPECT_TRUE( loaded._bRadiusOverridden == SW_TRUE );
    SW_EXPECT_NEAR( 0.7f, EditorThemeConfigTestUtil::accent( loaded )._r, EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 0.6f, EditorThemeConfigTestUtil::accent( loaded )._g, EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 0.5f, EditorThemeConfigTestUtil::accent( loaded )._b, EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 2.0f, EditorThemeConfigTestUtil::metric( loaded, "Radius.Popup" ), EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 6.0f, EditorThemeConfigTestUtil::metric( loaded, "Radius.Scrollbar" ), EditorThemeConfigTestUtil::kTolerance );
    SW_EXPECT_NEAR( 4.0f, EditorThemeConfigTestUtil::metric( loaded, "Radius.Tab" ), EditorThemeConfigTestUtil::kTolerance );
}
