# ==============================================================================
# @file Test/EditorUITest/CMakeLists.txt
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

# 에디터 · App 테스트는 클라이언트 코드다 — 전용 서버 타깃(Server)에는 에디터 · App · 창이 없어 짓지 않는다.
if(SW_TARGET_TYPE STREQUAL "Server")
	sw_declareUnbuiltDirectory("${CMAKE_CURRENT_SOURCE_DIR}")
	return()
endif()

# 같이 컴파일하는 에디터 백엔드 소스. `Platform/` 은 폴더 규칙으로 훑는다 — 플랫폼 창 백엔드(Win32 · X11 …)는 ImGui 만 쓰는 단위라
# 새 플랫폼 백엔드가 폴더에 들어오면 자동으로 따라온다(못 지으면 링크 오류로 드러난다). `Render/` 는 RHI 디바이스가 필요해 뺀다.
# 최상위 두 개(입력 필터 · 키 전달)는 폴더로 가를 수 없다 — 같은 폴더에 `EditorUICapture` · `DrawSnapshotHandoff` 처럼 엔진 밖 의존을 끄는
# 파일이 함께 있고, 에디터 타깃(Source/Editor)이 이 파일들을 따로 묶어 노출하는 선언도 없다(그 CMake 는 이 단위의 소유가 아니다).
# 이름 둘이 이 목록의 유일한 손 입력이다.
file(GLOB swEditorUIPlatformBackends CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Source/Editor/Common/Backend/Platform/*.cpp")
if(NOT swEditorUIPlatformBackends)
	message(FATAL_ERROR "EditorUITest: Source/Editor/Common/Backend/Platform 에 플랫폼 백엔드 소스가 없다")
endif()

sw_addTestExecutable(EditorUITest
	# 테스트 소스는 이 폴더를 훑는다. 같이 컴파일하는 에디터 백엔드 소스는 위 목록.
	EXTRA_SOURCES
		${swEditorUIPlatformBackends}
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Backend/ImGuiOsInputFilter.cpp"
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Backend/ImGuiKeyForward.cpp"
	LIBS
		Engine
		imgui
)

target_include_directories(EditorUITest PRIVATE "${CMAKE_SOURCE_DIR}/Source")
