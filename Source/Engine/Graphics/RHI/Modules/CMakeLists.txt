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
endif()

add_subdirectory(GL)
add_subdirectory(Vulkan)
