# ==============================================================================
# @file cmake/Modules/Toolchain/Vcpkg/arm64-osx.cmake
# @brief 프로젝트 전용 vcpkg overlay triplet (arm64-osx)
#
# `Vcpkg.cmake` 가 `APPLE` 에서 `${arch}-osx` 트리플릿을 고르는데 이 폴더에는 그 파일이
# **없었다.** 오버레이 트리플릿이 없으면 vcpkg 는 내장 트리플릿으로 떨어지고, 그러면 아래
# `GLAD_PROFILE` 같은 이 저장소만의 설정이 빠진다 — macOS 를 켜는 날 그 차이부터 문제가 된다.
#
# GitHub 의 `macos-14` 이후 러너는 **Apple Silicon** 이라 이쪽이 기본이고, 인텔 러너용은
# `x64-osx.cmake` 다. 둘은 아키텍처만 다르다.
# ==============================================================================

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

# OpenGLRHI는 core 4.6 컨텍스트를 쓰므로 glad도 core로 생성한다.
#
# @note macOS 의 OpenGL 은 **4.1 에서 멈춰 있고** 이 엔진의 GL 백엔드는 `GL_ARB_gl_spirv` 를
#       요구하므로, macOS 에서 GL 백엔드가 실제로 도는 일은 없다(WSL 과 같은 이유로 백엔드가
#       스스로 물러난다). 그래도 **빌드는 되어야** 한다 — 그것이 이 트리플릿이 있는 이유다.
set(GLAD_PROFILE core)
