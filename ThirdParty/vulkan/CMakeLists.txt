# ==============================================================================
# @file ThirdParty/vulkan/CMakeLists.txt
# @brief Vulkan API 헤더/로더 연동
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 헤더 INTERFACE — 로더는 sw_graphics_vulkan_libs로 분리
#    (헤더만 쓰는 타겟이 vulkan-1.lib를 끌지 않게)
# ------------------------------------------------------------------------------
add_library(vulkanheaders INTERFACE)

find_package(Vulkan QUIET)
if(Vulkan_FOUND)
	if(TARGET Vulkan::Headers)
		target_link_libraries(vulkanheaders INTERFACE Vulkan::Headers)
	elseif(TARGET Vulkan::Vulkan)
		target_include_directories(vulkanheaders INTERFACE ${Vulkan_INCLUDE_DIRS})
	endif()
else()
	sw_linkVcpkgHeaderOnlyTarget(vulkanheaders)
endif()

if(TARGET sw_third_party_includes)
	target_link_libraries(sw_third_party_includes INTERFACE vulkanheaders)
endif()

if(NOT TARGET sw_graphics_vulkan_libs)
	add_library(sw_graphics_vulkan_libs INTERFACE)
	if(TARGET Vulkan::Vulkan)
		target_link_libraries(sw_graphics_vulkan_libs INTERFACE Vulkan::Vulkan)
	elseif(Vulkan_LIBRARIES)
		target_link_libraries(sw_graphics_vulkan_libs INTERFACE ${Vulkan_LIBRARIES})
	endif()
endif()
