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
foreach(swRequired TARGET_ARGS PARSER FIXTURE WORK_DIR SOURCE_ROOT GENERATED_DIR BUILTINS ANNOTATION_META TEMPLATES CONFIG TYPES_LIST_NAME ASSET_FIELDS_LIST_NAME GEN_CPP_SUFFIX GEN_META_CPP_SUFFIX)
	if(NOT DEFINED ${swRequired})
		message(FATAL_ERROR "RunParserFixture: ${swRequired} is not given")
	endif()
endforeach()

set(swModule "ParserFixture")
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(listExpectError "")
set(listExpectGen "")
set(listExpectNotGen "")
set(listExpectMeta "")
set(listExpectType "")
set(listExpectAssetField "")
file(STRINGS "${FIXTURE}" listExpectLine REGEX "^// EXPECT-")
foreach(line IN LISTS listExpectLine)
	string(REGEX MATCH "^// EXPECT-([A-Z-]+): (.*)$" swMatched "${line}")
	if(NOT swMatched)
		message(FATAL_ERROR "${FIXTURE}: malformed expectation line: ${line}")
	endif()
	if(CMAKE_MATCH_1 STREQUAL "ERROR")
		list(APPEND listExpectError "${CMAKE_MATCH_2}")
	elseif(CMAKE_MATCH_1 STREQUAL "GEN")
		list(APPEND listExpectGen "${CMAKE_MATCH_2}")
	elseif(CMAKE_MATCH_1 STREQUAL "NOT-GEN")
		list(APPEND listExpectNotGen "${CMAKE_MATCH_2}")
	elseif(CMAKE_MATCH_1 STREQUAL "META")
		list(APPEND listExpectMeta "${CMAKE_MATCH_2}")
	elseif(CMAKE_MATCH_1 STREQUAL "TYPE")
		list(APPEND listExpectType "${CMAKE_MATCH_2}")
	elseif(CMAKE_MATCH_1 STREQUAL "ASSET-FIELD")
		list(APPEND listExpectAssetField "${CMAKE_MATCH_2}")
	else()
		message(FATAL_ERROR "${FIXTURE}: unknown expectation: ${line}")
	endif()
endforeach()
if(NOT listExpectError AND NOT listExpectGen AND NOT listExpectType)
	message(FATAL_ERROR "${FIXTURE}: no expectation - a fixture must say what it checks")
endif()

# 빌드와 같은 대상 인자 — Test/CMakeLists.txt 가 '|' 로 이어 넘긴다(sw_collect_reflection_target_args).
string(REPLACE "|" ";" listTargetArgValue "${TARGET_ARGS}")
set(listTargetArg "")
foreach(swTargetArg IN LISTS listTargetArgValue)
	list(APPEND listTargetArg --target-arg "${swTargetArg}")
endforeach()

# 빌드의 인자(`sw_make_reflection_parser_args`)와 같은 모양이다. 파서 로그 파일이 소스 트리에 남지 않게 작업 폴더를 옮긴다.
execute_process(
	COMMAND "${PARSER}"
		--input "${FIXTURE}"
		--output "${WORK_DIR}"
		--builtins "${BUILTINS}"
		--annotation-meta "${ANNOTATION_META}"
		--emit-templates "${TEMPLATES}"
		--module "${swModule}"
		--module-local
		--config "${CONFIG}"
		--include "${GENERATED_DIR}"
		--include "${SOURCE_ROOT}/Content/engine/shaders"
		--include "${SOURCE_ROOT}/Source"
		${listTargetArg}
	WORKING_DIRECTORY "${WORK_DIR}"
	RESULT_VARIABLE exitCode
	OUTPUT_VARIABLE outputText
	ERROR_VARIABLE outputText)

get_filename_component(swFixtureName "${FIXTURE}" NAME_WE)
if(listExpectError)
	if(exitCode EQUAL 0)
		message(FATAL_ERROR "${swFixtureName}: the parser accepted a bad header\n${outputText}")
	endif()
	foreach(needle IN LISTS listExpectError)
		string(FIND "${outputText}" "${needle}" swFound)
		if(swFound EQUAL -1)
			message(FATAL_ERROR "${swFixtureName}: the parser failed without saying '${needle}'\n${outputText}")
		endif()
	endforeach()
	return()
endif()

if(NOT exitCode EQUAL 0)
	message(FATAL_ERROR "${swFixtureName}: the parser exited ${exitCode}\n${outputText}")
endif()
file(READ "${WORK_DIR}/${swFixtureName}${GEN_CPP_SUFFIX}" genText)
foreach(needle IN LISTS listExpectGen)
	string(FIND "${genText}" "${needle}" swFound)
	if(swFound EQUAL -1)
		message(FATAL_ERROR "${swFixtureName}: '${needle}' is not in the generated code\n${genText}")
	endif()
endforeach()
foreach(needle IN LISTS listExpectNotGen)
	string(FIND "${genText}" "${needle}" swFound)
	if(NOT swFound EQUAL -1)
		message(FATAL_ERROR "${swFixtureName}: '${needle}' must not be in the generated code\n${genText}")
	endif()
endforeach()
file(READ "${WORK_DIR}/${swFixtureName}${GEN_META_CPP_SUFFIX}" metaText)
foreach(needle IN LISTS listExpectMeta)
	string(FIND "${metaText}" "${needle}" swFound)
	if(swFound EQUAL -1)
		message(FATAL_ERROR "${swFixtureName}: '${needle}' is not in the editor metadata unit\n${metaText}")
	endif()
endforeach()
file(READ "${WORK_DIR}/${TYPES_LIST_NAME}" typesText)
foreach(typeFqn IN LISTS listExpectType)
	string(FIND "${typesText}" "${typeFqn}\t${swModule}\n" swFound)
	if(swFound EQUAL -1)
		message(FATAL_ERROR "${swFixtureName}: '${typeFqn}' is not in ${TYPES_LIST_NAME}\n${typesText}")
	endif()
endforeach()
file(READ "${WORK_DIR}/${ASSET_FIELDS_LIST_NAME}" assetFieldsText)
foreach(assetField IN LISTS listExpectAssetField)
	string(REPLACE " " "\t" swAssetFieldLine "${assetField}")
	string(FIND "${assetFieldsText}" "${swAssetFieldLine}\n" swFound)
	if(swFound EQUAL -1)
		message(FATAL_ERROR "${swFixtureName}: '${swAssetFieldLine}' is not in ${ASSET_FIELDS_LIST_NAME}\n${assetFieldsText}")
	endif()
endforeach()
