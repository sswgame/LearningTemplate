# ==============================================================================
# @file cmake/Config/JsonValue.cmake
# @brief JSON 파일에서 값 읽기 — 필수(sw_requireJSONValue)와 선택(sw_readOptionalJSONValue) 둘뿐
# ==============================================================================
#
# CMake 가 JSON 을 읽는 곳은 모두 이 둘을 거친다. `string(JSON … ERROR_VARIABLE …)` 을 곳마다 적으면 오류 변수를 읽지 않거나,
# 오류 뒤에 기본값을 넣거나, 빈 값으로 지나가는 모양이 되풀이됐다(툴체인 · 모듈 매니페스트 · 테스트 정책).
# - 필수: 파일이 없거나 · JSON 이 아니거나 · 키가 없으면 구성을 세운다. 메시지에 파일 경로와 키 경로를 넣는다.
# - 선택: 키가 없으면 OUT_VAR 가 **정의되지 않는다**(`if(DEFINED …)` 로 묻는다). 기본값 인자는 없다 — 없을 때의 뜻은 부르는 쪽이 적는다.
#   파일이 없거나 JSON 이 아니거나 경로가 문자열 속으로 들어가는 것(형식 오류)은 선택 읽기에서도 구성을 세운다.
#
# 키 앞에 `LENGTH` · `TYPE` · `MEMBER` 를 두면 `string(JSON)` 의 그 하위 명령이 된다(없으면 GET). MEMBER 는 마지막 키가 순번이다.
#   sw_requireJSONValue( swName "${manifest}" _name )
#   sw_requireJSONValue( swCount "${manifest}" LENGTH _listTarget )
#   sw_readOptionalJSONValue( swMinVersion "${manifest}" _listDependency 0 _minVersion )
#
# 파일 내용은 configure 한 번에 한 번만 읽어 전역 속성에 둔다 — 매니페스트 하나에 키를 열 번 넘게 묻는다.
# 그래서 configure 도중에 다시 쓰는 파일은 쓴 **뒤에** 처음 읽어야 한다(`toolchain_config.json` 은 DetectToolchain 이 먼저 쓴다).
include_guard(GLOBAL)

# 파일 내용(OUT_VAR). 없거나 JSON 이 아니면 구성을 세운다.
function(sw_readJSONFileText JSON_FILE OUT_VAR)
	get_property(swCached GLOBAL PROPERTY "SW_JSON_TEXT:${JSON_FILE}" SET)

	if(NOT swCached)
		if(NOT EXISTS "${JSON_FILE}")
			message(FATAL_ERROR "[Json] ${JSON_FILE}: file not found")
		endif()

		file(READ "${JSON_FILE}" swText)
		string(JSON swType ERROR_VARIABLE swError TYPE "${swText}")

		if(NOT swError STREQUAL "NOTFOUND")
			message(FATAL_ERROR "[Json] ${JSON_FILE}: not valid JSON — ${swError}")
		endif()

		set_property(GLOBAL PROPERTY "SW_JSON_TEXT:${JSON_FILE}" "${swText}")
	endif()

	get_property(swText GLOBAL PROPERTY "SW_JSON_TEXT:${JSON_FILE}")
	set(${OUT_VAR} "${swText}" PARENT_SCOPE)
endfunction()

# 두 함수의 몸통. 결과는 swValue · swError(성공이면 NOTFOUND) · swMode · swKeyPath 로 부른 쪽 스코프에 둔다.
macro(sw_queryJSONInternal JSON_FILE)
	sw_readJSONFileText("${JSON_FILE}" swJSONText)
	set(swKeys ${ARGN})
	set(swMode GET)

	if(swKeys)
		list(GET swKeys 0 swFirstKey)

		if(swFirstKey MATCHES "^(LENGTH|TYPE|MEMBER)$")
			set(swMode ${swFirstKey})
			list(REMOVE_AT swKeys 0)
		endif()
	endif()

	list(JOIN swKeys "." swKeyPath)
	string(JSON swValue ERROR_VARIABLE swError ${swMode} "${swJSONText}" ${swKeys})
endmacro()

# 필수 값 하나를 OUT_VAR 에. 없으면 구성을 세운다.
function(sw_requireJSONValue OUT_VAR JSON_FILE)
	sw_queryJSONInternal("${JSON_FILE}" ${ARGN})

	if(NOT swError STREQUAL "NOTFOUND")
		message(FATAL_ERROR "[Json] ${JSON_FILE}: ${swMode} '${swKeyPath}' — ${swError}")
	endif()

	set(${OUT_VAR} "${swValue}" PARENT_SCOPE)
endfunction()

# 선택 값 하나를 OUT_VAR 에. 키가 없으면 OUT_VAR 를 정의하지 않는다. 형식 오류는 구성을 세운다.
function(sw_readOptionalJSONValue OUT_VAR JSON_FILE)
	sw_queryJSONInternal("${JSON_FILE}" ${ARGN})

	if(swError STREQUAL "NOTFOUND")
		set(${OUT_VAR} "${swValue}" PARENT_SCOPE)
	elseif(swError MATCHES "not found$")
		unset(${OUT_VAR} PARENT_SCOPE)
	else()
		message(FATAL_ERROR "[Json] ${JSON_FILE}: ${swMode} '${swKeyPath}' — ${swError}")
	endif()
endfunction()
