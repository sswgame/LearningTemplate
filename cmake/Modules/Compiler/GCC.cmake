# ==============================================================================
# @file cmake/Modules/Compiler/GCC.cmake
# @brief GCC 컴파일러 플래그 INTERFACE
# ==============================================================================

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	return()
endif()

add_library(sw_compiler_gcc INTERFACE)

# ------------------------------------------------------------------------------
# 1) 컴파일 — 문자셋 → 코드젠 → 경고
# ------------------------------------------------------------------------------
target_compile_options(sw_compiler_gcc INTERFACE
	$<$<COMPILE_LANGUAGE:CXX>:

	# 문자셋
	-finput-charset=UTF-8 # 소스 파일 UTF-8 입력 인코딩 지정
	-fexec-charset=UTF-8 # 실행 바이너리 문자셋 UTF-8 인코딩 지정

	# 코드젠
	-fno-rtti # RTTI 비활성화로 바이너리 크기 및 vtable 오버헤드 축소

	# 경고
	-Wall # 기본 표준 경고 활성화
	-Wextra # 추가 정밀 경고 활성화
	-Werror=unused-result # [[nodiscard]] 결과를 버리면 빌드가 선다(Clang.cmake 와 같은 규칙)
	>
)

# 정의는 언어를 가리지 않는다 — Clang.cmake · MSVC.cmake 와 같이 C 소스도 SW_COMPILER_* 하나를 받는다(TargetMacroCheck.h).
target_compile_definitions(sw_compiler_gcc INTERFACE SW_COMPILER_GCC)

message(STATUS "[Compiler] GCC Compiler Options Configured.")

list(APPEND sw_flag_libraries sw_compiler_gcc)
