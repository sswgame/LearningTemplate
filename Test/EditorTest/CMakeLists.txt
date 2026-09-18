# ==============================================================================
# @file Test/EditorTest/CMakeLists.txt
# @brief Editor 판별·세션 정책 단위 테스트 (UI/ImGui 없음)
# ==============================================================================

find_package(directxtex CONFIG REQUIRED)

# Editor 소스를 직접 나열해 같이 컴파일한다 — `EditorModule` 은 MODULE DLL 이라 링크할 수 없다.
#
# **이 목록은 "Editor/Common 전부" 도 "ImGui 를 안 쓰는 것 전부" 도 아니다.** 테스트가 실제로 링크해야
# 하는 것만 적는다(2026-09-13 기준 55 개 중 20 개). 기계가 대신 고를 수 없어서 목록은 손으로 두되,
# `CheckTestSuites.py` 가 썩는 두 방향을 막는다: 파일이 옮겨져 경로가 죽는 것과, ImGui 를 include 하는
# 파일이 섞여 들어오는 것(EditorTest 는 ImGui 를 링크하지 않으므로 그 순간 빌드가 깨진다).
sw_addTestExecutable(EditorTest
	SOURCES
		TestEditorAssetType.cpp
		TestEditorBackgroundJob.cpp
		TestEditorCommandRegistry.cpp
		TestEditorListFilter.cpp
		TestEditorPanelDocument.cpp
		TestEditorSceneCommands.cpp
		TestEditorSessionPolicy.cpp
		TestEditorTransaction.cpp
		TestEditorToolAssetCommands.cpp
		TestEditorViewportMath.cpp
		TestEditorViewportPick.cpp
		TestReloadFileManager.cpp
		TestSelectionManager.cpp
		TestTexturePipeline.cpp
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorCommandRegistry.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorSceneCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorViewportPick.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorTransformCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorToolAssetCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorInspectorCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorData.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorConfig.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorPlaySession.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/EditorUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Widgets/EditorListFilter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorAssetType.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorContext.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorService.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorTransaction.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorWorkspace.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/ReloadFileManager.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/SelectionManager.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/ImageUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/TextureImportConfig.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/TextureBaker.cpp"
	LIBS
		Engine
		Microsoft::DirectXTex
	LABELS
		"unit;editor;nogpu"
	TIMEOUT
		30
)

target_include_directories(EditorTest PRIVATE "${CMAKE_SOURCE_DIR}/Source")

# EditorConfig·EditorData 는 REFLECT 타입이라 StaticType() 이 코드젠에서 나온다. EditorModule 을
# 링크하지 않는 테스트도 그 심볼이 필요하므로 이 헤더들만 따로 코드젠한다.
sw_addReflectionStep(EditorTest
	HEADERS
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorConfig.h"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorData.h"
	INCLUDES
		"${CMAKE_SOURCE_DIR}/Source"
)
