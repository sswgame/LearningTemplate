# ==============================================================================
# @file cmake/Engine/UnbuiltSources.cmake
# @brief 이 구성이 일부러 짓지 않는 소스 목록 — 빼는 자리가 적고(sw_declare* · sw_exclude* · 플랫폼 폴더 규칙) 구성 끝에 CheckSourceGlob 이 읽는 파일로 쓴다
# ==============================================================================

# ------------------------------------------------------------------------------
# 이 구성이 일부러 짓지 않는 소스 — `Scripts/lint/gate/CheckSourceGlob.py` 가 빌드 트리에서 읽는 목록
#
# 그 게이트는 디스크의 소스와 compile_commands.json 을 맞춘다. 배포 구성은 에디터 · 핫 리로드 · 고르지 않은 RHI 백엔드를 **일부러**
# 짓지 않는데, 게이트는 그것을 CMake 와 따로 몰라 Shipping 트리에서 늘 졌다("compile_commands 에 없음" 95 개 — 그래서 Shipping 은
# `-L hostgpu` 만 돌려 왔다). 빼는 규칙은 여기 CMake 에 있으므로 빼는 자리가 직접 적고(`sw_declareUnbuiltSources` ·
# `sw_excludeUnbuiltSources`), 구성 끝에 `sw_writeUnbuiltSourceList` 가 빌드 트리에 목록을 쓴다 — 게이트는 그 목록만 읽는다.
# **다른 타겟으로 옮겨 짓는 것(키트 폴더 · RHI 모듈)은 적지 않는다** — 지어지지 않으면 게이트가 잡아야 한다.
# ------------------------------------------------------------------------------
# 게이트는 빌드 트리 기준 같은 상대 경로(`generated/sw/config/UnbuiltSources.txt`)를 읽는다.
set(SW_UNBUILT_SOURCE_LIST "${CMAKE_BINARY_DIR}/generated/sw/config/UnbuiltSources.txt")

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
# 한 단계만 본다(그 헤더를 간접으로 끄는 파일은 보지 않는다 — 간접까지 보는 것은 `sw_excludeSourcesIncludingUnbuiltEngineFolders`).
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

# 플랫폼 폴더 규칙 — 저장소 기준 경로에 `Windows/` 가 든 소스는 Windows 에서만, `Linux/` 는 리눅스에서만, `Posix/` 는 Windows 가 아닌 곳에서만 짓는다.
# 고르지 않은 것은 목록에서 빼고 "이 구성이 짓지 않는 소스" 로 적는다. (경로를 저장소 기준으로 보는 이유: 저장소를 담은 상위 폴더 이름에 걸리지 않게.)
# 지금은 Core 만 쓴다 — Engine 의 플랫폼 폴더 .cpp 는 파일 안 `#if` 로 비어 있는 채 모든 플랫폼에서 컴파일된다.
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

# 목록 LIST_VAR 에서 이 타깃이 짓지 않는 Engine 폴더(`SW_UNBUILT_ENGINE_FOLDERS`, cmake/Engine/BuildLayout.cmake)의 헤더를 include 하는 파일을 뺀다
# — 테스트 실행 파일이 없는 코드를 링크하지 않게. 뺀 파일을 include 하는 파일도 뺀다(뺀 .cpp 의 같은 이름 .h 도 뺀 것으로 본다 — 그 헤더를 쓰는
# 파일은 빠진 구현을 링크한다). 이름(파일 이름)으로 견주므로 같은 이름의 다른 헤더도 함께 빠질 수 있다 — 빼는 쪽으로 틀린다. 뺀 것은 "짓지 않는 소스" 로 적는다.
function(sw_excludeSourcesIncludingUnbuiltEngineFolders LIST_VAR)
	if(NOT SW_UNBUILT_ENGINE_FOLDERS)
		return()
	endif()
	set(listPrefix "")
	foreach(folder IN LISTS SW_UNBUILT_ENGINE_FOLDERS)
		list(APPEND listPrefix "\"Engine/${folder}/")
	endforeach()

	# 파일마다 include 줄을 한 번만 읽는다.
	set(listKept ${${LIST_VAR}})
	foreach(source IN LISTS listKept)
		string(MD5 sourceKey "${source}")
		set(includes_${sourceKey} "")
		if(source MATCHES "\\.(c|cpp|h|hpp)$")
			file(STRINGS "${source}" includeLines REGEX "^[ \t]*#[ \t]*include[ \t]+\"")
			set(includes_${sourceKey} "${includeLines}")
		endif()
	endforeach()

	set(listDropped "")
	set(listDroppedHeaderName "")
	set(bChanged ON)
	while(bChanged)
		set(bChanged OFF)
		set(listStillKept "")
		foreach(source IN LISTS listKept)
			string(MD5 sourceKey "${source}")
			set(bDrop OFF)
			foreach(prefix IN LISTS listPrefix)
				string(FIND "${includes_${sourceKey}}" "${prefix}" hit)
				if(NOT hit EQUAL -1)
					set(bDrop ON)
				endif()
			endforeach()
			if(NOT bDrop)
				foreach(headerName IN LISTS listDroppedHeaderName)
					if("${includes_${sourceKey}}" MATCHES "[\"/]${headerName}\"")
						set(bDrop ON)
					endif()
				endforeach()
			endif()
			if(bDrop)
				list(APPEND listDropped "${source}")
				get_filename_component(stem "${source}" NAME_WE)
				string(REPLACE "." "\\." stemPattern "${stem}")
				list(APPEND listDroppedHeaderName "${stemPattern}\\.h")
				set(bChanged ON)
			else()
				list(APPEND listStillKept "${source}")
			endif()
		endforeach()
		set(listKept ${listStillKept})
	endwhile()

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
