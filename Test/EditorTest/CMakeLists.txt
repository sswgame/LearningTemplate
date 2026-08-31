# ==============================================================================
# @file Test/EditorTest/CMakeLists.txt
# @brief Editor 판별·세션 정책 단위 테스트 (UI/ImGui 없음)
# ==============================================================================

sw_addTestExecutable(EditorTest
	SOURCES
		TestEditorAssetType.cpp
		TestEditorSessionPolicy.cpp
		TestSelectionManager.cpp
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/EditorAssetType.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Workspace/SelectionManager.cpp"
	LIBS
		Engine
	LABELS
		"unit;editor;nogpu"
	TIMEOUT
		30
)

target_include_directories(EditorTest PRIVATE "${CMAKE_SOURCE_DIR}/Source")
