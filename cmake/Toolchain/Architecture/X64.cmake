# ==============================================================================
# @file cmake/Toolchain/Architecture/X64.cmake
# @brief x64 아키텍처 INTERFACE 매크로 (판정은 DetectArchitecture.cmake)
# ==============================================================================

if(NOT SW_TARGET_ARCHITECTURE STREQUAL "x64")
	return()
endif()

add_library(sw_architecture_x64 INTERFACE)
target_compile_definitions(sw_architecture_x64 INTERFACE SW_X64)
list(APPEND SW_FLAG_LIBRARIES sw_architecture_x64)
# 명령어 집합은 아키텍처의 것이다 — 컴파일러 모듈에 두면 arm64 · wasm32 대상에도 실린다(Android 빌드가 -mavx2 로 섰다).
# Release 만 Haswell 수준(AVX2 · FMA · BMI1/2 · LZCNT · F16C). 철자는 드라이버가 정하고 수준은 플랫폼끼리 같다:
# clang-cl `/arch:AVX2` 는 대상 CPU 를 haswell 로 잡는다(`clang-cl -### /arch:AVX2` → "-target-cpu" "haswell"). GNU 드라이버의 `-mavx2` 는
# x86-64 에 +avx2 하나만 더해 FMA 가 빠졌다 — 같은 수준은 `-march=haswell`(`-###` → "-target-cpu" "haswell")이다. 기능 하나씩 켜지 않고 수준으로 고른다.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" OR MSVC)
	target_compile_options(sw_architecture_x64 INTERFACE $<$<CONFIG:Release>:/arch:AVX2>)
else()
	target_compile_options(sw_architecture_x64 INTERFACE $<$<CONFIG:Release>:-march=haswell>)
endif()
