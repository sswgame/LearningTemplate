# ==============================================================================
# @file cmake/internal/ThirdParty.cmake
# @brief Vendored ThirdParty 경고 억제 헬퍼 (소스 수정 없이 ignore)
# ==============================================================================

# ThirdParty 헤더를 SYSTEM include 로 노출해 소비자 TU 에서 경고를 숨깁니다.
# 사용 예:
#   sw_third_party_system_includes(imgui_notify INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
function(sw_third_party_system_includes TARGET_NAME)
	cmake_parse_arguments(ARG "" "" "INTERFACE;PUBLIC;PRIVATE" ${ARGN})
	if(NOT TARGET ${TARGET_NAME})
		message(FATAL_ERROR "[ThirdParty] Target not found: ${TARGET_NAME}")
	endif()
	if(ARG_INTERFACE)
		target_include_directories(${TARGET_NAME} SYSTEM INTERFACE ${ARG_INTERFACE})
	endif()
	if(ARG_PUBLIC)
		target_include_directories(${TARGET_NAME} SYSTEM PUBLIC ${ARG_PUBLIC})
	endif()
	if(ARG_PRIVATE)
		target_include_directories(${TARGET_NAME} SYSTEM PRIVATE ${ARG_PRIVATE})
	endif()
endfunction()

# ThirdParty 라이브러리 자체 컴파일 시 경고를 끕니다 (INTERFACE 타겟은 no-op).
function(sw_silence_third_party_warnings TARGET_NAME)
	if(NOT TARGET ${TARGET_NAME})
		message(FATAL_ERROR "[ThirdParty] Target not found: ${TARGET_NAME}")
	endif()

	get_target_property(_tp_type ${TARGET_NAME} TYPE)
	if(_tp_type STREQUAL "INTERFACE_LIBRARY")
		return()
	endif()

	# MSVC / clang-cl: /W0 ; GCC / Clang: -w
	target_compile_options(${TARGET_NAME} PRIVATE
		$<$<BOOL:${MSVC}>:/W0>
		$<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:-w>
	)
endfunction()
