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
	sw_addLibrary(EditorModule
		TYPE MODULE
		LINK_LIBRARIES
						Engine
			RuntimeAPI
			imgui
			implot
			imguizmo
			imgui_nodeeditor
			imgui_notify
			sw_graphics_libs
		LOG_TAG Editor
		PCH
	)
	sw_disableTargetIpo(EditorModule)
	if(COMMAND sw_setUnityBuild)
		sw_setUnityBuild(EditorModule OFF)
	endif()
	set_source_files_properties(
		"${CMAKE_CURRENT_SOURCE_DIR}/Backend/Renderer/ImGuiVulkanRendererBackend.cpp"
		PROPERTIES SKIP_PRECOMPILE_HEADERS ON
	)

	sw_addReflectionStep(EditorModule
		INCLUDES "${CMAKE_SOURCE_DIR}/Source"
		HEADERS
			"${CMAKE_CURRENT_SOURCE_DIR}/Config/EditorConfig.h"
	)
endif()

if(NOT SW_SHIPPING_BUILD)
target_compile_definitions(EditorModule PRIVATE SW_EDITOR_INTERNAL)
endif()
