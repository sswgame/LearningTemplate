# ==============================================================================
# @file cmake/Modules/Build/ReflectionMacroScan.cmake
# @brief 파일이 리플렉션 입력인지 — 구성 때 훑기와 빌드 때 후보 검사(`cmake -P`)가 같은 판정을 쓴다
# @details 확장자(`SW_REFLECTION_INPUT_EXTENSIONS`)와 줄 머리 매크로(`SW_REFLECTION_MACRO_LINE_PATTERN`)는 `Scripts/common/project_constants.py` 한 곳에 있고,
#          파이썬 도구(`lint.cpp_code.is_reflection_input`)도 같은 값을 쓴다. `cmake -P` 로 돌 때는 부르는 쪽이 `-DSW_GENERATED_CMAKE_VARS=<ConfigVars.cmake>` 를 준다.
# ==============================================================================

if(NOT DEFINED SW_REFLECTION_MACRO_LINE_PATTERN)
	if(NOT SW_GENERATED_CMAKE_VARS OR NOT EXISTS "${SW_GENERATED_CMAKE_VARS}")
		message(FATAL_ERROR "[Reflection] ReflectionMacroScan.cmake needs the project constants (SW_GENERATED_CMAKE_VARS = '${SW_GENERATED_CMAKE_VARS}')")
	endif()
	include("${SW_GENERATED_CMAKE_VARS}")
endif()

# 줄 머리에 반사 매크로가 있으면 OUT_VAR 가 ON 이다. 확장자는 보지 않는다(입력 판정은 `sw_is_reflection_input`).
function(sw_has_reflection_macro OUT_VAR FILE_PATH)
	# ENCODING UTF-8: 없으면 file(STRINGS) 가 한글 바이트에서 줄을 끊어 주석 중간의 `ENUM(` 도 `^` 에 맞는다.
	file(STRINGS "${FILE_PATH}" list_reflect_line ENCODING UTF-8 REGEX "${SW_REFLECTION_MACRO_LINE_PATTERN}")
	if(list_reflect_line)
		set(${OUT_VAR} ON PARENT_SCOPE)
	else()
		set(${OUT_VAR} OFF PARENT_SCOPE)
	endif()
endfunction()

# 리플렉션 입력의 확장자에 맞는 경로 정규식(`\.(h|hpp|cpp)$`)을 OUT_VAR 에 담는다 — 소스 목록에서 입력 후보를 고를 때 쓴다.
function(sw_get_reflection_input_regex OUT_VAR)
	set(list_alternative "")
	foreach(extension IN LISTS SW_REFLECTION_INPUT_EXTENSIONS)
		string(SUBSTRING "${extension}" 1 -1 extension_name)
		list(APPEND list_alternative "${extension_name}")
	endforeach()
	list(JOIN list_alternative "|" alternative_text)
	set(${OUT_VAR} "\\.(${alternative_text})$" PARENT_SCOPE)
endfunction()

# DIRECTORY 아래(재귀) 리플렉션 입력 확장자의 glob 패턴들을 OUT_VAR 에 담는다(`sw_glob_source_tree` 에 넘긴다).
function(sw_get_reflection_input_globs OUT_VAR DIRECTORY)
	set(list_glob "")
	foreach(extension IN LISTS SW_REFLECTION_INPUT_EXTENSIONS)
		list(APPEND list_glob "${DIRECTORY}/*${extension}")
	endforeach()
	set(${OUT_VAR} ${list_glob} PARENT_SCOPE)
endfunction()

# 리플렉션 입력(입력 확장자 + 줄 머리 반사 매크로)이면 OUT_VAR 가 ON 이다.
function(sw_is_reflection_input OUT_VAR FILE_PATH)
	sw_get_reflection_input_regex(input_regex)
	if(NOT FILE_PATH MATCHES "${input_regex}")
		set(${OUT_VAR} OFF PARENT_SCOPE)
		return()
	endif()
	sw_has_reflection_macro(reflected "${FILE_PATH}")
	set(${OUT_VAR} ${reflected} PARENT_SCOPE)
endfunction()
