#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Panels/ProjectSettings/ProjectSettingsCatalog.h"
#include "Editor/Panels/ProjectSettings/ProjectSettingsDocument.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Reflection/ReflectionTypes.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/ShippingHostDefaults.h"

// ProjectSettingsCatalog · ProjectSettingsDocument — 프로젝트 설정 창의 목록과 저장(ImGui 없음).

namespace
{
    /** @brief `EngineConfig` 처럼 명시 생성자가 없는 타입은 리플렉션 표에 `$ctor` 가 없어 설정 창이 열지 못한다(백로그 A: 파서가 기본 생성 가능 타입에 `$ctor` 를 합성하면 풀린다). */
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
 * @brief [ProjectSettingsDocumentTest] 파일에는 기본값과 다른 키만 쓰고, 기본값으로 되돌리면 키가 사라지며, 다시 열면 같은 값이다(게임 프리셋 층)
 */
SW_TEST_CASE( ProjectSettingsDocumentTest, SavesOnlyValuesThatDifferFromTheDefaults )
{
    if ( hasReflectedDefaultConstructor( *sw::GameConfig::StaticType() ) == false )
        SW_TEST_SKIP( "GameConfig has no reflected $ctor, so the settings window cannot open it yet" );
    const sw::string path = sw::FileUtil::joinPath( test::makeTempDirectory( "project_settings_document" ), "GameConfig.json" );
    (void)sw::FileUtil::removeFile( path ); // 임시 폴더는 실행마다 비워지지 않는다 — 이전 실행이 남긴 파일이 없는 것이 이 테스트의 출발점이다(없어도 괜찮다)

    sw::editor::ProjectSettingsDocument document;
    SW_ASSERT_TRUE( document.open( *sw::GameConfig::StaticType(), path ) == sw::ConfigReadResult::Missing );
    SW_EXPECT_FALSE( document.isBaseLayer() );
    SW_EXPECT_FALSE( document.hasFile() );
    SW_EXPECT_FALSE( document.isModified() );
    SW_EXPECT_EQUAL( 0u, document.countNonDefaultKeys() );

    sw::GameConfig* pConfig = static_cast<sw::GameConfig*>( document.getInstance() );
    SW_ASSERT_TRUE( pConfig != nullptr );
    pConfig->_windowTitle  = "Probe";
    pConfig->_bUIPauseMenu = true;
    SW_EXPECT_TRUE( document.isModified() );
    SW_EXPECT_EQUAL( 2u, document.countNonDefaultKeys() );
    SW_ASSERT_TRUE( document.save() );
    SW_EXPECT_FALSE( document.isModified() );
    SW_EXPECT_TRUE( document.hasFile() );

    sw::string fileText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( path, fileText ) );
    SW_EXPECT_TRUE( fileText.find( "_windowTitle" ) != sw::string::npos );
    SW_EXPECT_TRUE( fileText.find( "_bUIPauseMenu" ) != sw::string::npos );
    SW_EXPECT_TRUE( fileText.find( "_packRoot" ) == sw::string::npos ); // 기본값인 형제는 적지 않는다

    sw::editor::ProjectSettingsDocument reopened;
    SW_ASSERT_TRUE( reopened.open( *sw::GameConfig::StaticType(), path ) == sw::ConfigReadResult::Loaded );
    const sw::GameConfig* pReopened = static_cast<const sw::GameConfig*>( reopened.getInstance() );
    SW_EXPECT_TRUE( pReopened->_windowTitle == "Probe" );
    SW_EXPECT_FALSE( reopened.isModified() );

    // 기본값으로 되돌리면 키가 모두 사라진다 — 파일에 옛 값이 남아 코드의 기본값을 가리지 않게.
    reopened.resetToDefault();
    SW_EXPECT_TRUE( reopened.isModified() );
    SW_EXPECT_EQUAL( 0u, reopened.countNonDefaultKeys() );
    SW_ASSERT_TRUE( reopened.save() );
    sw::string clearedText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( path, clearedText ) );
    SW_EXPECT_TRUE( clearedText.find( "_windowTitle" ) == sw::string::npos );

    // 편집을 버리면 파일 값으로 돌아온다.
    sw::GameConfig* pEdited = static_cast<sw::GameConfig*>( reopened.getInstance() );
    pEdited->_packRoot      = "game/probe";
    SW_EXPECT_TRUE( reopened.isModified() );
    SW_EXPECT_TRUE( reopened.revert() == sw::ConfigReadResult::Loaded );
    SW_EXPECT_FALSE( reopened.isModified() );
}

/**
 * @brief [ProjectSettingsDocumentTest] 바탕 층(`EngineConfig` — 값이 파일에만 있다)은 모든 키를 쓰고, 기본값으로 되돌리기는 파일 값으로 되돌리기다
 */
SW_TEST_CASE( ProjectSettingsDocumentTest, BaseLayerWritesEveryKey )
{
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
    SW_ASSERT_TRUE( manager.reloadConfigFile( path ) );
    SW_EXPECT_NEAR_EQUAL( 0.3f, pHostConfig->_maxFrameDeltaTime, 0.0001f ); // 기동 때 받아 둔 포인터가 그대로 유효하다
}
