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
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "MSVC runtime library" FORCE)
	target_compile_options(sw_sanitizer INTERFACE /fsanitize=address)
	target_link_options(sw_sanitizer INTERFACE /INCREMENTAL:NO)
	if(sw_is_clang_cl)
		execute_process(
			COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir
			OUTPUT_VARIABLE sw_clang_resource_dir
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)
		set(sw_clang_asan_dir "${sw_clang_resource_dir}/lib/windows")
		if(EXISTS "${sw_clang_asan_dir}/clang_rt.asan_dynamic-x86_64.lib")
			target_link_libraries(sw_sanitizer INTERFACE
				"${sw_clang_asan_dir}/clang_rt.asan_dynamic-x86_64.lib"
				"${sw_clang_asan_dir}/clang_rt.asan_dynamic_runtime_thunk-x86_64.lib"
			)
			file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
			file(COPY "${sw_clang_asan_dir}/clang_rt.asan_dynamic-x86_64.dll"
				DESTINATION "${CMAKE_BINARY_DIR}/Bin")
		endif()
	endif()
	message(STATUS "[Sanitizer] AddressSanitizer (MSVC/clang-cl frontend, CRT: /MD)")
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
