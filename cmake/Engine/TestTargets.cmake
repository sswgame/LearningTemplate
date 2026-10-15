# ==============================================================================
# @file cmake/Engine/TestTargets.cmake
# @brief 테스트 타깃 · CTest 등록 — 실행 파일(sw_addTestExecutable · sw_registerTestRun · 샤드 · 새니타이저 보정)과 스크립트(sw_registerScriptTest)
#
# 라벨 · 샤드 · 제한 시간 · 호스트 분리는 `Test/TestPolicy.json` 한 곳이 정한다(머리말에 공식). 폴더의 CMakeLists 는 무엇을 짓는지(소스 · 링크)만 적는다.
# ==============================================================================

# ------------------------------------------------------------------------------
# AllTests — 테스트 실행 파일 전부를 짓는 타깃. `Test/` 는 `EXCLUDE_FROM_ALL` 이라 기본 `all` 은 테스트를 짓지 않는다.
#   테스트까지 짓기: `cmake --build --preset <프리셋> --target all AllTests`. CI · 검증 프리셋은 build 프리셋의 `targets` 로 둘 다 짓는다.
#   테스트 폴더 밖 타깃이 없어도(SW_ENABLE_TESTING OFF) 프리셋이 깨지지 않게 여기서 늘 만든다.
# ------------------------------------------------------------------------------
if(NOT TARGET AllTests)
	add_custom_target(AllTests)
	set_target_properties(AllTests PROPERTIES FOLDER "Test")
endif()

#: 테스트 실행 파일 항목이 기다리는 ctest 픽스처 — 그 셋업 항목이 `all` 과 `AllTests` 를 짓는다(`sw_registerTestBuildFixture`).
set(SW_TEST_BUILD_FIXTURE "SWTestBinaries")

#: 테스트 등록 정책 — 실행 파일마다 영역 라벨 · 시간 · 샤드 · 호스트 분리, 그리고 쓸 수 있는 라벨 표.
set(SW_TEST_POLICY_FILE "${CMAKE_SOURCE_DIR}/Test/TestPolicy.json")
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SW_TEST_POLICY_FILE}")
file(READ "${SW_TEST_POLICY_FILE}" SW_TEST_POLICY_JSON)

