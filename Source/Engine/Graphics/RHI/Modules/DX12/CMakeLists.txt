# ==============================================================================
# @file Source/Engine/Graphics/RHI/Modules/DX12/CMakeLists.txt
# @brief RHI_DX12 MODULE — Direct3D 12 백엔드
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) MODULE — RhiSources.cmake의 device .cpp를 Engine과 공유
# ------------------------------------------------------------------------------
include("${CMAKE_SOURCE_DIR}/cmake/RhiSources.cmake")

sw_addLibrary(RHI_DX12
	TYPE MODULE
	SOURCES ${SW_RHI_DX12_DEVICE_SOURCES}
	LINK_LIBRARIES Engine sw_graphics_dx12_libs sw_third_party_includes
	LOG_TAG RHI
	PCH
)
set_target_properties(RHI_DX12 PROPERTIES FOLDER "Source/Engine/Graphics/RHI/Modules")
