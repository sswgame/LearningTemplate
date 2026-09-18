# ==============================================================================
# @file Test/EditorUiTest/CMakeLists.txt
# @brief ImGui 를 **링크하는** 에디터 단위 테스트
#
# 왜 `EditorTest` 와 따로 있는가 — `EditorTest` 는 일부러 ImGui 를 링크하지 않는다(그 쪽
# CMakeLists 에 이유가 적혀 있고 `CheckTestSuites` 가 그 경계를 지킨다). 그런데 에디터 수정의
# 상당수가 **ImGui 컨텍스트가 있어야 재현되는 수명 문제**다(짝 없는 Shutdown, 팝업 훅, 도킹 배치).
# 그런 것들을 "테스트할 수 없다" 로 남기지 않으려고 ImGui 를 링크하는 작은 타깃을 따로 둔다.
#
# **여기 들어올 수 있는 것**: ImGui 컨텍스트만 있으면 도는 것(`ImGui::CreateContext()`).
# **여기 들어오면 안 되는 것**: GPU·창이 필요한 것 — 그런 것은 `hostgpu` 라벨이 필요하고
# 이 타깃은 `nogpu` 다. 렌더러 백엔드는 RHI 디바이스가 있어야 하므로 여기서 다루지 않는다.
# ==============================================================================

sw_addTestExecutable(EditorUiTest
	SOURCES
		TestImGuiPlatformBackend.cpp
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Backend/Platform/ImGuiWin32PlatformBackend.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Backend/Platform/ImGuiOSXPlatformBackend.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Backend/Platform/ImGuiX11PlatformBackend.cpp"
	LIBS
		Engine
		imgui
	LABELS
		"unit;editor;nogpu"
	TIMEOUT
		30
)

target_include_directories(EditorUiTest PRIVATE "${CMAKE_SOURCE_DIR}/Source")
