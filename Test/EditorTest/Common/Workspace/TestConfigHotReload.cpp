#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Workspace/ConfigHotReload.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/ConfigConstants.h"
#include "sw/config/ShippingHostDefaults.h"

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
    SW_EXPECT_TRUE( FileUtil::ensureDirectoryExists( rootDir ) );
    // `EngineConfig` 는 값이 파일에만 있다 — 구성 때 베이크한 바탕 파일 사본에서 한 칸만 바꿔 쓴다.
    JSONDocument document;
    SW_ASSERT_TRUE( document.parse( shipping_host::kEngineConfigJSON ) );
    document.getRoot().set( "_maxFrameDeltaTime", false ).setFloat( 0.25 );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, document.dump() ) );

    ConfigManager manager;
    SW_ASSERT_TRUE( manager.loadConfig<EngineConfig>( path ) );
    document.getRoot().set( "_maxFrameDeltaTime", false ).setFloat( 0.5 );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, document.dump() ) );

    SW_EXPECT_TRUE( ConfigHotReload::reloadChangedFile( &manager, path ) == ReloadOutcome::Reloaded );
    SW_EXPECT_NEAR_EQUAL( 0.5f, manager.getConfig<EngineConfig>()->_maxFrameDeltaTime, 1e-6f );

    SW_EXPECT_TRUE( ConfigHotReload::reloadChangedFile( &manager, FileUtil::joinPath( rootDir, "EditorConfig.json" ) ) == ReloadOutcome::NotTracked );
    SW_EXPECT_TRUE( ConfigHotReload::reloadChangedFile( nullptr, path ) == ReloadOutcome::NotTracked );
    // 맡은 파일이 깨졌으면 Failed 이고 이전 값을 지킨다.
    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, "{ \"_maxFrameDeltaTime\": " ) );
    SW_EXPECT_TRUE( ConfigHotReload::reloadChangedFile( &manager, path ) == ReloadOutcome::Failed );
    SW_EXPECT_NEAR_EQUAL( 0.5f, manager.getConfig<EngineConfig>()->_maxFrameDeltaTime, 1e-6f );

    // 에디터 도구 시드 파일(저장소에 있다)의 "변경" 은 에디터가 들고 있는 값을 파일 값으로 통째로 바꾼다.
    EditorToolDefaults toolDefaults{};
    toolDefaults._listBaseFont = { "stale.ttf" }; // 경로 칸(DataPath)은 데이터 읽기만 채우므로 글꼴 목록으로 "앞 값" 을 둔다
    setEditorToolDefaults( &toolDefaults );
    const string toolDefaultsPath = EditorPaths::resolveProjectRelativePath( config::kFileRuntimeEditorToolDefaults );
    SW_EXPECT_TRUE( ConfigHotReload::reloadChangedFile( nullptr, toolDefaultsPath ) == ReloadOutcome::Reloaded );
    SW_EXPECT_TRUE( toolDefaults._listBaseFont.empty() == false && toolDefaults._listBaseFont.front() != "stale.ttf" );
    setEditorToolDefaults( nullptr );
}
