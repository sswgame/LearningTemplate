// UserSettingsManager 품질 번들 — 옛 사용자 파일에 없는(판이 오른 뒤 번들에 새로 묶인) 설정이 저장된 프리셋을 따르는지, 렌더 품질 옵션 이름이 단계 이름과 한 벌인지(nogpu).
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/UserSettings/UserSettingsManager.h"
#include "Engine/UserSettings/UserSettingsSchema.h"

#include "sw/config/ShippingHostDefaults.h"

using namespace sw;

namespace
{
    // 대상 없는 설정만 둔다 — 전역 변수 · 핸들러 없이 값 · 번들만 본다. graphics.renderQuality 가 나중에 묶인 설정의 자리다.
    constexpr const char* kSchemaXML = "<UserSettingsSchema owner=\"engine\" version=\"1\"><Category id=\"graphics\"/>"
                                       "<Setting id=\"graphics.quality\" category=\"graphics\" type=\"Enum\" default=\"high\">"
                                       "<Option value=\"low\"/><Option value=\"high\"/><Option value=\"custom\"/></Setting>"
                                       "<Setting id=\"graphics.shadowQuality\" category=\"graphics\" type=\"Enum\" default=\"high\"><Option value=\"low\"/><Option value=\"high\"/></Setting>"
                                       "<Setting id=\"graphics.renderQuality\" category=\"graphics\" type=\"Enum\" default=\"high\"><Option value=\"low\"/><Option value=\"high\"/></Setting>"
                                       "<Scalability setting=\"graphics.quality\" custom=\"custom\">"
                                       "<Preset name=\"low\"><Value setting=\"graphics.shadowQuality\" value=\"low\"/><Value setting=\"graphics.renderQuality\" value=\"low\"/></Preset>"
                                       "<Preset name=\"high\"><Value setting=\"graphics.shadowQuality\" value=\"high\"/><Value setting=\"graphics.renderQuality\" value=\"high\"/></Preset>"
                                       "</Scalability>"
                                       "</UserSettingsSchema>";
} // namespace

/**
 * @brief [UserSettingsScalabilityTest] 옛 파일(quality=low, renderQuality 키 없음)을 읽으면 빠진 설정이 프리셋 low 를 따르고 번들은 low 로 남는다(Custom 으로 떨어지지 않는다)
 * @details custom 파일의 빠진 설정은 기본값 그대로다. 적힌 값은 프리셋보다 이긴다.
 */
SW_TEST_CASE( UserSettingsScalabilityTest, MissingBundledSettingFollowsSavedPreset )
{
    {
        sw::UserSettingsManager manager;
        SW_REQUIRE_TRUE( manager.loadSchemaFromXMLText( kSchemaXML, "test" ) );
        SW_REQUIRE_TRUE( manager.loadUserJSON( R"({ "versions": { "engine": 1 }, "values": { "graphics.quality": "low", "graphics.shadowQuality": "low" } })", "old" ) ==
                         sw::UserSettingsFileReadResult::Read );
        SW_EXPECT_TRUE( manager.getValue( sw::hashed_string( "graphics.renderQuality" ) ) == "low" );
        SW_EXPECT_TRUE( manager.getValue( sw::hashed_string( "graphics.quality" ) ) == "low" );
    }
    {
        sw::UserSettingsManager manager;
        SW_REQUIRE_TRUE( manager.loadSchemaFromXMLText( kSchemaXML, "test" ) );
        SW_REQUIRE_TRUE( manager.loadUserJSON( R"({ "versions": { "engine": 1 }, "values": { "graphics.quality": "custom", "graphics.shadowQuality": "low" } })", "custom" ) ==
                         sw::UserSettingsFileReadResult::Read );
        SW_EXPECT_TRUE( manager.getValue( sw::hashed_string( "graphics.renderQuality" ) ) == "high" );
        SW_EXPECT_TRUE( manager.getValue( sw::hashed_string( "graphics.quality" ) ) == "custom" );
    }
}

/**
 * @brief [UserSettingsScalabilityTest] 이름은 한 벌이다 — 옵션 `graphics.renderQuality` 의 선택지 = 커밋된 단계 표의 단계 = 품질 프리셋 이름이고, 프리셋마다 같은 이름의 단계를 고른다
 * @details 두 벌이면(옵션 low · 단계 Low) 옵션이 넣은 이름을 단계 표가 찾지 못해 로드 오류가 된다. 옵션은 `gv_renderQuality` 에 선택지 이름 그대로 넣는다.
 */
SW_TEST_CASE( UserSettingsScalabilityTest, RenderQualityOptionNamesAreTierNames )
{
    string schemaText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::toDiskPath( FileUtil::joinPath( ProcessRoots::get()._project.str(), "Content/engine/settings/engine.settings.xml" ) ), schemaText ) );
    UserSettingsSchema schema;
    SW_REQUIRE_TRUE( schema.loadFromXMLText( schemaText, "engine.settings.xml" ) );
    const UserSettingDef* pSetting = schema.findSetting( hashed_string( "graphics.renderQuality" ) );
    SW_REQUIRE_NOT_NULL( pSetting );
    SW_EXPECT_TRUE( pSetting->_targetKind == UserSettingTargetKind::GlobalVariable && pSetting->_targetName == hashed_string( "gv_renderQuality" ) );

    JSONDocument tiers;
    SW_REQUIRE_TRUE( tiers.parse( shipping_host::kRenderQualityJSON, "RenderQuality.json (configure-time copy)" ) );
    vector<string> listTier = tiers.getRoot().getMemberNames();
    vector<string> listOption;
    for ( const UserSettingOption& option : pSetting->_listOption )
    {
        SW_EXPECT_TRUE_MSG( option._targetValue.empty(), option._value.c_str() ); // 글 변수에는 선택지 이름이 그대로 간다
        listOption.push_back( option._value );
    }
    VectorUtil::sort( listTier );
    VectorUtil::sort( listOption );
    SW_EXPECT_TRUE( listTier == listOption );
    SW_EXPECT_TRUE( pSetting->_defaultValue == "high" ); // gv_renderQuality 의 기본값과 같다(바탕 그대로인 단계)

    bool bFoundGroup = false;
    for ( const ScalabilityGroupDef& group : schema.getScalabilityGroups() )
    {
        if ( ( group._settingID == hashed_string( "graphics.quality" ) ) == false )
            continue;
        bFoundGroup = true;
        for ( const ScalabilityPresetDef& preset : group._listPreset )
        {
            const ScalabilityPresetValue* pValue = VectorUtil::findIf( preset._listValue, []( const ScalabilityPresetValue& value )
            { return value._settingID == hashed_string( "graphics.renderQuality" ); } );
            SW_REQUIRE_TRUE_MSG( pValue != nullptr, preset._name.c_str() );
            SW_EXPECT_TRUE_MSG( pValue->_value == preset._name.c_str(), preset._name.c_str() );
        }
    }
    SW_EXPECT_TRUE( bFoundGroup );
}
