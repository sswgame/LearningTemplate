#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Panels/ProjectSettings/ProjectSettingsCatalog.h"
#include "Editor/Panels/ProjectSettings/ProjectSettingsDocument.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"

#include "TestFramework/TestFramework.h"

// ProjectSettingsCatalog · ProjectSettingsDocument — 프로젝트 설정 창의 목록과 저장(ImGui 없음).

/**
 * @brief [ProjectSettingsCatalogTest] 색인에서 Config/ · Resource/ 아래 JSON 설정만 고르고, XML · 매니페스트 · 타입 없는 항목은 뺀다
 */
SW_TEST_CASE( ProjectSettingsCatalogTest, PicksOnlyEditableJSONFiles )
{
    const sw::string                             referenceJSON = R"({"files":[
        {"page":"EngineConfig","path":"Config/Engine/EngineConfig.json","type":"EngineConfig"},
        {"page":"PhysicsSettings","path":"Resource/engine/physics/physicssettings.xml","type":"PhysicsSettings"},
        {"page":"ModuleManifest","path":"Source/*.module.json","type":"ModuleManifestFile"},
        {"page":"TextureImportConfig","path":"Config/Editor/TextureImportConfig.json","type":""},
        {"page":"GameConfig","path":"Config/Game/*.json","type":"GameConfig"},
        {"page":"LipSyncSettings","path":"Resource/engine/animation/lipsync.json","type":"LipSyncSettings"}]})";
    sw::vector<sw::editor::ProjectSettingsEntry> listEntry;
    SW_ASSERT_TRUE( sw::editor::ProjectSettingsCatalog::parseReference( referenceJSON, listEntry ) );
    SW_ASSERT_EQUAL( size_t( 3 ), listEntry.size() );
    SW_EXPECT_TRUE( listEntry[0]._page == "EngineConfig" );
    SW_EXPECT_TRUE( listEntry[1]._page == "GameConfig" );
    SW_EXPECT_TRUE( listEntry[2]._relativePath == "Resource/engine/animation/lipsync.json" );
    SW_EXPECT_FALSE( sw::editor::ProjectSettingsCatalog::parseReference( "not json", listEntry ) );
}

/**
 * @brief [ProjectSettingsCatalogTest] 와일드카드는 파일마다 항목 하나로 풀리고, 파일이 없으면 사라진다
 */
SW_TEST_CASE( ProjectSettingsCatalogTest, ExpandsWildcardsToFiles )
{
    const sw::string root = test::makeTempDirectory( "project_settings_wildcard" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( root, "Config/Game/Beta.json" ), "{}" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( root, "Config/Game/Alpha.json" ), "{}" ) );

    sw::vector<sw::editor::ProjectSettingsEntry> listEntry;
    listEntry.push_back( { "GameConfig", "Config/Game/*.json", "GameConfig" } );
    listEntry.push_back( { "ServerConfig", "Config/Server/*.json", "ServerConfig" } );
    listEntry.push_back( { "EngineConfig", "Config/Engine/EngineConfig.json", "EngineConfig" } );
    sw::editor::ProjectSettingsCatalog::expandWildcards( root, listEntry );
    SW_ASSERT_EQUAL( size_t( 3 ), listEntry.size() );
    SW_EXPECT_TRUE( listEntry[0]._page == "GameConfig/Alpha" );
    SW_EXPECT_TRUE( listEntry[0]._relativePath == "Config/Game/Alpha.json" );
    SW_EXPECT_TRUE( listEntry[1]._page == "GameConfig/Beta" );
    SW_EXPECT_TRUE( listEntry[2]._page == "EngineConfig" );
}

/**
 * @brief [ProjectSettingsDocumentTest] 파일에는 기본값과 다른 키만 쓰고, 기본값으로 되돌리면 키가 사라지며, 다시 열면 같은 값이다
 */
