#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Resource/ResourcePaths.h"

#include "GameFramework/Base/Foundation/Data/GameSettings.h"

#include "TestFramework/TestFramework.h"

// PackEngineSettings — 팩 gamesettings.xml 의 `<Engine>` 요소(엔진이 읽는 게임별 설정).

/**
 * @brief [PackEngineSettingsTest] `<Engine>` 요소의 칸을 읽고, 적지 않은 경로 칸은 "없음" 으로 남는다 · 같은 파일의 `GameSettings` 는 그 요소를 건너뛴다
 */
SW_TEST_CASE( PackEngineSettingsTest, ReadsTheEngineElementAndGameSettingsSkipsIt )
{
    const sw::string       xml = "<GameSettings>"
                                 "<Engine _windowTitle=\"Probe Game\" _bUIPauseMenu=\"true\">"
                                 "<_mapUserSettingDefault><entry key=\"gameplay.fieldOfView\">80</entry></_mapUserSettingDefault>"
                                 "</Engine>"
                                 "<startMap>game/probe/maps/start.scene.xml</startMap>"
                                 "</GameSettings>";
    sw::PackEngineSettings settings;
    SW_ASSERT_TRUE( settings.loadFromXMLText( xml, "probe" ) );
    SW_EXPECT_TRUE( settings._windowTitle == "Probe Game" );
    SW_EXPECT_TRUE( settings._bUIPauseMenu );
    SW_EXPECT_TRUE( settings._mapUserSettingDefault["gameplay.fieldOfView"] == "80" );
    SW_EXPECT_FALSE( settings._renderPipeline.isSet() );
    SW_EXPECT_FALSE( settings._uiThemes.isSet() );

    // 팩이 적지 않은 UI 규칙은 엔진의 값이다 — 적으면 팩의 값이다.
    sw::PackEngineSettings withThemes;
    SW_ASSERT_TRUE( withThemes.loadFromXMLText( "<GameSettings><Engine _windowTitle=\"T\" _uiThemes=\"engine/ui/uithemes.xml\" /></GameSettings>", "probe" ) );
    SW_EXPECT_TRUE( sw::PackEngineSettings::resolve( settings._uiThemes, withThemes._uiThemes ) == "engine/ui/uithemes.xml" ); // 팩이 없다 → 엔진
    SW_EXPECT_TRUE( sw::PackEngineSettings::resolve( withThemes._uiThemes, settings._uiThemes ) == "engine/ui/uithemes.xml" ); // 팩이 있다 → 엔진 쪽은 보지 않는다

    // 요소마다 읽는 쪽이 하나다 — GameSettings 는 `<Engine>` 을 모르는 원소로 다루지 않는다.
    sw::GameSettings gameSettings;
    SW_ASSERT_TRUE( gameSettings.loadFromXMLText( xml, "probe" ) );
    SW_EXPECT_TRUE( gameSettings._startMap == "game/probe/maps/start.scene.xml" );
}

/**
 * @brief [PackEngineSettingsTest] 요소가 없거나, 창 제목이 빠졌거나, 모르는 칸이 있거나, 경로 칸이 없는 파일을 가리키면 읽기 실패다(대신 쓸 값이 없다)
 */
SW_TEST_CASE( PackEngineSettingsTest, RejectsIncompleteOrUnknownSettings )
{
    SW_TEST_DEFENSIVE_SCOPE( "incomplete or unknown pack settings are rejected" ); // 거절 · 실패 로그가 이 테스트가 보려는 동작이다
    sw::PackEngineSettings settings;
    SW_EXPECT_FALSE( settings.loadFromXMLText( "<GameSettings><startMap>x</startMap></GameSettings>", "no element" ) );
    SW_EXPECT_FALSE( settings.loadFromXMLText( "<GameSettings><Engine _bUIPauseMenu=\"true\" /></GameSettings>", "no title" ) );
    SW_EXPECT_FALSE( settings.loadFromXMLText( "<GameSettings><Engine _windowTitle=\"T\" _windowTitel=\"typo\" /></GameSettings>", "unknown key" ) );
    SW_EXPECT_FALSE( settings.loadFromXMLText( "<Settings><Engine _windowTitle=\"T\" /></Settings>", "wrong root" ) );
#if !defined( SW_SHIPPING )
    SW_EXPECT_FALSE( settings.loadFromXMLText( "<GameSettings><Engine _windowTitle=\"T\" _uiThemes=\"game/probe/data/missing.uithemes.xml\" /></GameSettings>", "missing file" ) );
#endif
}

/**
 * @brief [PackEngineSettingsTest] 모든 게임 프리셋(`Config/Game` 의 JSON)의 팩이 `<Engine>` 요소를 읽힌다 — 창 제목이 있고 경로 칸이 가리키는 파일이 있다
 */
SW_TEST_CASE( PackEngineSettingsTest, EveryGamePackHasReadableEngineSettings )
{
    SW_TEST_REQUIRES_FEATURE( DevelopmentBuild ); // 느슨한 Resource/ 의 모든 팩을 본다 — 배포본은 베이크한 게임의 팩만 싣는다
    const sw::string       presetFolder = sw::FileUtil::joinPath( sw::ResourcePaths::getProjectFolderPath(), "Config/Game" );
    sw::vector<sw::string> listPreset;
    SW_ASSERT_TRUE( sw::FileUtil::collectFiles( presetFolder, ".json", listPreset, false ) );
    SW_ASSERT_TRUE( listPreset.empty() == false );
    for ( const sw::string& presetPath : listPreset )
    {
        sw::GameConfig preset;
        SW_ASSERT_TRUE_MSG( sw::ConfigManager::readConfigFile( preset, presetPath ) == sw::ConfigReadResult::Loaded, presetPath.c_str() );
        sw::PackEngineSettings settings;
        SW_EXPECT_TRUE_MSG( settings.loadFromPack( preset._packRoot ), presetPath.c_str() );
        SW_EXPECT_TRUE_MSG( settings._windowTitle.empty() == false, presetPath.c_str() );
    }
}

/**
 * @brief [PackEngineSettingsTest] 하네스가 활성 팩의 `<Engine>` 요소를 읽어 서비스 표에 둔다(앱과 같은 기동 단계)
 */
SW_TEST_CASE( PackEngineSettingsTest, HarnessBindsTheActivePackSettings )
{
    const sw::PackEngineSettings& settings = sw::engine::getSubsystem<sw::PackEngineSettings>();
    SW_EXPECT_TRUE( settings._windowTitle.empty() == false );
}
