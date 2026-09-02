# ==============================================================================
# @file Test/EditorTest/CMakeLists.txt
# @brief Editor 판별·세션 정책 단위 테스트 (UI/ImGui 없음)
# ==============================================================================

find_package(directxtex CONFIG REQUIRED)

sw_addTestExecutable(EditorTest
	SOURCES
		TestEditorAssetType.cpp
		TestEditorSessionPolicy.cpp
		TestEditorViewportMath.cpp
		TestSelectionManager.cpp
		TestTexturePipeline.cpp
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorAssetType.cpp"
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
