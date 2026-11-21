# 리플렉션 파서의 도구 입력 사례 하나를 돈다 — 산출물이 입력만으로 정해지는가(결정성)와, 도구 입력(설정 · 템플릿 · 내장 표)이 깨졌을 때 멈추는가.
#   MODE=determinism        같은 입력으로 두 번 생성한다(둘째는 스탬프를 지워 다시 파싱하게 한다). 산출물 바이트가 같고, 내용이 같으면 다시 쓰지 않아 시각도 그대로다
#                           (`GeneratedFileUtil::writeIfChanged` — 다시 쓰면 그 파일을 include 하는 TU 가 모두 다시 컴파일된다).
#   MODE=args-changed       같은 입력 · 같은 도구로 `--module` 만 바꿔 다시 돈다. 파일 시각은 그대로라도 다시 파싱해야 한다(스탬프의 `args` 줄 — A-8).
#   MODE=broken-config      설정 파일이 JSON 이 아니다.
#   MODE=broken-templates   템플릿 폴더에서 템플릿 하나가 빠졌다.
#   MODE=broken-builtins-interp   내장 표의 bool 줄에 Interp 역할을 단다 — 섞는 길이 없는 모양(none)이라 시작할 때 멈춰야 한다.
#   MODE=broken-builtins-widget   내장 표의 TagID 줄 위젯을 Packed 로 바꾼다 — 글 처리기가 packed 가 아니다.
#   MODE=broken-builtins-layout   내장 표의 int2 줄 Layout 을 모르는 철자(i64)로 바꾼다.
#   MODE=broken-builtins-container  내장 표의 TArray 줄에서 State 를 뺀다 — 같은 래퍼(Vector)의 vector 줄과 역할이 다르다.
#   깨진 입력은 0 이 아닌 종료여야 한다 — 기본값으로 이어 가면 빌드가 틀린 생성 코드로 지나간다(폴백 없음).
# 입력: MODE · TARGET_ARGS · PARSER · FIXTURE · WORK_DIR · SOURCE_ROOT · GENERATED_DIR · BUILTINS · TEMPLATES · CONFIG · GEN_CPP_SUFFIX
foreach(required MODE TARGET_ARGS PARSER FIXTURE WORK_DIR SOURCE_ROOT GENERATED_DIR BUILTINS TEMPLATES CONFIG GEN_CPP_SUFFIX)
	if(NOT DEFINED ${required})
		message(FATAL_ERROR "RunParserToolInputs: ${required} is not given")
	endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/out")
set(templates "${TEMPLATES}")
set(config "${CONFIG}")
set(module_name "ParserFixture")

# 빌드와 같은 대상 인자 — Test/CMakeLists.txt 가 '|' 로 이어 넘긴다(sw_collect_reflection_target_args).
string(REPLACE "|" ";" list_target_arg_value "${TARGET_ARGS}")
set(list_target_arg "")
foreach(target_arg IN LISTS list_target_arg_value)
	list(APPEND list_target_arg --target-arg "${target_arg}")
endforeach()

# 빌드의 인자(`sw_make_reflection_parser_args`)와 같은 모양이다(RunParserFixture.cmake 와 같다).
function(sw_run_parser out_exit_code out_text)
	execute_process(
		COMMAND "${PARSER}"
			--input "${FIXTURE}"
			--output "${WORK_DIR}/out"
			--builtins "${BUILTINS}"
			--emit-templates "${templates}"
			--module "${module_name}"
			--module-local
			--config "${config}"
			--include "${GENERATED_DIR}"
			--include "${SOURCE_ROOT}/Content/engine/shaders"
			--include "${SOURCE_ROOT}/Source"
			${list_target_arg}
		WORKING_DIRECTORY "${WORK_DIR}"
		RESULT_VARIABLE result_code
		OUTPUT_VARIABLE result_text
		ERROR_VARIABLE result_text)
	set(${out_exit_code} "${result_code}" PARENT_SCOPE)
	set(${out_text} "${result_text}" PARENT_SCOPE)
endfunction()

get_filename_component(fixture_name "${FIXTURE}" NAME_WE)
set(gen_cpp "${WORK_DIR}/out/${fixture_name}${GEN_CPP_SUFFIX}")

if(MODE STREQUAL "determinism")
	sw_run_parser(exit_code output)
	if(NOT exit_code EQUAL 0)
		message(FATAL_ERROR "first run exited ${exit_code}\n${output}")
	endif()
	file(GLOB list_generated "${WORK_DIR}/out/${fixture_name}.gen.*")
	list(FILTER list_generated EXCLUDE REGEX "\\.stamp$")
	if(NOT list_generated)
		message(FATAL_ERROR "the first run wrote no ${fixture_name}.gen.* file")
	endif()
	set(list_hash "")
	set(list_time "")
	foreach(generated IN LISTS list_generated)
		file(SHA256 "${generated}" first_hash)
		file(TIMESTAMP "${generated}" first_time "%Y%m%d%H%M%S" UTC)
		list(APPEND list_hash "${first_hash}")
		list(APPEND list_time "${first_time}")
	endforeach()
	# 시각은 초 단위로 읽는다 — 다시 썼다면 다른 초가 되도록 기다린다. 스탬프를 지워 증분 판정이 건너뛰지 못하게 한다.
	execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 2)
	file(REMOVE "${gen_cpp}.stamp")
	sw_run_parser(exit_code output)
	if(NOT exit_code EQUAL 0)
		message(FATAL_ERROR "second run exited ${exit_code}\n${output}")
	endif()
	if(NOT EXISTS "${gen_cpp}.stamp")
		message(FATAL_ERROR "the second run did not parse again (no new stamp) - the check would prove nothing")
	endif()
	foreach(generated hash time IN ZIP_LISTS list_generated list_hash list_time)
		file(SHA256 "${generated}" hash_again)
		file(TIMESTAMP "${generated}" time_again "%Y%m%d%H%M%S" UTC)
		if(NOT hash_again STREQUAL hash)
			message(FATAL_ERROR "${generated}: the same input gave different bytes - the generator is not deterministic")
		endif()
		if(NOT time_again STREQUAL time)
			message(FATAL_ERROR "${generated}: same bytes were written again (time ${time} -> ${time_again}) - every includer recompiles")
		endif()
	endforeach()
	return()
