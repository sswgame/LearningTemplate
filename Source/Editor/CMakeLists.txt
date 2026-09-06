# ==============================================================================
# @file Source/Editor/CMakeLists.txt
# @brief 인게임 에디터 모듈 (개발 빌드 MODULE DLL)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) EditorModule — Dev MODULE, imgui 계열만 직접 링크
#    Unity OFF: TU 간 ODR/헬퍼 충돌이 정리될 때까지
#    Vulkan 백엔드 TU: Types.h 별칭이 PCH와 union 멤버 충돌 → 해당 파일만 PCH 제외
# ------------------------------------------------------------------------------
if(NOT SW_SHIPPING_BUILD)
	file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

	add_library(EditorModule MODULE ${EDITOR_SOURCES})

	find_package(directxtex CONFIG REQUIRED)

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
			sw_graphics_libs
			sw_global_options
	)
	if(sw_flag_libraries)
		target_link_libraries(EditorModule PRIVATE ${sw_flag_libraries})
	endif()

	target_compile_definitions(EditorModule
		PRIVATE
			"SW_LOG_TAG=\"Editor\""
			SW_MODULE_EXPORTS
			SW_EDITOR_INTERNAL
	)
	sw_configurePch(EditorModule "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

	sw_setModuleBinOutput(EditorModule)
	set_target_properties(EditorModule PROPERTIES
		INTERPROCEDURAL_OPTIMIZATION FALSE
		FOLDER "Source/Editor"
	)

	if(COMMAND sw_setUnityBuild)
		sw_setUnityBuild(EditorModule OFF)
	endif()

	set_source_files_properties(
		"${CMAKE_CURRENT_SOURCE_DIR}/Common/Backend/Render/ImGuiVulkanRendererBackend.cpp"
		PROPERTIES SKIP_PRECOMPILE_HEADERS ON
	)

	sw_addReflectionStep(EditorModule
		INCLUDES "${CMAKE_SOURCE_DIR}/Source"
		HEADERS
			"${CMAKE_CURRENT_SOURCE_DIR}/Common/Config/EditorConfig.h"
			"${CMAKE_CURRENT_SOURCE_DIR}/Common/Config/EditorData.h"
			"${CMAKE_CURRENT_SOURCE_DIR}/Common/Gui/EditorChrome.h"
	)
endif()
