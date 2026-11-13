# ==============================================================================
# @file cmake/Modules/Environment/WindowsToolSearch.cmake
# @brief MSVC lib.exe · Windows SDK mt.exe 를 폴더 규칙대로 찾는 함수 둘 (본 프로젝트와 vcpkg 포트 툴체인이 함께 쓴다)
# ==============================================================================
# `FindWindowsTools.cmake`(본 프로젝트)와 `cmake/Toolchain/Vcpkg/VcpkgTriplets.cmake`(포트 툴체인 생성)가 둘 다 부른다 — 탐색을
# 한 벌씩 따로 들면 어느 쪽을 고칠 때 다른 쪽이 남는다. 무엇을 먼저 볼지(고정 llvm-lib · llvm-mt 등)는
# 부르는 쪽마다 다르므로 여기 두지 않는다 — 여기 있는 것은 "MSVC · SDK 폴더 안에서 어디를 보는가" 뿐이다.
include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# 1) sw_find_msvc_lib_exe — MSVC 도구 폴더 안의 lib.exe (호스트 x64 → x86, 대상 x64 → x86 순)
# 없으면 OUT_VAR 는 빈 문자열
# ------------------------------------------------------------------------------
function(sw_find_msvc_lib_exe MSVC_TOOLS_DIR OUT_VAR)
	set(lib_exe "")

	if(MSVC_TOOLS_DIR AND NOT MSVC_TOOLS_DIR STREQUAL "")
		foreach(host_arch IN ITEMS Hostx64 Hostx86)
			foreach(target_arch IN ITEMS x64 x86)
				set(lib_candidate "${MSVC_TOOLS_DIR}/bin/${host_arch}/${target_arch}/lib.exe")

				if(EXISTS "${lib_candidate}")
					set(lib_exe "${lib_candidate}")
					break()
				endif()
			endforeach()

			if(lib_exe)
				break()
			endif()
		endforeach()
	endif()

	set(${OUT_VAR} "${lib_exe}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# 2) sw_find_windows_sdk_mt — Windows SDK 의 mt.exe
# 주어진 SDK 폴더 · 버전을 먼저 보고, 없으면 설치된 Windows Kits 10 에서 가장 높은 버전을 찾는다(x64 → x86 순)
# 없으면 OUT_VAR 는 빈 문자열
# ------------------------------------------------------------------------------
function(sw_find_windows_sdk_mt SDK_DIR SDK_VERSION OUT_VAR)
	set(mt_exe "")

	if(SDK_DIR AND SDK_VERSION AND NOT SDK_DIR STREQUAL "" AND NOT SDK_VERSION STREQUAL "")
		foreach(arch IN ITEMS x64 x86)
			set(mt_candidate "${SDK_DIR}/bin/${SDK_VERSION}/${arch}/mt.exe")

			if(EXISTS "${mt_candidate}")
				set(mt_exe "${mt_candidate}")
				break()
			endif()
		endforeach()
	endif()

	if(NOT mt_exe)
		foreach(kits_root IN ITEMS
			"C:/Program Files (x86)/Windows Kits/10"
			"C:/Program Files/Windows Kits/10"
		)
			if(NOT IS_DIRECTORY "${kits_root}/bin")
				continue()
			endif()

			file(GLOB sdk_ver_dirs LIST_DIRECTORIES true "${kits_root}/bin/10.*")
			list(SORT sdk_ver_dirs COMPARE NATURAL ORDER DESCENDING)

			foreach(ver_dir IN LISTS sdk_ver_dirs)
				foreach(arch IN ITEMS x64 x86)
					set(mt_candidate "${ver_dir}/${arch}/mt.exe")

					if(EXISTS "${mt_candidate}")
						set(mt_exe "${mt_candidate}")
						break()
					endif()
				endforeach()

				if(mt_exe)
					break()
				endif()
			endforeach()

			if(mt_exe)
				break()
			endif()
		endforeach()
	endif()

	set(${OUT_VAR} "${mt_exe}" PARENT_SCOPE)
endfunction()
