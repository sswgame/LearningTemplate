# ==============================================================================
# @file Test/EditorTest/CMakeLists.txt
# @brief Editor 판별·세션 정책 단위 테스트 (UI/ImGui 없음)
# ==============================================================================

# 에디터 · App 테스트는 클라이언트 코드다 — 전용 서버 타깃(Server)에는 에디터 · App · 창이 없어 짓지 않는다.
if(SW_TARGET_TYPE STREQUAL "Server")
	sw_declareUnbuiltDirectory("${CMAKE_CURRENT_SOURCE_DIR}")
	return()
endif()

# 테스트 소스는 이 폴더를 훑는다(`sw_addTestExecutable`). `EditorModule` 은 ImGui 를 링크하므로 테스트는 그것을 링크하지 않는다(테스트는 ImGui 없이 돈다).
#
# ImGui 를 모르는 층(Editor/Common 의 Asset · Commands · Config · Localization · SourceControl · Workspace)은 `EditorCore`(Source/Editor/CMakeLists.txt)가
# 지은 오브젝트를 `sw_linkEditorCore` 로 EditorModule 과 **같은 것**으로 받는다 — 그 폴더의 목록은 여기 없다(폴더에 파일을 더하면 자동으로 들어온다).
# 코어 층의 REFLECT 헤더의 코드젠도 같은 곳(`EditorCoreReflect`)에서 받는다.
#
# 아래 EXTRA_SOURCES 는 코어 밖(Common 바로 아래 · Backend · GUI · Widgets · Viewport · Panels · Popups · SelfTest)에서 **테스트가 실제로 링크해야
# 하는 것만** 적은 손 목록이다(2b 가 Panels · Viewport 의 헤드리스 파일을 `Model` 폴더로 옮기면 이 목록도 코어로 간다).
# `CheckTestSuites.py` 가 썩는 두 방향을 막는다: 파일이 옮겨져 경로가 죽는 것과, ImGui 를 include 하는
# 파일이 섞여 들어오는 것(EditorTest 는 ImGui 를 링크하지 않으므로 그 순간 빌드가 깨진다).
sw_addTestExecutable(EditorTest
	EXTRA_SOURCES
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Backend/DrawSnapshotHandoff.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Backend/EditorDrawReleaseQueue.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorColliderHandleMath.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorViewportMouseClaim.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Widgets/EditorAssetFilter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/EditorBackgroundTask.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/EditorExternalToolJob.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorViewportRayPick.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorPreviewPick.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Animation/SequenceTimingUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/GUI/EditorComponentIcon.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/GUI/EditorDocumentPanelCore.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/EditorPaths.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/EditorUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Widgets/EditorCurveView.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Widgets/EditorLabelLayout.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Widgets/EditorListFilter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Widgets/EditorNodeGraphRules.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Catalog/CatalogXMLText.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Catalog/CatalogDocument.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Catalog/CatalogCurveModel.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Console/ConsoleLogRows.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/ContentBrowser/ContentBrowserFileActions.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/ContentBrowser/ContentBrowserImportQueue.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/ContentBrowser/ContentBrowserLogic.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorViewportOverlayLayout.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Inspector/InspectorBuiltinValue.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Inspector/InspectorFavorites.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Inspector/InspectorComponentManager.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Inspector/InspectorComponentPicker.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Inspector/InspectorPropertyLayout.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/InputMap/InputMapConflicts.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/AudioEvents/AudioEventBrowserUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/AudioGraph/AudioGraphEditUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/MapCheck/MapCheckRows.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Material/MaterialPreviewShading.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Packaging/PackagingProgressParser.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Profiler/ProfilerCallTree.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Profiler/ProfilerScopeHistory.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Profiler/ProfilerTimelineLayout.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/ProjectSettings/ProjectSettingsCatalog.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/ProjectSettings/ProjectSettingsDocument.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/RenderTarget/RenderTargetViewUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/SelfTest/EditorTestOutputParser.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/TileMap/TileMapPaintUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Popups/QuickLauncherLayout.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/UIPreview/UIPreviewLogic.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorGridUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorVisualizerGeometry.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorVisualizerToggles.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/TerrainBrush.cpp"
	LIBS
		Engine
		Microsoft::DirectXTex
		cgltf
		meshoptimizer
		stb
)

target_include_directories(EditorTest PRIVATE "${CMAKE_SOURCE_DIR}/Source")

# EditorCore 의 오브젝트(와 FlagOps 우산 · gen 폴더). 코어 오브젝트는 RuntimeAPI 를 쓰므로 직접 링크한다.
sw_linkEditorCore(EditorTest PRIVATE)
target_link_libraries(EditorTest PRIVATE RuntimeAPI)

# 멀티 프로세스 재생 테스트(EditorNetPlaySessionTest)은 빌드한 Server 를 서버 · 헤드리스 클라이언트로 띄운다 — 테스트보다 먼저 지어 둔다.
if(TARGET Server)
	add_dependencies(EditorTest Server)
endif()
get_property(swActiveGameTarget GLOBAL PROPERTY SW_ACTIVE_GAME_TARGET)
if(swActiveGameTarget AND TARGET ${swActiveGameTarget})
	add_dependencies(EditorTest ${swActiveGameTarget})
endif()

# 코어 밖의 REFLECT 헤더(뷰포트 겹침 배치 · 테스트 프로브)만 여기서 코드젠한다. 코어 층의 헤더는 EditorCoreReflect 가 만든다.
sw_addReflectionStep(EditorTest
	HEADERS
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorViewportOverlayLayout.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/EditorPreviewProbe.h"
	INCLUDES
		"${CMAKE_SOURCE_DIR}/Source"
)
