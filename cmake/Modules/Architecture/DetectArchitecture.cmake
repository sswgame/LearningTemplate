# ==============================================================================
# @file cmake/Modules/Architecture/DetectArchitecture.cmake
# @brief 컴파일러가 겨냥하는 아키텍처를 sw_target_architecture 에 정한다 (x64 · arm64)
# @note X64.cmake · ARM64.cmake 가 이 값으로 SW_X64 / SW_ARM64 를 고른다.
#       코드 쪽 검사는 Source/Core/Common/TargetMacroCheck.h — 여기 판정이 실제 컴파일러와 어긋나면 빌드가 선다.
# ==============================================================================

# CMAKE_SYSTEM_PROCESSOR 는 툴체인 파일이 정하지 않으면 **빌드하는 기계**의 CPU 다. Windows 에서 MSVC arm64 교차 도구나
# clang-cl --target=arm64-pc-windows-msvc 로 지어도 AMD64 로 남는다. 컴파일러가 실제로 겨냥하는 아키텍처는
# CMAKE_CXX_COMPILER_ARCHITECTURE_ID 가 안다 — CMake 가 컴파일러로 식별 소스를 지어 그 내장 매크로에서 읽은 값이다.
# MSVC 계열은 x64 · ARM64, 나머지는 x86_64 · aarch64 로 적는다. 오래된 CMake 는 GNU 계열에서 이 값을
# 비워 두므로 그때만 CMAKE_SYSTEM_PROCESSOR 로 물러선다.
set(swArchitectureId "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")

if(NOT swArchitectureId)
	set(swArchitectureId "${CMAKE_SYSTEM_PROCESSOR}")
endif()

# 매크로는 빌드 트리 하나에 값 하나다. 아키텍처 여럿을 한 번에 짓는 구성(값이 `arm64;x86_64` 같은 목록)은
# 한쪽 조각이 틀린 매크로로 컴파일되므로 받지 않는다.
list(LENGTH swArchitectureId swArchitectureCount)

if(swArchitectureCount GREATER 1)
	message(FATAL_ERROR "[Architecture] One build tree targets one architecture, got '${swArchitectureId}'. "
		"Configure one build directory per architecture.")
endif()

# 엔진은 64 비트만 짓는다 — 32 비트(x86 · armv7)와 ARM64EC 는 판정하지 않는다.
if(swArchitectureId MATCHES "^(x64|x86_64|AMD64|amd64)$")
	set(sw_target_architecture "x64")
elseif(swArchitectureId MATCHES "^(ARM64|arm64|aarch64|AArch64)$")
	set(sw_target_architecture "arm64")
else()
	message(FATAL_ERROR "[Architecture] Unsupported target architecture '${swArchitectureId}'. "
		"The engine builds x64 and arm64 only.")
endif()

message(STATUS "[Architecture] ${sw_target_architecture} (compiler architecture id: '${swArchitectureId}')")

unset(swArchitectureId)
unset(swArchitectureCount)
