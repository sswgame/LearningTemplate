# ==============================================================================
# @file cmake/Toolchain/Vcpkg/VcpkgInstallPolicy.cmake
# @brief vcpkg 설치 폴더 정책 — 이 구성이 설치 폴더에 쓸 수 있는지(읽기 전용이면 그 까닭, 빈 값 = 설치해도 된다) · 설치 계획(dry-run) 읽기 · 오버레이 포트 해시
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

# `vcpkg install --dry-run` 출력(영어로 고정 — VSLANG=1033)에서 지울 · 지을(다시 지을 포함) 포트를 읽는다.
# 절마다 머리 줄("The following packages will be removed:" 등) 아래에 포트가 한 줄씩 온다. 게이트(Vcpkg.cmake 1b)가 부른다.
function(sw_vcpkg_parse_install_plan OUTPUT OUT_REMOVE OUT_BUILD)
	string(REPLACE "\r" "" output "${OUTPUT}")
	string(REPLACE ";" "," output "${output}")
	string(REPLACE "\n" ";" lines "${output}")
	set(section "")
	set(remove "")
	set(build "")

	foreach(line IN LISTS lines)
		if(line MATCHES "^The following packages will be removed")
			set(section "remove")
		elseif(line MATCHES "^The following packages will be (built and installed|rebuilt)")
			set(section "build")
		elseif(line MATCHES "^The following packages")
			set(section "")
		elseif(line MATCHES "^ +[*]? *([^ :]+):")
			if(section STREQUAL "remove")
				list(APPEND remove "${CMAKE_MATCH_1}")
			elseif(section STREQUAL "build")
				list(APPEND build "${CMAKE_MATCH_1}")
			endif()
		elseif(NOT line MATCHES "^ ")
			set(section "")
		endif()
	endforeach()

	set(${OUT_REMOVE} "${remove}" PARENT_SCOPE)
	set(${OUT_BUILD} "${build}" PARENT_SCOPE)
endfunction()

# 줄끝(CRLF/LF)을 같게 보고 파일 내용의 SHA256 을 구한다.
# 스탬프는 main 체크아웃과 워크트리가 나눠 쓴다. 두 체크아웃의 줄끝이 다르면(core.autocrlf — main 은 LF, 워크트리는 CRLF 로 나온 적이 있다)
# 내용이 같아도 해시가 달라 configure 마다 스탬프가 번갈아 덮이고, 그때마다 바뀐 것 없이 설치가 돌았다.
function(sw_vcpkg_hash_file_normalized FILE_PATH OUT_VAR)
	file(READ "${FILE_PATH}" content)
	string(REPLACE "\r\n" "\n" content "${content}")
	string(SHA256 hash "${content}")
	set(${OUT_VAR} "${hash}" PARENT_SCOPE)
endfunction()

# 오버레이 포트(SOURCE_DIR/ThirdParty/*/vcpkg-port) 폴더의 모든 파일 — 이름과 내용 — 의 SHA256. vcpkg 도 포트 폴더의 모든 파일을 포트 ABI 해시에 넣는다.
# 확장자로 고르면(portfile · vcpkg.json · *.patch 만 셌다) *.diff · vcpkg-cmake-wrapper.cmake · CMakeLists.txt · usage 를 고쳐도 스탬프가 같아 설치를 건너뛰었다.
function(sw_vcpkg_hash_overlay_ports SOURCE_DIR OUT_VAR)
	file(GLOB list_port_folder "${SOURCE_DIR}/ThirdParty/*/vcpkg-port")
	set(entries "")
	foreach(port_folder IN LISTS list_port_folder)
		file(GLOB_RECURSE list_file LIST_DIRECTORIES false RELATIVE "${SOURCE_DIR}" "${port_folder}/*")
		list(SORT list_file)
		foreach(file IN LISTS list_file)
			sw_vcpkg_hash_file_normalized("${SOURCE_DIR}/${file}" content_hash)
			string(APPEND entries "${file}=${content_hash}\n")
		endforeach()
	endforeach()
	string(SHA256 hash "${entries}")
	set(${OUT_VAR} "${hash}" PARENT_SCOPE)
endfunction()
