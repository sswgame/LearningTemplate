# ==============================================================================
# @file cmake/Engine/SourceTree.cmake
# @brief 뿌리(`Source/` · `Test/` · 시나리오 폴더)를 구성마다 한 번 훑은 파일 목록과, 거기서 `file(GLOB)` 과 같은 결과를 걸러 내는 `sw_glob_source_tree`
# ==============================================================================

# ------------------------------------------------------------------------------
# 왜 한 번만 훑나
#
# `file(GLOB[_RECURSE] … CONFIGURE_DEPENDS)` 는 **패턴 하나가 폴더 훑기 하나**이고, Ninja 는 그 훑기를 **빌드마다** 모두 다시 한다
# (`<빌드>/CMakeFiles/VerifyGlobs.cmake`). 타깃 · 층 · 키트 · 짓지 않는 폴더마다 확장자 넷씩 따로 훑으니 Debug 트리에서 빌드 한 번에
# 405 번이었다 — 키트 · 층이 늘수록 아무것도 바뀌지 않은 빌드도 곱으로 느려지는 모양이다. 그래서 `Source/` 는 여기서 한 번만
# 훑고(CONFIGURE_DEPENDS — 빌드마다 훑기 한 번), 소스 · 헤더 목록은 모두 이 목록을 걸러 얻는다. 언리얼 UBT 도 모듈 폴더를 한 번 훑어
# 캐시(DirectoryItem)한 것을 나눠 쓴다. 대가는 뿌리 아래 아무 파일(.md 등)을 더하거나 지워도 다시 구성한다는 것이다.
#
# 뿌리는 `Source/` · `Test/` 와, 부르는 쪽이 `sw_add_source_tree_root` 로 더한 폴더(시나리오의 `Content/<팩>/automation` — `Content/` 전체는
# 에셋이라 훑지 않는다)다. `Source/` 는 include 때 훑고(ExcludedSources 가 목록을 바로 읽는다) 나머지는 처음 걸러 낼 때 훑는다.
# 이 뿌리들 아래를 `file(GLOB … CONFIGURE_DEPENDS)` 로 직접 훑는 코드는 빌드가 멈춘다(`sw_check_configure_globs` — 생성된 VerifyGlobs.cmake 를 본다).
# ------------------------------------------------------------------------------
include_guard(GLOBAL)

set(SW_SOURCE_TREE_ROOT "${CMAKE_SOURCE_DIR}/Source")
file(GLOB_RECURSE SW_SOURCE_TREE_FILES CONFIGURE_DEPENDS "${SW_SOURCE_TREE_ROOT}/*")
set_property(GLOBAL PROPERTY SW_SOURCE_TREE_ROOTS "${SW_SOURCE_TREE_ROOT}" "${CMAKE_SOURCE_DIR}/Test")
set_property(GLOBAL PROPERTY "SW_SOURCE_TREE_FILES:${SW_SOURCE_TREE_ROOT}" "${SW_SOURCE_TREE_FILES}")

# 훑을 뿌리를 더한다(절대 경로). 이미 있는 뿌리 안이나 위면 구성이 선다 — 같은 파일을 두 뿌리가 훑지 않게.
function(sw_add_source_tree_root DIRECTORY)
	get_property(list_root GLOBAL PROPERTY SW_SOURCE_TREE_ROOTS)
	if(DIRECTORY IN_LIST list_root)
		return()
	endif()
	foreach(root IN LISTS list_root)
		string(FIND "${DIRECTORY}/" "${root}/" inside_position)
		string(FIND "${root}/" "${DIRECTORY}/" outside_position)
		if(inside_position EQUAL 0 OR outside_position EQUAL 0)
			message(FATAL_ERROR "[SourceTree] sw_add_source_tree_root: '${DIRECTORY}' overlaps the root '${root}'")
		endif()
	endforeach()
	set_property(GLOBAL APPEND PROPERTY SW_SOURCE_TREE_ROOTS "${DIRECTORY}")
endfunction()

