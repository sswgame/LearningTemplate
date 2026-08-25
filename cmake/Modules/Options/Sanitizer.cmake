# ==============================================================================
# @file cmake/Modules/Options/Sanitizer.cmake
# @brief Address/UB Sanitizer 플래그 (SW_ENABLE_SANITIZER)
# ==============================================================================

if(NOT SW_ENABLE_SANITIZER)
	return()
endif()

add_library(sw_sanitizer INTERFACE)

# ------------------------------------------------------------------------------
# 1) 프론트엔드별 sanitizer 플래그
#    clang-cl: Clang ID + MSVC 프론트엔드 → /fsanitize=address (GNU -fsanitize 아님)
# ------------------------------------------------------------------------------
set(sw_is_clang_cl FALSE)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	set(sw_is_clang_cl TRUE)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
	set(sw_is_clang_cl TRUE)
endif()

if(MSVC OR sw_is_clang_cl)
	target_compile_options(sw_sanitizer INTERFACE /fsanitize=address)
	target_link_options(sw_sanitizer INTERFACE /INCREMENTAL:NO)
	message(STATUS "[Sanitizer] AddressSanitizer (MSVC/clang-cl frontend)")
else()
	target_compile_options(sw_sanitizer INTERFACE
		$<$<CXX_COMPILER_ID:GNU>:-fsanitize=address,undefined>
		$<$<CXX_COMPILER_ID:Clang>:-fsanitize=address,undefined>
		$<$<CXX_COMPILER_ID:AppleClang>:-fsanitize=address,undefined>
	)
	target_link_options(sw_sanitizer INTERFACE
		$<$<CXX_COMPILER_ID:GNU>:-fsanitize=address,undefined>
		$<$<CXX_COMPILER_ID:Clang>:-fsanitize=address,undefined>
		$<$<CXX_COMPILER_ID:AppleClang>:-fsanitize=address,undefined>
	)
	message(STATUS "[Sanitizer] Address+UBSanitizer (GNU/Clang)")
endif()

list(APPEND sw_flag_libraries sw_sanitizer)
