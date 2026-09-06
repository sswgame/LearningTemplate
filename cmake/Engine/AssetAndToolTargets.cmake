# ==============================================================================
# @file cmake/Engine/AssetAndToolTargets.cmake
# @brief 에셋 쿠킹(CookAssets), Doxygen 문서(GenerateDocs), 린트 스크립트 및 CTest 등록 헬퍼
# ==============================================================================

include("${CMAKE_CURRENT_LIST_DIR}/../Environment/PythonUtils.cmake")

# ------------------------------------------------------------------------------
# 0) sw_addRepoPythonTarget — 저장소 루트 기준 Python 커스텀 타겟
# ------------------------------------------------------------------------------
function(sw_addRepoPythonTarget TARGET_NAME SCRIPT_REL)
	cmake_parse_arguments(SW_PT "" "COMMENT" "ARGS" ${ARGN})
	add_custom_target(${TARGET_NAME}
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/${SCRIPT_REL}" ${SW_PT_ARGS}
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
		COMMENT "${SW_PT_COMMENT}"
		VERBATIM
	)
	set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Engine/Scripts")
endfunction()

# ------------------------------------------------------------------------------
# 1) Doxygen — SW_BUILD_DOCS일 때만 GenerateDocs
# ------------------------------------------------------------------------------
if(SW_BUILD_DOCS)
	if(NOT Python3_Interpreter_FOUND)
		find_package(Python3 COMPONENTS Interpreter REQUIRED)
	endif()

	find_program(DOXYGEN_EXECUTABLE doxygen)
	set(doxyfile "${CMAKE_SOURCE_DIR}/Doxyfile")

	if(DOXYGEN_EXECUTABLE AND EXISTS "${doxyfile}")
		sw_addRepoPythonTarget(GenerateDocs "${SW_SCRIPT_GENERATE_DOCS}"
			COMMENT "Generating Doxygen Documentation..."
		)
		message(STATUS "[Docs] Doxygen Documentation Target Enabled.")
	else()
		message(STATUS "[Docs] Skipping docs target (doxygen or Doxyfile missing).")
	endif()
endif()

# ------------------------------------------------------------------------------
# 2) 스크립트 타겟 — CookPrefabs, Engine 레이어/GLOB 린트
# CheckEngineLayers: Engine → Editor/GameFramework/Games 금지 include
# CheckSourceGlob: GLOB 누락 힌트 (compile_commands.json 필요)
# ------------------------------------------------------------------------------
if(NOT Python3_Interpreter_FOUND)
	find_package(Python3 COMPONENTS Interpreter QUIET)
endif()

if(Python3_Interpreter_FOUND)
	# 팩은 실행 파일 옆(Bin/Packs)에 놓는다. App 이 exeDir/Packs 를 먼저 찾기 때문.
	# 정의되지 않은 변수를 쓰면 "/Packs"(파일시스템 루트)가 되어 조용히 엉뚱한 곳에 쿠킹되거나
	# Linux 에서 권한 오류로 죽으므로, 비어 있으면 구성 단계에서 잡는다.
	set(swPackOutputDir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Packs")

	if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
		message(FATAL_ERROR "[CookAssets] CMAKE_RUNTIME_OUTPUT_DIRECTORY is empty — pack output path would resolve to the filesystem root.")
	endif()

	sw_addRepoPythonTarget(CookAssets "${SW_SCRIPT_COOK_ASSETS}"
		COMMENT "Cooking scene XML, prefab XML and Resource packs to binary..."
		ARGS --all --output "${swPackOutputDir}"
	)
	set_target_properties(CookAssets PROPERTIES FOLDER "Engine/Scripts")

	sw_addRepoPythonTarget(CheckEngineLayers "${SW_SCRIPT_LINT_CHECK_ENGINE_LAYERS}"
		COMMENT "Checking Engine layer include rules..."
		ARGS --root "${CMAKE_SOURCE_DIR}"
	)
	sw_addRepoPythonTarget(CheckIncludeOrder "${SW_SCRIPT_LINT_CHECK_INCLUDE_ORDER}"
		COMMENT "Checking Include Order rules..."
		ARGS --root "${CMAKE_SOURCE_DIR}"
	)
	sw_addRepoPythonTarget(CheckResourceCasing "${SW_SCRIPT_LINT_CHECK_RESOURCE_CASING}"
		COMMENT "Checking Resource lowercase casing rules..."
		ARGS --root "${CMAKE_SOURCE_DIR}"
	)
	sw_addRepoPythonTarget(CheckCodeConventions "${SW_SCRIPT_LINT_CHECK_CODE_CONVENTIONS}"
		COMMENT "Checking C++ code conventions..."
		ARGS --root "${CMAKE_SOURCE_DIR}"
	)
	sw_addRepoPythonTarget(CheckSourceGlob "${SW_SCRIPT_LINT_CHECK_SOURCE_GLOB}"
		COMMENT "Checking source GLOB coverage vs compile_commands..."
		ARGS --root "${CMAKE_SOURCE_DIR}" --build "${CMAKE_BINARY_DIR}" --active-game "${SW_ACTIVE_GAME}"
	)
endif()

# ------------------------------------------------------------------------------
# 3) CTest 린트 테스트 등록 헬퍼
# ------------------------------------------------------------------------------
function(sw_registerLintTests)
	if(NOT Python3_Interpreter_FOUND)
		return()
	endif()

	if(TARGET CheckEngineLayers)
		add_test(
			NAME CheckEngineLayers
			COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/${SW_SCRIPT_LINT_CHECK_ENGINE_LAYERS}"
			--root "${CMAKE_SOURCE_DIR}"
		)
		set_tests_properties(CheckEngineLayers PROPERTIES LABELS "lint" TIMEOUT 15)
	endif()

	if(TARGET CheckIncludeOrder)
		add_test(
			NAME CheckIncludeOrder
			COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/${SW_SCRIPT_LINT_CHECK_INCLUDE_ORDER}"
			--root "${CMAKE_SOURCE_DIR}"
		)
		set_tests_properties(CheckIncludeOrder PROPERTIES LABELS "lint" TIMEOUT 15)
	endif()

	if(TARGET CheckResourceCasing)
		add_test(
			NAME CheckResourceCasing
			COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/${SW_SCRIPT_LINT_CHECK_RESOURCE_CASING}"
			--root "${CMAKE_SOURCE_DIR}"
		)
		set_tests_properties(CheckResourceCasing PROPERTIES LABELS "lint" TIMEOUT 15)
	endif()

	if(TARGET CheckCodeConventions)
		add_test(
			NAME CheckCodeConventions
			COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/${SW_SCRIPT_LINT_CHECK_CODE_CONVENTIONS}"
			--root "${CMAKE_SOURCE_DIR}"
		)
		set_tests_properties(CheckCodeConventions PROPERTIES LABELS "lint" TIMEOUT 15)
	endif()

	if(TARGET CheckSourceGlob)
		add_test(
			NAME CheckSourceGlob
			COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/${SW_SCRIPT_LINT_CHECK_SOURCE_GLOB}"
			--root "${CMAKE_SOURCE_DIR}" --build "${CMAKE_BINARY_DIR}" --active-game "${SW_ACTIVE_GAME}"
		)
		set_tests_properties(CheckSourceGlob PROPERTIES LABELS "lint" TIMEOUT 15)
	endif()
endfunction()
