# ==============================================================================
# @file cmake/Environment/ToolchainBinaries.cmake
# @brief 아카이버(llvm-lib / llvm-ar) 바인딩과 IPO(LTO) 지원 판정 — 플랫폼 공통
# ==============================================================================
#
# **여기가 "아카이버를 누구로 쓸 것인가" 의 한 자리다.**
# 예전에는 같은 판단이 세 군데 있었다: `FindWindowsTools.cmake` 와 (잠깐 존재했던)
# `FindPosixTools.cmake` 가 각자 `toolchain_config.json` 을 직접 읽어 `llvm_path` 를 뒤졌고,
# 정작 그 일을 하는 `sw_findLlvmBin` 이 이미 옆 파일(`FindLlvmBin.cmake`)에 있었다.
#
# 왜 아카이버가 중요한가: clang 이 `-flto` 로 내는 `.obj`/`.o` 는 LLVM 비트코드다.
# 그것을 묶으려면 LLVM 아카이버여야 한다. 짝이 어긋나면 **LTO 가 통째로, 조용히 꺼진다** —
# Windows 는 `LNK1107`, 리눅스는 `"CMAKE_CXX_COMPILER_AR-NOTFOUND"` 로 죽고, CMake 는 그 실패를
# "이 컴파일러는 IPO 를 지원하지 않는다" 로 보고한다. `SW_ENABLE_LTO=ON` 인데 `-flto` 가 한 TU 에도
# 안 걸린 상태로 오래 있었던 것이 그 때문이다(2026-09-14 실측).

include("${CMAKE_CURRENT_LIST_DIR}/FindLlvmBin.cmake")

# ------------------------------------------------------------------------------
# 1) sw_pinnedArchiverPath — LLVM 아카이버 경로 (없으면 빈 문자열)
#
# **실제로 쓰는 컴파일러 옆을 먼저 본다.** 아카이버는 컴파일러가 낸 비트코드를 읽어야 하므로
# 둘은 같은 LLVM 에서 와야 한다. `sw_findLlvmBin` 은 "컴파일러를 어디서 찾을까" 에 답하는 함수라
# **시스템 설치를 프로젝트 Tools 보다 먼저** 본다(최초 clone 시 Tools 가 없어도 되도록). 그 우선순위를
# 아카이버에 그대로 쓰면 어긋난다 — 리눅스에서 실제로 그랬다: 시스템 clang 이 잡혀 `/usr/bin` 을
# 돌려주고, 거기엔 llvm-ar 이 없어 LTO 가 조용히 꺼졌다(2026-09-14 WSL 실측).
# ------------------------------------------------------------------------------
function(sw_pinnedArchiverPath OUT_VAR)
	set(${OUT_VAR} "" PARENT_SCOPE)

	if(WIN32)
		set(swArName "llvm-lib.exe")
	else()
		set(swArName "llvm-ar")
	endif()

	# (1) 지금 쓰는 컴파일러 옆.
	if(CMAKE_CXX_COMPILER)
		get_filename_component(swCompilerDir "${CMAKE_CXX_COMPILER}" DIRECTORY)

		if(swCompilerDir AND EXISTS "${swCompilerDir}/${swArName}")
			set(${OUT_VAR} "${swCompilerDir}/${swArName}" PARENT_SCOPE)
			return()
		endif()
	endif()

	# (2) 저장소가 고정한 LLVM. `project()` 이전처럼 컴파일러가 아직 없을 때도 여기가 답한다.
	sw_findLlvmBin(swLlvmBin)

	if(swLlvmBin AND EXISTS "${swLlvmBin}/${swArName}")
		set(${OUT_VAR} "${swLlvmBin}/${swArName}" PARENT_SCOPE)
	endif()
endfunction()

