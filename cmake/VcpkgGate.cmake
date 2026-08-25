# ==============================================================================
# @file cmake/VcpkgGate.cmake
# @brief SW_USE_VCPKG 게이트. Preset의 CMAKE_TOOLCHAIN_FILE(vcpkg.cmake)과 함께 사용.
# @note CMAKE_TOOLCHAIN_FILE 자체가 아님 — project() 전 매니페스트/루트 설정.
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 매니페스트 게이트 — Vcpkg.cmake만 include
#    실제 툴체인은 그 안에서 CMAKE_TOOLCHAIN_FILE로 확정
# ------------------------------------------------------------------------------
if(SW_USE_VCPKG)
	include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules/Toolchain/Vcpkg/Vcpkg.cmake")
endif()
