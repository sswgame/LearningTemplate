# ==============================================================================
# @file cmake/Environment/WindowsToolSearch.cmake
# @brief MSVC lib.exe · Windows SDK mt.exe 를 폴더 규칙대로 찾는 함수 둘 (본 프로젝트와 vcpkg 포트 툴체인이 함께 쓴다)
# ==============================================================================
# 예전에는 `FindWindowsTools.cmake`(본 프로젝트)와 `Modules/Toolchain/Vcpkg/VcpkgPortsToolchain.cmake`(포트 빌드)가 이 두 탐색을
# 글자 그대로 한 벌씩 들고 있었다(22 줄 + 9 줄). 어느 쪽을 고치면 다른 쪽이 남는다. 무엇을 먼저 볼지(고정 llvm-lib · llvm-mt 등)는
# 부르는 쪽마다 다르므로 여기 두지 않는다 — 여기 있는 것은 "MSVC · SDK 폴더 안에서 어디를 보는가" 뿐이다.
include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# 1) sw_findMsvcLibExe — MSVC 도구 폴더 안의 lib.exe (호스트 x64 → x86, 대상 x64 → x86 순)
# 없으면 OUT_VAR 는 빈 문자열
# ------------------------------------------------------------------------------
function(sw_findMsvcLibExe MSVC_TOOLS_DIR OUT_VAR)
	set(libExe "")

	if(MSVC_TOOLS_DIR AND NOT MSVC_TOOLS_DIR STREQUAL "")
		foreach(hostArch IN ITEMS Hostx64 Hostx86)
			foreach(targetArch IN ITEMS x64 x86)
				set(libCandidate "${MSVC_TOOLS_DIR}/bin/${hostArch}/${targetArch}/lib.exe")

				if(EXISTS "${libCandidate}")
					set(libExe "${libCandidate}")
					break()
				endif()
			endforeach()

			if(libExe)
				break()
			endif()
		endforeach()
	endif()

	set(${OUT_VAR} "${libExe}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# 2) sw_findWindowsSdkMt — Windows SDK 의 mt.exe
# 주어진 SDK 폴더 · 버전을 먼저 보고, 없으면 설치된 Windows Kits 10 에서 가장 높은 버전을 찾는다(x64 → x86 순)
# 없으면 OUT_VAR 는 빈 문자열
# ------------------------------------------------------------------------------
function(sw_findWindowsSdkMt SDK_DIR SDK_VERSION OUT_VAR)
	set(mtExe "")

	if(SDK_DIR AND SDK_VERSION AND NOT SDK_DIR STREQUAL "" AND NOT SDK_VERSION STREQUAL "")
		foreach(arch IN ITEMS x64 x86)
			set(mtCandidate "${SDK_DIR}/bin/${SDK_VERSION}/${arch}/mt.exe")

			if(EXISTS "${mtCandidate}")
				set(mtExe "${mtCandidate}")
				break()
			endif()
		endforeach()
	endif()

	if(NOT mtExe)
		foreach(kitsRoot IN ITEMS
			"C:/Program Files (x86)/Windows Kits/10"
			"C:/Program Files/Windows Kits/10"
		)
			if(NOT IS_DIRECTORY "${kitsRoot}/bin")
				continue()
			endif()

			file(GLOB sdkVerDirs LIST_DIRECTORIES true "${kitsRoot}/bin/10.*")
			list(SORT sdkVerDirs COMPARE NATURAL ORDER DESCENDING)

			foreach(verDir IN LISTS sdkVerDirs)
				foreach(arch IN ITEMS x64 x86)
					set(mtCandidate "${verDir}/${arch}/mt.exe")

					if(EXISTS "${mtCandidate}")
						set(mtExe "${mtCandidate}")
						break()
					endif()
				endforeach()

				if(mtExe)
					break()
				endif()
			endforeach()

			if(mtExe)
				break()
			endif()
		endforeach()
	endif()

	set(${OUT_VAR} "${mtExe}" PARENT_SCOPE)
endfunction()