# ------------------------------------------------------------------------------
# 2) sw_bindPinnedArchiver — POSIX 에서 아카이버·인덱서를 묶고 IPO 아카이브 규칙을 다시 쓴다
#
# Windows 는 `sw_bindClangClWindowsTools` 가 같은 일을 한다(정적 라이브러리 명령 문자열까지
# 거기서 다시 쓴다). 이 매크로는 그 POSIX 짝이다.
# ------------------------------------------------------------------------------
macro(sw_bindPinnedArchiver)
	if(NOT WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		sw_pinnedArchiverPath(swPosixAr)
		set(swPosixRanlib "")

		if(swPosixAr)
			get_filename_component(swPosixArDir "${swPosixAr}" DIRECTORY)

			if(EXISTS "${swPosixArDir}/llvm-ranlib")
				set(swPosixRanlib "${swPosixArDir}/llvm-ranlib")
			endif()

			set(CMAKE_AR "${swPosixAr}" CACHE FILEPATH "정적 라이브러리 아카이버" FORCE)
			set(CMAKE_C_COMPILER_AR "${swPosixAr}" CACHE FILEPATH "" FORCE)
			set(CMAKE_CXX_COMPILER_AR "${swPosixAr}" CACHE FILEPATH "" FORCE)

			# 캐시만으로는 부족하다 — `project()` 가 같은 이름의 **일반 변수**를 최상위 스코프에
			# 만들어 두고, 일반 변수가 캐시를 가린다. Windows 에서 이것 때문에 한참 헤맸다.
			set(CMAKE_AR "${swPosixAr}")
			set(CMAKE_C_COMPILER_AR "${swPosixAr}")
			set(CMAKE_CXX_COMPILER_AR "${swPosixAr}")

			if(swPosixRanlib)
				foreach(swPosixLang IN ITEMS C CXX)
					set(CMAKE_${swPosixLang}_COMPILER_RANLIB "${swPosixRanlib}" CACHE FILEPATH "" FORCE)
					set(CMAKE_${swPosixLang}_COMPILER_RANLIB "${swPosixRanlib}")
				endforeach()

				set(CMAKE_RANLIB "${swPosixRanlib}" CACHE FILEPATH "정적 라이브러리 인덱서" FORCE)
				set(CMAKE_RANLIB "${swPosixRanlib}")
			endif()

			# **변수만 고쳐서는 안 된다.** CMake 는 IPO 용 아카이브 명령을 `project()` 시점의
			# `CMAKE_<LANG>_COMPILER_AR` 로 **문자열에 구워 둔다**(Modules/Compiler/Clang.cmake).
			# 그때 NOTFOUND 였으면 나중에 변수를 고쳐도 규칙은 그대로
			# `"CMAKE_CXX_COMPILER_AR-NOTFOUND" qc ...` 다 — 실측으로 확인했다.
			foreach(swPosixLang IN ITEMS C CXX)
				set(CMAKE_${swPosixLang}_ARCHIVE_CREATE_IPO "\"${swPosixAr}\" qc <TARGET> <LINK_FLAGS> <OBJECTS>")
				set(CMAKE_${swPosixLang}_ARCHIVE_APPEND_IPO "\"${swPosixAr}\" q <TARGET> <LINK_FLAGS> <OBJECTS>")

				if(swPosixRanlib)
					set(CMAKE_${swPosixLang}_ARCHIVE_FINISH_IPO "\"${swPosixRanlib}\" <TARGET>")
				else()
					set(CMAKE_${swPosixLang}_ARCHIVE_FINISH_IPO "")
				endif()
			endforeach()

			message(STATUS "[ToolchainBinaries] CMAKE_AR=${CMAKE_AR}")
		else()
			# 치명적이지 않다 — 정적 라이브러리는 시스템 ar 로도 묶인다. LTO 만 안 켜진다.
			message(STATUS "[ToolchainBinaries] llvm-ar not found — LTO stays off (system ar keeps working)")
		endif()
	endif()
endmacro()

# ------------------------------------------------------------------------------
# 3) sw_checkIpoSupport — 이 툴체인이 실제로 IPO 를 할 수 있는가
#
# **CMake 의 `check_ipo_supported` 는 이 툴체인에서 구조적으로 거짓을 말한다.**
# 그것은 `try_compile(... PROJECT ...)` 로 작은 프로젝트를 따로 구성해서 재는데, 그 형식은
# `CMAKE_AR` 을 하위 프로젝트로 **넘기지 않는다**(CMake 4.x 의 CheckIPOSupported.cmake 는
# CMAKE_VERBOSE_MAKEFILE · CMAKE_INTERPROCEDURAL_OPTIMIZATION · 언어 플래그만 넘긴다).
# 그래서 하위 프로젝트는 아카이버를 스스로 찾아 시스템 것을 집고 실패한다 — 컴파일러는 멀쩡한데.
# clang + LLVM 아카이버 조합은 그래서 우리가 직접 판정한다. 그 조합이 아니면 CMake 에게 묻는다.
# ------------------------------------------------------------------------------
function(sw_checkIpoSupport OUT_SUPPORTED OUT_ERROR)
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