SW_TEST_CASE( ProjectSettingsDocumentTest, SavesOnlyValuesThatDifferFromTheDefaults )
{
    const sw::string path = sw::FileUtil::joinPath( test::makeTempDirectory( "project_settings_document" ), "EngineConfig.json" );

    sw::editor::ProjectSettingsDocument document;
    SW_ASSERT_TRUE( document.open( *sw::EngineConfig::StaticType(), path ) == sw::ConfigReadResult::Missing );
    SW_EXPECT_FALSE( document.hasFile() );
    SW_EXPECT_FALSE( document.isModified() );
    SW_EXPECT_EQUAL( 0u, document.countNonDefaultKeys() );

    sw::EngineConfig* pConfig = static_cast<sw::EngineConfig*>( document.getInstance() );
    SW_ASSERT_TRUE( pConfig != nullptr );
    pConfig->_maxFrameDeltaTime = 0.25f;
    pConfig->_window._width     = 800; // 구조체 칸 안의 값 — 키는 `_window` 아래에 중첩된다
    SW_EXPECT_TRUE( document.isModified() );
    SW_EXPECT_EQUAL( 2u, document.countNonDefaultKeys() );
    SW_ASSERT_TRUE( document.save() );
    SW_EXPECT_FALSE( document.isModified() );
    SW_EXPECT_TRUE( document.hasFile() );

    sw::string fileText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( path, fileText ) );
    SW_EXPECT_TRUE( fileText.find( "_maxFrameDeltaTime" ) != sw::string::npos );
    SW_EXPECT_TRUE( fileText.find( "_width" ) != sw::string::npos );
    SW_EXPECT_TRUE( fileText.find( "_height" ) == sw::string::npos ); // 기본값인 형제는 적지 않는다
    SW_EXPECT_TRUE( fileText.find( "_fixedDeltaTime" ) == sw::string::npos );

    sw::editor::ProjectSettingsDocument reopened;
    SW_ASSERT_TRUE( reopened.open( *sw::EngineConfig::StaticType(), path ) == sw::ConfigReadResult::Loaded );
    const sw::EngineConfig* pReopened = static_cast<const sw::EngineConfig*>( reopened.getInstance() );
    SW_EXPECT_NEAR_EQUAL( 0.25f, pReopened->_maxFrameDeltaTime, 0.0001f );
    SW_EXPECT_EQUAL( 800u, pReopened->_window._width );
    SW_EXPECT_FALSE( reopened.isModified() );

    // 기본값으로 되돌리면 키가 모두 사라진다 — 파일에 옛 값이 남아 코드의 기본값을 가리지 않게.
    reopened.resetToDefault();
    SW_EXPECT_TRUE( reopened.isModified() );
    SW_EXPECT_EQUAL( 0u, reopened.countNonDefaultKeys() );
    SW_ASSERT_TRUE( reopened.save() );
    sw::string clearedText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( path, clearedText ) );
    SW_EXPECT_TRUE( clearedText.find( "_maxFrameDeltaTime" ) == sw::string::npos );

    // 편집을 버리면 파일 값으로 돌아온다.
    sw::EngineConfig* pEdited      = static_cast<sw::EngineConfig*>( reopened.getInstance() );
    pEdited->_maxFixedStepPerFrame = 3;
    SW_EXPECT_TRUE( reopened.isModified() );
    SW_EXPECT_TRUE( reopened.revert() == sw::ConfigReadResult::Loaded );
    SW_EXPECT_FALSE( reopened.isModified() );
}

/**
 * @brief [ProjectSettingsDocumentTest] 저장한 파일을 호스트의 ConfigManager 가 제자리에서 다시 읽는다
 */
SW_TEST_CASE( ProjectSettingsDocumentTest, SavedFileIsReloadedByTheHostConfigManager )
{
    const sw::string path = sw::FileUtil::joinPath( test::makeTempDirectory( "project_settings_reload" ), "EngineConfig.json" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( path, "{}" ) );

    sw::ConfigManager manager;
    SW_ASSERT_TRUE( manager.loadConfig<sw::EngineConfig>( path ) );
    sw::EngineConfig* pHostConfig = manager.getConfig<sw::EngineConfig>();
    SW_ASSERT_TRUE( pHostConfig != nullptr );
    SW_EXPECT_NEAR_EQUAL( 0.1f, pHostConfig->_maxFrameDeltaTime, 0.0001f );

    sw::editor::ProjectSettingsDocument document;
    SW_ASSERT_TRUE( document.open( *sw::EngineConfig::StaticType(), path ) == sw::ConfigReadResult::Loaded );
    static_cast<sw::EngineConfig*>( document.getInstance() )->_maxFrameDeltaTime = 0.3f;
    SW_ASSERT_TRUE( document.save() );
    SW_ASSERT_TRUE( manager.reloadConfigFile( path ) );
    SW_EXPECT_NEAR_EQUAL( 0.3f, pHostConfig->_maxFrameDeltaTime, 0.0001f ); // 기동 때 받아 둔 포인터가 그대로 유효하다
}
