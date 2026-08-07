# ==============================================================================
# @file cmake/Toolchain.cmake
# @brief 사용자 툴체인 게이트 (vcpkg). Preset의 CMAKE_TOOLCHAIN_FILE과 함께 사용.
# ==============================================================================

if(SW_USE_VCPKG)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules/Toolchain/Vcpkg/Vcpkg.cmake")
endif()
