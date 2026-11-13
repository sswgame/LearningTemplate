# 리플렉션 파서 사례 하나를 돈다 — 파서 실행 파일을 사례 헤더 하나로 실제 인자 그대로 돌리고, 헤더 안의 기대 줄과 대조한다.
# 왜 이 모양인가: 파서는 Engine 을 링크하지 못해(Engine 이 파서 산출물에 기댄다) `App --test` 틀에 들어가지 못하고, 핵심 규칙이 libclang AST 를 지난다. 로직을 라이브러리로 떼어 테스트 실행 파일을
# 따로 짓는 것보다 입력 헤더 → 출력 대조가 작고 실제 길을 그대로 지난다. 언리얼 UHT 의 테스트 하니스(`.uhttest` 입력 + 기대 출력)와 같은 자리다(기억에 의존).
# 기대 줄(헤더 맨 앞, `;` 는 쓰지 않는다 — CMake 목록 구분자다):
#   // EXPECT-ERROR: <글>      파서가 실패(0 이 아닌 종료)하고 출력에 이 글이 있다. 이 줄이 하나라도 있으면 실패를 기대하는 사례다.
#   // EXPECT-GEN: <글>        파서가 성공하고 <이름>.gen.cpp 에 이 글이 있다.
#   // EXPECT-NOT-GEN: <글>    <이름>.gen.cpp 에 이 글이 없다.
#   // EXPECT-META: <글>       <이름>.gen.meta.cpp(개발 전용 에디터 메타데이터 단위)에 이 글이 있다.
#   // EXPECT-TYPE: <FQN>      ReflectedTypes.list 에 `<FQN>\t<모듈>` 줄이 있다.
#   // EXPECT-ASSET-FIELD: <이름> <대상>  AssetReferenceFields.list 에 `<이름>\t<대상>` 줄이 있다.
# 입력: TARGET_ARGS · PARSER · FIXTURE · WORK_DIR · SOURCE_ROOT · GENERATED_DIR · BUILTINS · ANNOTATION_META · TEMPLATES · CONFIG · TYPES_LIST_NAME · ASSET_FIELDS_LIST_NAME · GEN_CPP_SUFFIX · GEN_META_CPP_SUFFIX
foreach(required TARGET_ARGS PARSER FIXTURE WORK_DIR SOURCE_ROOT GENERATED_DIR BUILTINS ANNOTATION_META TEMPLATES CONFIG TYPES_LIST_NAME ASSET_FIELDS_LIST_NAME GEN_CPP_SUFFIX GEN_META_CPP_SUFFIX)
	if(NOT DEFINED ${required})
		message(FATAL_ERROR "RunParserFixture: ${required} is not given")
	endif()
endforeach()

set(module "ParserFixture")
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(list_expect_error "")
set(list_expect_gen "")
set(list_expect_not_gen "")
set(list_expect_meta "")
set(list_expect_type "")
set(list_expect_asset_field "")
file(STRINGS "${FIXTURE}" list_expect_line REGEX "^// EXPECT-")
foreach(line IN LISTS list_expect_line)
	string(REGEX MATCH "^// EXPECT-([A-Z-]+): (.*)$" matched "${line}")
	if(NOT matched)
		message(FATAL_ERROR "${FIXTURE}: malformed expectation line: ${line}")
	endif()
	if(CMAKE_MATCH_1 STREQUAL "ERROR")
		list(APPEND list_expect_error "${CMAKE_MATCH_2}")
	elseif(CMAKE_MATCH_1 STREQUAL "GEN")
		list(APPEND list_expect_gen "${CMAKE_MATCH_2}")
	elseif(CMAKE_MATCH_1 STREQUAL "NOT-GEN")
		list(APPEND list_expect_not_gen "${CMAKE_MATCH_2}")
	elseif(CMAKE_MATCH_1 STREQUAL "META")
		list(APPEND list_expect_meta "${CMAKE_MATCH_2}")
	elseif(CMAKE_MATCH_1 STREQUAL "TYPE")
		list(APPEND list_expect_type "${CMAKE_MATCH_2}")
	elseif(CMAKE_MATCH_1 STREQUAL "ASSET-FIELD")
		list(APPEND list_expect_asset_field "${CMAKE_MATCH_2}")
	else()
		message(FATAL_ERROR "${FIXTURE}: unknown expectation: ${line}")
	endif()
