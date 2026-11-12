# ==============================================================================
# @file cmake/Engine/SourceTree.cmake
# @brief `Source/` 를 구성마다 한 번 훑은 파일 목록과, 거기서 `file(GLOB)` 과 같은 결과를 걸러 내는 `sw_globSourceTree`
# ==============================================================================

# ------------------------------------------------------------------------------
# 왜 한 번만 훑나
#
# `file(GLOB[_RECURSE] … CONFIGURE_DEPENDS)` 는 **패턴 하나가 폴더 훑기 하나**이고, Ninja 는 그 훑기를 **빌드마다** 모두 다시 한다
# (`<빌드>/CMakeFiles/VerifyGlobs.cmake`). 타깃 · 층 · 키트 · 짓지 않는 폴더마다 확장자 넷씩 따로 훑으니 Debug 트리에서 빌드 한 번에
# 405 번이었다 — 키트 · 층이 늘수록 아무것도 바뀌지 않은 빌드도 곱으로 느려지는 모양이다. 그래서 `Source/` 는 여기서 한 번만
# 훑고(CONFIGURE_DEPENDS — 빌드마다 훑기 한 번), 소스 · 헤더 목록은 모두 이 목록을 걸러 얻는다. 언리얼 UBT 도 모듈 폴더를 한 번 훑어
# 캐시(DirectoryItem)한 것을 나눠 쓴다. 대가는 `Source/` 아래 아무 파일(.md 등)을 더하거나 지워도 다시 구성한다는 것이다.
# ------------------------------------------------------------------------------
include_guard(GLOBAL)

set(SW_SOURCE_TREE_ROOT "${CMAKE_SOURCE_DIR}/Source")
file(GLOB_RECURSE SW_SOURCE_TREE_FILES CONFIGURE_DEPENDS "${SW_SOURCE_TREE_ROOT}/*")

# 글을 정규식 안에 그대로 넣을 수 있게 특수 문자를 이스케이프한다(경로에 `.` · `+` 가 든다 — 워크트리 `.claude/`).
function(sw_escapeRegex OUT_VAR TEXT)
	set(backslash "\\")
	foreach(character IN ITEMS "${backslash}" "." "+" "*" "?" "^" "$" "(" ")" "[" "]" "|" "{" "}")
		string(REPLACE "${character}" "${backslash}${character}" TEXT "${TEXT}")
	endforeach()
	set(${OUT_VAR} "${TEXT}" PARENT_SCOPE)
endfunction()

# sw_globSourceTree(<OUT_VAR> [RECURSE] [RELATIVE <폴더>] <패턴>...)
#   `file(GLOB` · `GLOB_RECURSE <OUT_VAR> [RELATIVE <폴더>] <패턴>...)` 과 같은 결과(패턴을 모두 합쳐 정렬, 중복 없음)를 훑기 없이 낸다.
#   패턴은 `<폴더>/<파일 이름 글롭>` 이다 — 폴더에는 와일드카드가 없고(`CMAKE_CURRENT_SOURCE_DIR` 기준 상대 또는 절대) `Source/` 아래여야 하며,
#   파일 이름 글롭은 `*` · `?` 만 쓴다. RECURSE 면 폴더 아래 모든 깊이, 아니면 바로 아래 파일만이다. 다른 모양은 구성을 세운다.
function(sw_globSourceTree OUT_VAR)
	cmake_parse_arguments(ARG "RECURSE" "RELATIVE" "" ${ARGN})
	if(NOT ARG_UNPARSED_ARGUMENTS)
		message(FATAL_ERROR "[SourceTree] sw_globSourceTree(${OUT_VAR}): no pattern")
	endif()
	set(listResult "")
	foreach(pattern IN LISTS ARG_UNPARSED_ARGUMENTS)
		if(NOT IS_ABSOLUTE "${pattern}")
			set(pattern "${CMAKE_CURRENT_SOURCE_DIR}/${pattern}")
		endif()
		get_filename_component(directory "${pattern}" DIRECTORY)
		get_filename_component(nameGlob "${pattern}" NAME)
		string(FIND "${directory}/" "${SW_SOURCE_TREE_ROOT}/" rootPosition)
		if(NOT rootPosition EQUAL 0 OR directory MATCHES "[][*?]" OR nameGlob MATCHES "[][]")
			message(FATAL_ERROR "[SourceTree] sw_globSourceTree: '${pattern}' must be <folder under ${SW_SOURCE_TREE_ROOT}>/<name with * ?> (cmake/Engine/SourceTree.cmake)")
		endif()
		sw_escapeRegex(directoryRegex "${directory}")
		sw_escapeRegex(nameRegex "${nameGlob}")
		string(REPLACE "\\*" "[^/]*" nameRegex "${nameRegex}")   # 이스케이프된 `\*` · `\?` 를 글롭 뜻으로 되돌린다
		string(REPLACE "\\?" "[^/]" nameRegex "${nameRegex}")
		set(listMatch ${SW_SOURCE_TREE_FILES})
		if(ARG_RECURSE)
			list(FILTER listMatch INCLUDE REGEX "^${directoryRegex}/(.*/)?${nameRegex}$")
		else()
			list(FILTER listMatch INCLUDE REGEX "^${directoryRegex}/${nameRegex}$")
		endif()
		list(APPEND listResult ${listMatch})
	endforeach()
	if(listResult)
		list(REMOVE_DUPLICATES listResult)
		list(SORT listResult)
	endif()
	if(DEFINED ARG_RELATIVE)
		sw_escapeRegex(relativeRegex "${ARG_RELATIVE}")
		list(TRANSFORM listResult REPLACE "^${relativeRegex}/" "")
	endif()
	set(${OUT_VAR} ${listResult} PARENT_SCOPE)
endfunction()
