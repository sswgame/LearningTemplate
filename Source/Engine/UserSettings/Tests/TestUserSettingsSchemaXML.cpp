// UserSettingsSchema — 정의 구조체를 스키마로 읽기: 이름 · 열거자 · 기본값 · 작은 문법 칸 · 칸 오류(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/UserSettings/HardwareDetectionResult.h"
#include "Engine/UserSettings/UserSettingsSchema.h"

/**
 * @brief [UserSettingsSchemaXMLTest] 설정 · 선택지 · 품질 번들 · 판 올림을 정의 구조체의 PROPERTY 로 읽는다(작은 문법 칸 target · enabledWhen · platforms 는 손으로)
 */
SW_TEST_CASE( UserSettingsSchemaXMLTest, ReadsDefinitionsThroughSchemas )
{
    sw::UserSettingsSchema schema;
    SW_REQUIRE_TRUE( schema.loadFromXMLText( "<UserSettingsSchema version=\"3\">"
                                             "<Category id=\"audio\" text=\"Menu.Audio\"/>"
                                             "<Setting id=\"audio.master\" category=\"audio\" type=\"Float\" default=\"0.50\" min=\"0\" max=\"1\" step=\"0.1\" apply=\"Immediate\"/>"
                                             "<Setting id=\"audio.mode\" category=\"audio\" type=\"Enum\" default=\"stereo\" enabledWhen=\"audio.master!=0\" platforms=\"windows\">"
                                             "<Option value=\"stereo\" text=\"Menu.Stereo\"/><Option value=\"surround\"/></Setting>"
                                             "<Upgrade version=\"2\" op=\"Rename\" key=\"audio.volume\" to=\"audio.master\"/>"
                                             "</UserSettingsSchema>",
                                             "test" ) );
    SW_EXPECT_EQUAL( uint32{ 3 }, schema.getVersion() );
    const sw::UserSettingDef* pMaster = schema.findSetting( sw::hashed_string( "audio.master" ) );
    SW_REQUIRE_TRUE( pMaster != nullptr );
    SW_EXPECT_TRUE( pMaster->_type == sw::UserSettingType::Float && pMaster->_applyTiming == sw::UserSettingApplyTiming::Immediate );
    SW_EXPECT_EQUAL( sw::string( "0.5" ), pMaster->_defaultValue ); // 기본값은 정규화된다
    SW_EXPECT_EQUAL( 0.1, pMaster->_step );
    const sw::UserSettingDef* pMode = schema.findSetting( sw::hashed_string( "audio.mode" ) );
    SW_REQUIRE_TRUE( pMode != nullptr );
    SW_EXPECT_EQUAL( size_t{ 2 }, pMode->_listOption.size() );
    SW_EXPECT_EQUAL( size_t{ 1 }, pMode->_listEnabledCondition.size() );
    SW_EXPECT_EQUAL( uint8{ sw::UserSettingsPlatform::kWindows }, pMode->_platformMask );
    SW_EXPECT_TRUE( pMode->_applyTiming == sw::UserSettingApplyTiming::OnConfirm ); // 적지 않은 칸은 정의 구조체의 초기값
    SW_REQUIRE_EQUAL( size_t{ 1 }, schema.getUpgradeSteps().size() );
    SW_EXPECT_TRUE( schema.getUpgradeSteps()[0]._operation == sw::UserSettingsUpgradeOperation::Rename );
    SW_EXPECT_EQUAL( sw::string( "audio.master" ), schema.getUpgradeSteps()[0]._newKey );
}

/**
 * @brief [UserSettingsSchemaXMLTest] 모르는 속성 · 모르는 열거 이름 · 빠진 필수는 스키마를 받지 않는다
 */
SW_TEST_CASE( UserSettingsSchemaXMLTest, RejectsFieldErrors )
{
    SW_EXPECT_LOG( "XMLFields.UnknownField" );
    SW_EXPECT_LOG( "XMLFields.ValueUnreadable" );
    SW_EXPECT_LOG( "Serialization.UnknownEnumerator" );
    SW_EXPECT_LOG( "XMLFields.RequiredFieldMissing" );
    sw::UserSettingsSchema schema;
    SW_EXPECT_FALSE( schema.loadFromXMLText( "<UserSettingsSchema><Setting id=\"a\" type=\"Bool\" colour=\"red\"/></UserSettingsSchema>", "test" ) );
    SW_EXPECT_FALSE( schema.loadFromXMLText( "<UserSettingsSchema><Setting id=\"b\" type=\"bool\"/></UserSettingsSchema>", "test" ) );
    SW_EXPECT_FALSE( schema.loadFromXMLText( "<UserSettingsSchema><Setting type=\"Bool\"/></UserSettingsSchema>", "test" ) );
}

/**
 * @brief [UserSettingsSchemaXMLTest] 자동 품질 규칙은 하한 · 플랫폼 · RHI 능력을 모두 넘어야 맞는다(휴대 기기 줄은 그 플랫폼에서만)
 */
SW_TEST_CASE( UserSettingsSchemaXMLTest, AutoDetectRuleChecksPlatformAndCapabilities )
{
    sw::ScalabilityAutoDetectRule rule;
    rule._platform               = "Android";
    rule._minSystemMemoryMb      = 7500;
    rule._bRequireNativeBindless = true;

    sw::HardwareDetectionResult probe;
    probe._platformName                     = "Android";
    probe._systemMemoryMb                   = 8000;
    probe._rhiCapabilities._bNativeBindless = SW_TRUE;
    SW_EXPECT_TRUE( rule.matches( probe ) );

    probe._rhiCapabilities._bNativeBindless = SW_FALSE;
    SW_EXPECT_FALSE( rule.matches( probe ) ); // 능력이 모자란다

    probe._rhiCapabilities._bNativeBindless = SW_TRUE;
    probe._platformName                     = "Windows";
    SW_EXPECT_FALSE( rule.matches( probe ) ); // 다른 플랫폼

    const sw::ScalabilityAutoDetectRule anyPlatform;
    SW_EXPECT_TRUE( anyPlatform.matches( probe ) ); // 빈 규칙은 무엇이든 맞는다(마지막 줄)
}
