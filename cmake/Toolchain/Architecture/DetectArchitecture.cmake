# ==============================================================================
# @file cmake/Toolchain/Architecture/DetectArchitecture.cmake
# @brief 컴파일러가 겨냥하는 아키텍처를 SW_TARGET_ARCHITECTURE 에 정한다 (x64 · arm64)
# @note X64.cmake · ARM64.cmake 가 이 값으로 SW_X64 / SW_ARM64 를 고른다.
#       코드 쪽 검사는 Source/Core/Foundation/TargetMacroCheck.h — 여기 판정이 실제 컴파일러와 어긋나면 빌드가 선다.
# ==============================================================================

# CMAKE_SYSTEM_PROCESSOR 는 툴체인 파일이 정하지 않으면 **빌드하는 기계**의 CPU 다. Windows 에서 MSVC arm64 교차 도구나
# clang-cl --target=arm64-pc-windows-msvc 로 지어도 AMD64 로 남는다. 컴파일러가 실제로 겨냥하는 아키텍처는
# CMAKE_CXX_COMPILER_ARCHITECTURE_ID 가 안다 — CMake 가 컴파일러로 식별 소스를 지어 그 내장 매크로에서 읽은 값이다.
# MSVC 계열은 x64 · ARM64, 나머지는 x86_64 · aarch64 로 적는다. 오래된 CMake 는 GNU 계열에서 이 값을
# 비워 두므로 그때만 CMAKE_SYSTEM_PROCESSOR 로 물러선다.
set(architecture_id "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")

if(NOT architecture_id)
	set(architecture_id "${CMAKE_SYSTEM_PROCESSOR}")
endif()

# 매크로는 빌드 트리 하나에 값 하나다. 아키텍처 여럿을 한 번에 짓는 구성(값이 `arm64;x86_64` 같은 목록)은
# 한쪽 조각이 틀린 매크로로 컴파일되므로 받지 않는다.
list(LENGTH architecture_id architecture_count)

if(architecture_count GREATER 1)
	message(FATAL_ERROR "[Architecture] One build tree targets one architecture, got '${architecture_id}'. "
		"Configure one build directory per architecture.")
endif()

# 엔진은 64 비트 네이티브(x64 · arm64)와 웹(wasm32)을 짓는다 — 32 비트 네이티브(x86 · armv7)와 ARM64EC 는 판정하지 않는다.
# 웹은 wasm32 다(유니티 · Godot 웹과 같다): memory64 는 Safari 가 받지 않고 경계 검사 비용이 있어 고르지 않았다. Emscripten 툴체인은 프로세서를 x86 으로 적어 EMSCRIPTEN 으로 가른다.
if(EMSCRIPTEN)
	set(SW_TARGET_ARCHITECTURE "wasm32")
elseif(architecture_id MATCHES "^(x64|x86_64|AMD64|amd64)$")
	set(SW_TARGET_ARCHITECTURE "x64")
elseif(architecture_id MATCHES "^(ARM64|arm64|aarch64|AArch64)$")
	set(SW_TARGET_ARCHITECTURE "arm64")
else()
	message(FATAL_ERROR "[Architecture] Unsupported target architecture '${architecture_id}'. "
		"The engine builds x64, arm64 and wasm32 (web) only.")
endif()

message(STATUS "[Architecture] ${SW_TARGET_ARCHITECTURE} (compiler architecture id: '${architecture_id}')")

unset(architecture_id)
unset(architecture_count)
