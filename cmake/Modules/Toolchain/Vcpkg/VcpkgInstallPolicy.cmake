# ==============================================================================
# @file cmake/Modules/Toolchain/Vcpkg/VcpkgInstallPolicy.cmake
# @brief 이 구성이 vcpkg 설치 폴더에 쓸 수 있는지 — 읽기 전용이면 그 까닭을 돌려준다(빈 값 = 설치해도 된다)
#
# 2026-11-10: 워크트리 구성에 `-DVCPKG_INSTALLED_DIR=<본 트리>/build/vcpkg_installed -DVCPKG_MANIFEST_INSTALL=OFF` 를 주었는데
# 게이트가 VCPKG_MANIFEST_INSTALL 을 자기 판단으로 FORCE 해 덮고, 워크트리의 vcpkg 가 본 트리 설치(포트 49 → 10)를 지웠다.
# 그래서 두 경우는 판단 없이 읽기 전용이다 — 사용자가 끈 설치, 그리고 구성하는 소스 트리 밖의 설치 폴더(남의 트리는 빌려 읽기만 한다).
# 게이트(Vcpkg.cmake 5 절)는 읽기 전용이면 설치 · 스탬프 쓰기를 하지 않고, 트리가 없거나 강제 설치를 부탁받으면 멈춘다.
# `cmake -P` 로도 부를 수 있게 함수만 둔다(Scripts/setup/tests/test_vcpkg_install_policy.py).
# ==============================================================================
include_guard(GLOBAL)

# SOURCE_DIR: 구성하는 소스 트리. INSTALLED_DIR: VCPKG_INSTALLED_DIR. USER_INSTALL: 사용자가 준 VCPKG_MANIFEST_INSTALL(없으면 빈 값).
function(sw_vcpkg_install_read_only_reason SOURCE_DIR INSTALLED_DIR USER_INSTALL OUT_REASON)
	set(reason "")

	if(NOT USER_INSTALL STREQUAL "" AND NOT USER_INSTALL)
		set(reason "VCPKG_MANIFEST_INSTALL=${USER_INSTALL} was given")
	else()
		# 접합점 · 심볼릭 링크로 소스 트리 안을 가리키는 척하는 경로를 풀고, Windows 는 대소문자를 가리지 않는다.
		file(REAL_PATH "${SOURCE_DIR}" source)
		file(REAL_PATH "${INSTALLED_DIR}" installed BASE_DIRECTORY "${SOURCE_DIR}")

		if(CMAKE_HOST_WIN32)
			string(TOLOWER "${source}" source)
			string(TOLOWER "${installed}" installed)
		endif()

		cmake_path(IS_PREFIX source "${installed}" NORMALIZE inside)

		if(NOT inside)
			set(reason "VCPKG_INSTALLED_DIR '${INSTALLED_DIR}' is outside the source tree '${SOURCE_DIR}'")
		endif()
	endif()

	set(${OUT_REASON} "${reason}" PARENT_SCOPE)
endfunction()
