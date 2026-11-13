# 리플렉션 파서의 도구 입력 사례 하나를 돈다 — 산출물이 입력만으로 정해지는가(결정성)와, 도구 입력(설정 · 템플릿 · 철자 표)이 깨졌을 때 멈추는가.
#   MODE=determinism        같은 입력으로 두 번 생성한다(둘째는 스탬프를 지워 다시 파싱하게 한다). 산출물 바이트가 같고, 내용이 같으면 다시 쓰지 않아 시각도 그대로다
#                           (`GeneratedFileUtil::writeIfChanged` — 다시 쓰면 그 파일을 include 하는 TU 가 모두 다시 컴파일된다).
#   MODE=broken-config      설정 파일이 JSON 이 아니다.
#   MODE=broken-templates   템플릿 폴더에서 템플릿 하나가 빠졌다.
#   MODE=broken-spelling    철자 표의 첫 줄이 [scope] 절 밖의 바인딩이다.
#   깨진 입력은 0 이 아닌 종료여야 한다 — 기본값으로 이어 가면 빌드가 틀린 생성 코드로 지나간다(폴백 없음).
# 입력: MODE · TARGET_ARGS · PARSER · FIXTURE · WORK_DIR · SOURCE_ROOT · GENERATED_DIR · BUILTINS · ANNOTATION_META · TEMPLATES · CONFIG · GEN_CPP_SUFFIX
foreach(swRequired MODE TARGET_ARGS PARSER FIXTURE WORK_DIR SOURCE_ROOT GENERATED_DIR BUILTINS ANNOTATION_META TEMPLATES CONFIG GEN_CPP_SUFFIX)
	if(NOT DEFINED ${swRequired})
		message(FATAL_ERROR "RunParserToolInputs: ${swRequired} is not given")
	endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/out")
set(swAnnotationMeta "${ANNOTATION_META}")
set(swTemplates "${TEMPLATES}")
set(swConfig "${CONFIG}")

# 빌드와 같은 대상 인자 — Test/CMakeLists.txt 가 '|' 로 이어 넘긴다(sw_collect_reflection_target_args).
string(REPLACE "|" ";" listTargetArgValue "${TARGET_ARGS}")
set(listTargetArg "")
foreach(swTargetArg IN LISTS listTargetArgValue)
	list(APPEND listTargetArg --target-arg "${swTargetArg}")
endforeach()

# 빌드의 인자(`sw_make_reflection_parser_args`)와 같은 모양이다(RunParserFixture.cmake 와 같다).
function(sw_run_parser outExitCode outText)
	execute_process(
		COMMAND "${PARSER}"
			--input "${FIXTURE}"
			--output "${WORK_DIR}/out"
			--builtins "${BUILTINS}"
			--annotation-meta "${swAnnotationMeta}"
			--emit-templates "${swTemplates}"
			--module "ParserFixture"
			--module-local
			--config "${swConfig}"
			--include "${GENERATED_DIR}"
			--include "${SOURCE_ROOT}/Content/engine/shaders"
			--include "${SOURCE_ROOT}/Source"
			${listTargetArg}
		WORKING_DIRECTORY "${WORK_DIR}"
		RESULT_VARIABLE result_code
		OUTPUT_VARIABLE result_text
		ERROR_VARIABLE result_text)
	set(${outExitCode} "${result_code}" PARENT_SCOPE)
	set(${outText} "${result_text}" PARENT_SCOPE)
endfunction()

get_filename_component(swFixtureName "${FIXTURE}" NAME_WE)
set(swGenCpp "${WORK_DIR}/out/${swFixtureName}${GEN_CPP_SUFFIX}")

if(MODE STREQUAL "determinism")
	sw_run_parser(swExitCode swOutput)
	if(NOT swExitCode EQUAL 0)
		message(FATAL_ERROR "first run exited ${swExitCode}\n${swOutput}")
	endif()
	file(GLOB listGenerated "${WORK_DIR}/out/${swFixtureName}.gen.*")
	list(FILTER listGenerated EXCLUDE REGEX "\\.stamp$")
	if(NOT listGenerated)
		message(FATAL_ERROR "the first run wrote no ${swFixtureName}.gen.* file")
	endif()
	set(listHash "")
	set(listTime "")
	foreach(generated IN LISTS listGenerated)
		file(SHA256 "${generated}" swFirstHash)
		file(TIMESTAMP "${generated}" swFirstTime "%Y%m%d%H%M%S" UTC)
		list(APPEND listHash "${swFirstHash}")
		list(APPEND listTime "${swFirstTime}")
	endforeach()
	# 시각은 초 단위로 읽는다 — 다시 썼다면 다른 초가 되도록 기다린다. 스탬프를 지워 증분 판정이 건너뛰지 못하게 한다.
	execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 2)
	file(REMOVE "${swGenCpp}.stamp")
	sw_run_parser(swExitCode swOutput)
	if(NOT swExitCode EQUAL 0)
		message(FATAL_ERROR "second run exited ${swExitCode}\n${swOutput}")
	endif()
	if(NOT EXISTS "${swGenCpp}.stamp")
		message(FATAL_ERROR "the second run did not parse again (no new stamp) - the check would prove nothing")
	endif()
	foreach(generated hash time IN ZIP_LISTS listGenerated listHash listTime)
		file(SHA256 "${generated}" swHashAgain)
		file(TIMESTAMP "${generated}" swTimeAgain "%Y%m%d%H%M%S" UTC)
		if(NOT swHashAgain STREQUAL hash)
			message(FATAL_ERROR "${generated}: the same input gave different bytes - the generator is not deterministic")
		endif()
		if(NOT swTimeAgain STREQUAL time)
			message(FATAL_ERROR "${generated}: same bytes were written again (time ${time} -> ${swTimeAgain}) - every includer recompiles")
		endif()
	endforeach()
	return()
endif()

if(MODE STREQUAL "broken-config")
	set(swConfig "${WORK_DIR}/broken.json")
	file(WRITE "${swConfig}" "{ \"tuning\": ")
elseif(MODE STREQUAL "broken-templates")
	set(swTemplates "${WORK_DIR}/templates")
	file(GLOB listTemplate "${TEMPLATES}/*")
	list(LENGTH listTemplate swTemplateCount)
	if(swTemplateCount LESS 2)
		message(FATAL_ERROR "${TEMPLATES} has fewer than two templates - cannot drop one")
	endif()
	list(POP_FRONT listTemplate)
	file(COPY ${listTemplate} DESTINATION "${swTemplates}")
elseif(MODE STREQUAL "broken-spelling")
	set(swAnnotationMeta "${WORK_DIR}/broken.annotation.txt")
	file(READ "${ANNOTATION_META}" swMetaText)
	file(WRITE "${swAnnotationMeta}" "flag.NotInAScope = NotInAScope\n${swMetaText}")
else()
	message(FATAL_ERROR "RunParserToolInputs: unknown MODE '${MODE}'")
endif()
sw_run_parser(swExitCode swOutput)
if(swExitCode EQUAL 0)
	message(FATAL_ERROR "${MODE}: the parser accepted a broken tool input\n${swOutput}")
endif()
