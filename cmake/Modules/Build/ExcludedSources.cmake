# ==============================================================================
# @file cmake/Modules/Build/ExcludedSources.cmake
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
include("${CMAKE_CURRENT_LIST_DIR}/PlatformFolders.cmake")

# 게이트는 빌드 트리 기준 같은 상대 경로(`generated/sw/config/ExcludedSources.txt`)를 읽는다.
set(SW_EXCLUDED_SOURCE_LIST "${CMAKE_BINARY_DIR}/generated/sw/config/ExcludedSources.txt")

# ------------------------------------------------------------------------------
# sw_read_source_folder_table — 자리 표(TABLE 의 KEY: `Source/` 폴더 → 까닭)를 읽어 이름이 FOLDER_NAME 인 폴더의 경로 정규식을 OUT_VAR 에 돌려준다.
#   값이 `<FOLDER_NAME>/` 로 끝나면 그 폴더 하나, 아니면 그 아래 어느 `<FOLDER_NAME>/` 든이다. 정규식은 저장소 기준 경로 앞에 `/` 를 붙인 모양에 맞는다.
#   다음이면 configure 가 멈춘다: 표의 값이 없는 폴더이거나 그 아래 `<FOLDER_NAME>/` 가 하나도 없다(옮겼는데 줄이 남았다), `Source/` 에 표 밖 `<FOLDER_NAME>/` 가 있다.
#   폴더 이름만 보고 빼면 옮기거나 이름을 바꿀 때 다른 구성이 조용히 짓는다 — 언리얼 UBT 가 모듈 규칙(`Type`)으로 가르는 자리를 표로 둔다.
# ------------------------------------------------------------------------------
function(sw_read_source_folder_table TABLE KEY FOLDER_NAME OUT_VAR)
	set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${TABLE}")
	file(READ "${TABLE}" table_json)
	string(JSON entry_count ERROR_VARIABLE table_error LENGTH "${table_json}" ${KEY})
	if(table_error)
		message(FATAL_ERROR "${TABLE}: ${KEY} 를 읽지 못했다 (${table_error})")
	endif()
	set(list_pattern "")
	math(EXPR entry_last "${entry_count} - 1")
	foreach(entry_index RANGE ${entry_last})
		string(JSON folder_prefix MEMBER "${table_json}" ${KEY} ${entry_index})
		if(NOT folder_prefix MATCHES "^Source/[A-Za-z0-9_/]+/$" OR NOT IS_DIRECTORY "${CMAKE_SOURCE_DIR}/${folder_prefix}")
			message(FATAL_ERROR "${TABLE}: ${KEY} 의 '${folder_prefix}' 는 있는 `Source/` 폴더(끝에 /)가 아니다 — 옮겼으면 줄도 고치세요")
		endif()
		if(folder_prefix MATCHES "/${FOLDER_NAME}/$")
			list(APPEND list_pattern "/${folder_prefix}")
		else()
			list(APPEND list_pattern "/${folder_prefix}(.*/)?${FOLDER_NAME}/")
		endif()
	endforeach()

	# 파일마다가 아니라 첫 `<FOLDER_NAME>/` 까지 자른 폴더마다 본다(더 깊은 같은 이름 폴더는 바깥 폴더를 따른다). 저장소 기준이라 저장소를 담은 상위 폴더 이름에 걸리지 않는다.
	set(source_files ${SW_SOURCE_TREE_FILES})
	list(FILTER source_files INCLUDE REGEX "/${FOLDER_NAME}/")
	string(LENGTH "/${FOLDER_NAME}/" folder_length)
	set(list_folder "")
	foreach(source_file IN LISTS source_files)
		file(RELATIVE_PATH relative_path "${CMAKE_SOURCE_DIR}" "${source_file}")
		string(FIND "/${relative_path}" "/${FOLDER_NAME}/" folder_index)
		if(NOT folder_index EQUAL -1)
			math(EXPR folder_end "${folder_index} + ${folder_length}")
			string(SUBSTRING "/${relative_path}" 0 ${folder_end} folder)
			list(APPEND list_folder "${folder}")
		endif()
	endforeach()
	list(REMOVE_DUPLICATES list_folder)

	set(list_stray "")
	set(list_unused ${list_pattern})
	foreach(folder IN LISTS list_folder)
		set(allowed OFF)
		foreach(pattern IN LISTS list_pattern)
			if(folder MATCHES "^${pattern}")
				set(allowed ON)
				list(REMOVE_ITEM list_unused "${pattern}")
			endif()
		endforeach()
		if(NOT allowed)
			list(APPEND list_stray "${folder}")
		endif()
	endforeach()
	if(list_unused)
		message(FATAL_ERROR "${TABLE}: ${KEY} 의 줄이 가리키는 `${FOLDER_NAME}/` 폴더가 없다: ${list_unused} — 옮겼으면 줄도 고치세요")
	endif()
	if(list_stray)
		message(FATAL_ERROR "`${FOLDER_NAME}/` 폴더가 표 밖에 있다: ${list_stray} — ${TABLE} 의 ${KEY} 에 까닭과 함께 적거나, 그 구성이 지어야 하는 코드면 폴더 이름을 바꾸세요")
	endif()
	set(${OUT_VAR} ${list_pattern} PARENT_SCOPE)
