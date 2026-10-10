# ==============================================================================
# @file Source/Editor/CMakeLists.txt
# @brief 인게임 에디터 모듈 (개발 빌드 MODULE DLL)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) EditorModule: Dev 용 MODULE 이고, imgui 계열만 직접 링크한다(모듈 라이브러리 기본값은 sw_addModuleLibrary)
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
	DEV_TYPE MODULE
	EXPORTS MODULE
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
