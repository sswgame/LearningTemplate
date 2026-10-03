# ==============================================================================
# @file Source/Engine/Graphics/RHI/Modules/CMakeLists.txt
# @brief RHI 백엔드 MODULE DLL 플러그인 (SW_RHI_AS_MODULES)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 백엔드 MODULE — DX는 Windows만, GL/Vulkan은 전 플랫폼
# ------------------------------------------------------------------------------
if(WIN32)
    add_subdirectory(DX11)
    add_subdirectory(DX12)
else()
    # DX 모듈이 없는 플랫폼 — 엔트리와 장치 소스(모듈 구성에서는 Engine 도 빼 둔다)를 소스 검사 게이트에 적는다.
    include("${CMAKE_SOURCE_DIR}/cmake/Engine/RhiBackendSources.cmake")
    sw_declareUnbuiltDirectory("${CMAKE_CURRENT_SOURCE_DIR}/DX11" "${CMAKE_CURRENT_SOURCE_DIR}/DX12")
    sw_declareUnbuiltSources(${SW_RHI_DX11_DEVICE_SOURCES} ${SW_RHI_DX12_DEVICE_SOURCES})
endif()

add_subdirectory(GL)
add_subdirectory(Vulkan)
