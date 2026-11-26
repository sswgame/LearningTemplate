# 리플렉션 파서의 타깃 인자 응답 파일(`sw_write_reflection_target_args`)을 작은 프로젝트로 구성해 만들고 줄을 대조한다(cmake -P).
# 왜: 그 함수는 file(GENERATE) 와 생성기 식으로 구성 · 생성 때만 돌아, 인자 꼴이 틀려도(CONTENT 앞의 TARGET) 파서 사례로는 드러나지 않고 모든 구성이 멈춘 뒤에야 보였다.
# 입력: SOURCE_ROOT · WORK_DIR · GENERATED_CMAKE_VARS · GENERATOR · MAKE_PROGRAM
foreach(required SOURCE_ROOT WORK_DIR GENERATED_CMAKE_VARS GENERATOR MAKE_PROGRAM)
	if(NOT DEFINED ${required})
		message(FATAL_ERROR "RunTargetArgsCheck: ${required} is not given")
	endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
set(check_output "${WORK_DIR}/ReflectionParser.target.rsp")
execute_process(
	COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_ROOT}/IntegrationTests/ReflectionParser/TargetArgs" -B "${WORK_DIR}/build" -G "${GENERATOR}"
		"-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}" "-DSW_SOURCE_ROOT=${SOURCE_ROOT}" "-DSW_GENERATED_CMAKE_VARS=${GENERATED_CMAKE_VARS}" "-DCHECK_OUTPUT=${check_output}"
	RESULT_VARIABLE exit_code
	OUTPUT_VARIABLE output_text
	ERROR_VARIABLE output_text)
if(NOT exit_code EQUAL 0)
	message(FATAL_ERROR "sw_write_reflection_target_args did not configure (${exit_code}):\n${output_text}")
endif()
if(NOT EXISTS "${check_output}")
	message(FATAL_ERROR "sw_write_reflection_target_args wrote no response file: ${check_output}")
endif()
file(READ "${check_output}" response_text)
set(expected_text "--target-arg\n-DSW_FIXTURE_FLAG=1\n--target-arg\n-DSW_FIXTURE_TEXT=\"text\"\n--target-arg\n-std=c++17\n--target-option\n/GR-\n--include\n${SOURCE_ROOT}/IntegrationTests/ReflectionParser/TargetArgs/fixture_include\n")
if(NOT response_text STREQUAL expected_text)
	message(FATAL_ERROR "the target response file differs\n--- expected\n${expected_text}--- actual\n${response_text}")
endif()
