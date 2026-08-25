# ==============================================================================
# @file Source/Engine/Graphics/RHI/Modules/Vulkan/CMakeLists.txt
# @brief RHI_Vulkan MODULE — Vulkan 백엔드
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) MODULE — RhiSources.cmake의 device .cpp를 Engine과 공유
# ------------------------------------------------------------------------------
include("${CMAKE_SOURCE_DIR}/cmake/RhiSources.cmake")

sw_addLibrary(RHI_Vulkan
	TYPE MODULE
	SOURCES ${SW_RHI_VULKAN_DEVICE_SOURCES}
	LINK_LIBRARIES Engine sw_graphics_vulkan_libs sw_third_party_includes
	LOG_TAG RHI
	PCH
)
set_target_properties(RHI_Vulkan PROPERTIES FOLDER "Source/Engine/Graphics/RHI/Modules")
