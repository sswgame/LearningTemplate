# ==============================================================================
# @file Test/EditorTest/CMakeLists.txt
# @brief Editor 판별·세션 정책 단위 테스트 (UI/ImGui 없음)
# ==============================================================================

# 에디터 · App 시험은 클라이언트 코드다 — 전용 서버 타깃(Server)에는 에디터 · App · 창이 없어 짓지 않는다.
if(SW_TARGET_TYPE STREQUAL "Server")
	sw_declareUnbuiltDirectory("${CMAKE_CURRENT_SOURCE_DIR}")
	return()
endif()

# 시험 소스는 이 폴더를 훑는다(`sw_addTestExecutable`). Editor 소스는 직접 나열해 같이 컴파일한다 — `EditorModule` 은 ImGui 를
# 링크하므로 시험은 그것을 링크하지 않는다(시험은 ImGui 없이 돈다).
#
# **이 목록은 "Editor/Common 전부" 도 "ImGui 를 안 쓰는 것 전부" 도 아니다.** 테스트가 실제로 링크해야
# 하는 것만 적는다. 기계가 대신 고를 수 없어서 목록은 손으로 두되,
# `CheckTestSuites.py` 가 썩는 두 방향을 막는다: 파일이 옮겨져 경로가 죽는 것과, ImGui 를 include 하는
# 파일이 섞여 들어오는 것(EditorTest 는 ImGui 를 링크하지 않으므로 그 순간 빌드가 깨진다).
sw_addTestExecutable(EditorTest
	EXTRA_SOURCES
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Backend/EditorDrawReleaseQueue.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorAssetFileCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorCommandRegistry.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorDataTableCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorExternalToolJob.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorReferenceIndex.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorResourceIndex.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorGlobalVariableCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorLogCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorModuleOverrides.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorMultiEdit.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorSceneCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorShortcutOverrides.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorViewportPick.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorViewportPreview.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorTransformCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorToolAssetCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorTracyLauncher.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorInspectorCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/SequenceTimingUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/GUI/EditorComponentIcon.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorToolDefaults.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorConfig.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorSettingsRegistry.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorPlaySession.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/EditorUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Widgets/EditorCurveView.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Widgets/EditorLabelLayout.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Widgets/EditorListFilter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Widgets/EditorNodeGraphRules.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/ConsoleLogRows.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/ContentBrowserLogic.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorViewportOverlayLayout.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Inspector/InspectorBuiltinValue.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Inspector/InspectorComponentManager.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Inspector/InspectorPropertyLayout.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/MapCheckRows.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/MaterialPreviewShading.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/PackagingProgressParser.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/ProfilerCallTree.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/ProfilerScopeHistory.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/ProfilerTimelineLayout.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/EditorTestOutputParser.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/TileMapPaintUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Popups/QuickLauncherLayout.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/UIPreviewLogic.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/AssetHotReload.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/ConfigHotReload.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorAssetType.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/AssetActions/EditorAssetTypeActions.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorRegistry.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorSceneGenerationSync.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorContext.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorDefaultObjects.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorLayoutStore.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorService.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorTransaction.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorWindowTitle.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorWorkspace.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/FileWatchDispatcher.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorSelection.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/AssetImportPathFilter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/AssetImportStamp.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/EditorAssetValidation.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/HeightfieldImporter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/ImageUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/ModelImportConfig.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/ModelImporter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/TextureImportConfig.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/TextureImporter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/VrmMaterialImporter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Localization/LocalizationTools.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/SourceControl/EditorSourceControl.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/SourceControl/SourceControlProvider.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorGridUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorViewTargetUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorVisualizerGeometry.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorVisualizerToggles.cpp"
	LIBS
		Engine
		Microsoft::DirectXTex
		cgltf
		meshoptimizer
	LABELS
		"unit;editor;nogpu"
	# 케이스 하나(ModelImporterTest.SkinnedModelImportsSkeletonClipsAndAttachments — VRM 스킨 모델을 Debug 로 임포트)가 혼자 31 초다
	# (부하 걸린 이 PC). 조각으로 나눠도 그 케이스는 한 조각에 남으므로 한도를 올린다. 실행 전체는 직접 40 초 안팎.
	TIMEOUT
		120
)

target_include_directories(EditorTest PRIVATE "${CMAKE_SOURCE_DIR}/Source")

# EditorConfig·EditorToolDefaults 는 REFLECT 타입이라 StaticType() 이 코드젠에서 나온다. EditorModule 을
# 링크하지 않는 테스트도 그 심볼이 필요하므로 이 헤더들만 따로 코드젠한다.
sw_addReflectionStep(EditorTest
	HEADERS
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorConfig.h"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorPreferences.h"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorToolDefaults.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/EditorPreviewProbe.h"
	INCLUDES
		"${CMAKE_SOURCE_DIR}/Source"
)
