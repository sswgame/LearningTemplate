# ==============================================================================
# @file cmake/Environment/DetectToolchain.cmake
# @brief Scripts/setup/Environment.py를 실행하여 개발 환경을 탐색하고,
# 생성된 Config/Environment/toolchain_config.json으로부터 LLVM/vcpkg/SDK 경로를 CMake에 주입
#
# [동작 파이프라인]:
# 1. Python 서브프로세스를 통해 `environment.py`를 실행하여 툴체인 및 패키지 매니저 경로를 자동 탐지/다운로드.
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

# environment.py 실행 (설정 파일 변경 및 도구 경로 갱신을 항상 정확하게 반영).
# 필수다 — 실패하면 아래가 낡은 toolchain_config.json 이나 빈 값으로 지나가 다른 컴파일러를 집는다.
# 같은 프로세스가 아래 2) 의 ToolchainVars.cmake 도 쓴다(`--cmake-vars` — 파이썬을 한 번 덜 띄운다).
set(SW_GENERATED_TOOLCHAIN_VARS "${CMAKE_BINARY_DIR}/generated/sw/config/ToolchainVars.cmake")
sw_execute_python_script(
	"${SW_SCRIPT_SETUP_ENVIRONMENT}"
	ARGS --cmake-vars "${SW_GENERATED_TOOLCHAIN_VARS}"
	REQUIRED
)

# ------------------------------------------------------------------------------
# 2) toolchain_config.json 파싱 및 project() 호출 전 빌드 환경 주입
# ------------------------------------------------------------------------------
# toolchain_config.json 을 CMake 가 다시 파싱하지 않는다. 그 파일을 쓰는 쪽이 파이썬이니
# 읽는 모양도 파이썬이 준다 — CMake 가 키마다 `string(JSON ... GET)` 블록을 두면 같은 키를 곳마다
# 다른 철자(상수 · 리터럴)로 적게 된다.
# 키는 Environment 가 늘 모두 쓴다. 값이 빈 것은 "이 호스트에 그 도구가 없다" 이다(리눅스의 Windows SDK 등) —
# 그래서 아래는 `if(X AND EXISTS ...)` 로 묻는다. 파일 자체가 없으면 구성을 세운다.
# 생성기는 `Scripts/generate/toolchain_cmake.py`(위 1) 의 Environment 가 부른다).
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

		sw_prepend_env_path("${SW_TOOLCHAIN_LLVM_PATH}/bin")

		# project() 전이다 — 호스트 판정(CMAKE_HOST_WIN32)으로 고른다.
		# 교차 빌드(웹 · Android)는 대상 툴체인(VCPKG_CHAINLOAD_TOOLCHAIN_FILE — emcc · NDK clang)이 컴파일러를 정한다. 여기서 호스트 clang-cl 을 강제로 묶으면
		# 대상 툴체인을 덮어 웹 · Android 구성이 Windows 컴파일러 · x64-windows 패키지로 섰다.
		if(VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
			message(STATUS "[Toolchain] cross build - compilers come from ${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}")
		elseif(CMAKE_HOST_WIN32 AND EXISTS "${SW_TOOLCHAIN_LLVM_PATH}/bin/clang-cl.exe")
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
			sw_find_windows_archive_and_mt(swAr swMt)

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
			# vcpkg 포트도 같은 clang 으로 짓는다 — 매니페스트 설치는 이 구성의 환경(CC/CXX)을 물려받는 자식 프로세스다. 비어 있으면 시스템 GCC 로 지어
			# DirectXTex 가 OpenMP 판으로 서고, 그것을 쓰는 이 clang(libomp 없음)의 구성이 `Could NOT find OpenMP_C` 로 선다(Windows 는 생성한 포트 툴체인 — VcpkgTriplets.cmake — 이 같은 일을 한다).
			set(ENV{CC} "${SW_TOOLCHAIN_LLVM_PATH}/bin/clang")
			set(ENV{CXX} "${SW_TOOLCHAIN_LLVM_PATH}/bin/clang++")
			message(STATUS "[DetectToolchain] Using clang from ${SW_TOOLCHAIN_LLVM_PATH}/bin")
		endif()
	endif()

	if(SW_TOOLCHAIN_NINJA_PATH AND EXISTS "${SW_TOOLCHAIN_NINJA_PATH}")
		set(CMAKE_MAKE_PROGRAM "${SW_TOOLCHAIN_NINJA_PATH}" CACHE FILEPATH "Ninja" FORCE)
		get_filename_component(swNinjaDir "${SW_TOOLCHAIN_NINJA_PATH}" DIRECTORY)

		sw_prepend_env_path("${swNinjaDir}")

		message(STATUS "[DetectToolchain] Using Ninja: ${SW_TOOLCHAIN_NINJA_PATH}")
	endif()
endif()

