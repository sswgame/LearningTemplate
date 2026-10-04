# ==============================================================================
# @file Source/Editor/CMakeLists.txt
# @brief 인게임 에디터 모듈 (개발 빌드 MODULE DLL)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) EditorModule: Dev 용 MODULE 이고, imgui 계열만 직접 링크한다
#    유니티 빌드: TU 내부 도우미는 모두 익명 네임스페이스의 *Internal 구조체로 묶여 있다
#    Vulkan 백엔드 TU: Types.h 별칭이 PCH 와 union 멤버에서 충돌하므로 그 파일만 PCH 에서 뺀다
# ------------------------------------------------------------------------------
file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

# 배포는 에디터를 짓지 않는다 — 소스 검사 게이트가 알도록 적는다(`sw_declareUnbuiltSources`). EditorTest 가 가져가는 몇 개는 그대로 지어진다.
if(SW_SHIPPING_BUILD)
	sw_declareUnbuiltSources(${EDITOR_SOURCES})
endif()

# 배포본이 아니어도 프로젝트가 에디터 모듈을 끌 수 있다(매니페스트 `EditorModule.module.json`).
set(swBuildEditorModule OFF)
if(NOT SW_SHIPPING_BUILD)
	sw_skipInactiveModule(EditorModule swSkipEditor)
	if(NOT swSkipEditor)
		set(swBuildEditorModule ON)
	endif()
endif()

if(swBuildEditorModule)
	add_library(EditorModule MODULE ${EDITOR_SOURCES})

	target_include_directories(EditorModule PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
	target_link_libraries(EditorModule
		PRIVATE
			Engine
			RuntimeAPI
			imgui
			imgui_nodeeditor
			imgui_notify
			imguizmo
			implot
			Microsoft::DirectXTex
			cgltf
			meshoptimizer
			sw_graphics_libs
			sw_global_options
	)
	target_compile_definitions(EditorModule
		PRIVATE
			"SW_LOG_TAG=\"Editor\""
			SW_MODULE_EXPORTS
	)
	sw_configurePch(EditorModule "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

	sw_setModuleBinOutput(EditorModule)
	set_target_properties(EditorModule PROPERTIES
		INTERPROCEDURAL_OPTIMIZATION FALSE
		FOLDER "Source/Editor"
	)
	sw_registerDynamicModule(EditorModule editor)

	if(COMMAND sw_setUnityBuild)
		sw_setUnityBuild(EditorModule BATCH_SIZE 8)
	endif()

	set_source_files_properties(
		"${CMAKE_CURRENT_SOURCE_DIR}/Common/Backend/Render/ImGuiVulkanRendererBackend.cpp"
		PROPERTIES SKIP_PRECOMPILE_HEADERS ON
	)

	sw_addReflectionStep(EditorModule
		INCLUDES "${CMAKE_SOURCE_DIR}/Source"
		HEADERS
			"${CMAKE_CURRENT_SOURCE_DIR}/Common/Config/EditorConfig.h"
			"${CMAKE_CURRENT_SOURCE_DIR}/Common/Config/EditorToolDefaults.h"
			"${CMAKE_CURRENT_SOURCE_DIR}/Common/Gui/EditorChrome.h"
	)
endif()
