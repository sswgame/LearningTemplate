// UserSettingsStageBaselineTest — 자동화 단계 경계의 사용자 설정 되돌리기(핸들러 대상 · 대상 없는 값)와 언어 설정 빈 값("게임 기본")으로 돌아가기(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Localization/LocalizationManager.h"
#include "Engine/UserSettings/UserSettingsManager.h"

using namespace sw;

namespace
{
    /** @brief 핸들러가 마지막으로 받은 값을 적어 두는 대상입니다. */
    struct UserSettingsHandlerProbe
    {
        string _lastValue{};
        uint32 _callCount{ 0 };
        bool   _bLastStartup{ true };

        bool apply( const UserSettingApplyParams& context )
        {
            _lastValue    = string( context._value );
            _bLastStartup = context._bStartup;
            ++_callCount;
            return true;
        }
    };

    constexpr const char* kStageSchemaXML = "<UserSettingsSchema owner=\"engine\" version=\"1\"><Category id=\"test\"/>"
                                            "<Setting id=\"test.handled\" category=\"test\" type=\"String\" default=\"\" target=\"handler:test.sink\"/>"
                                            "<Setting id=\"test.plain\" category=\"test\" type=\"Bool\" default=\"true\"/>"
                                            "</UserSettingsSchema>";

    constexpr const char* kLanguageSchemaXML = "<UserSettingsSchema owner=\"engine\" version=\"1\"><Category id=\"language\"/>"
                                               "<Setting id=\"language.text\" category=\"language\" type=\"Enum\" default=\"\" optionsFrom=\"localization.languages\" apply=\"Immediate\" "
                                               "target=\"handler:localization.language\"/>"
                                               "</UserSettingsSchema>";
} // namespace

/** @brief [UserSettingsStageBaselineTest] 단계 경계는 앞 단계가 바꾼 확정 값을 기준으로 되돌리고 핸들러 대상에도 다시 넣는다 — 바뀐 것이 없으면 아무것도 부르지 않는다 */
SW_TEST_CASE( UserSettingsStageBaselineTest, RestoresChangedSettingsAndTheirTargets )
{
    UserSettingsManager      manager;
    UserSettingsHandlerProbe probe;
    const Subscription       registration = manager.getRegistry().registerHandler( hashed_string( "test.sink" ),
                                                                                   SW_DELEGATE_METHOD( UserSettingHandlerDelegate, &UserSettingsHandlerProbe::apply, &probe ) );
    SW_REQUIRE_TRUE( manager.loadSchemaFromXMLText( kStageSchemaXML, "test" ) );
    SW_EXPECT_EQUAL( uint32( 0 ), manager.restoreStageBaseline() ); // 기준이 없으면 아무 일도 없다
    manager.captureStageBaseline();

    SW_EXPECT_TRUE( manager.setPendingValue( hashed_string( "test.handled" ), "changed" ) == UserSettingSetResult::Accepted );
    SW_EXPECT_TRUE( manager.setPendingValue( hashed_string( "test.plain" ), "false" ) == UserSettingSetResult::Accepted );
    (void)manager.applyPending();
    SW_EXPECT_TRUE( probe._lastValue == "changed" );

    const uint32 callsBefore = probe._callCount;
    SW_EXPECT_EQUAL( uint32( 2 ), manager.restoreStageBaseline() );
    SW_EXPECT_TRUE( manager.getValue( hashed_string( "test.handled" ) ) == "" );
    SW_EXPECT_TRUE( manager.getValue( hashed_string( "test.plain" ) ) == "true" );
    SW_EXPECT_EQUAL( callsBefore + 1, probe._callCount );
    SW_EXPECT_TRUE( probe._lastValue.empty() );
    SW_EXPECT_FALSE( probe._bLastStartup ); // 핸들러 대상은 메뉴에서 바꾼 것처럼 넣는다(빈 값 = 기본으로 돌아가기가 듣는다)

    SW_EXPECT_EQUAL( uint32( 0 ), manager.restoreStageBaseline() );
    SW_EXPECT_EQUAL( callsBefore + 1, probe._callCount );
}

