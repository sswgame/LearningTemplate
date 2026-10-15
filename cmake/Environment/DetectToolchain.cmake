# ==============================================================================
# @file cmake/Environment/DetectToolchain.cmake
# @brief Scripts/setup/SetupEnvironment.py를 실행하여 개발 환경을 탐색하고,
# 생성된 Config/Environment/toolchain_config.json으로부터 LLVM/vcpkg/SDK 경로를 CMake에 주입
#
# [동작 파이프라인]:
# 1. Python 서브프로세스를 통해 `SetupEnvironment.py`를 실행하여 툴체인 및 패키지 매니저 경로를 자동 탐지/다운로드.
# 2. `toolchain_config.json`의 키(llvm_path, vcpkg_root, windows_sdk_dir, msvc_tools_dir, ninja_path 등)를 파싱.
# 3. `CMAKE_TOOLCHAIN_FILE`, `CMAKE_C_COMPILER`, `CMAKE_CXX_COMPILER`, `CMAKE_AR` 등의 빌드 도구 변수를 강제 바인딩(FORCE).
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) Python 헬퍼 로드 및 자동 부트스트랩 환경 변수 전달
# ------------------------------------------------------------------------------
include("${CMAKE_CURRENT_LIST_DIR}/PythonUtils.cmake")

if(SW_VCPKG_AUTO_BOOTSTRAP)
	set(ENV{SW_VCPKG_AUTO_BOOTSTRAP} "1")
endif()

if(SW_LLVM_AUTO_BOOTSTRAP)
	set(ENV{SW_LLVM_AUTO_BOOTSTRAP} "1")
endif()

# SetupEnvironment.py 실행 (설정 파일 변경 및 도구 경로 갱신을 항상 정확하게 반영).
# 필수다 — 실패하면 아래가 낡은 toolchain_config.json 이나 빈 값으로 지나가 다른 컴파일러를 집는다.
sw_executePythonScript(
	"${SW_SCRIPT_SETUP_ENVIRONMENT}"
	REQUIRED
)

# ------------------------------------------------------------------------------
# 2) toolchain_config.json 파싱 및 project() 호출 전 빌드 환경 주입
# ------------------------------------------------------------------------------
# toolchain_config.json 을 CMake 가 다시 파싱하지 않는다. 그 파일을 쓰는 쪽이 파이썬이니
# 읽는 모양도 파이썬이 준다 — CMake 가 키마다 `string(JSON ... GET)` 블록을 두면 같은 키를 곳마다
# 다른 철자(상수 · 리터럴)로 적게 된다.
# 키는 SetupEnvironment 가 늘 모두 쓴다. 값이 빈 것은 "이 호스트에 그 도구가 없다" 이다(리눅스의 Windows SDK 등) —
# 그래서 아래는 `if(X AND EXISTS ...)` 로 묻는다. 파일 자체가 없으면 구성을 세운다.
set(SW_GENERATED_TOOLCHAIN_VARS "${CMAKE_BINARY_DIR}/generated/sw/config/ToolchainVars.cmake")
sw_executePythonScript("Scripts/generate/GenerateToolchainCMake.py"
	ARGS "${SW_GENERATED_TOOLCHAIN_VARS}"
	REQUIRED
)
include("${SW_GENERATED_TOOLCHAIN_VARS}")

if(NOT SW_TOOLCHAIN_CONFIG_FOUND)
	message(FATAL_ERROR "[DetectToolchain] ${SW_DIR_CONFIG_ENV}/${SW_FILE_TOOLCHAIN_CONFIG} is missing after ${SW_SCRIPT_SETUP_ENVIRONMENT} ran")
endif()