endfunction()

# 배포 구성이 짓지 않는 개발 전용 소스 — 기능 폴더의 단위 테스트(`Tests/`)와 개발 전용 단위 폴더(`Dev/` — 호스트의 개발 계층 · 서버 개발 조종 · 에디터 모듈 개발 경로).
# 파일 이름 꼬리로 빼지 않는다: 개발 명령 · 캐시 리로드 · 온라인 개발 대체물은 기능 파일에 두고 등록 매크로가 배포에서 등록하지 않는다(`docs/agents/CppStyle.md "Build configuration switches"`).
#
# `Dev/` 는 아무 폴더에나 붙는 패턴이 아니라 표(`Config/Rules/Layers/DevFolders.json`)에 적은 자리만이다 — 예전 `/Dev/` 한 줄은 어디든 `Dev/` 를 만들면 배포에서 빠지는
# 편의라 개발 기능이 DevTools 대신 엔진 곳곳에 쌓였다(린트가 뒤에서 막았다). 표 밖 `Source/` 의 `Dev/` 폴더는 configure 가 멈춘다. 언리얼이 개발 코드를 Developer 모듈 단위로만 빼는 자리다.
sw_read_source_folder_table("${CMAKE_SOURCE_DIR}/Config/Rules/Layers/DevFolders.json" _devFolderAllowed Dev dev_folder_patterns)
set(SW_DEV_ONLY_SOURCE_PATTERNS "/Tests/" ${dev_folder_patterns})

# 전용 서버가 짓지 않는 클라이언트 전용 소스와 클라이언트 구성이 짓지 않는 서버 전용 소스 — 자리는 표(`Config/Rules/Layers/TargetFolders.json`)에 적는다.
# 공유 코드는 그 헤더를 include 하지 않는다 — 서버 구성 가드 `serverbuild` 는 `Client/`, 클라이언트 구성 가드 `clientbuild` 는 `Server/` 폴더의 헤더를 막는다
# (`TargetLayers.toml` 의 `[guardfolder]`. 표 밖 같은 이름 폴더가 없으니 이름으로 막아도 표와 같다). 서버가 클라이언트 전용 컴포넌트를 씬에서 만나면 MissingComponent 가 아니라
# 조용히 건너뛴다(서버 빌드가 만드는 `Bin/ClientOnlyTypes.json` — Source/Server/CMakeLists.txt). 세션이 서버를 띄우는 일은 창구(`IOnlineLocalServerFactory`)로 부른다.
# 언리얼 `WITH_SERVER_CODE` 와 ClientOnly · ServerOnly 모듈 자리다.
sw_read_source_folder_table("${CMAKE_SOURCE_DIR}/Config/Rules/Layers/TargetFolders.json" _clientOnlyFolders Client SW_CLIENT_ONLY_SOURCE_PATTERNS)
sw_read_source_folder_table("${CMAKE_SOURCE_DIR}/Config/Rules/Layers/TargetFolders.json" _serverOnlyFolders Server SW_SERVER_ONLY_SOURCE_PATTERNS)

# 이 구성이 짓지 않는 소스를 적는다(절대 경로). 헤더도 받아 두지만 목록에는 `.c` · `.cpp` 만 쓴다.
function(sw_declare_excluded_sources)
	set_property(GLOBAL APPEND PROPERTY SW_EXCLUDED_SOURCES ${ARGN})
endfunction()

# 폴더(ARGN, 절대 경로) 아래의 .c · .cpp 를 전부 "이 구성이 짓지 않는 소스" 로 적는다 — 통째로 빼는 폴더(고르지 않은 게임 팩 ·
# 끈 GameFramework)에 쓴다.
function(sw_declare_excluded_directory)
	foreach(directory IN LISTS ARGN)
		sw_glob_source_tree(list_source RECURSE "${directory}/*.c" "${directory}/*.cpp")
		sw_declare_excluded_sources(${list_source})
	endforeach()
endfunction()

