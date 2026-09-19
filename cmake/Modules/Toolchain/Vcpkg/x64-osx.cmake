# ==============================================================================
# @file cmake/Modules/Toolchain/Vcpkg/x64-osx.cmake
# @brief 프로젝트 전용 vcpkg overlay triplet (x64-osx)
#
# 인텔 맥용이다. Apple Silicon 은 `arm64-osx.cmake` 이고, 둘은 아키텍처만 다르다 —
# 왜 이 파일들이 필요한지는 그쪽 머리말에 적어 두었다.
# ==============================================================================

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

# OpenGLRHI는 core 4.6 컨텍스트를 쓰므로 glad도 core로 생성한다.
set(GLAD_PROFILE core)
