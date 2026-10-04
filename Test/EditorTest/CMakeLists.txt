# ==============================================================================
# @file Test/EditorTest/CMakeLists.txt
# @brief Editor 판별·세션 정책 단위 테스트 (UI/ImGui 없음)
# ==============================================================================

# Editor 소스를 직접 나열해 같이 컴파일한다 — `EditorModule` 은 MODULE DLL 이라 링크할 수 없다.
#
# **이 목록은 "Editor/Common 전부" 도 "ImGui 를 안 쓰는 것 전부" 도 아니다.** 테스트가 실제로 링크해야
# 하는 것만 적는다. 기계가 대신 고를 수 없어서 목록은 손으로 두되,
# `CheckTestSuites.py` 가 썩는 두 방향을 막는다: 파일이 옮겨져 경로가 죽는 것과, ImGui 를 include 하는
# 파일이 섞여 들어오는 것(EditorTest 는 ImGui 를 링크하지 않으므로 그 순간 빌드가 깨진다).
sw_addTestExecutable(EditorTest
	SOURCES
		TestAssetHotReload.cpp
		TestEditorAssetType.cpp
		TestEditorAssetValidation.cpp
		TestEditorAssetTypeActions.cpp
		TestEditorBackgroundTask.cpp
		TestEditorCommandRegistry.cpp
		TestEditorDataTableCommands.cpp
		TestEditorDrawReleaseQueue.cpp
		TestEditorGlobalVariableCommands.cpp
		TestEditorListFilter.cpp
		TestEditorPanelDocument.cpp
		TestEditorRegistry.cpp
		TestEditorPlaySession.cpp
		TestEditorResourceIndex.cpp
		TestEditorSceneCommands.cpp
		TestEditorSessionPolicy.cpp
		TestEditorSourceControl.cpp
		TestEditorTransaction.cpp
		TestEditorTransformCommands.cpp
		TestEditorToolAssetCommands.cpp
		TestEditorViewportMath.cpp
		TestEditorWorkspace.cpp
		TestInspectorBuiltinValue.cpp
		TestInspectorPropertyLayout.cpp
		TestEditorViewportPick.cpp
		TestEditorViewportPreview.cpp
		TestEditorVisualizerGeometry.cpp
		TestFileWatchDispatcher.cpp
		TestEditorSelection.cpp
		TestModelImporter.cpp
		TestTexturePipeline.cpp
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Backend/EditorDrawReleaseQueue.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorCommandRegistry.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorDataTableCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorExternalToolJob.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorResourceIndex.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorGlobalVariableCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorSceneCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorViewportPick.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorViewportPreview.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorTransformCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorToolAssetCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorInspectorCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorToolDefaults.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorConfig.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorPlaySession.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/EditorUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Widgets/EditorListFilter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Inspector/InspectorBuiltinValue.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Panels/Inspector/InspectorPropertyLayout.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/AssetHotReload.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorAssetType.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/AssetActions/EditorAssetTypeActions.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorRegistry.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorContext.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorService.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorTransaction.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorWorkspace.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/FileWatchDispatcher.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorSelection.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/AssetImportPathFilter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/AssetImportStamp.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/EditorAssetValidation.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/ImageUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/ModelImportConfig.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/ModelImporter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/TextureImportConfig.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/TextureImporter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/SourceControl/EditorSourceControl.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/SourceControl/SourceControlProvider.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Viewport/EditorVisualizerGeometry.cpp"
	LIBS
		Engine
		Microsoft::DirectXTex
		cgltf
		meshoptimizer
	LABELS
		"unit;editor;nogpu"
	TIMEOUT
		30
)

target_include_directories(EditorTest PRIVATE "${CMAKE_SOURCE_DIR}/Source")

# EditorConfig·EditorToolDefaults 는 REFLECT 타입이라 StaticType() 이 코드젠에서 나온다. EditorModule 을
# 링크하지 않는 테스트도 그 심볼이 필요하므로 이 헤더들만 따로 코드젠한다.
sw_addReflectionStep(EditorTest
	HEADERS
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorConfig.h"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorToolDefaults.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/EditorPreviewProbe.h"
	INCLUDES
		"${CMAKE_SOURCE_DIR}/Source"
)