# 폴더 DIRECTORY 를 담은 뿌리(OUT_ROOT)와 그 뿌리의 파일 목록(OUT_FILES). 처음 묻는 뿌리는 여기서 한 번 훑는다. 뿌리 밖이면 구성이 선다.
function(sw_get_source_tree_files OUT_ROOT OUT_FILES DIRECTORY)
	get_property(list_root GLOBAL PROPERTY SW_SOURCE_TREE_ROOTS)
	foreach(root IN LISTS list_root)
		string(FIND "${DIRECTORY}/" "${root}/" root_position)
		if(root_position EQUAL 0)
			get_property(scanned GLOBAL PROPERTY "SW_SOURCE_TREE_FILES:${root}" SET)
			if(NOT scanned)
				file(GLOB_RECURSE list_file CONFIGURE_DEPENDS "${root}/*")
				set_property(GLOBAL PROPERTY "SW_SOURCE_TREE_FILES:${root}" "${list_file}")
			endif()
			get_property(list_file GLOBAL PROPERTY "SW_SOURCE_TREE_FILES:${root}")
			set(${OUT_ROOT} "${root}" PARENT_SCOPE)
			set(${OUT_FILES} "${list_file}" PARENT_SCOPE)
			return()
		endif()
	endforeach()
	message(FATAL_ERROR "[SourceTree] '${DIRECTORY}' is under no source tree root (${list_root}) — add one with sw_add_source_tree_root (cmake/Engine/SourceTree.cmake)")
endfunction()

# 글을 정규식 안에 그대로 넣을 수 있게 특수 문자를 이스케이프한다(경로에 `.` · `+` 가 든다 — 워크트리 `.claude/`).
function(sw_escape_regex OUT_VAR TEXT)
	set(backslash "\\")
	foreach(character IN ITEMS "${backslash}" "." "+" "*" "?" "^" "$" "(" ")" "[" "]" "|" "{" "}")
		string(REPLACE "${character}" "${backslash}${character}" TEXT "${TEXT}")
	endforeach()
	set(${OUT_VAR} "${TEXT}" PARENT_SCOPE)
endfunction()

# sw_glob_source_tree(<OUT_VAR> [RECURSE] [RELATIVE <폴더>] <패턴>...)
#   `file(GLOB` · `GLOB_RECURSE <OUT_VAR> [RELATIVE <폴더>] <패턴>...)` 과 같은 결과(패턴을 모두 합쳐 정렬, 중복 없음)를 훑기 없이 낸다.
#   패턴은 `<폴더>/<파일 이름 글롭>` 이다 — 폴더에는 와일드카드가 없고(`CMAKE_CURRENT_SOURCE_DIR` 기준 상대 또는 절대) 뿌리 아래여야 하며,
#   파일 이름 글롭은 `*` · `?` 만 쓴다. RECURSE 면 폴더 아래 모든 깊이, 아니면 바로 아래 파일만이다. 다른 모양은 구성을 세운다.
function(sw_glob_source_tree OUT_VAR)
	cmake_parse_arguments(ARG "RECURSE" "RELATIVE" "" ${ARGN})
	if(NOT ARG_UNPARSED_ARGUMENTS)
		message(FATAL_ERROR "[SourceTree] sw_glob_source_tree(${OUT_VAR}): no pattern")
	endif()
	set(list_result "")
	foreach(pattern IN LISTS ARG_UNPARSED_ARGUMENTS)
		if(NOT IS_ABSOLUTE "${pattern}")
			set(pattern "${CMAKE_CURRENT_SOURCE_DIR}/${pattern}")
		endif()
		get_filename_component(directory "${pattern}" DIRECTORY)
		get_filename_component(name_glob "${pattern}" NAME)
		if(directory MATCHES "[][*?]" OR name_glob MATCHES "[][]")
			message(FATAL_ERROR "[SourceTree] sw_glob_source_tree: '${pattern}' must be <folder without wildcards>/<name with * ?> (cmake/Engine/SourceTree.cmake)")
		endif()
		sw_get_source_tree_files(root list_match "${directory}")
		sw_escape_regex(directory_regex "${directory}")
		sw_escape_regex(name_regex "${name_glob}")
		string(REPLACE "\\*" "[^/]*" name_regex "${name_regex}")   # 이스케이프된 `\*` · `\?` 를 글롭 뜻으로 되돌린다
		string(REPLACE "\\?" "[^/]" name_regex "${name_regex}")
		if(ARG_RECURSE)
			list(FILTER list_match INCLUDE REGEX "^${directory_regex}/(.*/)?${name_regex}$")
		else()
			list(FILTER list_match INCLUDE REGEX "^${directory_regex}/${name_regex}$")
		endif()
		list(APPEND list_result ${list_match})
	endforeach()
	if(list_result)
		list(REMOVE_DUPLICATES list_result)
		list(SORT list_result)
	endif()
	if(DEFINED ARG_RELATIVE)
		sw_escape_regex(relative_regex "${ARG_RELATIVE}")
		list(TRANSFORM list_result REPLACE "^${relative_regex}/" "")
	endif()
	set(${OUT_VAR} ${list_result} PARENT_SCOPE)
