// PackEngineSettings — 팩 gamesettings.xml 의 `<Engine>` 요소(엔진이 읽는 게임별 설정): 칸 읽기 · 엔진 기본과의 해석, 틀린 요소 거절,
// 저장소의 모든 게임 팩이 읽히는지, 기동이 활성 팩의 설정을 서비스 표에 두는지(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Foundation/EngineServices.h"

/**
 * @brief [PackEngineSettingsTest] `<Engine>` 요소의 칸을 읽고, 적지 않은 경로 칸은 "없음" 으로 남으며, 팩이 적지 않은 UI 규칙은 엔진의 값이다
 */
SW_TEST_CASE( PackEngineSettingsTest, ReadsTheEngineElement )
{
    const sw::string       xml = "<GameSettings>"
                                 "<Engine _windowTitle=\"Probe Game\" _bUIPauseMenu=\"true\">"
                                 "<_mapUserSettingDefault><entry key=\"gameplay.fieldOfView\">80</entry></_mapUserSettingDefault>"
                                 "</Engine>"
                                 "<startMap>game/probe/maps/start.scene.xml</startMap>"
                                 "</GameSettings>";
    sw::PackEngineSettings settings;
    SW_REQUIRE_TRUE( settings.loadFromXMLText( xml, "probe" ) );
    SW_EXPECT_EQUAL( sw::string( "Probe Game" ), settings._windowTitle );
    SW_EXPECT_TRUE( settings._bUIPauseMenu );
    SW_EXPECT_EQUAL( sw::string( "80" ), settings._mapUserSettingDefault["gameplay.fieldOfView"] );
    SW_EXPECT_FALSE( settings._renderPipeline.isSet() );
    SW_EXPECT_FALSE( settings._uiThemes.isSet() );

    // 팩이 적지 않은 UI 규칙은 엔진의 값이다 — 적으면 팩의 값이다.
    sw::PackEngineSettings withThemes;
    SW_REQUIRE_TRUE( withThemes.loadFromXMLText( "<GameSettings><Engine _windowTitle=\"T\" _uiThemes=\"engine/ui/uithemes.xml\" /></GameSettings>", "probe" ) );
    SW_EXPECT_EQUAL( sw::string( "engine/ui/uithemes.xml" ), sw::PackEngineSettings::resolve( settings._uiThemes, withThemes._uiThemes ) ); // 팩이 없다 → 엔진
    SW_EXPECT_EQUAL( sw::string( "engine/ui/uithemes.xml" ), sw::PackEngineSettings::resolve( withThemes._uiThemes, settings._uiThemes ) ); // 팩이 있다 → 엔진 쪽은 보지 않는다
}

/**
 * @brief [PackEngineSettingsTest] 요소가 없거나, 창 제목이 빠졌거나, 모르는 칸이 있거나, 경로 칸이 없는 파일을 가리키면 읽기 실패다(대신 쓸 값이 없다)
 */
SW_TEST_CASE( PackEngineSettingsTest, RejectsIncompleteOrUnknownSettings )
{
    sw::PackEngineSettings settings;
    SW_EXPECT_LOG( "PackSettings.ElementMissing" );
    SW_EXPECT_FALSE( settings.loadFromXMLText( "<GameSettings><startMap>x</startMap></GameSettings>", "no element" ) );
    SW_EXPECT_FALSE( settings.loadFromXMLText( "<Settings><Engine _windowTitle=\"T\" /></Settings>", "wrong root" ) );
    SW_EXPECT_LOG( "Config.KeyMissing" );
    SW_EXPECT_FALSE( settings.loadFromXMLText( "<GameSettings><Engine _bUIPauseMenu=\"true\" /></GameSettings>", "no title" ) );
    SW_EXPECT_LOG( "Config.KeyUnknown" );
    SW_EXPECT_FALSE( settings.loadFromXMLText( "<GameSettings><Engine _windowTitle=\"T\" _windowTitel=\"typo\" /></GameSettings>", "unknown key" ) );
    SW_EXPECT_LOG( "PackSettings.FileMissing" );
    SW_EXPECT_FALSE( settings.loadFromXMLText( "<GameSettings><Engine _windowTitle=\"T\" _uiThemes=\"game/probe/data/missing.uithemes.xml\" /></GameSettings>", "missing file" ) );
}

/**
 * @brief [PackEngineSettingsTest] 모든 게임 프리셋(`Config/Game` 의 JSON)의 팩이 `<Engine>` 요소를 읽힌다 — 창 제목이 있고 경로 칸이 가리키는 파일이 있다
 * @details 활성 게임만이 아니라 저장소의 모든 팩을 낱개 파일로 본다(배포본은 베이크한 게임의 팩만 싣고, 테스트를 짓지 않는다).
 */
SW_TEST_CASE( PackEngineSettingsTest, EveryGamePackHasReadableEngineSettings )
{
    const sw::string       presetFolder = sw::FileUtil::joinPath( sw::ProcessRoots::get()._project.str(), "Config/Game" );
    sw::vector<sw::string> listPreset;
    SW_REQUIRE_TRUE( sw::FileUtil::collectFiles( presetFolder, ".json", listPreset, false ) );
    SW_REQUIRE_TRUE( listPreset.empty() == false );
    for ( const sw::string& presetPath : listPreset )
    {
        sw::GameConfig preset;
        SW_REQUIRE_TRUE_MSG( sw::ConfigManager::readConfigFile( preset, presetPath ) == sw::ConfigReadResult::Loaded, presetPath.c_str() );
        sw::PackEngineSettings settings;
        SW_EXPECT_TRUE_MSG( settings.loadFromPack( preset._packRoot ), presetPath.c_str() );
        SW_EXPECT_TRUE_MSG( settings._windowTitle.empty() == false, presetPath.c_str() );
    }
}

/**
 * @brief [PackEngineSettingsTest] 기동이 활성 팩의 `<Engine>` 요소를 읽어 서비스 표에 둔다(앱과 같은 기동 단계)
 */
SW_TEST_CASE( PackEngineSettingsTest, StartupBindsTheActivePackSettings )
{
    const sw::PackEngineSettings& settings = sw::engine::getSubsystem<sw::PackEngineSettings>();
    SW_EXPECT_TRUE( settings._windowTitle.empty() == false );
}
