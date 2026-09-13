# ==============================================================================
# @file cmake/Environment/FindPosixTools.cmake
# @brief POSIX(clang) 정적 아카이버 바인딩 — LTO 가 성립하려면 llvm-ar 이어야 한다
# ==============================================================================
#
# Windows 쪽 사정은 FindWindowsTools.cmake 에 적혀 있다. 리눅스는 증상이 조금 다르지만 뿌리가 같다:
# CMake 는 IPO 빌드의 정적 라이브러리를 `CMAKE_<LANG>_COMPILER_AR` 로 묶는데, 배포판 clang 패키지는
# `clang++` 옆에 `llvm-ar` 을 두지 않는 경우가 많다. 그러면 그 변수가 `-NOTFOUND` 로 남고
# IPO 탐지가 "Error running link command: no such file or directory" 로 실패한다
# (2026-09-14 WSL Ubuntu 26.04 · clang 21.1.8 에서 실측).
#
# 저장소가 고정한 LLVM(`Tools/LLVM`)에는 llvm-ar 이 함께 들어 있다 — SetupLlvm.py 의 허용 목록이
# 그것을 남기도록 되어 있다. 여기서 그 경로를 명시적으로 묶는다. 컴파일러 옆을 CMake 가 알아서
# 찾아 주기를 기대하지 않는다: 그 기대가 어긋나면 **조용히** LTO 만 꺼지기 때문이다.

macro(sw_bindPosixLlvmArchiver)
	if(NOT WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		set(swPosixPinnedLlvm "")

		if(EXISTS "${CMAKE_SOURCE_DIR}/Config/Environment/toolchain_config.json")
			file(READ "${CMAKE_SOURCE_DIR}/Config/Environment/toolchain_config.json" swPosixCfg)
			string(JSON swPosixPinnedLlvm ERROR_VARIABLE swPosixCfgErr GET "${swPosixCfg}" "llvm_path")
		endif()

		set(swPosixAr "")
		set(swPosixRanlib "")

		if(swPosixPinnedLlvm AND EXISTS "${swPosixPinnedLlvm}/bin/llvm-ar")
			set(swPosixAr "${swPosixPinnedLlvm}/bin/llvm-ar")

			if(EXISTS "${swPosixPinnedLlvm}/bin/llvm-ranlib")
				set(swPosixRanlib "${swPosixPinnedLlvm}/bin/llvm-ranlib")
			endif()
		else()
			# 고정 툴체인이 없으면(시스템 clang 으로 빌드하는 경우) 컴파일러 옆을 본다.
			get_filename_component(swPosixCompilerDir "${CMAKE_CXX_COMPILER}" DIRECTORY)

			if(EXISTS "${swPosixCompilerDir}/llvm-ar")
				set(swPosixAr "${swPosixCompilerDir}/llvm-ar")

				if(EXISTS "${swPosixCompilerDir}/llvm-ranlib")
					set(swPosixRanlib "${swPosixCompilerDir}/llvm-ranlib")
				endif()
			endif()
		endif()

		if(swPosixAr)
			set(CMAKE_AR "${swPosixAr}" CACHE FILEPATH "정적 라이브러리 아카이버" FORCE)
			set(CMAKE_C_COMPILER_AR "${swPosixAr}" CACHE FILEPATH "" FORCE)
			set(CMAKE_CXX_COMPILER_AR "${swPosixAr}" CACHE FILEPATH "" FORCE)

			# 캐시만으로는 부족하다 — `project()` 가 같은 이름의 일반 변수를 최상위 스코프에 만들어
			# 두고, 일반 변수가 캐시를 가린다. Windows 에서 이것 때문에 한참 헤맸다(같은 날).
			set(CMAKE_AR "${swPosixAr}")
			set(CMAKE_C_COMPILER_AR "${swPosixAr}")
			set(CMAKE_CXX_COMPILER_AR "${swPosixAr}")

			if(swPosixRanlib)
				set(CMAKE_RANLIB "${swPosixRanlib}" CACHE FILEPATH "정적 라이브러리 인덱서" FORCE)
				set(CMAKE_C_COMPILER_RANLIB "${swPosixRanlib}" CACHE FILEPATH "" FORCE)
				set(CMAKE_CXX_COMPILER_RANLIB "${swPosixRanlib}" CACHE FILEPATH "" FORCE)
				set(CMAKE_RANLIB "${swPosixRanlib}")
				set(CMAKE_C_COMPILER_RANLIB "${swPosixRanlib}")
				set(CMAKE_CXX_COMPILER_RANLIB "${swPosixRanlib}")
			endif()

			# **변수만 고쳐서는 안 된다.** CMake 는 IPO 용 아카이브 명령을 `project()` 시점의
			# `CMAKE_<LANG>_COMPILER_AR` 로 **문자열에 구워 둔다**(Modules/Compiler/Clang.cmake).
			# 그래서 그때 NOTFOUND 였으면 나중에 변수를 고쳐도 생성된 규칙은 그대로
			# `"CMAKE_CXX_COMPILER_AR-NOTFOUND" qc ...` 다 — 실측으로 확인했다. 규칙을 다시 쓴다.
			# (Windows 쪽도 같은 이유로 sw_bindClangClWindowsTools 가 명령 문자열을 다시 쓴다.)
			foreach(swPosixLang IN ITEMS C CXX)
				set(CMAKE_${swPosixLang}_ARCHIVE_CREATE_IPO "\"${swPosixAr}\" qc <TARGET> <LINK_FLAGS> <OBJECTS>")
				set(CMAKE_${swPosixLang}_ARCHIVE_APPEND_IPO "\"${swPosixAr}\" q <TARGET> <LINK_FLAGS> <OBJECTS>")

				if(swPosixRanlib)
					set(CMAKE_${swPosixLang}_ARCHIVE_FINISH_IPO "\"${swPosixRanlib}\" <TARGET>")
				else()
					set(CMAKE_${swPosixLang}_ARCHIVE_FINISH_IPO "")
				endif()
			endforeach()

			message(STATUS "[FindPosixTools] CMAKE_AR=${CMAKE_AR}")
		else()
			# 치명적이지 않다 — 정적 라이브러리는 시스템 ar 로도 묶인다. 다만 LTO 는 못 켠다.
			message(STATUS "[FindPosixTools] llvm-ar not found — LTO will stay off (system ar keeps working)")
		endif()
	endif()
endmacro()
