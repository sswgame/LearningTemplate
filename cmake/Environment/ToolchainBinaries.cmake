# ==============================================================================
# @file cmake/Environment/ToolchainBinaries.cmake
# @brief 아카이버(llvm-lib / llvm-ar) 바인딩과 IPO(LTO) 지원 판정 — 플랫폼 공통
# ==============================================================================
#
# **여기가 "아카이버를 누구로 쓸 것인가" 의 한 자리다.** 플랫폼별 파일이 `toolchain_config.json` 을 직접
# 읽어 `llvm_path` 를 뒤지지 않는다 — DetectToolchain 이 생성 파일로 받아 둔 `SW_TOOLCHAIN_LLVM_PATH` 를 쓴다.
#
# 왜 아카이버가 중요한가: clang 이 `-flto` 로 내는 `.obj`/`.o` 는 LLVM 비트코드다.
# 그것을 묶으려면 LLVM 아카이버여야 한다. 짝이 어긋나면 **LTO 가 통째로, 조용히 꺼진다** —
# Windows 는 `LNK1107`, 리눅스는 `"CMAKE_CXX_COMPILER_AR-NOTFOUND"` 로 죽고, CMake 는 그 실패를
# "이 컴파일러는 IPO 를 지원하지 않는다" 로 보고한다. 그러면 `SW_ENABLE_LTO=ON` 인데 `-flto` 가 한 TU 에도
# 안 걸리고 아무 오류도 나지 않는다.

# ------------------------------------------------------------------------------
# 1) sw_pinned_archiver_path — LLVM 아카이버 경로 (없으면 빈 문자열)
#
# **실제로 쓰는 컴파일러 옆을 먼저 본다.** 아카이버는 컴파일러가 낸 비트코드를 읽어야 하므로
# 둘은 같은 LLVM 에서 와야 한다. 컴파일러 경로가 링크(리눅스 `/usr/bin/clang++` → `/usr/lib/llvm-N/bin`)면
# 링크가 가리키는 폴더도 본다 — `/usr/bin` 에는 llvm-ar 이 없어 LTO 가 조용히 꺼진다.
#
# 아카이버 이름은 부르는 쪽이 준다 — 컴파일러 앞단(clang-cl 은 llvm-lib, GNU 앞단 clang 은 llvm-ar)이 정하지 대상 플랫폼이 정하지 않는다.
# 예전에는 `if(WIN32) … else()` 로 골랐는데 `project()` 전(DetectToolchain)에는 WIN32 가 아직 없어 Windows 에서도 llvm-ar 을 찾았다.
# ------------------------------------------------------------------------------
function(sw_pinned_archiver_path OUT_VAR ARCHIVER_NAME)
	set(${OUT_VAR} "" PARENT_SCOPE)
	set(swArName "${ARCHIVER_NAME}")

	set(swCandidateDirs "")

	# (1) 지금 쓰는 컴파일러 옆(그리고 링크가 가리키는 실제 폴더).
	if(CMAKE_CXX_COMPILER)
		get_filename_component(swCompilerDir "${CMAKE_CXX_COMPILER}" DIRECTORY)
		get_filename_component(swCompilerRealPath "${CMAKE_CXX_COMPILER}" REALPATH)
		get_filename_component(swCompilerRealDir "${swCompilerRealPath}" DIRECTORY)
		list(APPEND swCandidateDirs "${swCompilerDir}" "${swCompilerRealDir}")
	endif()

	# (2) 저장소가 고정한 LLVM(Environment 가 적은 llvm_path). `project()` 이전처럼 컴파일러가 아직 없을 때 여기가 답한다.
	if(SW_TOOLCHAIN_LLVM_PATH)
		list(APPEND swCandidateDirs "${SW_TOOLCHAIN_LLVM_PATH}/bin")
	endif()

	foreach(swCandidateDir IN LISTS swCandidateDirs)
		if(EXISTS "${swCandidateDir}/${swArName}")
			set(${OUT_VAR} "${swCandidateDir}/${swArName}" PARENT_SCOPE)
			return()
		endif()
	endforeach()
endfunction()

