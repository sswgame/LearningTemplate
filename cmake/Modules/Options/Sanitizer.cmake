# ==============================================================================
# @file cmake/Modules/Options/Sanitizer.cmake
# @brief Address/UB Sanitizer 플래그 (SW_ENABLE_SANITIZER)
# ==============================================================================

if(NOT SW_ENABLE_SANITIZER)
	return()
endif()

add_library(sw_sanitizer INTERFACE)

# 코드·테스트가 "지금 ASan 빌드인가" 를 알아야 하는 자리가 있다. clang-cl 은
# `__SANITIZE_ADDRESS__` 를 정의하지 않고(`__has_feature` 방식) 컴파일러마다 달라서, 빌드가
# 직접 내려 준다.
target_compile_definitions(sw_sanitizer INTERFACE SW_SANITIZER_ADDRESS=1)

# ------------------------------------------------------------------------------
# 1) 프론트엔드별 sanitizer 플래그
# clang-cl: Clang ID + MSVC 프론트엔드 → /fsanitize=address (GNU -fsanitize 아님)
# ------------------------------------------------------------------------------
set(sw_is_clang_cl FALSE)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	set(sw_is_clang_cl TRUE)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
	set(sw_is_clang_cl TRUE)
endif()

if(MSVC OR sw_is_clang_cl)
	# clang-cl 의 ASan 은 디버그 CRT 를 지원하지 않는다("AddressSanitizer doesn't support linking
	# with debug runtime libraries yet"). 그래서 Debug 구성이어도 릴리스 CRT(/MD)를 써야 한다.
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "MSVC runtime library" FORCE)

	# 그런데 CRT 를 바꾸면 **의존성도 같이 바꿔야 한다.** vcpkg 는 Debug 구성에서 /MDd 로 빌드된
	# 디버그 라이브러리를 물려주는데, 우리 오브젝트는 /MD(`_ITERATOR_DEBUG_LEVEL=0`)이므로
	# /failifmismatch 를 심어 둔 라이브러리(imgui-node-editor 등)가 링크를 세운다. 이것 때문에
	# Windows ASan 빌드는 EditorModule.dll 링크에서 멈춰 **한 번도 완주하지 못했다** — CI 는
	# Linux ASan 만 돌려서(ci.yml) 이 경로를 밟지 않았다.
	# 임포트 타깃을 릴리스 쪽으로 매핑해 CRT 를 맞춘다.
	set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG "Release;RelWithDebInfo;" CACHE STRING
		"ASan(/MD)은 vcpkg 릴리스 라이브러리와 링크한다" FORCE)

	# 링크를 릴리스로 바꿨으면 **런타임 DLL 도 릴리스여야 한다.** vcpkg 의 applocal 은 구성이
	# Debug 면 `debug/bin` 에서만 복사한다(경로가 툴체인에 박혀 있어 우리가 바꿀 수 없다). 그래서
	# 이름이 다른 DLL(z.dll ↔ zd.dll)은 아예 안 오고, 이름이 같은 DLL(pugixml.dll)은 /MDd 로
	# 빌드된 것이 와서 CRT 가 엇갈린다. 실제로 이 때문에 CoreTest.exe 가 z.dll 을 못 찾아 떴다.
	# applocal 을 끄고 릴리스 DLL 을 직접 넣는다.
	set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "ASan: 릴리스 DLL 을 직접 staging 한다" FORCE)

	if(DEFINED _VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
		file(GLOB sw_asan_dep_dll "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/*.dll")
		if(sw_asan_dep_dll)
			file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
			file(COPY ${sw_asan_dep_dll} DESTINATION "${CMAKE_BINARY_DIR}/Bin")
			list(LENGTH sw_asan_dep_dll sw_asan_dep_dll_count)
			message(STATUS "[Sanitizer] vcpkg 릴리스 DLL ${sw_asan_dep_dll_count}개를 Bin 으로 복사했다")
		endif()
	endif()

	target_compile_options(sw_sanitizer INTERFACE /fsanitize=address)
	target_link_options(sw_sanitizer INTERFACE /INCREMENTAL:NO)

	# /fsanitize=address 를 켜면 MSVC STL 이 컨테이너 버퍼에 ASan 주석을 달고 오브젝트에
	# `annotate_string` / `annotate_vector` / `annotate_optional` 표시를 심는다
	# (`__msvc_sanitizer_annotate_container.hpp`). vcpkg 라이브러리는 ASan 없이 빌드되어 0 이므로
	# 또 링크가 선다. 우산 매크로 하나로 전부 끈다 — 개별 매크로를 나열하면 STL 이 주석 대상을
	# 늘릴 때마다 같은 링크 오류를 다시 만난다(실제로 string·vector 를 막자 optional 이 나왔다).
	# 잃는 것은 STL 컨테이너 오버플로 탐지뿐이고, 힙·스택·use-after-free 검사는 그대로다.
	target_compile_definitions(sw_sanitizer INTERFACE _DISABLE_STL_ANNOTATION)

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
			# ASan 런타임은 **실행 파일 옆**에 있어야 한다. 실행 파일이 나가는 곳이 두 군데다:
			#   Bin/        — App/테스트
			#   BuildTools/ — ReflectionParser (libclang.dll 때문에 따로 나간다)
			# BuildTools 를 빼먹으면 ASan 빌드가 **빌드 도중에** 죽는다. 코드젠이 빌드 단계에서
			# ReflectionParser 를 실행하는데, 그게 DLL 을 못 찾아 0xc0000135 로 뜨지 못한다.
			# CI 는 Linux ASan 만 돌려서(ci.yml) 이 경로를 아무도 밟지 않았다.
			foreach(sw_asan_dest "Bin" "BuildTools")
				file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/${sw_asan_dest}")
				file(COPY "${sw_clang_asan_dir}/clang_rt.asan_dynamic-x86_64.dll"
					DESTINATION "${CMAKE_BINARY_DIR}/${sw_asan_dest}")
			endforeach()
		endif()
	endif()

	message(STATUS "[Sanitizer] AddressSanitizer (MSVC/clang-cl frontend, CRT: /MD + 릴리스 의존성)")
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
