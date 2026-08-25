# ==============================================================================
# @file Source/Engine/Graphics/RHI/Modules/DX12/CMakeLists.txt
# @brief RHI_DX12 MODULE — Direct3D 12 백엔드
# ==============================================================================

include("${CMAKE_SOURCE_DIR}/cmake/Engine/RhiBackendSources.cmake")
sw_addRhiBackendModule(RHI_DX12 sw_graphics_dx12_libs SOURCES ${SW_RHI_DX12_DEVICE_SOURCES})
