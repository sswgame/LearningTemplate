# ==============================================================================
# @file cmake/Engine/UnbuiltSources.cmake
# @brief 이 구성이 일부러 짓지 않는 소스 목록 — 빼는 자리가 적고(sw_declare* · sw_exclude* · 플랫폼 폴더 규칙) 구성 끝에 파일로 쓴다
# ==============================================================================

# ------------------------------------------------------------------------------
# 이 구성이 일부러 짓지 않는 소스 — 빌드 트리에 쓰는 목록
#
# 그 게이트는 디스크의 소스와 compile_commands.json 을 맞춘다. 배포 구성은 에디터 · 핫 리로드 · 고르지 않은 RHI 백엔드를 **일부러**
# 짓지 않는데, 게이트는 그것을 CMake 와 따로 몰라 Shipping 트리에서 늘 졌다("compile_commands 에 없음" 95 개 — 그래서 Shipping 은
# `-L hostgpu` 만 돌려 왔다). 빼는 규칙은 여기 CMake 에 있으므로 빼는 자리가 직접 적고(`sw_declareUnbuiltSources` ·
# `sw_excludeUnbuiltSources`), 구성 끝에 `sw_writeUnbuiltSourceList` 가 빌드 트리에 목록을 쓴다 — 게이트는 그 목록만 읽는다.
# **다른 타겟으로 옮겨 짓는 것(키트 폴더 · RHI 모듈)은 적지 않는다** — 지어지지 않으면 게이트가 잡아야 한다.
# ------------------------------------------------------------------------------
# 게이트는 빌드 트리 기준 같은 상대 경로(`generated/sw/config/UnbuiltSources.txt`)를 읽는다.
set(SW_UNBUILT_SOURCE_LIST "${CMAKE_BINARY_DIR}/generated/sw/config/UnbuiltSources.txt")

# 배포 구성이 짓지 않는 개발 전용 소스 — 기능 폴더의 단위 테스트(`Tests/`)와 개발 전용 단위 폴더(`Dev/` — 호스트의 개발 계층 · 서버 개발 조종 · 에디터 모듈 개발 경로).
# 파일 이름 꼬리로 빼지 않는다: 개발 명령 · 캐시 리로드 · 온라인 개발 대체물은 기능 파일에 두고 등록 매크로가 배포에서 등록하지 않는다(`docs/agents/BuildConfigSwitch.md`).
set(SW_DEV_ONLY_SOURCE_PATTERNS "/Tests/" "/Dev/")

# 전용 서버가 짓지 않는 클라이언트 전용 소스 — GameFramework · 게임 모듈 안의 `Client/` 폴더(HUD · 마커 · 로딩 화면 · UI 시연). 키트의 `Client/` 는
# 따로 짓는 모듈(`GF_Client_<X>`, 대상 Client)이라 여기 걸리지 않는다. 공유 코드는 그 헤더를 include 하지 않는다(린트 `server-ui`, 서버 구성 게이트 `serverbuild`).
# 서버가 그 컴포넌트를 씬에서 만나면 MissingComponent 가 아니라 조용히 건너뛴다(`Config/Engine/ClientOnlyTypes.generated.json` — 생성기 `client-only-types`).
set(SW_CLIENT_ONLY_SOURCE_PATTERNS "/Client/")

# 이 구성이 짓지 않는 소스를 적는다(절대 경로). 헤더도 받아 두지만 목록에는 `.c` · `.cpp` 만 쓴다.
function(sw_declareUnbuiltSources)
	set_property(GLOBAL APPEND PROPERTY SW_UNBUILT_SOURCES ${ARGN})
endfunction()

# 폴더(ARGN, 절대 경로) 아래의 .c · .cpp 를 전부 "이 구성이 짓지 않는 소스" 로 적는다 — 통째로 빼는 폴더(고르지 않은 게임 팩 ·
# 끈 GameFramework)에 쓴다.
function(sw_declareUnbuiltDirectory)
	foreach(directory IN LISTS ARGN)
		file(GLOB_RECURSE listSource CONFIGURE_DEPENDS "${directory}/*.c" "${directory}/*.cpp")
		sw_declareUnbuiltSources(${listSource})
	endforeach()
endfunction()

# 목록 변수 LIST_VAR 에서 정규식(ARGN)에 맞는 소스를 빼고, 뺀 것을 "이 구성이 짓지 않는 소스" 로 적는다.
# 빼기와 적기가 한 호출이라 한쪽만 하는 일이 없다. 다른 타겟으로 옮겨 짓는 것에는 쓰지 않는다(그때는 `list(FILTER)`).
function(sw_excludeUnbuiltSources LIST_VAR)
	set(listKept ${${LIST_VAR}})
	foreach(pattern IN LISTS ARGN)
		list(FILTER listKept EXCLUDE REGEX "${pattern}")
	endforeach()

	set(listRemoved ${${LIST_VAR}})
	if(listKept)
		list(REMOVE_ITEM listRemoved ${listKept})
	endif()
	sw_declareUnbuiltSources(${listRemoved})

	set(${LIST_VAR} "${listKept}" PARENT_SCOPE)
