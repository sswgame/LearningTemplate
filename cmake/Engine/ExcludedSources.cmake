# ==============================================================================
# @file cmake/Engine/ExcludedSources.cmake
# @brief 이 구성이 일부러 짓지 않는 소스 목록 — 빼는 자리가 적고(sw_declare* · sw_exclude* · 플랫폼 폴더 규칙) 구성 끝에 파일로 쓴다
# ==============================================================================

# ------------------------------------------------------------------------------
# 이 구성이 일부러 짓지 않는 소스 — 빌드 트리에 쓰는 목록
#
# 그 게이트는 디스크의 소스와 compile_commands.json 을 맞춘다. 배포 구성은 에디터 · 핫 리로드 · 고르지 않은 RHI 백엔드를 **일부러**
# 짓지 않는데, 게이트는 그것을 CMake 와 따로 몰라 Shipping 트리에서 늘 졌다("compile_commands 에 없음" 95 개 — 그래서 Shipping 은
# `-L hostgpu` 만 돌려 왔다). 빼는 규칙은 여기 CMake 에 있으므로 빼는 자리가 직접 적고(`sw_declare_excluded_sources` ·
# `sw_remove_excluded_sources`), 구성 끝에 `sw_write_excluded_source_list` 가 빌드 트리에 목록을 쓴다 — 게이트는 그 목록만 읽는다.
# **다른 타겟으로 옮겨 짓는 것(키트 폴더 · RHI 모듈)은 적지 않는다** — 지어지지 않으면 게이트가 잡아야 한다.
# ------------------------------------------------------------------------------
# 게이트는 빌드 트리 기준 같은 상대 경로(`generated/sw/config/ExcludedSources.txt`)를 읽는다.
set(SW_EXCLUDED_SOURCE_LIST "${CMAKE_BINARY_DIR}/generated/sw/config/ExcludedSources.txt")

# 배포 구성이 짓지 않는 개발 전용 소스 — 기능 폴더의 단위 테스트(`Tests/`)와 개발 전용 단위 폴더(`Dev/` — 호스트의 개발 계층 · 서버 개발 조종 · 에디터 모듈 개발 경로).
# 파일 이름 꼬리로 빼지 않는다: 개발 명령 · 캐시 리로드 · 온라인 개발 대체물은 기능 파일에 두고 등록 매크로가 배포에서 등록하지 않는다(`docs/agents/BuildConfigSwitch.md`).
#
# `Dev/` 는 아무 폴더에나 붙는 패턴이 아니라 표(`Config/Rules/Layers/DevFolders.json`)에 적은 자리만이다 — 예전 `/Dev/` 한 줄은 어디든 `Dev/` 를 만들면 배포에서 빠지는
# 편의라 개발 기능이 DevTools 대신 엔진 곳곳에 쌓였다(린트가 뒤에서 막았다). 표 밖 `Source/` 의 `Dev/` 폴더는 configure 가 멈춘다. 언리얼이 개발 코드를 Developer 모듈 단위로만 빼는 자리다.
set(swDevFolderTable "${CMAKE_SOURCE_DIR}/Config/Rules/Layers/DevFolders.json")
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${swDevFolderTable}")
file(READ "${swDevFolderTable}" swDevFolderJson)
string(JSON swDevFolderCount ERROR_VARIABLE swDevFolderError LENGTH "${swDevFolderJson}" _devFolderAllowed)
if(swDevFolderError)
	message(FATAL_ERROR "${swDevFolderTable}: _devFolderAllowed 를 읽지 못했다 (${swDevFolderError})")
endif()
set(SW_DEV_ONLY_SOURCE_PATTERNS "/Tests/")
set(swDevFolderPatterns "")
math(EXPR swDevFolderLast "${swDevFolderCount} - 1")
foreach(swDevFolderIndex RANGE ${swDevFolderLast})
	string(JSON swDevFolderPrefix MEMBER "${swDevFolderJson}" _devFolderAllowed ${swDevFolderIndex})
	if(NOT swDevFolderPrefix MATCHES "^Source/[A-Za-z0-9_/]+/$" OR NOT IS_DIRECTORY "${CMAKE_SOURCE_DIR}/${swDevFolderPrefix}")
		message(FATAL_ERROR "${swDevFolderTable}: '${swDevFolderPrefix}' 는 있는 `Source/` 폴더(끝에 /)가 아니다 — 옮겼으면 줄을 지우세요")
	endif()
	if(swDevFolderPrefix MATCHES "/Dev/$")
		list(APPEND swDevFolderPatterns "/${swDevFolderPrefix}")
	else()
		list(APPEND swDevFolderPatterns "/${swDevFolderPrefix}(.*/)?Dev/")
	endif()
