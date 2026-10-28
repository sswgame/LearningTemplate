# ==============================================================================
# @file cmake/Engine/TestTargets.cmake
# @brief CTest 등록 — 파이썬 스크립트 하나를 ctest 항목 하나로(sw_registerScriptTest). 린트 게이트가 쓴다
#
# 실행 파일 테스트(옛 `Test/` · `AllTests` · TestBin)는 프레임워크를 새로 만들려고 지웠다(새 설계는 아직 정하지 않았다).
# 새 테스트 폴더가 생기면 이 파일에 실행 파일 등록 함수를 더하고 CMakeLists.txt 에서 `add_subdirectory` 한다.
# ==============================================================================

# ------------------------------------------------------------------------------
# sw_registerScriptTest — 파이썬 스크립트 하나를 ctest 항목 하나로(린트)
#
#   SCRIPT            저장소 기준 .py 경로
#   ARGS              스크립트 인자
#   LABELS · TIMEOUT  `ctest -L` 로 고르는 라벨, 제한 시간(초)
#   WORKING_DIRECTORY 기본은 저장소 루트
#   RUN_SERIAL · SKIP_RETURN_CODE
# ------------------------------------------------------------------------------
function(sw_registerScriptTest TEST_NAME)
	cmake_parse_arguments(ARG "RUN_SERIAL" "SCRIPT;TIMEOUT;SKIP_RETURN_CODE;WORKING_DIRECTORY" "ARGS;LABELS" ${ARGN})
	if(NOT ARG_SCRIPT OR NOT ARG_TIMEOUT)
		message(FATAL_ERROR "sw_registerScriptTest(${TEST_NAME}): SCRIPT 와 TIMEOUT 은 꼭 준다")
	endif()
	if(NOT ARG_WORKING_DIRECTORY)
		set(ARG_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
	endif()

	add_test(NAME ${TEST_NAME} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/${ARG_SCRIPT}" ${ARG_ARGS})
	set_tests_properties(${TEST_NAME} PROPERTIES
		WORKING_DIRECTORY "${ARG_WORKING_DIRECTORY}"
		LABELS "${ARG_LABELS}"
		TIMEOUT ${ARG_TIMEOUT}
		# 스크립트가 "이 테스트를 등록한 빌드" 를 안다 — 작업 폴더(저장소 루트)로는 알 수 없다.
		ENVIRONMENT "SW_BUILD_DIR=${CMAKE_BINARY_DIR}"
	)
	if(ARG_RUN_SERIAL)
		set_tests_properties(${TEST_NAME} PROPERTIES RUN_SERIAL TRUE)
	endif()
	if(DEFINED ARG_SKIP_RETURN_CODE)
		set_tests_properties(${TEST_NAME} PROPERTIES SKIP_RETURN_CODE ${ARG_SKIP_RETURN_CODE})
	endif()
endfunction()