endfunction()

# 목록 LIST_VAR 에서 접두(ARGN, 예: `OnlineLoadBot/`)로 시작하는 헤더를 `#include "…"` 하는 소스를 빼고, 뺀 것을 "짓지 않는 소스" 로 적는다.
# 파일 이름 정규식을 손으로 적는 대신 소스가 스스로 말하는 의존(include)을 읽는다 — 도구 타깃이 없는 구성에서 그 도구를 쓰는 테스트가 빠진다.
# 한 단계만 본다(그 헤더를 간접으로 끄는 파일은 보지 않는다).
function(sw_excludeSourcesIncludingPrefix LIST_VAR)
	set(listKept "")
	set(listDropped "")
	foreach(source IN LISTS ${LIST_VAR})
		set(bDrop OFF)
		if(source MATCHES "\\.(cpp|h)$")
			file(STRINGS "${source}" includeLines REGEX "^[ \t]*#[ \t]*include[ \t]+\"")
			foreach(prefix IN LISTS ARGN)
				string(FIND "${includeLines}" "\"${prefix}" hit)
				if(NOT hit EQUAL -1)
					set(bDrop ON)
				endif()
			endforeach()
		endif()
		if(bDrop)
			list(APPEND listDropped "${source}")
		else()
			list(APPEND listKept "${source}")
		endif()
	endforeach()
	if(listDropped)
		sw_declareUnbuiltSources(${listDropped})
	endif()
	set(${LIST_VAR} ${listKept} PARENT_SCOPE)
endfunction()

# 플랫폼 폴더 규칙 — 저장소 기준 경로에 `Windows/` · `Linux/` · `Android/` · `Web/` 이 든 소스는 그 플랫폼에서만, `Posix/` 는 Windows 가 아닌 곳에서만 짓는다.
# 고르지 않은 것은 목록에서 빼고 "이 구성이 짓지 않는 소스" 로 적는다. (경로를 저장소 기준으로 보는 이유: 저장소를 담은 상위 폴더 이름에 걸리지 않게.)
# Core · Engine · DevTools 가 쓴다 — 플랫폼 폴더 .cpp 에 파일 전체 플랫폼 `#if` 를 두지 않는다.
function(sw_filterPlatformSources LIST_VAR)
	set(listKept "")
	set(listDropped "")
	foreach(source IN LISTS ${LIST_VAR})
		file(RELATIVE_PATH relativePath "${CMAKE_SOURCE_DIR}" "${source}")
		set(isBuilt ON)
		if(relativePath MATCHES "(^|/)Windows/" AND NOT WIN32)
			set(isBuilt OFF)
		elseif(relativePath MATCHES "(^|/)Linux/" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
			set(isBuilt OFF)
		# Linux 커널 · bionic 이 함께 가진 것(epoll · eventfd · inotify) — 데스크톱 Linux 와 Android 가 짓는다(언리얼 Android 가 Unix · Linux 코드를 나눠 쓰는 것과 같다). Linux/ 는 데스크톱(X11 · Wayland · ALSA)만이다.
		elseif(relativePath MATCHES "(^|/)LinuxKernel/" AND NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Android)$")
			set(isBuilt OFF)
		elseif(relativePath MATCHES "(^|/)Android/" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
			set(isBuilt OFF)
		elseif(relativePath MATCHES "(^|/)Web/" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
			set(isBuilt OFF)
		elseif(relativePath MATCHES "(^|/)Posix/" AND WIN32)
			set(isBuilt OFF)
		endif()
		if(isBuilt)
			list(APPEND listKept "${source}")
		else()
			list(APPEND listDropped "${source}")
		endif()
	endforeach()
	if(listDropped)
		sw_declareUnbuiltSources(${listDropped})
	endif()
	set(${LIST_VAR} ${listKept} PARENT_SCOPE)
endfunction()

# 구성 끝(모든 소스 타겟을 만든 뒤)에 한 번 — 적어 둔 것을 빌드 트리에 쓴다. 내용이 같으면 파일을 다시 쓰지 않는다.
function(sw_writeUnbuiltSourceList)
	get_property(listSource GLOBAL PROPERTY SW_UNBUILT_SOURCES)
	list(FILTER listSource INCLUDE REGEX "\\.(c|cpp)$")
	set(listRelative "")
	foreach(source IN LISTS listSource)
		file(RELATIVE_PATH relativePath "${CMAKE_SOURCE_DIR}" "${source}")
		list(APPEND listRelative "${relativePath}")
	endforeach()
	if(listRelative)
		list(REMOVE_DUPLICATES listRelative)
		list(SORT listRelative)
	endif()

	string(JOIN "\n" content ${listRelative})
	file(CONFIGURE OUTPUT "${SW_UNBUILT_SOURCE_LIST}" CONTENT "${content}\n" @ONLY)
	list(LENGTH listRelative unbuiltCount)
	message(STATUS "[Build] Sources this configuration does not build: ${unbuiltCount} (${SW_UNBUILT_SOURCE_LIST})")
endfunction()