# 목록 변수 LIST_VAR 에서 정규식(ARGN)에 맞는 소스를 빼고, 뺀 것을 "이 구성이 짓지 않는 소스" 로 적는다.
# 빼기와 적기가 한 호출이라 한쪽만 하는 일이 없다. 다른 타겟으로 옮겨 짓는 것에는 쓰지 않는다(그때는 `list(FILTER)`).
function(sw_remove_excluded_sources LIST_VAR)
	set(list_kept ${${LIST_VAR}})
	foreach(pattern IN LISTS ARGN)
		list(FILTER list_kept EXCLUDE REGEX "${pattern}")
	endforeach()

	set(list_removed ${${LIST_VAR}})
	if(list_kept)
		list(REMOVE_ITEM list_removed ${list_kept})
	endif()
	sw_declare_excluded_sources(${list_removed})

	set(${LIST_VAR} "${list_kept}" PARENT_SCOPE)
endfunction()

# 목록 변수 LIST_VAR 에서 이 대상 종류가 짓지 않는 쪽의 소스를 뺀다 — 전용 서버(Server)는 `Client/`, 클라이언트(Client)는 `Server/`.
# 소스를 트리째 모으는 타깃(GameFramework · GameFrameworkDev · 키트 · 게임 · DevTools)이 모두 이 함수를 지난다 — 타깃마다 같은 갈래를 다시 적으면 새 타깃이 빠뜨린다.
function(sw_remove_other_target_sources LIST_VAR)
	if(SW_TARGET_TYPE STREQUAL "Server")
		sw_remove_excluded_sources(${LIST_VAR} ${SW_CLIENT_ONLY_SOURCE_PATTERNS})
	elseif(SW_TARGET_TYPE STREQUAL "Client")
		sw_remove_excluded_sources(${LIST_VAR} ${SW_SERVER_ONLY_SOURCE_PATTERNS})
	endif()
	set(${LIST_VAR} "${${LIST_VAR}}" PARENT_SCOPE)
endfunction()

# 목록 LIST_VAR 에서 접두(ARGN, 예: `OnlineLoadBot/`)로 시작하는 헤더를 `#include "…"` 하는 소스를 빼고, 뺀 것을 "짓지 않는 소스" 로 적는다.
# 파일 이름 정규식을 손으로 적는 대신 소스가 스스로 말하는 의존(include)을 읽는다 — 도구 타깃이 없는 구성에서 그 도구를 쓰는 테스트가 빠진다.
# 한 단계만 본다(그 헤더를 간접으로 끄는 파일은 보지 않는다).
function(sw_exclude_sources_including_prefix LIST_VAR)
	set(list_kept "")
	set(list_dropped "")
	foreach(source IN LISTS ${LIST_VAR})
		set(drop OFF)
		if(source MATCHES "\\.(cpp|h)$")
			file(STRINGS "${source}" include_lines ENCODING UTF-8 REGEX "^[ \t]*#[ \t]*include[ \t]+\"")
			foreach(prefix IN LISTS ARGN)
				string(FIND "${include_lines}" "\"${prefix}" hit)
				if(NOT hit EQUAL -1)
					set(drop ON)
				endif()
			endforeach()
		endif()
		if(drop)
			list(APPEND list_dropped "${source}")
		else()
			list(APPEND list_kept "${source}")
		endif()
	endforeach()
	if(list_dropped)
		sw_declare_excluded_sources(${list_dropped})
	endif()
	set(${LIST_VAR} ${list_kept} PARENT_SCOPE)
endfunction()

