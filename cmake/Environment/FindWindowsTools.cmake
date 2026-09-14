# ==============================================================================
# @file cmake/Environment/FindWindowsTools.cmake
# @brief Windows lib.exe / mt.exe 탐색 및 clang-cl 아카이버 재바인딩 헬퍼 (캐시 최적화)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) sw_findWindowsArchiveAndMt — lib.exe(아카이버) / mt.exe 경로
# 우선순위: llvm-lib·llvm-mt → toolchain_config MSVC/SDK → Windows Kits
# 결과를 SW_CACHED_WIN_AR / SW_CACHED_WIN_MT에 캐싱하여 불필요한 디스크 I/O 방지
# ------------------------------------------------------------------------------
function(sw_findWindowsArchiveAndMt OUT_AR OUT_MT)
	# 캐시된 아카이버가 llvm-lib 이 아닌데 고정 툴체인에는 있다면 **다시 찾는다.**
	# 안 그러면 이미 만들어 둔 빌드 트리는 영원히 MSVC lib.exe 를 쥐고 있고, LTO 는 계속 꺼진 채로
	# 남는다 — 새로 클론한 사람만 고쳐진 상태가 된다.
	set(swReDetect FALSE)

	if(DEFINED CACHE{SW_CACHED_WIN_AR} AND NOT "$CACHE{SW_CACHED_WIN_AR}" MATCHES "llvm-lib")
		sw_pinnedArchiverPath(swProbeAr)

		if(swProbeAr)
			set(swReDetect TRUE)
		endif()
	endif()

	if(DEFINED CACHE{SW_CACHED_WIN_AR} AND DEFINED CACHE{SW_CACHED_WIN_MT} AND NOT swReDetect)
		set(${OUT_AR} "$CACHE{SW_CACHED_WIN_AR}" PARENT_SCOPE)
		set(${OUT_MT} "$CACHE{SW_CACHED_WIN_MT}" PARENT_SCOPE)
		return()
	endif()

	set(swAr "")
	set(swMt "")

	# SDK/MSVC 경로는 `DetectToolchain` 이 include 한 생성 파일(ToolchainVars.cmake)이 준다.
	# 예전에는 여기서 toolchain_config.json 을 **다시** 읽었고, 키 이름을 리터럴로 적어서
	# DetectToolchain 의 `SW_KEY_*` 상수와 철자가 갈라져 있었다.
	set(swSdkDir "${SW_TOOLCHAIN_WINDOWS_SDK_DIR}")
	set(swSdkVer "${SW_TOOLCHAIN_WINDOWS_SDK_VERSION}")
	set(swMsvcTools "${SW_TOOLCHAIN_MSVC_TOOLS_DIR}")

	# **고정 LLVM 의 llvm-lib 을 가장 먼저 본다** (왜 그래야 하는지는 ToolchainBinaries.cmake 머리 주석).
	# 예전엔 환경변수 둘만 보고 없으면 MSVC lib.exe 로 떨어졌는데, 정작 컴파일러는 Tools/LLVM 것을 쓴다.
	sw_pinnedArchiverPath(swPinnedAr)

	if(swPinnedAr)
		set(swAr "${swPinnedAr}")
	elseif(swMsvcTools AND NOT swMsvcTools STREQUAL "")
		foreach(hostArch IN ITEMS Hostx64 Hostx86)
			foreach(targetArch IN ITEMS x64 x86)
				set(arCandidate "${swMsvcTools}/bin/${hostArch}/${targetArch}/lib.exe")

				if(EXISTS "${arCandidate}")
					set(swAr "${arCandidate}")
					break()
				endif()
			endforeach()

			if(swAr)
				break()
			endif()
		endforeach()
	endif()

	if(DEFINED ENV{LLVM_DIR} AND EXISTS "$ENV{LLVM_DIR}/bin/llvm-mt.exe")
		set(swMt "$ENV{LLVM_DIR}/bin/llvm-mt.exe")
	elseif(DEFINED ENV{LLVM_ROOT} AND EXISTS "$ENV{LLVM_ROOT}/bin/llvm-mt.exe")
		set(swMt "$ENV{LLVM_ROOT}/bin/llvm-mt.exe")
	elseif(swSdkDir AND swSdkVer AND NOT swSdkDir STREQUAL "" AND NOT swSdkVer STREQUAL "")
		foreach(arch IN ITEMS x64 x86)
			set(mtCandidate "${swSdkDir}/bin/${swSdkVer}/${arch}/mt.exe")

			if(EXISTS "${mtCandidate}")
				set(swMt "${mtCandidate}")
				break()
			endif()
		endforeach()
	endif()

	if(NOT swMt)
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
						set(swMt "${mtCandidate}")
						break()
					endif()
				endforeach()

				if(swMt)
					break()
				endif()
			endforeach()

			if(swMt)
				break()
			endif()
		endforeach()
	endif()

	set(SW_CACHED_WIN_AR "${swAr}" CACHE INTERNAL "Cached Windows ar tool")
	set(SW_CACHED_WIN_MT "${swMt}" CACHE INTERNAL "Cached Windows mt tool")
	set(${OUT_AR} "${swAr}" PARENT_SCOPE)
	set(${OUT_MT} "${swMt}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# 2) sw_bindClangClWindowsTools — project() 직후 clang-cl + Ninja 정적 아카이버 재바인딩
# ------------------------------------------------------------------------------
macro(sw_bindClangClWindowsTools)
	if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		set(swArTool "")

		# **먼저 찾고, 있으면 그것을 쓴다.** 예전엔 이미 설정된 `CMAKE_AR` 을 먼저 봤는데, 그 값은
		# 바로 위 `project()` 가 다시 채워 넣은 MSVC lib.exe 다(DetectToolchain 이 llvm-lib 으로
		# FORCE 해 두어도 덮인다). 그래서 이 매크로가 존재하는 이유 자체 — "project() 뒤에 다시
		# 묶는다" — 가 무력했고, LTO 는 계속 꺼져 있었다. 탐색이 아무것도 못 찾을 때만 물려받는다.
		sw_findWindowsArchiveAndMt(swFoundAr swFoundMt)

		if(swFoundAr)
			set(swArTool "${swFoundAr}")
		elseif(CMAKE_AR AND NOT CMAKE_AR MATCHES "NOTFOUND" AND EXISTS "${CMAKE_AR}")
			set(swArTool "${CMAKE_AR}")
		endif()

		if(swFoundMt AND(NOT CMAKE_MT OR CMAKE_MT MATCHES "NOTFOUND"))
			set(CMAKE_MT "${swFoundMt}" CACHE FILEPATH "매니페스트 도구" FORCE)
		endif()

		if(swArTool)
			set(CMAKE_AR "${swArTool}" CACHE FILEPATH "정적 라이브러리 아카이버" FORCE)
			set(CMAKE_C_COMPILER_AR "${swArTool}" CACHE FILEPATH "" FORCE)
			set(CMAKE_CXX_COMPILER_AR "${swArTool}" CACHE FILEPATH "" FORCE)

			# **캐시만 고치면 안 된다.** `project()` 의 컴파일러 탐지가 같은 이름의 **일반 변수**를
			# 최상위 스코프에 만들어 두고, 일반 변수는 캐시를 가린다. 그래서 캐시엔 llvm-lib 이
			# 적혀 있는데 정작 읽는 쪽은 MSVC lib.exe 를 보는 상태가 됐다(2026-09-14 에 확인).
			# 이 매크로는 호출자 스코프에서 펼쳐지므로 여기서 덮으면 그 그림자가 걷힌다.
			set(CMAKE_AR "${swArTool}")
			set(CMAKE_C_COMPILER_AR "${swArTool}")
			set(CMAKE_CXX_COMPILER_AR "${swArTool}")

			set(CMAKE_C_CREATE_STATIC_LIBRARY "<CMAKE_AR> /nologo <LINK_FLAGS> /out:<TARGET> <OBJECTS>")
			set(CMAKE_CXX_CREATE_STATIC_LIBRARY "<CMAKE_AR> /nologo <LINK_FLAGS> /out:<TARGET> <OBJECTS>")
			set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> /nologo <LINK_FLAGS> /out:<TARGET> <OBJECTS>")
			set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> /nologo <LINK_FLAGS> /out:<TARGET> <OBJECTS>")
			set(CMAKE_C_ARCHIVE_APPEND "<CMAKE_AR> /nologo <LINK_FLAGS> /out:<TARGET> <OBJECTS>")
			set(CMAKE_CXX_ARCHIVE_APPEND "<CMAKE_AR> /nologo <LINK_FLAGS> /out:<TARGET> <OBJECTS>")
			set(CMAKE_C_ARCHIVE_FINISH "")
			set(CMAKE_CXX_ARCHIVE_FINISH "")

			message(STATUS "[FindWindowsTools] archive tool=${swArTool}")
		else()
			message(WARNING "[FindWindowsTools] lib.exe not found — static libraries may fail to link")
		endif()
	endif()
endmacro()
