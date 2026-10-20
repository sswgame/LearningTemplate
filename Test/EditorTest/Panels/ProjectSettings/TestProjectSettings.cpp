#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Panels/ProjectSettings/ProjectSettingsCatalog.h"
#include "Editor/Panels/ProjectSettings/ProjectSettingsDocument.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Reflection/ReflectionTypes.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/ShippingHostDefaults.h"

// ProjectSettingsCatalog · ProjectSettingsDocument — 프로젝트 설정 창의 목록과 저장(ImGui 없음).

namespace
{
    /** @brief `EngineConfig` 처럼 명시 생성자가 없는 타입은 리플렉션 표에 `$ctor` 가 없어 설정 창이 열지 못한다(옛 A: 파서가 기본 생성 가능 타입에 `$ctor` 를 합성하면 풀린다). */
    bool hasReflectedDefaultConstructor( const sw::TypeInfo& type )
    {
        const sw::FunctionInfo* pConstructor = type.findMethod( sw::hashed_string( "$ctor" ) );
        return pConstructor != nullptr && pConstructor->_invoker.isBound() && type._destroyInstance != nullptr;
    }
} // namespace

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
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( sw::FileUtil::joinPath( root, "Config/Game" ) ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( root, "Config/Game/Beta.json" ), "{}" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( root, "Config/Game/Alpha.json" ), "{}" ) );

    sw::vector<sw::editor::ProjectSettingsEntry> listEntry;
    listEntry.push_back( { "GameConfig", "Config/Game/*.json", "GameConfig", "" } );
    listEntry.push_back( { "ServerConfig", "Config/Server/*.json", "ServerConfig", "" } );
    listEntry.push_back( { "EngineConfig", "Config/Engine/EngineConfig.json", "EngineConfig", "" } );
    sw::editor::ProjectSettingsCatalog::expandWildcards( root, listEntry );
    SW_ASSERT_EQUAL( size_t( 3 ), listEntry.size() );
    SW_EXPECT_TRUE( listEntry[0]._page == "GameConfig/Alpha" );
    SW_EXPECT_TRUE( listEntry[0]._relativePath == "Config/Game/Alpha.json" );
    SW_EXPECT_TRUE( listEntry[1]._page == "GameConfig/Beta" );
    SW_EXPECT_TRUE( listEntry[2]._page == "EngineConfig" );
}

/**
 * @brief [ProjectSettingsDocumentTest] 팩 설정 요소(`<Engine>`)는 기본값과 다른 칸만 그 요소에 쓰고, 파일의 다른 요소 · 주석은 그대로이며, 다시 열면 같은 값이다
 */
SW_TEST_CASE( ProjectSettingsDocumentTest, PackElementKeepsTheRestOfTheFile )
{
    const sw::string path     = sw::FileUtil::joinPath( test::makeTempDirectory( "project_settings_pack_element" ), "gamesettings.xml" );
    const sw::string original = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                                "<!-- probe pack -->\n"
                                "<GameSettings>\n"
                                "  <Engine _windowTitle=\"Probe\" />\n"
                                "  <startMap>game/probe/maps/start.scene.xml</startMap>\n"
                                "</GameSettings>\n";
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( path, original ) );

    sw::editor::ProjectSettingsDocument document;
    SW_ASSERT_TRUE( document.open( *sw::PackEngineSettings::StaticType(), path, sw::PackEngineSettings::kElementName ) == sw::ConfigReadResult::Loaded );
    SW_EXPECT_TRUE( document.isElementMode() );
    SW_EXPECT_FALSE( document.isModified() );
    SW_EXPECT_EQUAL( 1u, document.countNonDefaultKeys() ); // 창 제목만 적혀 있다

    sw::PackEngineSettings* pSettings = static_cast<sw::PackEngineSettings*>( document.getInstance() );
    SW_ASSERT_TRUE( pSettings != nullptr );
    SW_EXPECT_TRUE( pSettings->_windowTitle == "Probe" );
    pSettings->_windowTitle                                   = "Probe Two";
    pSettings->_bUIPauseMenu                                  = true;
    pSettings->_mapUserSettingDefault["gameplay.fieldOfView"] = "80";
    SW_EXPECT_TRUE( document.isModified() );
    SW_EXPECT_EQUAL( 3u, document.countNonDefaultKeys() );
    SW_ASSERT_TRUE( document.save() );
    SW_EXPECT_FALSE( document.isModified() );

    sw::string fileText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( path, fileText ) );
    SW_EXPECT_TRUE( fileText.find( "<!-- probe pack -->" ) != sw::string::npos );                                  // 요소 밖 주석은 그대로다
    SW_EXPECT_TRUE( fileText.find( "<startMap>game/probe/maps/start.scene.xml</startMap>" ) != sw::string::npos ); // 다른 요소도 그대로다
    SW_EXPECT_TRUE( fileText.find( "_renderPipeline" ) == sw::string::npos );                                      // 기본값인 칸은 적지 않는다
    SW_EXPECT_TRUE( fileText.find( "Probe Two" ) != sw::string::npos );

    // 엔진의 읽기 창구가 저장한 파일을 그대로 읽는다.
    sw::PackEngineSettings loaded;
    SW_ASSERT_TRUE( loaded.loadFromXMLText( fileText, path ) );
    SW_EXPECT_TRUE( loaded._windowTitle == "Probe Two" );
    SW_EXPECT_TRUE( loaded._bUIPauseMenu );
    SW_EXPECT_TRUE( loaded._mapUserSettingDefault["gameplay.fieldOfView"] == "80" );

    sw::editor::ProjectSettingsDocument reopened;
    SW_ASSERT_TRUE( reopened.open( *sw::PackEngineSettings::StaticType(), path, sw::PackEngineSettings::kElementName ) == sw::ConfigReadResult::Loaded );
    SW_EXPECT_FALSE( reopened.isModified() );

    // 기본값으로 되돌리면 필수 칸(창 제목)이 비어 다음 기동이 읽지 못한다 — 저장하지 않고 파일은 그대로다.
    reopened.resetToDefault();
    SW_EXPECT_FALSE( reopened.save() );
    sw::string unchangedText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( path, unchangedText ) );
    SW_EXPECT_TRUE( unchangedText == fileText );
}