# 플랫폼 폴더 규칙 — 저장소 기준 경로에 `Windows/` · `Linux/` · `Android/` · `Web/` 이 든 소스는 그 플랫폼에서만, `Posix/` 는 Windows 가 아닌 곳에서만 짓는다.
# 고르지 않은 것은 목록에서 빼고 "이 구성이 짓지 않는 소스" 로 적는다. (경로를 저장소 기준으로 보는 이유: 저장소를 담은 상위 폴더 이름에 걸리지 않게.)
# Core · Engine · DevTools 가 쓴다 — 플랫폼 폴더 .cpp 에 파일 전체 플랫폼 `#if` 를 두지 않는다.
function(sw_filter_platform_sources LIST_VAR)
	set(list_kept "")
	set(list_dropped "")
	foreach(source IN LISTS ${LIST_VAR})
		file(RELATIVE_PATH relative_path "${CMAKE_SOURCE_DIR}" "${source}")
		sw_is_platform_folder_built("${relative_path}" is_built)
		if(is_built)
			list(APPEND list_kept "${source}")
		else()
			list(APPEND list_dropped "${source}")
		endif()
	endforeach()
	if(list_dropped)
		sw_declare_excluded_sources(${list_dropped})
	endif()
	set(${LIST_VAR} ${list_kept} PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# sw_require_built_sources — 구성 끝(모든 타깃이 선 뒤) 한 번: 어느 타깃이든 이 구성이 짓지 않을 소스를 SOURCES 에 들고 있으면 멈춘다.
#   빼기는 타깃마다 고른 자리(`if(SW_SHIPPING_BUILD) sw_remove_excluded_sources(…)`)라 새 타깃 · 팩토리를 거치지 않는 타깃(App · Server · Core · DevTools)이
#   빠뜨리면 조용히 지어졌다. 팩토리 인자로는 팩토리 밖 타깃을 못 잡아 모든 타깃을 훑는다. 판정은 `Source/` 아래 .c · .cpp 만:
#     - 적어 둔 "짓지 않는 소스"(SW_EXCLUDED_SOURCES)
#     - 플랫폼 폴더 규칙(sw_is_platform_folder_built)
#     - 배포 구성의 개발 전용 폴더(SW_DEV_ONLY_SOURCE_PATTERNS), 전용 서버의 `Client/`, 클라이언트 구성의 `Server/`
# ------------------------------------------------------------------------------
function(sw_require_built_sources)
	set(list_forbidden_pattern "")
	if(SW_SHIPPING_BUILD)
		list(APPEND list_forbidden_pattern ${SW_DEV_ONLY_SOURCE_PATTERNS})
	endif()
	if(SW_TARGET_TYPE STREQUAL "Server")
		list(APPEND list_forbidden_pattern ${SW_CLIENT_ONLY_SOURCE_PATTERNS})
	elseif(SW_TARGET_TYPE STREQUAL "Client")
		list(APPEND list_forbidden_pattern ${SW_SERVER_ONLY_SOURCE_PATTERNS})
	endif()
	get_property(list_excluded GLOBAL PROPERTY SW_EXCLUDED_SOURCES)

	set(list_directory "${CMAKE_SOURCE_DIR}")
	set(list_violation "")
	while(list_directory)
		list(POP_FRONT list_directory directory)
		get_property(list_subdirectory DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
		list(APPEND list_directory ${list_subdirectory})
		get_property(list_target DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
		foreach(target IN LISTS list_target)
			get_target_property(list_source ${target} SOURCES)
			if(NOT list_source)
				continue()
			endif()
			get_target_property(target_source_dir ${target} SOURCE_DIR)
			list(FILTER list_source INCLUDE REGEX "\\.(c|cpp)$")
			list(FILTER list_source EXCLUDE REGEX "^\\$<")
			foreach(source IN LISTS list_source)
				cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${target_source_dir}" NORMALIZE OUTPUT_VARIABLE source_path)
				file(RELATIVE_PATH relative_path "${CMAKE_SOURCE_DIR}" "${source_path}")
				if(NOT relative_path MATCHES "^Source/")
					continue()
				endif()
				set(reason "")
				sw_is_platform_folder_built("${relative_path}" is_built)
				if(NOT is_built)
					set(reason "platform folder")
				elseif(source_path IN_LIST list_excluded)
					set(reason "declared as not built")
				else()
					foreach(pattern IN LISTS list_forbidden_pattern)
						if("/${relative_path}" MATCHES "${pattern}")
							set(reason "matches ${pattern}")
							break()
						endif()
					endforeach()
				endif()
				if(reason)
					list(APPEND list_violation "${target}: ${relative_path} (${reason})")
				endif()
			endforeach()
		endforeach()
	endwhile()
	if(list_violation)
		list(JOIN list_violation "\n  " violation_text)
		message(FATAL_ERROR "A target builds sources this configuration does not build "
			"(${SW_TARGET_TYPE}, shipping=${SW_SHIPPING_BUILD}, ${CMAKE_SYSTEM_NAME}) — filter them with sw_remove_excluded_sources · sw_filter_platform_sources:\n  ${violation_text}")
	endif()
endfunction()

# 구성 끝(모든 소스 타겟을 만든 뒤)에 한 번 — 적어 둔 것을 빌드 트리에 쓴다. 내용이 같으면 파일을 다시 쓰지 않는다.
function(sw_write_excluded_source_list)
	get_property(list_source GLOBAL PROPERTY SW_EXCLUDED_SOURCES)
	list(FILTER list_source INCLUDE REGEX "\\.(c|cpp)$")
	set(list_relative "")
	foreach(source IN LISTS list_source)
		file(RELATIVE_PATH relative_path "${CMAKE_SOURCE_DIR}" "${source}")
		list(APPEND list_relative "${relative_path}")
	endforeach()
	if(list_relative)
		list(REMOVE_DUPLICATES list_relative)
		list(SORT list_relative)
	endif()

	string(JOIN "\n" content ${list_relative})
	file(CONFIGURE OUTPUT "${SW_EXCLUDED_SOURCE_LIST}" CONTENT "${content}\n" @ONLY)
	list(LENGTH list_relative excluded_count)
	message(STATUS "[Build] Sources this configuration does not build: ${excluded_count} (${SW_EXCLUDED_SOURCE_LIST})")
endfunction()