# ------------------------------------------------------------------------------
# sw_getTestPolicyValue — 정책 JSON 의 값 하나(경로 ARGN). 없으면 DEFAULT_VALUE.
# ------------------------------------------------------------------------------
function(sw_getTestPolicyValue OUT_VAR DEFAULT_VALUE)
	string(JSON swValue ERROR_VARIABLE swError GET "${SW_TEST_POLICY_JSON}" ${ARGN})
	if(swError)
		set(swValue "${DEFAULT_VALUE}")
	endif()
	set(${OUT_VAR} "${swValue}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# sw_assertTestLabels — 라벨마다 정책의 `labels` 표에 있는지 본다. 모르는 라벨은 `ctest -L` 로 고를 수 없는 철자 실수다.
# ------------------------------------------------------------------------------
function(sw_assertTestLabels CONTEXT)
	foreach(swLabel IN LISTS ARGN)
		string(JSON swType ERROR_VARIABLE swError TYPE "${SW_TEST_POLICY_JSON}" labels "${swLabel}")
		if(swError)
			message(FATAL_ERROR "${CONTEXT}: 라벨 '${swLabel}' 이 Test/TestPolicy.json 의 labels 표에 없다 — 표에 뜻과 함께 더하거나 철자를 고친다")
		endif()
	endforeach()
endfunction()

# ------------------------------------------------------------------------------
# sw_computeTestTimeout — 제한 시간 = max(최소, 여유 배수 × max(조각 하나의 초, 가장 비싼 케이스의 초))
# ------------------------------------------------------------------------------
function(sw_computeTestTimeout OUT_VAR TOTAL_SECONDS SHARD_COUNT MAX_CASE_SECONDS)
	sw_getTestPolicyValue(swHeadroom 4 timeoutHeadroom)
	sw_getTestPolicyValue(swMinimum 60 minimumTimeoutSeconds)
	math(EXPR swShardSeconds "(${TOTAL_SECONDS} + ${SHARD_COUNT} - 1) / ${SHARD_COUNT}")
	if(MAX_CASE_SECONDS GREATER swShardSeconds)
		set(swShardSeconds ${MAX_CASE_SECONDS})
	endif()
	math(EXPR swTimeout "${swShardSeconds} * ${swHeadroom}")
	if(swTimeout LESS swMinimum)
		set(swTimeout ${swMinimum})
	endif()
	set(${OUT_VAR} ${swTimeout} PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# sw_registerTestBuildFixture — `ctest` 가 테스트 실행 파일 없이 지지 않게, 첫 항목으로 빌드를 부르는 셋업 항목 하나
#
# 실행 파일 테스트(`sw_registerTestRun`)은 모두 이 픽스처를 요구한다. ctest 는 `-L` · `-R` 로 거른 실행에도 요구된 셋업 항목을 스스로 더하고,
# 셋업이 지면 그 픽스처를 요구한 항목은 돌지 않는다. 이미 지은 트리면 ninja 가 할 일이 없어 몇 초다. 린트 · 스크립트 항목은 요구하지 않는다
# (`ctest -L lint` 는 빌드하지 않는다). 빌드 없이 돌리려면 `ctest -FS SWTestBinaries`(셋업 항목을 건너뛴다).
# 직렬인 이유: 빌드가 CPU 를 다 쓰는 동안 다른 항목이 돌면 제한 시간이 가짜로 진다.
# ------------------------------------------------------------------------------
function(sw_registerTestBuildFixture)
	set(swDefaultTarget all)
	if(CMAKE_GENERATOR MATCHES "^Visual Studio")
		set(swDefaultTarget ALL_BUILD)
	endif()
	add_test(NAME BuildTestBinaries COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target ${swDefaultTarget} AllTests)
	set_tests_properties(BuildTestBinaries PROPERTIES
		FIXTURES_SETUP ${SW_TEST_BUILD_FIXTURE}
		RUN_SERIAL TRUE
		TIMEOUT 7200
		LABELS "build"
	)
endfunction()

# ------------------------------------------------------------------------------
# ASan 테스트 보정 — 등록된 CTest 이름 하나에 적용한다.
#
# 실행 파일 테스트(`sw_registerTestRun`)이 부른다. 스크립트 테스트(`sw_registerScriptTest`)은 새니타이저와 무관하다(파이썬 프로세스).
# ------------------------------------------------------------------------------
function(sw_applySanitizerTestProperties TEST_NAME)
	cmake_parse_arguments(ARG "" "" "ASAN_OPTIONS" ${ARGN})

	if(NOT SW_ENABLE_SANITIZER)
		return()
	endif()

	# ASan 은 실행을 한 자릿수 배로 늦춘다. 평시 기준을 그대로 쓰면 테스트가 전부 타임아웃으로
	# 떨어져 결함처럼 보인다. 테스트가 띄우는 자식 프로세스의 시한도 같은 배수다(`test::ProcessDeadlineWatch`).
	get_test_property(${TEST_NAME} TIMEOUT swCurrentTimeout)
	if(NOT swCurrentTimeout)
		set(swCurrentTimeout 30)
	endif()
	math(EXPR swCurrentTimeout "${swCurrentTimeout} * 10")

	# ODR 검사는 **아예 끈다(0).** 처음에는 1 로 두려 했다 — 크기가 다른 중복만 보고하니 진짜 ODR
	# 버그는 남는다는 계산이었다. 그런데 레벨 1 도 등록된 전역 전체를 훑는 비용은 그대로 치른다.
	# 모듈을 반복해 올리고 내리는 SmokeTest 가 비-ASan 6.9초 → ASan 1800초 초과(타임아웃)가 됐고,
	# 0 으로 바꾸자 5초로 끝났다. 260배는 오버헤드가 아니라 사용 불가다.
	#
	# 보고 자체도 이 구조에서는 영구 오탐이다: 플러그인 DLL 이 여럿이고(RHI_*, SWGame, GF_*,
	# EditorModule) 같은 SDK·CRT 헤더를 포함하니 헤더가 박는 전역이 DLL 마다 생긴다 — 나온 것이
	# `d3d11.h` 의 D3D11_DEFAULT 와 CRT 내부 _Avx2WmemEnabledWeakValue 다.
	# 실행 파일별 추가 옵션은 정책의 `asanOptions` 다(지금은 SmokeTest 의 report_globals=0 하나).
	set(swAsanOptions "detect_odr_violation=0")
	foreach(swAsanOption IN LISTS ARG_ASAN_OPTIONS)
		string(APPEND swAsanOptions ":${swAsanOption}")
	endforeach()

	# TSan: 경쟁이 하나라도 보고되면 종료 코드 66 으로 끝나 그 테스트가 진다. 두 번째 스택까지 적어 교착 · 경쟁 원인을 좁힌다. 억제 목록은 비어 있는
	# 것이 정상이다(파일 머리말 — 스스로 동기화하는 서드파티는 계측해 짓는다). CI 와 손으로 돌린 ctest 가 같은 옵션을 쓴다.
	set(swTsanOptions "suppressions=${CMAKE_SOURCE_DIR}/cmake/Modules/Options/TsanSuppressions.txt:second_deadlock_stack=1:history_size=4")

	set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT ${swCurrentTimeout})
	set_property(TEST ${TEST_NAME} APPEND PROPERTY ENVIRONMENT "ASAN_OPTIONS=${swAsanOptions}" "TSAN_OPTIONS=${swTsanOptions}")
endfunction()

# ------------------------------------------------------------------------------
# sw_registerTestRun — 테스트 실행 파일 하나를 ctest 항목 하나로 등록한다
#
# 작업 폴더는 **구성과 무관하게 `Bin`** 이다. 테스트는 거기서 위로 올라가며 `Resource/` 를 찾고, 실행 파일만
# `TestBin` 으로 뺀다(`Bin` 에 테스트와 DXC 가 섞이지 않게). 주의: 실행 파일이 나가는 폴더를 작업 폴더로 쓰면
# `TestBin` 에서 돌게 되고, `AppTest` 는 거기서 `App.exe` 를, Dev 테스트는 `Engine.dll` 을 못 찾는다.
# 항목 이름은 `SW_TEST_CTEST_NAME` 으로 넘어가 실패 요약의 재현 명령(`ctest -R`)에 찍힌다.
# ------------------------------------------------------------------------------
function(sw_registerTestRun TEST_NAME TARGET_NAME)
	cmake_parse_arguments(ARG "RUN_SERIAL" "TIMEOUT" "ARGS;LABELS;ASAN_OPTIONS" ${ARGN})

	add_test(NAME ${TEST_NAME} COMMAND ${TARGET_NAME} ${ARG_ARGS})
	set_tests_properties(${TEST_NAME} PROPERTIES
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
		LABELS "${ARG_LABELS}"
		TIMEOUT ${ARG_TIMEOUT}
		FIXTURES_REQUIRED ${SW_TEST_BUILD_FIXTURE}
		ENVIRONMENT "SW_TEST_CTEST_NAME=${TEST_NAME}"
	)
	if(ARG_RUN_SERIAL)
		set_tests_properties(${TEST_NAME} PROPERTIES RUN_SERIAL TRUE)
	endif()

	# 새니타이저 구성의 제한 시간 · 옵션(ODR 검사 끄기 · TSan 억제)은 한 함수가 정한다.
	sw_applySanitizerTestProperties(${TEST_NAME} ASAN_OPTIONS ${ARG_ASAN_OPTIONS})
endfunction()

# ------------------------------------------------------------------------------
# sw_registerTestShards — 같은 실행을 SHARD_COUNT 개의 ctest 항목으로 가른다
#   1 이면 TEST_NAME 하나(`sw_registerTestRun` 그대로), 넘으면 `<TEST_NAME>_Shard<k>` 마다 `--test_shard=<k-1>/<n>` 과 비용표(COSTS → `--test_costs=`)를
#   ARGS 뒤에 붙인다. 나머지 인자(RUN_SERIAL · ARGS · LABELS · TIMEOUT · ASAN_OPTIONS)는 `sw_registerTestRun` 에 그대로 넘긴다.
# ------------------------------------------------------------------------------
function(sw_registerTestShards TEST_NAME TARGET_NAME SHARD_COUNT)
	cmake_parse_arguments(ARG "RUN_SERIAL" "TIMEOUT;COSTS" "ARGS;LABELS;ASAN_OPTIONS" ${ARGN})
	set(runSerial "")
	if(ARG_RUN_SERIAL)
		set(runSerial RUN_SERIAL)
	endif()

	if(SHARD_COUNT LESS_EQUAL 1)
		set(SHARD_COUNT 1)
	endif()
	math(EXPR lastShard "${SHARD_COUNT} - 1")
	foreach(shardIndex RANGE 0 ${lastShard})
		set(shardName ${TEST_NAME})
		set(shardArgs ${ARG_ARGS})
		if(SHARD_COUNT GREATER 1)
			math(EXPR shardNumber "${shardIndex} + 1")
			set(shardName ${TEST_NAME}_Shard${shardNumber})
			list(APPEND shardArgs --test_shard=${shardIndex}/${SHARD_COUNT})
			if(ARG_COSTS)
				list(APPEND shardArgs "--test_costs=${ARG_COSTS}")
			endif()
		endif()
		sw_registerTestRun(${shardName} ${TARGET_NAME} ${runSerial}
			ARGS ${shardArgs} LABELS "${ARG_LABELS}" TIMEOUT ${ARG_TIMEOUT} ASAN_OPTIONS ${ARG_ASAN_OPTIONS})
	endforeach()
endfunction()

# ------------------------------------------------------------------------------
# sw_registerTestPolicy — 정책 JSON 의 한 줄(TARGET_NAME)대로 ctest 항목을 등록한다
#
#   라벨: `unit;<area>` 에 갈래마다 `nogpu`(CI 가 도는 집합) · `hostgpu`(호스트 스위트, 직렬) · `load`(부하 스위트, 직렬)를 붙인다.
#   샤드: `shards` 가 없으면 ceil(seconds / targetShardSeconds). 비용표(`caseCosts`)는 샤드를 비용으로 고르게 나누는 데 넘긴다.
#   이름: 갈래가 없으면 `<타깃>`(샤드면 `_Shard<k>`), `hostSplit` 이면 `<타깃>_NoGPU` · `<타깃>_HostOnly`, `loadSeconds` 가 있으면 `<타깃>_Load`.
#         갈라진 항목 말고 전체 실행을 하나 더 등록하지 않는다(라벨 없는 `ctest` 가 같은 테스트를 두 번 돈다).
# ------------------------------------------------------------------------------
function(sw_registerTestPolicy TARGET_NAME)
	string(JSON swRowType ERROR_VARIABLE swError TYPE "${SW_TEST_POLICY_JSON}" executables ${TARGET_NAME})
	if(swError)
		message(FATAL_ERROR "sw_addTestExecutable(${TARGET_NAME}): Test/TestPolicy.json 의 executables 에 줄이 없다 — 영역(area)과 한 실행 시간(seconds)을 더한다")
	endif()

	sw_getTestPolicyValue(swArea "" executables ${TARGET_NAME} area)
	sw_getTestPolicyValue(swSeconds 30 executables ${TARGET_NAME} seconds)
	sw_getTestPolicyValue(swShards "" executables ${TARGET_NAME} shards)
	sw_getTestPolicyValue(swHostSplit OFF executables ${TARGET_NAME} hostSplit)
	sw_getTestPolicyValue(swHostSeconds ${swSeconds} executables ${TARGET_NAME} hostSeconds)
	sw_getTestPolicyValue(swHostShards 1 executables ${TARGET_NAME} hostShards)
	sw_getTestPolicyValue(swLoadSeconds "" executables ${TARGET_NAME} loadSeconds)
	sw_getTestPolicyValue(swTargetShardSeconds 30 targetShardSeconds)
	sw_assertTestLabels("sw_addTestExecutable(${TARGET_NAME})" ${swArea})

	set(swAsanOptions "")
	string(JSON swAsanCount ERROR_VARIABLE swError LENGTH "${SW_TEST_POLICY_JSON}" executables ${TARGET_NAME} asanOptions)
	if(NOT swError AND swAsanCount GREATER 0)
		math(EXPR swLastAsan "${swAsanCount} - 1")
		foreach(swIndex RANGE 0 ${swLastAsan})
			string(JSON swOption GET "${SW_TEST_POLICY_JSON}" executables ${TARGET_NAME} asanOptions ${swIndex})
			list(APPEND swAsanOptions "${swOption}")
		endforeach()
	endif()

	set(swCosts "")
	set(swMaxCaseSeconds 0)
	string(JSON swCostCount ERROR_VARIABLE swError LENGTH "${SW_TEST_POLICY_JSON}" executables ${TARGET_NAME} caseCosts)
	if(NOT swError AND swCostCount GREATER 0)
		math(EXPR swLastCost "${swCostCount} - 1")
		foreach(swIndex RANGE 0 ${swLastCost})
			string(JSON swCaseName MEMBER "${SW_TEST_POLICY_JSON}" executables ${TARGET_NAME} caseCosts ${swIndex})
			string(JSON swCaseSeconds GET "${SW_TEST_POLICY_JSON}" executables ${TARGET_NAME} caseCosts ${swCaseName})
			if(swCosts)
				string(APPEND swCosts ",")
			endif()
			string(APPEND swCosts "${swCaseName}=${swCaseSeconds}")
			if(swCaseSeconds GREATER swMaxCaseSeconds)
				set(swMaxCaseSeconds ${swCaseSeconds})
			endif()
		endforeach()
	endif()

	if(swShards STREQUAL "")
		math(EXPR swShards "(${swSeconds} + ${swTargetShardSeconds} - 1) / ${swTargetShardSeconds}")
	endif()
	if(swShards LESS 1)
		set(swShards 1)
	endif()
	sw_computeTestTimeout(swTimeout ${swSeconds} ${swShards} ${swMaxCaseSeconds})

	set(swBaseLabels "unit;${swArea}")
	if(NOT swHostSplit)
		sw_registerTestShards(${TARGET_NAME} ${TARGET_NAME} ${swShards}
			LABELS "${swBaseLabels};nogpu" TIMEOUT ${swTimeout} COSTS "${swCosts}" ASAN_OPTIONS ${swAsanOptions})
	else()
		sw_registerTestShards(${TARGET_NAME}_NoGPU ${TARGET_NAME} ${swShards}
			ARGS --host_suites=exclude LABELS "${swBaseLabels};nogpu" TIMEOUT ${swTimeout} COSTS "${swCosts}" ASAN_OPTIONS ${swAsanOptions})
		# 직렬인 이유: 창을 띄우고 GPU 를 잡는다. 다른 GPU 테스트와 겹치면 서로를 느리게 만들고, 드라이버에 따라
		# 서로의 디바이스 생성을 막는다. 조각마다 호스트 케이스가 하나는 있어야 한다(`--host_suites=only` 가 아무것도 고르지 않으면 진다).
		sw_computeTestTimeout(swHostTimeout ${swHostSeconds} ${swHostShards} 0)
		sw_registerTestShards(${TARGET_NAME}_HostOnly ${TARGET_NAME} ${swHostShards} RUN_SERIAL
			ARGS --host_suites=only LABELS "${swBaseLabels};hostgpu" TIMEOUT ${swHostTimeout} ASAN_OPTIONS ${swAsanOptions})
	endif()

	# 부하 · 시간 예산 검증(`SW_TEST_LOAD_SUITE`) — 혼자 돌아야 숫자가 뜻이 있어 직렬이고, 예산을 넘은 케이스가 진다.
	if(NOT swLoadSeconds STREQUAL "")
		sw_computeTestTimeout(swLoadTimeout ${swLoadSeconds} 1 0)
		sw_registerTestRun(${TARGET_NAME}_Load ${TARGET_NAME} RUN_SERIAL
			ARGS --host_suites=all --load_suites=only --enforce_budgets LABELS "${swBaseLabels};load" TIMEOUT ${swLoadTimeout} ASAN_OPTIONS ${swAsanOptions})
	endif()
endfunction()

# ------------------------------------------------------------------------------
# sw_addTestExecutable — 테스트 실행 파일 타겟을 만들고 공통 PCH · 로그 태그 · ctest 등록을 한다
#
#   소스는 **이 폴더를 재귀로 훑는다**(목록이 없다 — 케이스 · 파일을 더해도 CMake 를 고치지 않는다). 폴더 규칙:
#     - `Dev/` 아래 파일은 개발 구성에서만 짓는다(Shipping 에 없는 기능의 테스트 — 핫 리로드 · 모듈 컴파일러).
#     - 꺼진 키트의 헤더를 include 하는 파일은 빠진다(`sw_excludeSourcesOfInactiveKits`).
#   EXTRA_SOURCES 폴더 소스에 더할 소스 — 테스트가 같이 컴파일하는 엔진 밖 소스(에디터 · App 은 링크할 라이브러리가 없다).
#   EXCLUDE       이 구성에서 짓지 않을 테스트 소스의 정규식(그 도구 타깃이 없는 구성 — "짓지 않는 소스" 로 적힌다).
#   LIBS · DEFINITIONS  링크할 것 · 정의.
#   라벨 · 샤드 · 제한 시간 · 호스트 분리 · ASan 옵션은 `Test/TestPolicy.json` 의 같은 이름 줄이 정한다(`sw_registerTestPolicy`).
#   폴더 이름은 실행 파일 이름과 같다(`Test/<이름>/`).
# ------------------------------------------------------------------------------
function(sw_addTestExecutable TARGET_NAME)
	cmake_parse_arguments(ARG "" "" "EXTRA_SOURCES;EXCLUDE;LIBS;DEFINITIONS" ${ARGN})
	if(ARG_UNPARSED_ARGUMENTS)
		message(FATAL_ERROR "sw_addTestExecutable(${TARGET_NAME}): 모르는 인자 ${ARG_UNPARSED_ARGUMENTS} — 라벨 · 샤드 · 시간은 Test/TestPolicy.json 에 둔다")
	endif()
	get_filename_component(swFolderName "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
	if(NOT swFolderName STREQUAL TARGET_NAME)
		message(FATAL_ERROR "sw_addTestExecutable(${TARGET_NAME}): 폴더 이름(${swFolderName})과 실행 파일 이름이 같아야 한다")
	endif()

	file(GLOB_RECURSE swSources CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")
	sw_excludeSourcesOfInactiveKits(swSources)
	if(SW_SHIPPING_BUILD)
		sw_excludeUnbuiltSources(swSources "/${TARGET_NAME}/Dev/")
	endif()
	if(ARG_EXCLUDE)
		sw_excludeUnbuiltSources(swSources ${ARG_EXCLUDE})
	endif()

	add_executable(${TARGET_NAME} ${swSources} ${ARG_EXTRA_SOURCES})
	target_sources(${TARGET_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/Test/TestFramework/main.cpp" "${CMAKE_SOURCE_DIR}/Test/TestFramework/Harness/TestHostRuntime.cpp")
	sw_embedProcessManifest(${TARGET_NAME})
	set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Test")
	add_dependencies(AllTests ${TARGET_NAME})

	# 테스트 실행 파일은 모든 구성에서 `Bin` 옆 `TestBin` 으로 뺀다 — Bin 에는 App · 모듈 · 런타임 DLL 만 둔다(Dev 도 Shipping 과 같은 모양).
	# 작업 폴더는 그래도 `Bin` 이다(`sw_registerTestRun`). Dev 테스트가 링크한 Engine.dll · 서드파티 DLL 은 Bin 에 있다 — Windows 로더는 작업 폴더를
	# 검색하므로 Bin 에서 띄우면 찾고, 모듈은 `FileUtil::getBinaryDirectory`(TestBin → Bin) 기준으로 `Bin/Modules` 에서 올린다.
	# 리눅스는 링크한 공유 라이브러리 폴더가 빌드 RPATH 에 절대 경로로 들어간다(CMake 기본).
	set(testOutputDir "${CMAKE_BINARY_DIR}/TestBin")
	# PDB 도 실행 파일 옆 — 테스트는 배포물이 아니고, 크래시 핸들러의 스택(DbgHelp)이 실행 파일 폴더에서 PDB 를 찾는다.
	set_target_properties(${TARGET_NAME} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${testOutputDir}$<0:>"
		PDB_OUTPUT_DIRECTORY "${testOutputDir}"
		PDB_OUTPUT_DIRECTORY_DEBUG "${testOutputDir}"
		PDB_OUTPUT_DIRECTORY_RELEASE "${testOutputDir}"
	)

	target_include_directories(${TARGET_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
	target_link_libraries(${TARGET_NAME}
		PRIVATE
		TestFramework
		${ARG_LIBS}
		sw_global_options
	)

	# Dev(Windows): 테스트가 링크한 모듈 DLL(키트 — `Bin/Modules`)은 OS 로더가 시작 때 찾지 못한다(실행 파일 폴더 TestBin · 작업 폴더 Bin 만 본다).
	# 그래서 링크한 모듈 DLL 을 실행 파일 옆(TestBin)에 복사한다. 키트끼리의 import 는 이미 올라온 같은 이름 이미지를 먼저 쓴다(OS 로더 규칙).
	# GameFramework 는 `Bin` 에 있어 작업 폴더에서 찾는다. 리눅스는 링크한 라이브러리 폴더가 빌드 RPATH 에 들어간다.
	if(WIN32 AND NOT SW_SHIPPING_BUILD)
		get_property(swModuleList GLOBAL PROPERTY SW_DYNAMIC_MODULES)
		get_property(swBinModuleList GLOBAL PROPERTY SW_DYNAMIC_MODULES_gameframework)
		set(swCopyCommands "")
		foreach(lib IN LISTS ARG_LIBS)
			if(lib IN_LIST swModuleList AND NOT lib IN_LIST swBinModuleList)
				list(APPEND swCopyCommands COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${lib}>" "${testOutputDir}/")
			endif()
		endforeach()
		if(swCopyCommands)
			add_custom_command(TARGET ${TARGET_NAME} POST_BUILD ${swCopyCommands}
				COMMENT "[${TARGET_NAME}] Copying linked module DLLs next to the test executable" VERBATIM)
		endif()
	endif()

	# App 과 같은 이유로 테스트 실행 파일도 리플렉션 정적 라이브러리를 통째로 링크한다 —
	# 왜 그래야 하는지, 플랫폼마다 무슨 플래그인지는 `sw_linkWholeArchive` 머리말에 있다.
	# 플랫폼 가드는 여기 두지 않는다: `WIN32` 로 가드하면 리눅스 Shipping 만 조용히 깨진다.
	# Engine 은 모든 테스트 실행 파일에 들어간다(TestFramework 가 끈다) — `LIBS` 에 적지 않은 실행 파일(CoreTest)도 Engine 의 등록기를
	# 통째로 받아야 기동 단계의 설정 역직렬화가 열거형을 찾는다. 같은 아카이브가 `LIBS` 에 또 나와도 된다.
	if(SW_SHIPPING_BUILD)
		sw_linkWholeArchive(${TARGET_NAME} Engine ${ARG_LIBS})
	endif()

	target_compile_definitions(${TARGET_NAME}
		PRIVATE
		"SW_LOG_TAG=\"Test\""
		${ARG_DEFINITIONS}
	)
	sw_configurePch(${TARGET_NAME} "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

	if(NOT BUILD_TESTING)
		return()
	endif()
	sw_registerTestPolicy(${TARGET_NAME})
endfunction()

# ------------------------------------------------------------------------------
# sw_registerScriptTest — 파이썬 스크립트 하나를 ctest 항목 하나로(파이썬 단위 테스트 · QA · 린트)
#
#   SCRIPT            저장소 기준 .py 경로
#   ARGS              스크립트 인자
#   LABELS · TIMEOUT  실행 파일 테스트와 같은 철자(라벨은 `Test/TestPolicy.json` 의 labels 표에 있어야 한다)
#   WORKING_DIRECTORY 기본은 저장소 루트(App 을 띄우는 QA 는 `Bin`)
#   RUN_SERIAL · SKIP_RETURN_CODE
# ------------------------------------------------------------------------------
function(sw_registerScriptTest TEST_NAME)
	cmake_parse_arguments(ARG "RUN_SERIAL" "SCRIPT;TIMEOUT;SKIP_RETURN_CODE;WORKING_DIRECTORY" "ARGS;LABELS" ${ARGN})
	if(NOT ARG_SCRIPT OR NOT ARG_TIMEOUT)
		message(FATAL_ERROR "sw_registerScriptTest(${TEST_NAME}): SCRIPT 와 TIMEOUT 은 꼭 준다")
	endif()
	sw_assertTestLabels("sw_registerScriptTest(${TEST_NAME})" ${ARG_LABELS})
	if(NOT ARG_WORKING_DIRECTORY)
		set(ARG_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
	endif()

	add_test(NAME ${TEST_NAME} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/${ARG_SCRIPT}" ${ARG_ARGS})
	set_tests_properties(${TEST_NAME} PROPERTIES
		WORKING_DIRECTORY "${ARG_WORKING_DIRECTORY}"
		LABELS "${ARG_LABELS}"
		TIMEOUT ${ARG_TIMEOUT}
		# 스크립트가 "이 테스트를 등록한 빌드" 를 안다 — 작업 폴더(저장소 루트)로는 알 수 없다(PythonTest_TestGenerators).
		ENVIRONMENT "SW_BUILD_DIR=${CMAKE_BINARY_DIR}"
	)
	if(ARG_RUN_SERIAL)
		set_tests_properties(${TEST_NAME} PROPERTIES RUN_SERIAL TRUE)
	endif()
	if(DEFINED ARG_SKIP_RETURN_CODE)
		set_tests_properties(${TEST_NAME} PROPERTIES SKIP_RETURN_CODE ${ARG_SKIP_RETURN_CODE})
	endif()
endfunction()