/**
 * @brief [ProjectSettingsDocumentTest] 요소가 없는 팩 파일은 기본값으로 열리고, 저장하면 루트 맨 앞에 요소를 넣는다 · 파일이 없으면 만들지 않는다
 */
SW_TEST_CASE( ProjectSettingsDocumentTest, PackElementIsInsertedWhenMissing )
{
    const sw::string folder = test::makeTempDirectory( "project_settings_pack_insert" );
    const sw::string path   = sw::FileUtil::joinPath( folder, "gamesettings.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( path, "<GameSettings>\r\n  <startMap>game/probe/maps/start.scene.xml</startMap>\r\n</GameSettings>\r\n" ) );

    sw::editor::ProjectSettingsDocument document;
    SW_ASSERT_TRUE( document.open( *sw::PackEngineSettings::StaticType(), path, sw::PackEngineSettings::kElementName ) == sw::ConfigReadResult::Missing );
    static_cast<sw::PackEngineSettings*>( document.getInstance() )->_windowTitle = "Inserted";
    SW_ASSERT_TRUE( document.save() );

    sw::string fileText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( path, fileText ) );
    SW_EXPECT_TRUE( fileText.find( "<GameSettings>\r\n  <Engine _windowTitle=\"Inserted\"" ) != sw::string::npos ); // 파일의 줄 끝 · 들여쓰기를 따른다
    sw::PackEngineSettings loaded;
    SW_ASSERT_TRUE( loaded.loadFromXMLText( fileText, path ) );
    SW_EXPECT_TRUE( loaded._windowTitle == "Inserted" );

    sw::editor::ProjectSettingsDocument noFile;
    const sw::string                    missingPath = sw::FileUtil::joinPath( folder, "missing.xml" );
    (void)sw::FileUtil::removeFile( missingPath ); // 임시 폴더는 실행마다 비워지지 않는다(없어도 괜찮다)
    SW_ASSERT_TRUE( noFile.open( *sw::PackEngineSettings::StaticType(), missingPath, sw::PackEngineSettings::kElementName ) == sw::ConfigReadResult::Missing );
    static_cast<sw::PackEngineSettings*>( noFile.getInstance() )->_windowTitle = "Nowhere";
    SW_EXPECT_FALSE( noFile.save() );
    SW_EXPECT_FALSE( sw::FileUtil::isRegularFile( missingPath ) );
}

/**
 * @brief [ProjectSettingsDocumentTest] 게임 프리셋(JSON)은 팩 루트 하나다 — 기본값과 다른 키만 쓰고, 편집을 버리면 파일 값으로 돌아온다
 */
SW_TEST_CASE( ProjectSettingsDocumentTest, GamePresetHoldsOnlyThePackRoot )
{
    const sw::string path = sw::FileUtil::joinPath( test::makeTempDirectory( "project_settings_document" ), "GameConfig.json" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( path, "{ \"_packRoot\": \"game/probe\" }" ) );

    sw::editor::ProjectSettingsDocument document;
    SW_ASSERT_TRUE( document.open( *sw::GameConfig::StaticType(), path ) == sw::ConfigReadResult::Loaded );
    SW_EXPECT_FALSE( document.isBaseLayer() );
    SW_EXPECT_FALSE( document.isElementMode() );
    SW_EXPECT_EQUAL( 1u, document.countNonDefaultKeys() );
    const sw::GameConfig* pConfig = static_cast<const sw::GameConfig*>( document.getInstance() );
    SW_EXPECT_TRUE( pConfig->_packRoot == "game/probe" );

    // 편집을 버리면 파일 값으로 돌아온다(팩 루트는 데이터만 넣으므로 기본값으로 되돌려 다르게 만든다).
    document.resetToDefault();
    SW_EXPECT_TRUE( document.isModified() );
    SW_EXPECT_TRUE( document.revert() == sw::ConfigReadResult::Loaded );
    SW_EXPECT_FALSE( document.isModified() );
}

