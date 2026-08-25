# ==============================================================================
# @file cmake/Modules/Toolchain/Vcpkg/x64-linux.cmake
# @brief 프로젝트 전용 vcpkg overlay triplet (x64-linux)
# ==============================================================================

set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

# OpenGLRHI는 core 4.6 컨텍스트를 쓰므로 glad도 core로 생성한다.
set(GLAD_PROFILE core)
