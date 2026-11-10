#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Content/Pack/PackConfig.h"

#include "GameFramework/Runtime/Foundation/GameData/GamePackDataUtil.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"

// GameSettingsTest — 공용 팩에서 읽는 필수 데이터(<RequiredData kind pack>)와 그 자리 규칙, 엔진이 읽는 `<Engine>` 요소는 건너뛰기(nogpu).

/**
 * @brief [GameSettingsTest] <RequiredData> 는 종류마다 팩을 적고, 자리는 그 팩 안의 등록 줄 규칙이다(공용 팩의 아이템 · 외형)
 */
SW_TEST_CASE( GameSettingsTest, RequiredDataNamesThePackOfAKind )
{
    sw::GameSettings settings;
    SW_REQUIRE_TRUE( settings.loadFromXMLText( R"(<GameSettings>
  <startMap>game/test/maps/start.scene.xml</startMap>
  <defaultLanguage>ko_kr</defaultLanguage>
  <RequiredData kind="items" pack="common"/>
  <RequiredData kind="appearance" pack="common"/>
</GameSettings>)",
                                               "GameSettingsTest" ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), settings._mapRequiredDataPack.size() );
    SW_EXPECT_EQUAL( sw::string( "common" ), settings._mapRequiredDataPack.at( "items" ) );

    sw::string path;
    SW_REQUIRE_TRUE( sw::GamePackDataUtil::tryMakeKindPath( "common", "items", path ) );
    SW_EXPECT_EQUAL( sw::string( "common/data/items.xml" ), path );
    SW_REQUIRE_TRUE( sw::GamePackDataUtil::tryMakeKindPath( "common", "appearance", path ) );
    SW_EXPECT_EQUAL( sw::string( "common/data/appearance" ), path );
}

/**
 * @brief [GameSettingsTest] 같은 파일의 `<Engine>` 요소는 `PackEngineSettings` 의 것이라 `GameSettings` 는 건너뛰고 자기 칸만 읽는다
 */
SW_TEST_CASE( GameSettingsTest, SkipsTheEngineElement )
{
    const sw::string xml = "<GameSettings>"
                           "<Engine _windowTitle=\"Probe Game\" _bUIPauseMenu=\"true\" />"
                           "<startMap>game/probe/maps/start.scene.xml</startMap>"
                           "<defaultLanguage>ko_kr</defaultLanguage>"
                           "</GameSettings>";
    sw::GameSettings gameSettings;
    SW_REQUIRE_TRUE( gameSettings.loadFromXMLText( xml, "probe" ) );
    SW_EXPECT_EQUAL( sw::string( "game/probe/maps/start.scene.xml" ), gameSettings._startMap.get() );

    sw::PackEngineSettings engineSettings;
    SW_REQUIRE_TRUE( engineSettings.loadFromXMLText( xml, "probe" ) );
    SW_EXPECT_EQUAL( sw::string( "Probe Game" ), engineSettings._windowTitle );
}

/**
 * @brief [GameSettingsTest] 팩 쿠킹 규칙의 필수 데이터 자리(`PackConfig::_cookRules._listRequiredData`)는 종류 등록 줄의 규칙(`tryMakeKindPath`)과 같다
 * @details 팩 쿠킹은 게임 모듈을 올리기 전에 돌아 등록 줄을 보지 못해 같은 자리를 데이터로 적는다. 둘이 어긋나면 배포 팩에 필수 데이터가 빠지거나 엉뚱한 것이 든다.
 */
SW_TEST_CASE( GameSettingsTest, CookRequiredDataPathsMatchTheKindRegistry )
{
    sw::PackConfig   config;
    const sw::string path = sw::FileUtil::joinPath( sw::ProcessRoots::get()._project.str(), sw::PackConfig::kConfigPath );
    SW_REQUIRE_TRUE( sw::ConfigManager::readConfigFile( config, path ) == sw::ConfigReadResult::Loaded );
    SW_REQUIRE_TRUE( config._cookRules._listRequiredData.empty() == false );
    for ( const sw::PackRequiredDataPath& entry : config._cookRules._listRequiredData )
    {
        sw::string kindPath;
        SW_REQUIRE_TRUE( sw::GamePackDataUtil::tryMakeKindPath( "common", entry._kind, kindPath ) );
        SW_EXPECT_EQUAL( sw::FileUtil::joinPath( "common", entry._path ), kindPath );
    }
}

/**
 * @brief [GameSettingsTest] 쿠킹의 항목 단위 카탈로그 자리(`_listEntryCatalog`)는 런타임이 읽는 자리다 — 어긋나면 고르기가 그 파일을 카탈로그로 보지 못해 항목을 가리지 못한다
 * @details 런타임의 자리는 둘이다: 종류의 경로 규칙(아이템)과 외형 폴더의 파일 표(`AppearanceDatabase::isDataFile`). 쿠킹은 게임 모듈을 올리기 전에 돌아 둘을 보지 못해 데이터로 적는다.
 */
SW_TEST_CASE( GameSettingsTest, CookEntryCatalogsMatchTheRuntimeFiles )
{
    sw::PackConfig   config;
    const sw::string path = sw::FileUtil::joinPath( sw::ProcessRoots::get()._project.str(), sw::PackConfig::kConfigPath );
    SW_REQUIRE_TRUE( sw::ConfigManager::readConfigFile( config, path ) == sw::ConfigReadResult::Loaded );
    SW_REQUIRE_TRUE( config._cookRules._listEntryCatalog.empty() == false );

    sw::string itemPath;
    sw::string appearanceFolder;
    SW_REQUIRE_TRUE( sw::GamePackDataUtil::tryMakeKindPath( "common", "items", itemPath ) );
    SW_REQUIRE_TRUE( sw::GamePackDataUtil::tryMakeKindPath( "common", "appearance", appearanceFolder ) );
    for ( const sw::PackEntryCatalog& entry : config._cookRules._listEntryCatalog )
    {
        const sw::string      entryPath = sw::FileUtil::joinPath( "common", entry._path );
        const sw::string      prefix    = appearanceFolder + "/";
        const bool            bItems    = entryPath == itemPath;
        const bool            bFolder   = sw::StringUtil::startsWith( entryPath, prefix );
        const sw::string_view file      = bFolder ? sw::string_view( entryPath ).substr( prefix.size() ) : sw::string_view{};
        SW_EXPECT_TRUE_MSG( bItems || ( bFolder && sw::AppearanceDatabase::isDataFile( file ) ), "_listEntryCatalog row '" + entry._kind + "' (" + entry._path + ") is no file the runtime reads" );
    }
}
