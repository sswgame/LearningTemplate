# ==============================================================================
# @file cmake/Modules/Toolchain/Vcpkg/x64-windows.cmake
# @brief 프로젝트 전용 vcpkg overlay triplet (x64-windows, clang-cl 포트 빌드)
# ==============================================================================

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# OpenGLRHI는 core 4.6 컨텍스트를 쓰므로 glad도 core로 생성한다.
set(GLAD_PROFILE core)

# vcpkg가 포트를 빌드할 때 clang-cl + llvm-rc를 사용하도록 지정합니다.
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/VcpkgPortsToolchain.cmake")
