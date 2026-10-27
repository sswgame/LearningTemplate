#include "pch.h"

#include "Engine/UI/Style/UIThemeController.h"

#include "TestFramework/TestFramework.h"

// UIThemeControllerTest — 테마 고르기 규칙(엔진 없음): 목록을 걸 때 설정의 테마 · 설정이 바뀔 때만 이름을 내놓기 · 모르는 이름 거절 · 빈 이름은 지금 테마. 디바이스 없음(nogpu).

namespace
{
    struct UIThemeControllerTestUtil
    {
        /** @brief light(기본) · dark 두 테마 목록입니다. 시트 경로는 쓰지 않습니다(스타일 세트를 짓지 않는 테스트). */
        static sw::UIThemeCatalog makeCatalog()
        {
            sw::UIThemeCatalog catalog{};
            catalog._defaultTheme = "light";
            sw::UIThemeDesc light{};
            light._name = "light";
            sw::UIThemeDesc dark{};
            dark._name = "dark";
            catalog._listTheme.push_back( light );
            catalog._listTheme.push_back( dark );
            return catalog;
        }
    };
} // namespace

/** @brief [UIThemeControllerTest] 설정 값이 목록에 있으면 그 테마를, 없거나 비었으면 빈 이름(기본 테마로 가라는 뜻)을 낸다 */
SW_TEST_CASE( UIThemeControllerTest, SettingThemeIsUsedOnlyWhenListed )
{
    sw::UIThemeController controller;
    controller.setCatalog( UIThemeControllerTestUtil::makeCatalog(), sw::string( "dark" ) );
    SW_EXPECT_STREQ( "dark", controller.findSettingTheme().c_str() );

    controller.setCatalog( UIThemeControllerTestUtil::makeCatalog(), sw::string( "missing" ) );
    SW_EXPECT_TRUE( controller.findSettingTheme().empty() );

    controller.setCatalog( UIThemeControllerTestUtil::makeCatalog(), sw::string() );
    SW_EXPECT_TRUE( controller.findSettingTheme().empty() );
    SW_EXPECT_STREQ( "light", controller.getCatalog()._defaultTheme.c_str() );
}

/** @brief [UIThemeControllerTest] 모르는 이름은 거절하고 지금 테마를 유지한다 */
SW_TEST_CASE( UIThemeControllerTest, UnknownThemeKeepsCurrent )
{
    SW_TEST_DEFENSIVE_SCOPE( "an unknown theme name keeps the current one" ); // 거절 · 실패 로그가 이 테스트가 보려는 동작이다
    sw::UIThemeController controller;
    controller.setCatalog( UIThemeControllerTestUtil::makeCatalog(), sw::string() );
    SW_EXPECT_TRUE( controller.getThemeName().empty() );
    SW_EXPECT_TRUE( controller.selectTheme( "dark" ) );
    SW_EXPECT_STREQ( "dark", controller.getThemeName().c_str() );
    SW_EXPECT_FALSE( controller.selectTheme( "missing" ) );
    SW_EXPECT_STREQ( "dark", controller.getThemeName().c_str() );
}

/** @brief [UIThemeControllerTest] 설정이 바뀔 때만 한 번 이름을 내놓고, 빈 값이나 빈 목록이면 기억만 한다 */
SW_TEST_CASE( UIThemeControllerTest, SettingChangeIsReportedOnce )
{
    sw::UIThemeController controller;
    controller.setCatalog( UIThemeControllerTestUtil::makeCatalog(), sw::string( "light" ) );
    sw::hashed_string theme;
    SW_EXPECT_FALSE( controller.pollSetting( sw::string( "light" ), theme ) ); // 목록을 걸 때 본 값과 같다

    SW_EXPECT_TRUE( controller.pollSetting( sw::string( "dark" ), theme ) );
    SW_EXPECT_STREQ( "dark", theme.c_str() );
    SW_EXPECT_FALSE( controller.pollSetting( sw::string( "dark" ), theme ) ); // 같은 값은 다시 내놓지 않는다

    // 모르는 이름도 내놓는다(거절은 selectTheme 의 일이다).
    SW_EXPECT_TRUE( controller.pollSetting( sw::string( "missing" ), theme ) );
    SW_EXPECT_STREQ( "missing", theme.c_str() );

    SW_EXPECT_FALSE( controller.pollSetting( sw::string(), theme ) ); // 빈 값은 기억만 한다

    sw::UIThemeController empty;
    empty.setCatalog( sw::UIThemeCatalog{}, sw::string() );
    SW_EXPECT_FALSE( empty.pollSetting( sw::string( "dark" ), theme ) ); // 테마 목록이 없으면 고를 것이 없다
}

/** @brief [UIThemeControllerTest] 빈 이름은 지금 테마를 뜻하고, 지금 테마도 없으면 서술이 없다 */
SW_TEST_CASE( UIThemeControllerTest, EmptyNameMeansCurrentTheme )
{
    sw::UIThemeController controller;
    controller.setCatalog( UIThemeControllerTestUtil::makeCatalog(), sw::string() );
    SW_EXPECT_NULL( controller.findTheme( sw::hashed_string{} ) );
    SW_ASSERT_TRUE( controller.selectTheme( "light" ) );
    const sw::UIThemeDesc* pCurrent = controller.findTheme( sw::hashed_string{} );
    SW_ASSERT_NOT_NULL( pCurrent );
    SW_EXPECT_STREQ( "light", pCurrent->_name.c_str() );
    const sw::UIThemeDesc* pDark = controller.findTheme( "dark" );
    SW_ASSERT_NOT_NULL( pDark );
    SW_EXPECT_STREQ( "dark", pDark->_name.c_str() );
    SW_EXPECT_NULL( controller.findTheme( "missing" ) );
}