# ------------------------------------------------------------------------------
# 2) sw_bind_pinned_archiver — POSIX 에서 아카이버·인덱서를 묶고 IPO 아카이브 규칙을 다시 쓴다
#
# Windows 는 `sw_bind_clang_cl_windows_tools` 가 같은 일을 한다(정적 라이브러리 명령 문자열까지
# 거기서 다시 쓴다). 이 함수는 그 POSIX 짝이다.
# ------------------------------------------------------------------------------
function(sw_bind_pinned_archiver)
	# 플랫폼이 아니라 컴파일러 앞단으로 고른다 — GNU 앞단 clang(Linux · Android NDK · Emscripten)은 llvm-ar, clang-cl 은 `sw_bind_clang_cl_windows_tools` 가 맡는다.
	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
		sw_pinned_archiver_path(swPosixAr "llvm-ar${CMAKE_HOST_EXECUTABLE_SUFFIX}")
		set(swPosixRanlib "")

		if(swPosixAr)
			get_filename_component(swPosixArDir "${swPosixAr}" DIRECTORY)

			if(EXISTS "${swPosixArDir}/llvm-ranlib${CMAKE_HOST_EXECUTABLE_SUFFIX}")
				set(swPosixRanlib "${swPosixArDir}/llvm-ranlib${CMAKE_HOST_EXECUTABLE_SUFFIX}")
			endif()

			set(CMAKE_AR "${swPosixAr}" CACHE FILEPATH "정적 라이브러리 아카이버" FORCE)
			set(CMAKE_C_COMPILER_AR "${swPosixAr}" CACHE FILEPATH "" FORCE)
			set(CMAKE_CXX_COMPILER_AR "${swPosixAr}" CACHE FILEPATH "" FORCE)

			# 캐시만으로는 부족하다 — `project()` 가 같은 이름의 **일반 변수**를 최상위 스코프에
			# 만들어 두고, 일반 변수가 캐시를 가린다(그래서 부른 쪽 스코프에 PARENT_SCOPE 로). Windows 에서 이것 때문에 한참 헤맸다.
			set(CMAKE_AR "${swPosixAr}" PARENT_SCOPE)
			set(CMAKE_C_COMPILER_AR "${swPosixAr}" PARENT_SCOPE)
			set(CMAKE_CXX_COMPILER_AR "${swPosixAr}" PARENT_SCOPE)

			if(swPosixRanlib)
				foreach(swPosixLang IN ITEMS C CXX)
					set(CMAKE_${swPosixLang}_COMPILER_RANLIB "${swPosixRanlib}" CACHE FILEPATH "" FORCE)
					set(CMAKE_${swPosixLang}_COMPILER_RANLIB "${swPosixRanlib}" PARENT_SCOPE)
				endforeach()

				set(CMAKE_RANLIB "${swPosixRanlib}" CACHE FILEPATH "정적 라이브러리 인덱서" FORCE)
				set(CMAKE_RANLIB "${swPosixRanlib}" PARENT_SCOPE)
			endif()

			# **변수만 고쳐서는 안 된다.** CMake 는 IPO 용 아카이브 명령을 `project()` 시점의
			# `CMAKE_<LANG>_COMPILER_AR` 로 **문자열로 박아 둔다**(Modules/Compiler/Clang.cmake).
			# 그때 NOTFOUND 였으면 나중에 변수를 고쳐도 규칙은 그대로
			# `"CMAKE_CXX_COMPILER_AR-NOTFOUND" qc ...` 다 — 실측으로 확인했다.
			foreach(swPosixLang IN ITEMS C CXX)
				set(CMAKE_${swPosixLang}_ARCHIVE_CREATE_IPO "\"${swPosixAr}\" qc <TARGET> <LINK_FLAGS> <OBJECTS>" PARENT_SCOPE)
				set(CMAKE_${swPosixLang}_ARCHIVE_APPEND_IPO "\"${swPosixAr}\" q <TARGET> <LINK_FLAGS> <OBJECTS>" PARENT_SCOPE)

				if(swPosixRanlib)
					set(CMAKE_${swPosixLang}_ARCHIVE_FINISH_IPO "\"${swPosixRanlib}\" <TARGET>" PARENT_SCOPE)
				else()
					set(CMAKE_${swPosixLang}_ARCHIVE_FINISH_IPO "" PARENT_SCOPE)
				endif()
			endforeach()

			message(STATUS "[ToolchainBinaries] CMAKE_AR=${swPosixAr}")
		else()
			# 치명적이지 않다 — 정적 라이브러리는 시스템 ar 로도 묶인다. LTO 만 안 켜진다.
			message(STATUS "[ToolchainBinaries] llvm-ar not found — LTO stays off (system ar keeps working)")
		endif()
	endif()
endfunction()

# ------------------------------------------------------------------------------
# 3) sw_check_ipo_support — 이 툴체인이 실제로 IPO 를 할 수 있는가
#
# **CMake 의 `check_ipo_supported` 는 이 툴체인에서 구조적으로 거짓을 말한다.**
# 그것은 `try_compile(... PROJECT ...)` 로 작은 프로젝트를 따로 구성해서 측정하는데, 그 형식은
# `CMAKE_AR` 을 하위 프로젝트로 **넘기지 않는다**(CMake 4.x 의 CheckIPOSupported.cmake 는
# CMAKE_VERBOSE_MAKEFILE · CMAKE_INTERPROCEDURAL_OPTIMIZATION · 언어 플래그만 넘긴다).
# 그래서 하위 프로젝트는 아카이버를 스스로 찾아 시스템 것을 집고 실패한다 — 컴파일러는 멀쩡한데.
# clang + LLVM 아카이버 조합은 그래서 우리가 직접 판정한다. 그 조합이 아니면 CMake 에게 묻는다.
# ------------------------------------------------------------------------------
function(sw_check_ipo_support OUT_SUPPORTED OUT_ERROR)
	set(${OUT_ERROR} "" PARENT_SCOPE)

	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_AR MATCHES "llvm-lib|llvm-ar")
		set(${OUT_SUPPORTED} TRUE PARENT_SCOPE)
		return()
	endif()

	include(CheckIPOSupported)
	check_ipo_supported(RESULT swIpoOk OUTPUT swIpoErr)
	set(${OUT_SUPPORTED} ${swIpoOk} PARENT_SCOPE)
	set(${OUT_ERROR} "${swIpoErr}" PARENT_SCOPE)
endfunction()
