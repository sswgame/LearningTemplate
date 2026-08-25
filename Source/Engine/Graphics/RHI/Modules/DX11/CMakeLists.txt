# ==============================================================================
# @file Source/Engine/Graphics/RHI/Modules/DX11/CMakeLists.txt
# @brief RHI_DX11 MODULE — Direct3D 11 백엔드
# ==============================================================================

include("${CMAKE_SOURCE_DIR}/cmake/Engine/RhiBackendSources.cmake")
sw_addRhiBackendModule(RHI_DX11 sw_graphics_dx11_libs SOURCES ${SW_RHI_DX11_DEVICE_SOURCES})
