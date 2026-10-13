# ==============================================================================
# @file Test/EditorTest/CMakeLists.txt
# @brief Editor 판별·세션 정책 단위 테스트 (UI/ImGui 없음)
# ==============================================================================

# 에디터 · App 테스트는 클라이언트 코드다 — 전용 서버 타깃(Server)에는 에디터 · App · 창이 없어 짓지 않는다.
if(SW_TARGET_TYPE STREQUAL "Server")
	sw_declareUnbuiltDirectory("${CMAKE_CURRENT_SOURCE_DIR}")
	return()
endif()

# 테스트 소스는 이 폴더를 훑는다(`sw_addTestExecutable`). Editor 소스는 직접 나열해 같이 컴파일한다 — `EditorModule` 은 ImGui 를
# 링크하므로 테스트는 그것을 링크하지 않는다(테스트는 ImGui 없이 돈다).
#
# **이 목록은 "Editor/Common 전부" 도 "ImGui 를 안 쓰는 것 전부" 도 아니다.** 테스트가 실제로 링크해야
# 하는 것만 적는다. 기계가 대신 고를 수 없어서 목록은 손으로 두되,
# `CheckTestSuites.py` 가 썩는 두 방향을 막는다: 파일이 옮겨져 경로가 죽는 것과, ImGui 를 include 하는
# 파일이 섞여 들어오는 것(EditorTest 는 ImGui 를 링크하지 않으므로 그 순간 빌드가 깨진다).
sw_addTestExecutable(EditorTest
	EXTRA_SOURCES
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Backend/DrawSnapshotHandoff.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Backend/EditorDrawReleaseQueue.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorAssetFileCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorColliderHandleMath.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorCommandRegistry.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorDataTableCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/EditorExternalToolJob.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorFolderListing.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorReferenceIndex.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorResourceIndex.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorGlobalVariableCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorLogCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorModuleOverrides.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorNavigationCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorNetConditions.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorNetPlaySession.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorNetProfilerHistory.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorMultiEdit.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorSceneCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorShortcutOverrides.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorSiblingOrder.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorViewportRayPick.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorViewportPreview.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorTransformCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorToolAssetCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorTracyLauncher.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorInspectorCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Animation/SequenceTimingUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/GUI/EditorComponentIcon.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorToolDefaults.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorConfig.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorSettingsRegistry.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorPlaySession.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/EditorPaths.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/EditorUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Widgets/EditorCurveView.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Widgets/EditorLabelLayout.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Widgets/EditorListFilter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Widgets/EditorNodeGraphRules.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Catalog/CatalogXMLText.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Catalog/CatalogDocument.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Console/ConsoleLogRows.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/ContentBrowser/ContentBrowserFileActions.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/ContentBrowser/ContentBrowserImportQueue.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/ContentBrowser/ContentBrowserLogic.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorViewportOverlayLayout.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Inspector/InspectorBuiltinValue.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Inspector/InspectorComponentManager.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Inspector/InspectorPropertyLayout.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/InputMap/InputMapConflicts.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/MapCheck/MapCheckRows.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Material/MaterialPreviewShading.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Packaging/PackagingProgressParser.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Profiler/ProfilerCallTree.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Profiler/ProfilerScopeHistory.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Profiler/ProfilerTimelineLayout.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/AudioMixer/AudioDataHotReload.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/ProjectSettings/ProjectSettingsCatalog.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/ProjectSettings/ProjectSettingsDocument.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/RenderTarget/RenderTargetViewUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/SelfTest/EditorTestOutputParser.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/TileMap/TileMapPaintUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Popups/QuickLauncherLayout.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/UIPreview/UIPreviewLogic.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/AssetHotReload.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/ConfigHotReload.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorAssetType.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorAssetTypeActions.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorExtensionRegistry.cpp"
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
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/ModelImportAnimation.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/ModelImportConfig.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/ModelImportGeometry.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/ModelImportMaterial.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/ModelImportVrm.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/ModelImporter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/TextureImportConfig.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/TextureImporter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/VrmAvatarImporter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/VrmMaterialImporter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Localization/LocalizationTools.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/SourceControl/EditorSourceControl.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/SourceControl/SourceControlProvider.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorGridUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorViewTargetUtil.cpp"
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

# 멀티 프로세스 재생 테스트(EditorNetPlaySessionTest)은 빌드한 Server 를 서버 · 헤드리스 클라이언트로 띄운다 — 테스트보다 먼저 지어 둔다.
if(TARGET Server)
	add_dependencies(EditorTest Server)
endif()
get_property(swActiveGameTarget GLOBAL PROPERTY SW_ACTIVE_GAME_TARGET)
if(swActiveGameTarget AND TARGET ${swActiveGameTarget})
	add_dependencies(EditorTest ${swActiveGameTarget})
endif()

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
