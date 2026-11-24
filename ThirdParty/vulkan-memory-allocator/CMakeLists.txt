# ==============================================================================
# @file ThirdParty/vulkan-memory-allocator/CMakeLists.txt
# @brief Vulkan Memory Allocator(GPUOpen, MIT, 헤더 하나 `vk_mem_alloc.h`) — Vulkan 백엔드의 장치 메모리 할당기.
#        헤더는 Source/Engine/Graphics/RHI/Vulkan/VulkanMemoryAllocator.cpp 에서만 include 한다(구현도 그 파일 하나가 짓는다).
#        Vulkan 백엔드의 그래픽 라이브러리(`sw_graphics_vulkan_libs`)에 잇는다 — 헤더 전용이라 링크 항목은 늘지 않는다.
#        웹(WebGPU 하나)과 전용 서버(RHI 없음)는 Vulkan 을 짓지 않으므로 찾지 않는다.
# ==============================================================================

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR SW_TARGET_TYPE STREQUAL "Server")
	return()
endif()

sw_add_vcpkg_package(
	NAME vulkan-memory-allocator
	PACKAGE VulkanMemoryAllocator
	CONFIG_TARGET GPUOpen::VulkanMemoryAllocator
)
target_link_libraries(sw_graphics_vulkan_libs INTERFACE vulkan-memory-allocator)
