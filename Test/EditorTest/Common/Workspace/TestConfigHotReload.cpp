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

    // 에디터 도구 시드는 저장소의 그 파일이다. 시험이 묶은 사본에 다시 읽는다.
    EditorToolDefaults toolDefaults{};
    toolDefaults._ideOpenCommand = "stale";
    setEditorToolDefaults( &toolDefaults );
    const string toolDefaultsPath = EditorUtil::resolveProjectRelativePath( config::kFileRuntimeEditorToolDefaults );
    SW_EXPECT_TRUE( ConfigHotReload::reloadChangedFile( nullptr, toolDefaultsPath ) );
    SW_EXPECT_TRUE_MSG( toolDefaults._ideOpenCommand != "stale", "editortooldefaults.json 을 다시 읽지 않았습니다" );
    setEditorToolDefaults( nullptr );
}