if(SW_TOOLCHAIN_CONFIG_FOUND)
	# 1) vcpkg 툴체인 및 루트 주입
	if(SW_TOOLCHAIN_VCPKG_ROOT AND EXISTS "${SW_TOOLCHAIN_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
		set(sw_vcpkg_root "${SW_TOOLCHAIN_VCPKG_ROOT}" CACHE PATH "vcpkg 루트 디렉터리" FORCE)
		set(CMAKE_TOOLCHAIN_FILE "${SW_TOOLCHAIN_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg 툴체인" FORCE)
		set(ENV{VCPKG_ROOT} "${SW_TOOLCHAIN_VCPKG_ROOT}")
		message(STATUS "[DetectToolchain] Using vcpkg: ${SW_TOOLCHAIN_VCPKG_ROOT}")
	endif()

	# 2) LLVM / Clang 컴파일러 및 링커 설정
	if(SW_TOOLCHAIN_LLVM_PATH AND EXISTS "${SW_TOOLCHAIN_LLVM_PATH}/bin")
		set(ENV{LLVM_DIR} "${SW_TOOLCHAIN_LLVM_PATH}")
		set(ENV{LLVM_ROOT} "${SW_TOOLCHAIN_LLVM_PATH}")
		set(ENV{LLVM_HOME} "${SW_TOOLCHAIN_LLVM_PATH}")

		sw_prependEnvPath("${SW_TOOLCHAIN_LLVM_PATH}/bin")

		# project() 전이다 — 호스트 판정(CMAKE_HOST_WIN32)으로 고른다.
		if(CMAKE_HOST_WIN32 AND EXISTS "${SW_TOOLCHAIN_LLVM_PATH}/bin/clang-cl.exe")
			set(CMAKE_C_COMPILER "${SW_TOOLCHAIN_LLVM_PATH}/bin/clang-cl.exe" CACHE FILEPATH "C 컴파일러" FORCE)
			set(CMAKE_CXX_COMPILER "${SW_TOOLCHAIN_LLVM_PATH}/bin/clang-cl.exe" CACHE FILEPATH "CXX 컴파일러" FORCE)

			if(EXISTS "${SW_TOOLCHAIN_LLVM_PATH}/bin/lld-link.exe")
				set(CMAKE_LINKER "${SW_TOOLCHAIN_LLVM_PATH}/bin/lld-link.exe" CACHE FILEPATH "링커" FORCE)
			endif()

			if(EXISTS "${SW_TOOLCHAIN_LLVM_PATH}/bin/llvm-rc.exe")
				set(CMAKE_RC_COMPILER "${SW_TOOLCHAIN_LLVM_PATH}/bin/llvm-rc.exe" CACHE FILEPATH "RC 컴파일러" FORCE)
			endif()

			# 정적 아카이브(lib.exe/llvm-lib.exe) 및 매니페스트 도구(mt.exe/llvm-mt.exe) 탐색
			include("${CMAKE_CURRENT_LIST_DIR}/ToolchainBinaries.cmake")
			include("${CMAKE_CURRENT_LIST_DIR}/FindWindowsTools.cmake")
			sw_findWindowsArchiveAndMt(swAr swMt)

			# 아카이버 · 매니페스트 도구가 없으면 정적 라이브러리 · 링크가 나중에 선다 — 지금 세운다.
			if(NOT swAr)
				message(FATAL_ERROR "[DetectToolchain] llvm-lib / lib.exe not found (LLVM ${SW_TOOLCHAIN_LLVM_PATH}, MSVC '${SW_TOOLCHAIN_MSVC_TOOLS_DIR}')")
			endif()

			if(NOT swMt)
				message(FATAL_ERROR "[DetectToolchain] llvm-mt / mt.exe not found (Windows SDK '${SW_TOOLCHAIN_WINDOWS_SDK_DIR}' ${SW_TOOLCHAIN_WINDOWS_SDK_VERSION})")
			endif()

			set(CMAKE_AR "${swAr}" CACHE FILEPATH "정적 라이브러리 아카이버" FORCE)
			set(CMAKE_C_COMPILER_AR "${swAr}" CACHE FILEPATH "" FORCE)
			set(CMAKE_CXX_COMPILER_AR "${swAr}" CACHE FILEPATH "" FORCE)
			set(CMAKE_MT "${swMt}" CACHE FILEPATH "매니페스트 도구" FORCE)
			message(STATUS "[DetectToolchain] CMAKE_AR=${CMAKE_AR} CMAKE_MT=${CMAKE_MT}")

			message(STATUS "[DetectToolchain] Using clang-cl from ${SW_TOOLCHAIN_LLVM_PATH}/bin")
		elseif(EXISTS "${SW_TOOLCHAIN_LLVM_PATH}/bin/clang")
			set(CMAKE_C_COMPILER "${SW_TOOLCHAIN_LLVM_PATH}/bin/clang" CACHE FILEPATH "C 컴파일러" FORCE)
			set(CMAKE_CXX_COMPILER "${SW_TOOLCHAIN_LLVM_PATH}/bin/clang++" CACHE FILEPATH "CXX 컴파일러" FORCE)
			message(STATUS "[DetectToolchain] Using clang from ${SW_TOOLCHAIN_LLVM_PATH}/bin")
		endif()
	endif()

	if(SW_TOOLCHAIN_NINJA_PATH AND EXISTS "${SW_TOOLCHAIN_NINJA_PATH}")
		set(CMAKE_MAKE_PROGRAM "${SW_TOOLCHAIN_NINJA_PATH}" CACHE FILEPATH "Ninja" FORCE)
		get_filename_component(swNinjaDir "${SW_TOOLCHAIN_NINJA_PATH}" DIRECTORY)

		sw_prependEnvPath("${swNinjaDir}")

		message(STATUS "[DetectToolchain] Using Ninja: ${SW_TOOLCHAIN_NINJA_PATH}")
	endif()
endif()

