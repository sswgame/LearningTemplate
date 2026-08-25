# ==============================================================================
# @file cmake/Modules/Compiler/MSVC.cmake
# @brief MSVC 컴파일러 플래그 INTERFACE
# ==============================================================================

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	return()
endif()

# 파일 스코프 include: 멀티프로세서 컴파일 활성화
set(CMAKE_MSVC_PARALLEL_COMPILE ON)

add_library(sw_compiler_msvc INTERFACE)

# ------------------------------------------------------------------------------
# 1) 컴파일 — 문자셋 → 코드젠 → 경고 → 서드파티 → 빌드설정(Debug/Release)
# ------------------------------------------------------------------------------
target_compile_options(sw_compiler_msvc INTERFACE
	# 문자셋
	/utf-8

	# 코드젠 / 최적화 기본 설정
	/bigobj
	/EHsc
	/GR-
	/MP

	# 경고 활성화 및 특정 경고 비활성화
	/W4
	/wd4201
	/wd4251

	# 서드파티 외부 헤더 경고 억제 (/external)
	/external:I${CMAKE_SOURCE_DIR}/ThirdParty
	/external:W0

	# 빌드 설정별 최적화 및 디버그 심볼
	# Debug: 디버그 심볼 & 최적화 끄기
	$<$<CONFIG:Debug>:/Od>
	$<$<CONFIG:Debug>:/Zi>

	# Release: 최고 최적화
	$<$<CONFIG:Release>:/O2>
)

# ------------------------------------------------------------------------------
# 2) 링크 — 빌드 설정별 증분 링크 및 최적화
# ------------------------------------------------------------------------------
target_link_options(sw_compiler_msvc INTERFACE
	# Debug: FastLink 디버그 정보
	$<$<CONFIG:Debug>:/DEBUG:FASTLINK>
	$<$<CONFIG:Debug>:/INCREMENTAL>

	# Release: 참조 제거 및 중복 함수 병합(ICF)
	$<$<CONFIG:Release>:/INCREMENTAL:NO>
	$<$<CONFIG:Release>:/OPT:ICF>
	$<$<CONFIG:Release>:/OPT:REF>
)

# ------------------------------------------------------------------------------
# 3) 런타임 라이브러리 및 정의 매크로
# ------------------------------------------------------------------------------
set_property(TARGET sw_compiler_msvc PROPERTY
	MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

target_compile_definitions(sw_compiler_msvc INTERFACE SW_COMPILER_MSVC)

list(APPEND sw_flag_libraries sw_compiler_msvc)
