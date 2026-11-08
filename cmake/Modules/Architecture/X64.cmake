# ==============================================================================
# @file cmake/Modules/Architecture/X64.cmake
# @brief x64 아키텍처 INTERFACE 매크로 (판정은 DetectArchitecture.cmake)
# ==============================================================================

if(NOT sw_target_architecture STREQUAL "x64")
	return()
endif()

add_library(sw_architecture_x64 INTERFACE)
target_compile_definitions(sw_architecture_x64 INTERFACE SW_X64)
list(APPEND sw_flag_libraries sw_architecture_x64)
# 명령어 집합은 아키텍처의 것이다 — 컴파일러 모듈에 두면 arm64 · wasm32 대상에도 실린다(Android 빌드가 -mavx2 로 섰다).
# Release 만 AVX2(256 비트 SIMD). 철자는 드라이버가 정한다(clang-cl · MSVC 는 /arch, GNU 드라이버는 -m).
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" OR MSVC)
	target_compile_options(sw_architecture_x64 INTERFACE $<$<CONFIG:Release>:/arch:AVX2>)
else()
	target_compile_options(sw_architecture_x64 INTERFACE $<$<CONFIG:Release>:-mavx2>)
endif()
