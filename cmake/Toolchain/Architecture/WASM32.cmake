# ==============================================================================
# @file cmake/Toolchain/Architecture/WASM32.cmake
# @brief WASM32 아키텍처 INTERFACE 매크로 (판정은 DetectArchitecture.cmake)
# ==============================================================================

if(NOT SW_TARGET_ARCHITECTURE STREQUAL "wasm32")
	return()
endif()

add_library(sw_architecture_wasm32 INTERFACE)
target_compile_definitions(sw_architecture_wasm32 INTERFACE SW_WASM32)
list(APPEND SW_FLAG_LIBRARIES sw_architecture_wasm32)
# 명령어 집합: WebAssembly SIMD(128 비트) — 모든 구성. 수학 경로(Jolt · 엔진 벡터)가 x64 의 SSE 와 같은 폭을 쓴다.
# Jolt 는 `__wasm_simd128__` 을 보고 벡터 형식을 v128 로 바꾸므로 포트도 같은 깃발로 지어야 형식 배치(ABI)가 맞는다 — 웹 vcpkg 트리플릿
# (VcpkgTriplets.cmake 의 wasm32-emscripten-pthread)이 C · C++ 깃발에 같은 값을 둔다. LTO(Web-Shipping)는 링크에서 코드를 만들므로 링크에도 준다.
# SIMD 가 없는 브라우저는 모듈을 컴파일하지 못한다 — 배포 페이지가 띄우기 전에 확인하고 안내한다(Scripts/package/web_package.py SIMD_PROBE_BYTES).
target_compile_options(sw_architecture_wasm32 INTERFACE -msimd128)
target_link_options(sw_architecture_wasm32 INTERFACE -msimd128)