endforeach()
if(NOT list_expect_error AND NOT list_expect_gen AND NOT list_expect_type)
	message(FATAL_ERROR "${FIXTURE}: no expectation - a fixture must say what it checks")
endif()

# 빌드와 같은 대상 인자 — Test/CMakeLists.txt 가 '|' 로 이어 넘긴다(sw_collect_reflection_target_args).
string(REPLACE "|" ";" list_target_arg_value "${TARGET_ARGS}")
set(list_target_arg "")
foreach(target_arg IN LISTS list_target_arg_value)
	list(APPEND list_target_arg --target-arg "${target_arg}")
endforeach()

# 빌드의 인자(`sw_make_reflection_parser_args`)와 같은 모양이다. 파서 로그 파일이 소스 트리에 남지 않게 작업 폴더를 옮긴다.
execute_process(
	COMMAND "${PARSER}"
		--input "${FIXTURE}"
		--output "${WORK_DIR}"
		--builtins "${BUILTINS}"
		--annotation-meta "${ANNOTATION_META}"
		--emit-templates "${TEMPLATES}"
		--module "${module}"
		--module-local
		--config "${CONFIG}"
		--include "${GENERATED_DIR}"
		--include "${SOURCE_ROOT}/Content/engine/shaders"
		--include "${SOURCE_ROOT}/Source"
		${list_target_arg}
	WORKING_DIRECTORY "${WORK_DIR}"
	RESULT_VARIABLE exit_code
	OUTPUT_VARIABLE output_text
	ERROR_VARIABLE output_text)

get_filename_component(fixture_name "${FIXTURE}" NAME_WE)
if(list_expect_error)
	if(exit_code EQUAL 0)
		message(FATAL_ERROR "${fixture_name}: the parser accepted a bad header\n${output_text}")
	endif()
	foreach(needle IN LISTS list_expect_error)
		string(FIND "${output_text}" "${needle}" found)
		if(found EQUAL -1)
			message(FATAL_ERROR "${fixture_name}: the parser failed without saying '${needle}'\n${output_text}")
		endif()
	endforeach()
	return()
endif()

if(NOT exit_code EQUAL 0)
	message(FATAL_ERROR "${fixture_name}: the parser exited ${exit_code}\n${output_text}")
endif()
file(READ "${WORK_DIR}/${fixture_name}${GEN_CPP_SUFFIX}" gen_text)
foreach(needle IN LISTS list_expect_gen)
	string(FIND "${gen_text}" "${needle}" found)
	if(found EQUAL -1)
		message(FATAL_ERROR "${fixture_name}: '${needle}' is not in the generated code\n${gen_text}")
	endif()
endforeach()
foreach(needle IN LISTS list_expect_not_gen)
	string(FIND "${gen_text}" "${needle}" found)
	if(NOT found EQUAL -1)
		message(FATAL_ERROR "${fixture_name}: '${needle}' must not be in the generated code\n${gen_text}")
	endif()
endforeach()
file(READ "${WORK_DIR}/${fixture_name}${GEN_META_CPP_SUFFIX}" meta_text)
foreach(needle IN LISTS list_expect_meta)
	string(FIND "${meta_text}" "${needle}" found)
	if(found EQUAL -1)
		message(FATAL_ERROR "${fixture_name}: '${needle}' is not in the editor metadata unit\n${meta_text}")
	endif()
endforeach()
file(READ "${WORK_DIR}/${TYPES_LIST_NAME}" types_text)
foreach(type_fqn IN LISTS list_expect_type)
	string(FIND "${types_text}" "${type_fqn}\t${module}\n" found)
	if(found EQUAL -1)
		message(FATAL_ERROR "${fixture_name}: '${type_fqn}' is not in ${TYPES_LIST_NAME}\n${types_text}")
	endif()
endforeach()
file(READ "${WORK_DIR}/${ASSET_FIELDS_LIST_NAME}" asset_fields_text)
foreach(asset_field IN LISTS list_expect_asset_field)
	string(REPLACE " " "\t" asset_field_line "${asset_field}")
	string(FIND "${asset_fields_text}" "${asset_field_line}\n" found)
	if(found EQUAL -1)
		message(FATAL_ERROR "${fixture_name}: '${asset_field_line}' is not in ${ASSET_FIELDS_LIST_NAME}\n${asset_fields_text}")
	endif()
endforeach()