endforeach()
list(APPEND SW_DEV_ONLY_SOURCE_PATTERNS ${swDevFolderPatterns})

# 표 밖 `Dev/` 폴더 — 경로 비교는 저장소 기준 앞에 `/` 를 붙여 패턴과 같은 모양으로 한다(저장소를 담은 상위 폴더 이름에 걸리지 않게).
set(swSourceFiles ${SW_SOURCE_TREE_FILES})
list(FILTER swSourceFiles INCLUDE REGEX "/Dev/")
set(swStrayDevFolders "")
foreach(swSourceFile IN LISTS swSourceFiles)
	file(RELATIVE_PATH swRelativePath "${CMAKE_SOURCE_DIR}" "${swSourceFile}")
	set(bAllowed OFF)
	foreach(swDevFolderPattern IN LISTS swDevFolderPatterns)
		if("/${swRelativePath}" MATCHES "^${swDevFolderPattern}")
			set(bAllowed ON)
		endif()
	endforeach()
	if(NOT bAllowed)
		string(REGEX REPLACE "/Dev/.*$" "/Dev/" swStrayFolder "${swRelativePath}")
		list(APPEND swStrayDevFolders "${swStrayFolder}")
	endif()
endforeach()
if(swStrayDevFolders)
	list(REMOVE_DUPLICATES swStrayDevFolders)
	message(FATAL_ERROR "개발 전용 `Dev/` 폴더가 표 밖에 있다: ${swStrayDevFolders} — 개발 기능은 Source/DevTools/ 로 옮기세요(엔진이 확장점을 내고 DevTools 가 붙는다). "
		"DevTools 가 볼 수 없는 모듈 안이면 ${swDevFolderTable} 의 _devFolderAllowed 에 까닭과 함께 적습니다.")
endif()

# 전용 서버가 짓지 않는 클라이언트 전용 소스 — GameFramework · 게임 모듈 안의 `Client/` 폴더(HUD · 마커 · 로딩 화면 · UI 시연). 키트의 `Client/` 는
# 따로 짓는 모듈(`GF_Client_<X>`, 대상 Client)이라 여기 걸리지 않는다. 공유 코드는 그 헤더를 include 하지 않는다 — 서버 구성 게이트 `serverbuild` 가 이름이 `Client` 인 모든 폴더의 헤더를 막는다(`TargetLayers.toml` 의 `[gatefolder]`).
# 서버가 그 컴포넌트를 씬에서 만나면 MissingComponent 가 아니라 조용히 건너뛴다(`Config/Engine/ClientOnlyTypes.generated.json` — 생성기 `client-only-types`).
set(SW_CLIENT_ONLY_SOURCE_PATTERNS "/Client/")

# 클라이언트 구성이 짓지 않는 서버 전용 소스 — 위의 반대쪽. Engine · GameFramework · DevTools · 게임 모듈 안의 `Server/` 폴더(전용 서버 운영 `Engine/Server` · 서버 비밀 `Engine/Config/Server` ·
# 온라인 서버 조립 `GameFramework/Runtime/Online/Server` · 기능마다의 서버 구현 `Online/<기능>/Server` — 서비스 호스트 · 메모리 저장소 · 캐시 · 버스). 키트의 `Server/` 는 따로 짓는 모듈(`GF_Server_<X>`, 대상 Server)이고 전용 서버 실행 파일 `Source/Server` 는
# 제 CMake 가 Client 에서 빼므로 여기 걸리지 않는다. 공유 코드는 그 헤더를 include 하지 않는다 — 클라이언트 구성 게이트 `clientbuild` 가 이름이 `Server` 인 모든 폴더의
# 헤더를 막는다(`TargetLayers.toml` 의 `[gatefolder]`). 세션이 서버를 띄우는 일은 창구(`IOnlineLocalServerFactory`)로 부른다. 언리얼 `WITH_SERVER_CODE` 와 서버 전용 모듈 자리다(기억).
set(SW_SERVER_ONLY_SOURCE_PATTERNS "/Server/")

