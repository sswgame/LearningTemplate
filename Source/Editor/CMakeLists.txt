# ==============================================================================
# @file Source/Editor/CMakeLists.txt
# @brief 인게임 에디터 모듈 (개발 빌드 MODULE DLL)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) EditorModule: Dev 용 SHARED 이고, imgui 계열만 직접 링크한다(모듈 라이브러리 기본값은 sw_addModuleLibrary)
#    SHARED 인 까닭: 확장 모듈(GF_Editor_* · SWGameEditor)이 링크해 등록부 · 위젯 · 선택 · Undo 를 쓴다(언리얼 UnrealEd 와 같다).
#    핫 리로드는 키트와 같은 경로(섀도 복사 + 지연 로드 결속)다.
#    유니티 빌드: TU 내부 도우미는 모두 익명 네임스페이스의 *Internal 구조체로 묶여 있다
# ------------------------------------------------------------------------------
file(GLOB_RECURSE listEditorSource CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

# 배포는 에디터를 짓지 않는다 — 소스 검사 게이트가 알도록 적는다(`sw_declareUnbuiltSources`). EditorTest 가 가져가는 몇 개는 그쪽에서 그대로 지어진다.
if(SW_SHIPPING_BUILD)
	sw_declareUnbuiltSources(${listEditorSource})
	return()
endif()

# 배포본이 아니어도 프로젝트가 에디터 모듈을 끌 수 있다(매니페스트 `EditorModule.module.json`).
sw_skipInactiveModule(EditorModule swSkipEditor)
if(swSkipEditor)
	return()
endif()

sw_addModuleLibrary(EditorModule
	KIND editor
	DEV_TYPE SHARED
	EXPORTS EDITOR
	LOG_TAG "Editor"
	FOLDER "Source/Editor"
	SOURCES ${listEditorSource}
	LINK_PRIVATE Engine RuntimeAPI imgui imgui_nodeeditor imgui_notify imguizmo implot Microsoft::DirectXTex cgltf meshoptimizer sw_graphics_libs
	UNITY_BATCH 8
	REFLECTION_HEADERS
		"${CMAKE_CURRENT_SOURCE_DIR}/Common/Config/EditorConfig.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/Common/Config/EditorToolDefaults.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/Common/GUI/EditorChrome.h"
)

# 에디터는 LTO 를 받지 않는다(링크 시간 — Test/ · Tools/ 와 같은 이유).
set_target_properties(EditorModule PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)

# 확장 모듈은 에디터 헤더(Source/ 기준 경로)를 쓴다. ImGui 링크는 확장이 스스로 한다 — vcpkg imgui 는 정적이라 DLL 마다 사본이고 결속기가 컨텍스트를 맞춘다.
target_include_directories(EditorModule INTERFACE "${CMAKE_SOURCE_DIR}/Source")

# 에디터 빌드 단추(Build > Compile Editor)가 짓는 묶음 — EditorModule + 모든 확장 모듈(sw_addEditorExtension 이 의존을 더한다).
add_custom_target(EditorAll DEPENDS EditorModule)
set_target_properties(EditorAll PROPERTIES FOLDER "Source/Editor")
