# ==============================================================================
# @file cmake/VcpkgGate.cmake
# @brief SW_USE_VCPKG 게이트. Preset의 CMAKE_TOOLCHAIN_FILE(vcpkg.cmake)과 함께 사용.
# @note 이 파일은 CMAKE_TOOLCHAIN_FILE 자체가 아님 — project() 전 매니페스트/루트 설정.
# ==============================================================================

if(SW_USE_VCPKG)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules/Toolchain/Vcpkg/Vcpkg.cmake")
endif()
