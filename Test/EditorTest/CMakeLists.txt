# ==============================================================================
# @file Test/EditorTest/CMakeLists.txt
# @brief Editor 판별·세션 정책 단위 테스트 (UI/ImGui 없음)
# ==============================================================================

find_package(directxtex CONFIG REQUIRED)

sw_addTestExecutable(EditorTest
	SOURCES
		TestEditorAssetType.cpp
		TestEditorBackgroundJob.cpp
		TestEditorSceneCommands.cpp
		TestEditorSessionPolicy.cpp
		TestEditorViewportMath.cpp
		TestSelectionManager.cpp
		TestTexturePipeline.cpp
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorSceneCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Commands/EditorTransformCommands.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorConfig.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/EditorPlaySession.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/EditorUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorAssetType.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorContext.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorService.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorTransaction.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorWorkspace.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/SelectionManager.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/ImageUtil.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/TextureImportConfig.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/TextureBaker.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/TextureWatcher.cpp"
	LIBS
		Engine
		Microsoft::DirectXTex
	LABELS
		"unit;editor;nogpu"
	TIMEOUT
		30
)

target_include_directories(EditorTest PRIVATE "${CMAKE_SOURCE_DIR}/Source")

# EditorConfig 는 REFLECT 타입이라 StaticType() 이 코드젠에서 나온다. EditorModule 을 링크하지
# 않는 테스트도 그 심볼이 필요하므로 헤더 하나만 따로 코드젠한다.
sw_addReflectionStep(EditorTest
	HEADERS "${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorConfig.h"
	INCLUDES
		"${CMAKE_SOURCE_DIR}/Source"
)