/**
 * @brief [ProjectSettingsDocumentTest] 바탕 층(`EngineConfig` — 값이 파일에만 있다)은 모든 키를 쓰고, 기본값으로 되돌리기는 파일 값으로 되돌리기다
 */
SW_TEST_CASE( ProjectSettingsDocumentTest, BaseLayerWritesEveryKey )
{
    SW_TEST_REQUIRES_FEATURE( DevelopmentBuild ); // 바탕 층 표시(`ConfigBaseLayer`)는 속성 메타라 배포본에 없다 — 설정 창도 개발 빌드에만 있다
    if ( hasReflectedDefaultConstructor( *sw::EngineConfig::StaticType() ) == false )
        SW_TEST_SKIP( "EngineConfig has no reflected $ctor, so the settings window cannot open it yet" );
    const sw::string path = sw::FileUtil::joinPath( test::makeTempDirectory( "project_settings_base" ), "EngineConfig.json" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( path, sw::shipping_host::kEngineConfigJSON ) );

    sw::editor::ProjectSettingsDocument document;
    SW_ASSERT_TRUE( document.open( *sw::EngineConfig::StaticType(), path ) == sw::ConfigReadResult::Loaded );
    SW_EXPECT_TRUE( document.isBaseLayer() );
    sw::EngineConfig* pConfig   = static_cast<sw::EngineConfig*>( document.getInstance() );
    const uint32      fileWidth = pConfig->_window._width;
    pConfig->_window._width     = fileWidth + 16u;
    pConfig->_maxFrameDeltaTime = 0.25f;
    SW_EXPECT_TRUE( document.isModified() );
    SW_ASSERT_TRUE( document.save() );

    sw::string fileText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( path, fileText ) );
    for ( const utf8* pKey : { "_width", "_height", "_clearColor", "_maxFrameDeltaTime", "_fixedDeltaTime", "_maxFixedStepPerFrame", "_listResourcePriority" } )
    {
        SW_EXPECT_TRUE_MSG( fileText.find( pKey ) != sw::string::npos, pKey ); // 형제까지 모두 쓴다
    }

    // 되돌리기는 파일 값이다(빈 형식 0 으로 가지 않는다).
    pConfig->_window._width = 1u;
    document.resetToDefault();
    const sw::EngineConfig* pReset = static_cast<const sw::EngineConfig*>( document.getInstance() );
    SW_EXPECT_EQUAL( fileWidth + 16u, pReset->_window._width );
    SW_EXPECT_FALSE( document.isModified() );
}

/**
 * @brief [ProjectSettingsDocumentTest] 저장한 파일을 호스트의 ConfigManager 가 제자리에서 다시 읽는다
 */
SW_TEST_CASE( ProjectSettingsDocumentTest, SavedFileIsReloadedByTheHostConfigManager )
{
    if ( hasReflectedDefaultConstructor( *sw::EngineConfig::StaticType() ) == false )
        SW_TEST_SKIP( "EngineConfig has no reflected $ctor, so the settings window cannot open it yet" );
    const sw::string path = sw::FileUtil::joinPath( test::makeTempDirectory( "project_settings_reload" ), "EngineConfig.json" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( path, sw::shipping_host::kEngineConfigJSON ) );

    sw::ConfigManager manager;
    SW_ASSERT_TRUE( manager.loadConfig<sw::EngineConfig>( path ) );
    sw::EngineConfig* pHostConfig = manager.getConfig<sw::EngineConfig>();
    SW_ASSERT_TRUE( pHostConfig != nullptr );

    sw::editor::ProjectSettingsDocument document;
    SW_ASSERT_TRUE( document.open( *sw::EngineConfig::StaticType(), path ) == sw::ConfigReadResult::Loaded );
    static_cast<sw::EngineConfig*>( document.getInstance() )->_maxFrameDeltaTime = 0.3f;
    SW_ASSERT_TRUE( document.save() );
    SW_ASSERT_TRUE( manager.reloadConfigFile( path ) == sw::ReloadOutcome::Reloaded );
    SW_EXPECT_NEAR_EQUAL( 0.3f, pHostConfig->_maxFrameDeltaTime, 0.0001f ); // 기동 때 받아 둔 포인터가 그대로 유효하다
}