/** @brief [UserSettingsStageBaselineTest] 보류만 한 값(`Immediate` 미리 보기로 이미 대상에 들어간 값)도 되돌린다 */
SW_TEST_CASE( UserSettingsStageBaselineTest, RestoresPendingPreview )
{
    UserSettingsManager      manager;
    UserSettingsHandlerProbe probe;
    const Subscription       registration = manager.getRegistry().registerHandler( hashed_string( "test.sink" ),
                                                                                   SW_DELEGATE_METHOD( UserSettingHandlerDelegate, &UserSettingsHandlerProbe::apply, &probe ) );
    SW_REQUIRE_TRUE( manager.loadSchemaFromXMLText( kStageSchemaXML, "test" ) );
    manager.captureStageBaseline();
    SW_EXPECT_TRUE( manager.setPendingValue( hashed_string( "test.plain" ), "false" ) == UserSettingSetResult::Accepted );
    SW_EXPECT_TRUE( manager.isPending( hashed_string( "test.plain" ) ) );
    SW_EXPECT_EQUAL( uint32( 1 ), manager.restoreStageBaseline() );
    SW_EXPECT_FALSE( manager.hasPendingChanges() );
    SW_EXPECT_TRUE( manager.getValue( hashed_string( "test.plain" ) ) == "true" );
}

/** @brief [UserSettingsStageBaselineTest] 언어 설정의 빈 값은 "게임 기본 언어" 다 — 다른 언어를 골랐다가 빈 값을 고르면 처음 정한 언어로 돌아온다 */
SW_TEST_CASE( UserSettingsStageBaselineTest, EmptyLanguageReturnsToStartupLanguage )
{
    LocalizationManager localization;
    const hashed_string key( "GREETING" );
    localization.setString( "en", key, "Hello" );
    localization.setString( "fr", key, "Bonjour" );
    localization.setStartupLanguage( "en" ); // 기동(`initialize`)이 고른 게임 기본 언어 자리
    SW_REQUIRE_TRUE( localization.setCurrentLanguage( "en" ) );

    UserSettingsManager manager;
    UserSettingsTargets targets;
    targets._pLocalizationManager = &localization;
    manager.initialize( targets );
    SW_REQUIRE_TRUE( manager.loadSchemaFromXMLText( kLanguageSchemaXML, "test" ) );

    const hashed_string settingID( "language.text" );
    SW_EXPECT_TRUE( manager.setPendingValue( settingID, "fr" ) == UserSettingSetResult::Accepted );
    (void)manager.applyPending();
    SW_EXPECT_TRUE( localization.getCurrentLanguage() == "fr" );
    SW_EXPECT_TRUE( localization.getStartupLanguage() == "en" ); // 사용자가 고른 언어는 기본을 바꾸지 않는다

    SW_EXPECT_TRUE( manager.setPendingValue( settingID, "" ) == UserSettingSetResult::Accepted );
    (void)manager.applyPending();
    SW_EXPECT_TRUE( localization.getCurrentLanguage() == "en" );
    manager.shutdown();
}

/** @brief [UserSettingsApplyValueTest] 설정 하나만 확정한다 — 다른 설정의 보류 값은 그대로 보류다(시점 순환 키가 메뉴의 보류를 확정하지 않는다) */
SW_TEST_CASE( UserSettingsApplyValueTest, AppliesOnlyThatSetting )
{
    UserSettingsManager      manager;
    UserSettingsHandlerProbe probe;
    const Subscription       registration = manager.getRegistry().registerHandler( hashed_string( "test.sink" ),
                                                                                   SW_DELEGATE_METHOD( UserSettingHandlerDelegate, &UserSettingsHandlerProbe::apply, &probe ) );
    SW_REQUIRE_TRUE( manager.loadSchemaFromXMLText( kStageSchemaXML, "test" ) );
    SW_EXPECT_TRUE( manager.setPendingValue( hashed_string( "test.plain" ), "false" ) == UserSettingSetResult::Accepted );

    SW_EXPECT_TRUE( manager.applyValue( hashed_string( "test.handled" ), "chosen" ) == UserSettingSetResult::Accepted );
    SW_EXPECT_TRUE( probe._lastValue == "chosen" );
    SW_EXPECT_TRUE( manager.getAppliedValue( hashed_string( "test.handled" ) ) == "chosen" );
    SW_EXPECT_TRUE( manager.isPending( hashed_string( "test.plain" ) ) ); // 남의 보류는 그대로
    SW_EXPECT_TRUE( manager.getAppliedValue( hashed_string( "test.plain" ) ) == "true" );

    SW_EXPECT_TRUE( manager.applyValue( hashed_string( "test.handled" ), "chosen" ) == UserSettingSetResult::Unchanged );
    SW_EXPECT_TRUE( manager.applyValue( hashed_string( "test.missing" ), "x" ) == UserSettingSetResult::Unknown );
}
