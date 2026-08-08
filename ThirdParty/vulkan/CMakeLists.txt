# ==============================================================================
# @file ThirdParty/vulkan/CMakeLists.txt
# @brief Vulkan API 헤더/로더 연동
# ==============================================================================

add_library(vulkan_headers INTERFACE)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	# vcpkg vulkan-loader is frequently built without X11 WSI (no xlib/xcb).
	# Link the distro loader so VK_KHR_xlib_surface / VK_KHR_xcb_surface exist.
	find_library(SW_SYSTEM_VULKAN_LIBRARY NAMES vulkan REQUIRED)
	target_link_libraries(vulkan_headers INTERFACE ${SW_SYSTEM_VULKAN_LIBRARY})
	message(STATUS "[vulkan] Linux: linking system loader ${SW_SYSTEM_VULKAN_LIBRARY}")

	# Headers: prefer vcpkg vulkan-headers when present, else system.
	if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET
		AND EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/vulkan/vulkan.h")
		target_include_directories(vulkan_headers INTERFACE
			"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
	else()
		find_path(SW_SYSTEM_VULKAN_INCLUDE NAMES vulkan/vulkan.h)
		if(SW_SYSTEM_VULKAN_INCLUDE)
			target_include_directories(vulkan_headers INTERFACE ${SW_SYSTEM_VULKAN_INCLUDE})
		endif()
	endif()
else()
	find_package(Vulkan QUIET)
	if(Vulkan_FOUND)
		if(TARGET Vulkan::Vulkan)
			target_link_libraries(vulkan_headers INTERFACE Vulkan::Vulkan)
		elseif(TARGET Vulkan::Headers)
			target_link_libraries(vulkan_headers INTERFACE Vulkan::Headers)
		endif()
	else()
		sw_link_vcpkg_header_only_target(vulkan_headers)
	endif()
endif()

if(TARGET sw_third_party_includes)
	target_link_libraries(sw_third_party_includes INTERFACE vulkan_headers)
endif()