endif()

if(MODE STREQUAL "args-changed")
	sw_run_parser(exit_code output)
	if(NOT exit_code EQUAL 0)
		message(FATAL_ERROR "first run exited ${exit_code}\n${output}")
	endif()
	if(NOT EXISTS "${gen_cpp}.stamp")
		message(FATAL_ERROR "the first run wrote no stamp")
	endif()
	file(READ "${gen_cpp}.stamp" first_stamp)
	set(module_name "ParserFixtureRenamed")
	sw_run_parser(exit_code output)
	if(NOT exit_code EQUAL 0)
		message(FATAL_ERROR "second run exited ${exit_code}\n${output}")
	endif()
	file(READ "${gen_cpp}.stamp" second_stamp)
	if(second_stamp STREQUAL first_stamp)
		message(FATAL_ERROR "--module changed but the parser kept the old output (the stamp did not record the command line)")
	endif()
	return()
endif()

if(MODE STREQUAL "broken-config")
	set(config "${WORK_DIR}/broken.json")
	file(WRITE "${config}" "{ \"tuning\": ")
elseif(MODE STREQUAL "broken-templates")
	set(templates "${WORK_DIR}/templates")
	file(GLOB list_template "${TEMPLATES}/*")
	list(LENGTH list_template template_count)
	if(template_count LESS 2)
		message(FATAL_ERROR "${TEMPLATES} has fewer than two templates - cannot drop one")
	endif()
	list(POP_FRONT list_template)
	file(COPY ${list_template} DESTINATION "${templates}")
elseif(MODE MATCHES "^broken-builtins-")
	# 내장 표 한 줄만 바꾼 사본 — 바꿀 줄이 없으면 이 사례는 아무것도 증명하지 않으므로 멈춘다.
	if(MODE STREQUAL "broken-builtins-interp")
		set(builtins_from "SW_REFLECT_BUILTIN_TYPE( bool, bool, none, -, Checkbox, none, State, _ )")
		set(builtins_to "SW_REFLECT_BUILTIN_TYPE( bool, bool, none, -, Checkbox, none, Interp|State, _ )")
		set(expected_text "Interp needs num, f32 or angle")
	elseif(MODE STREQUAL "broken-builtins-widget")
		set(builtins_from "SW_REFLECT_BUILTIN_TYPE( TagID, sw::TagID, none, sw, Tag, none, None, _ )")
		set(builtins_to "SW_REFLECT_BUILTIN_TYPE( TagID, sw::TagID, none, sw, Packed, none, None, _ )")
		set(expected_text "the Packed widget needs packed")
	elseif(MODE STREQUAL "broken-builtins-layout")
		set(builtins_from "SW_REFLECT_BUILTIN_TYPE( int2, sw::int2, packed, sw, Packed, i32(_x,_y), State, _ )")
		set(builtins_to "SW_REFLECT_BUILTIN_TYPE( int2, sw::int2, packed, sw, Packed, i64(_x,_y), State, _ )")
		set(expected_text "use none, num, angle, f32(members) or i32(members)")
	elseif(MODE STREQUAL "broken-builtins-container")
		set(builtins_from "SW_REFLECT_BUILTIN_CONTAINER( TArray, Sequence, Vector, State )")
		set(builtins_to "SW_REFLECT_BUILTIN_CONTAINER( TArray, Sequence, Vector, None )")
		set(expected_text "one wrapper has one shape")
	else()
		message(FATAL_ERROR "RunParserToolInputs: unknown MODE '${MODE}'")
	endif()
	file(READ "${BUILTINS}" builtins_text)
	string(FIND "${builtins_text}" "${builtins_from}" builtins_found)
	if(builtins_found EQUAL -1)
		message(FATAL_ERROR "${MODE}: '${builtins_from}' is not in ${BUILTINS} - update this case with the table")
	endif()
	string(REPLACE "${builtins_from}" "${builtins_to}" builtins_text "${builtins_text}")
	set(BUILTINS "${WORK_DIR}/broken.ReflectBuiltins.xxx")
	file(WRITE "${BUILTINS}" "${builtins_text}")
else()
	message(FATAL_ERROR "RunParserToolInputs: unknown MODE '${MODE}'")
endif()
sw_run_parser(exit_code output)
if(exit_code EQUAL 0)
	message(FATAL_ERROR "${MODE}: the parser accepted a broken tool input\n${output}")
endif()
if(DEFINED expected_text)
	string(FIND "${output}" "${expected_text}" expected_found)
	if(expected_found EQUAL -1)
		message(FATAL_ERROR "${MODE}: the parser failed without saying '${expected_text}'\n${output}")
	endif()
endif()
