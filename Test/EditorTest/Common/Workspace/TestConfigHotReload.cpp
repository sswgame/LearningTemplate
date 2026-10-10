#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/ConfigHotReload.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/ConfigConstants.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [ConfigHotReloadTest] 바뀐 파일이 호스트 설정이면 ConfigManager 가, 에디터 도구 시드면 에디터가 다시 읽고, 모르는 파일은 넘긴다
 * @details 감시는 `Config/` 의 모든 `.json` 을 받는다 — 앱이 다시 쓰는 `EditorConfig.json` 이나 임포트 설정이 와도 아무 일도 하지 않아야 한다.
 */
SW_TEST_CASE( ConfigHotReloadTest, ChangedFileGoesToItsOwner )
{
    test::ScopedLogSuppressor suppressor;

    const string rootDir = test::makeTempPath( "sw_config_hot_reload" );
    const string path    = FileUtil::joinPath( rootDir, "HotEngineConfig.json" );
    FileUtil::ensureDirectoryExists( rootDir );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, "{ \"_maxFrameDeltaTime\": 0.25 }" ) );

    ConfigManager manager;
    SW_ASSERT_TRUE( manager.loadConfig<EngineConfig>( path ) );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, "{ \"_maxFrameDeltaTime\": 0.5 }" ) );

    SW_EXPECT_TRUE( ConfigHotReload::reloadChangedFile( &manager, path ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, manager.getConfig<EngineConfig>()->_maxFrameDeltaTime, 1e-6f );

    SW_EXPECT_FALSE( ConfigHotReload::reloadChangedFile( &manager, FileUtil::joinPath( rootDir, "EditorConfig.json" ) ) );
    SW_EXPECT_FALSE( ConfigHotReload::reloadChangedFile( nullptr, path ) );

    // 에디터 도구 시드 파일은 기본값과 다른 값이 있을 때만 생긴다. 저장소에는 없으니 그 경로의 "변경" 은 아무것도 바꾸지 않는다.
    EditorToolDefaults toolDefaults{};
    toolDefaults._defaultMap = "stale";
    setEditorToolDefaults( &toolDefaults );
    const string toolDefaultsPath = EditorUtil::resolveProjectRelativePath( config::kFileRuntimeEditorToolDefaults );
    if ( FileUtil::exists( toolDefaultsPath ) == false )
    {
        SW_EXPECT_FALSE( ConfigHotReload::reloadChangedFile( nullptr, toolDefaultsPath ) );
        SW_EXPECT_EQUAL( string( "stale" ), toolDefaults._defaultMap );
    }
    setEditorToolDefaults( nullptr );

    // 파일이 있으면 그 값만 덮고 나머지는 기본값이다.
    const string localToolDefaults = FileUtil::joinPath( rootDir, "editortooldefaults.json" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( localToolDefaults, "{ \"_defaultMap\": \"game/empty/maps/probe.tilemap.json\" }" ) );
    EditorToolDefaults loaded{};
    SW_EXPECT_TRUE( loaded.loadFromHostPath( localToolDefaults ) );
    SW_EXPECT_EQUAL( string( "game/empty/maps/probe.tilemap.json" ), loaded._defaultMap );
    SW_EXPECT_NEAR_EQUAL( EditorToolDefaults{}._fontSize, loaded._fontSize, 1e-6f );
}