endfunction()

# 폴더 DIRECTORY 의 단위 테스트 소스 — `<폴더>/Tests/` 와 `<폴더>/<한 단계>/Tests/` 아래 모든 깊이의 .cpp(예전 글롭 `<폴더>/*/Tests/*.cpp` · `<폴더>/Tests/*.cpp` 와 같다).
function(sw_glob_tests_under OUT_VAR DIRECTORY)
	sw_glob_source_tree(list_source RECURSE "${DIRECTORY}/*.cpp")
	sw_escape_regex(directory_regex "${DIRECTORY}")
	list(FILTER list_source INCLUDE REGEX "^${directory_regex}/([^/]+/)?Tests/")
	set(${OUT_VAR} ${list_source} PARENT_SCOPE)
endfunction()

# 빌드가 생성된 `CMakeFiles/VerifyGlobs.cmake`(CMake 가 빌드마다 다시 훑는 글롭 목록)를 본다 — 뿌리 아래를 이 파일 밖의 `file(GLOB … CONFIGURE_DEPENDS)` 로
# 훑는 자리가 있으면 멈춘다. 그 목록은 생성 단계가 쓰므로 configure 안에서는 볼 수 없다. 그래서 그 파일이 바뀐 빌드에서만 한 번 돈다(스탬프).
# 모든 빌드가 거치는 Core 가 기다린다. 루트 CMakeLists 의 끝(모든 sw_add_source_tree_root 뒤)에서 부른다.
function(sw_check_configure_globs)
	get_property(list_root GLOBAL PROPERTY SW_SOURCE_TREE_ROOTS)
	string(REPLACE ";" "|" root_text "${list_root}")
	set(verify_globs "${CMAKE_BINARY_DIR}/CMakeFiles/VerifyGlobs.cmake")
	set(stamp "${CMAKE_BINARY_DIR}/CMakeFiles/swConfigureGlobCheck.stamp")
	add_custom_command(
		OUTPUT "${stamp}"
		COMMAND "${CMAKE_COMMAND}" "-DVERIFY_GLOBS=${verify_globs}" "-DROOTS=${root_text}" "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}" "-DSTAMP=${stamp}"
			-P "${CMAKE_SOURCE_DIR}/cmake/Engine/CheckConfigureGlobs.cmake"
		DEPENDS "${verify_globs}" "${CMAKE_SOURCE_DIR}/cmake/Engine/CheckConfigureGlobs.cmake"
		COMMENT "Checking that source globs go through sw_glob_source_tree"
		VERBATIM
	)
	add_custom_target(SwConfigureGlobCheck DEPENDS "${stamp}")
	set_target_properties(SwConfigureGlobCheck PROPERTIES FOLDER "CMakePredefinedTargets")
	add_dependencies(Core SwConfigureGlobCheck)
endfunction()