# 이 구성이 짓지 않는 소스를 적는다(절대 경로). 헤더도 받아 두지만 목록에는 `.c` · `.cpp` 만 쓴다.
function(sw_declare_excluded_sources)
	set_property(GLOBAL APPEND PROPERTY SW_EXCLUDED_SOURCES ${ARGN})
endfunction()

# 폴더(ARGN, 절대 경로) 아래의 .c · .cpp 를 전부 "이 구성이 짓지 않는 소스" 로 적는다 — 통째로 빼는 폴더(고르지 않은 게임 팩 ·
# 끈 GameFramework)에 쓴다.
function(sw_declare_excluded_directory)
	foreach(directory IN LISTS ARGN)
		sw_glob_source_tree(listSource RECURSE "${directory}/*.c" "${directory}/*.cpp")
		sw_declare_excluded_sources(${listSource})
	endforeach()
endfunction()

# 목록 변수 LIST_VAR 에서 정규식(ARGN)에 맞는 소스를 빼고, 뺀 것을 "이 구성이 짓지 않는 소스" 로 적는다.
# 빼기와 적기가 한 호출이라 한쪽만 하는 일이 없다. 다른 타겟으로 옮겨 짓는 것에는 쓰지 않는다(그때는 `list(FILTER)`).
function(sw_remove_excluded_sources LIST_VAR)
	set(listKept ${${LIST_VAR}})
	foreach(pattern IN LISTS ARGN)
		list(FILTER listKept EXCLUDE REGEX "${pattern}")
	endforeach()

	set(listRemoved ${${LIST_VAR}})
	if(listKept)
		list(REMOVE_ITEM listRemoved ${listKept})
	endif()
	sw_declare_excluded_sources(${listRemoved})

	set(${LIST_VAR} "${listKept}" PARENT_SCOPE)
endfunction()

# 목록 LIST_VAR 에서 접두(ARGN, 예: `OnlineLoadBot/`)로 시작하는 헤더를 `#include "…"` 하는 소스를 빼고, 뺀 것을 "짓지 않는 소스" 로 적는다.
# 파일 이름 정규식을 손으로 적는 대신 소스가 스스로 말하는 의존(include)을 읽는다 — 도구 타깃이 없는 구성에서 그 도구를 쓰는 테스트가 빠진다.
# 한 단계만 본다(그 헤더를 간접으로 끄는 파일은 보지 않는다).
function(sw_exclude_sources_including_prefix LIST_VAR)
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
		sw_declare_excluded_sources(${listDropped})
	endif()
	set(${LIST_VAR} ${listKept} PARENT_SCOPE)
endfunction()

# 플랫폼 폴더 규칙 — 저장소 기준 경로에 `Windows/` · `Linux/` · `Android/` · `Web/` 이 든 소스는 그 플랫폼에서만, `Posix/` 는 Windows 가 아닌 곳에서만 짓는다.
# 고르지 않은 것은 목록에서 빼고 "이 구성이 짓지 않는 소스" 로 적는다. (경로를 저장소 기준으로 보는 이유: 저장소를 담은 상위 폴더 이름에 걸리지 않게.)
# Core · Engine · DevTools 가 쓴다 — 플랫폼 폴더 .cpp 에 파일 전체 플랫폼 `#if` 를 두지 않는다.
function(sw_filter_platform_sources LIST_VAR)
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
		sw_declare_excluded_sources(${listDropped})
	endif()
	set(${LIST_VAR} ${listKept} PARENT_SCOPE)
endfunction()

# 구성 끝(모든 소스 타겟을 만든 뒤)에 한 번 — 적어 둔 것을 빌드 트리에 쓴다. 내용이 같으면 파일을 다시 쓰지 않는다.
function(sw_write_excluded_source_list)
	get_property(listSource GLOBAL PROPERTY SW_EXCLUDED_SOURCES)
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
	file(CONFIGURE OUTPUT "${SW_EXCLUDED_SOURCE_LIST}" CONTENT "${content}\n" @ONLY)
	list(LENGTH listRelative excludedCount)
	message(STATUS "[Build] Sources this configuration does not build: ${excludedCount} (${SW_EXCLUDED_SOURCE_LIST})")
endfunction()
