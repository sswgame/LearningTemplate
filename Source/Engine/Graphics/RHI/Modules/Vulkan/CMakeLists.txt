# ==============================================================================
# @file Source/Engine/Graphics/RHI/Modules/Vulkan/CMakeLists.txt
# @brief RHI_Vulkan MODULE — Vulkan 백엔드
# ==============================================================================

include("${CMAKE_SOURCE_DIR}/cmake/Engine/RhiBackendSources.cmake")
sw_addRhiBackendModule(RHI_Vulkan sw_graphics_vulkan_libs SOURCES ${SW_RHI_VULKAN_DEVICE_SOURCES})
