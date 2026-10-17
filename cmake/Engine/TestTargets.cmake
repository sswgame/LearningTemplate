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

include("${CMAKE_CURRENT_LIST_DIR}/../Config/JsonValue.cmake")

#: 테스트 실행 파일 항목이 기다리는 ctest 픽스처 — 그 셋업 항목이 `all` 과 `AllTests` 를 짓는다(`sw_registerTestBuildFixture`).
set(SW_TEST_BUILD_FIXTURE "SWTestBinaries")

#: 테스트 등록 정책 — 실행 파일마다 영역 라벨 · 시간 · 샤드 · 호스트 분리, 그리고 쓸 수 있는 라벨 표.
#   값은 모두 `sw_requireJsonValue` · `sw_readOptionalJsonValue` 로 읽는다. 선택 키는 "없다는 사실이 곧 뜻" 인 것뿐이고 읽는 자리에 그 뜻을 적는다.
#   나머지 키(시간 공식 · 영역 · seconds 등)는 필수다 — 코드 쪽 기본값을 두지 않는다(정책 파일과 코드가 같은 숫자를 둘 다 들고 있게 된다).
set(SW_TEST_POLICY_FILE "${CMAKE_SOURCE_DIR}/Test/TestPolicy.json")
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SW_TEST_POLICY_FILE}")

# GPU 칸 — hostgpu 항목은 RUN_SERIAL 대신 ctest 자원 `gpu` 한 칸을 받는다(`RESOURCE_GROUPS gpu:1`). 칸 수는 정책 `gpuSlots`(GPU 공유 잠금의 두 프로세스와 같은 뜻).
# 자원 명세 파일은 이 빌드 폴더에 쓰고 `CTEST_RESOURCE_SPEC_FILE` 로 알린다 — `include(CTest)` 가 DartConfiguration 에 적어 `ctest` 가 인자 없이 쓴다.
sw_requireJsonValue(swGpuSlots "${SW_TEST_POLICY_FILE}" gpuSlots)
set(CTEST_RESOURCE_SPEC_FILE "${CMAKE_BINARY_DIR}/CTestResources.json")
file(CONFIGURE OUTPUT "${CTEST_RESOURCE_SPEC_FILE}" CONTENT
	"{\"version\":{\"major\":1,\"minor\":0},\"local\":[{\"gpu\":[{\"id\":\"0\",\"slots\":${swGpuSlots}}]}]}\n")

# ------------------------------------------------------------------------------
# sw_assertTestLabels — 라벨마다 정책의 `labels` 표에 있는지 본다. 모르는 라벨은 `ctest -L` 로 고를 수 없는 철자 실수다.
# ------------------------------------------------------------------------------
function(sw_assertTestLabels CONTEXT)
	foreach(swLabel IN LISTS ARGN)
		sw_readOptionalJsonValue(swType "${SW_TEST_POLICY_FILE}" TYPE labels "${swLabel}")
		if(NOT DEFINED swType)
			message(FATAL_ERROR "${CONTEXT}: 라벨 '${swLabel}' 이 Test/TestPolicy.json 의 labels 표에 없다 — 표에 뜻과 함께 더하거나 철자를 고친다")
		endif()
	endforeach()
endfunction()

# ------------------------------------------------------------------------------
# sw_computeTestTimeout — 제한 시간 = max(최소, 여유 배수 × max(조각 하나의 초, 가장 비싼 케이스의 초))
# ------------------------------------------------------------------------------
function(sw_computeTestTimeout OUT_VAR TOTAL_SECONDS SHARD_COUNT MAX_CASE_SECONDS)
	sw_requireJsonValue(swHeadroom "${SW_TEST_POLICY_FILE}" timeoutHeadroom)
	sw_requireJsonValue(swMinimum "${SW_TEST_POLICY_FILE}" minimumTimeoutSeconds)
	math(EXPR swShardSeconds "(${TOTAL_SECONDS} + ${SHARD_COUNT} - 1) / ${SHARD_COUNT}")
	if(MAX_CASE_SECONDS GREATER swShardSeconds)
		set(swShardSeconds ${MAX_CASE_SECONDS})
	endif()
	math(EXPR swTimeout "${swShardSeconds} * ${swHeadroom}")
	if(swTimeout LESS swMinimum)
		set(swTimeout ${swMinimum})
	endif()
	# 새니타이저 배율은 `sw_applySanitizerTestProperties` 가 곱한다 — 여기서는 플랫폼 배율만(둘을 겹치지 않는다).
	if(NOT SW_ENABLE_SANITIZER)
		sw_getTestTimeoutScale(swScale)
		math(EXPR swTimeout "${swTimeout} * ${swScale}")
	endif()
	set(${OUT_VAR} ${swTimeout} PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# sw_getTestTimeoutScale — 이 구성의 제한 시간 배율(정책 `timeoutScale`). 새니타이저면 sanitizer, 아니면 리눅스는 linux, 그 밖은 1.
#   CTest 제한 시간과 테스트가 띄우는 자식 프로세스의 시한(`SW_TEST_TIMEOUT_SCALE` → `test::ProcessDeadlineWatch`)이 같은 값을 쓴다.
# ------------------------------------------------------------------------------
function(sw_getTestTimeoutScale OUT_VAR)
	set(swScale 1)
	if(SW_ENABLE_SANITIZER)
		sw_requireJsonValue(swScale "${SW_TEST_POLICY_FILE}" timeoutScale sanitizer)
	elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		sw_requireJsonValue(swScale "${SW_TEST_POLICY_FILE}" timeoutScale linux)
	endif()
	set(${OUT_VAR} ${swScale} PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# sw_getRequiredBackendsArgument — GPU 항목에 넘길 `--required_backends=<이름,…>`(정책 `<POLICY_KEY>.<플랫폼>`, 필수 키).
#   POLICY_KEY 는 `requiredBackends`(hostgpu — 실제 GPU) 또는 `softwareRequiredBackends`(swrender — 소프트웨어 렌더러)다.
#   그 백엔드가 서지 못하면 케이스가 건너뛰지 않고 진다(C-41 3단계). 이름은 쿠킹 표의 명령줄 이름이고, 모르는 이름은 테스트 실행이 거절한다.
#   빌드에 없는 백엔드(배포본의 고르지 않은 것)는 실행 파일이 스스로 요구에서 뺀다(`test::isBackendInThisBuild`).
# ------------------------------------------------------------------------------
function(sw_getRequiredBackendsArgument OUT_VAR POLICY_KEY)
	string(TOLOWER "${CMAKE_SYSTEM_NAME}" swPlatform)
	sw_requireJsonValue(swCount "${SW_TEST_POLICY_FILE}" LENGTH ${POLICY_KEY} ${swPlatform})
	set(swNames "")
	if(swCount GREATER 0)
		math(EXPR swLast "${swCount} - 1")
		foreach(swIndex RANGE 0 ${swLast})
			sw_requireJsonValue(swName "${SW_TEST_POLICY_FILE}" ${POLICY_KEY} ${swPlatform} ${swIndex})
			list(APPEND swNames "${swName}")
		endforeach()
	endif()
	list(JOIN swNames "," swJoined)
	set(${OUT_VAR} "--required_backends=${swJoined}" PARENT_SCOPE)
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
	# 떨어져 결함처럼 보인다. 배율은 정책 `timeoutScale.sanitizer` 이고 테스트가 띄우는 자식 프로세스의 시한도 같은 배수다(`test::ProcessDeadlineWatch`).
	get_test_property(${TEST_NAME} TIMEOUT swCurrentTimeout)
	if(NOT swCurrentTimeout)
		set(swCurrentTimeout 30)
	endif()
	sw_getTestTimeoutScale(swScale)
	math(EXPR swCurrentTimeout "${swCurrentTimeout} * ${swScale}")

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
#   COST          예상 초 — ctest 가 병렬 실행에서 긴 항목부터 띄운다(실행 기록이 없는 새 빌드 폴더 · CI 도 같은 순서).
#   GPU           GPU 칸 하나를 받는다(`RESOURCE_GROUPS gpu:1`, 칸 수는 정책 `gpuSlots`).
#   RESOURCE_LOCK 같은 이름을 쥔 항목끼리는 동시에 돌지 않는다(같은 `Bin` 에 산출물을 쓰는 App 실행 등).
function(sw_registerTestRun TEST_NAME TARGET_NAME)
	cmake_parse_arguments(ARG "RUN_SERIAL;GPU" "TIMEOUT;COST;RESOURCE_LOCK" "ARGS;LABELS;ASAN_OPTIONS" ${ARGN})

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
	if(ARG_COST)
		set_tests_properties(${TEST_NAME} PROPERTIES COST ${ARG_COST})
	endif()
	if(ARG_GPU)
		set_tests_properties(${TEST_NAME} PROPERTIES RESOURCE_GROUPS "gpu:1")
	endif()
	if(ARG_RESOURCE_LOCK)
		set_tests_properties(${TEST_NAME} PROPERTIES RESOURCE_LOCK ${ARG_RESOURCE_LOCK})
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
	cmake_parse_arguments(ARG "RUN_SERIAL;GPU" "TIMEOUT;COSTS;COST;RESOURCE_LOCK" "ARGS;LABELS;ASAN_OPTIONS" ${ARGN})
	set(runSerial "")
	if(ARG_RUN_SERIAL)
		set(runSerial RUN_SERIAL)
	endif()
	if(ARG_GPU)
		list(APPEND runSerial GPU)
	endif()
	if(ARG_RESOURCE_LOCK)
		list(APPEND runSerial RESOURCE_LOCK ${ARG_RESOURCE_LOCK})
	endif()
	if(ARG_COST)
		list(APPEND runSerial COST ${ARG_COST})
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
#   라벨: `unit;<area>` 에 갈래마다 `nogpu`(CI 가 도는 집합) · `hostgpu`(호스트 스위트, GPU 칸) · `load`(부하 스위트, 직렬) · `race`(Debug 레이스 묶음)를 붙인다.
#   샤드: `shards` 가 없으면 ceil(seconds / targetShardSeconds). 비용표(`caseCosts`)는 샤드를 비용으로 고르게 나누는 데 넘긴다.
#   이름: 갈래가 없으면 `<타깃>`(샤드면 `_Shard<k>`), `hostSplit` 이면 `<타깃>_NoGPU` · `<타깃>_HostOnly`, `loadSeconds` 가 있으면 `<타깃>_Load`,
#         `raceCases` 가 있으면(Debug) `<타깃>_Race`.
#         갈라진 항목 말고 전체 실행을 하나 더 등록하지 않는다(라벨 없는 `ctest` 가 같은 테스트를 두 번 돈다).
# ------------------------------------------------------------------------------
function(sw_registerTestPolicy TARGET_NAME)
	set(swPolicy "${SW_TEST_POLICY_FILE}")
	sw_readOptionalJsonValue(swRowType "${swPolicy}" TYPE executables ${TARGET_NAME})
	if(NOT DEFINED swRowType)
		message(FATAL_ERROR "sw_addTestExecutable(${TARGET_NAME}): Test/TestPolicy.json 의 executables 에 줄이 없다 — 영역(area)과 한 실행 시간(seconds)을 더한다")
	endif()

	sw_requireJsonValue(swArea "${swPolicy}" executables ${TARGET_NAME} area)
	sw_requireJsonValue(swSeconds "${swPolicy}" executables ${TARGET_NAME} seconds)
	sw_requireJsonValue(swTargetShardSeconds "${swPolicy}" targetShardSeconds)
	# 선택 키 — 없을 때의 뜻: shards 없음 = seconds 로 계산, hostSplit 없음 = 호스트 스위트로 가르지 않음, loadSeconds 없음 = 부하 스위트 없음.
	sw_readOptionalJsonValue(swShards "${swPolicy}" executables ${TARGET_NAME} shards)
	sw_readOptionalJsonValue(swHostSplit "${swPolicy}" executables ${TARGET_NAME} hostSplit)
	sw_readOptionalJsonValue(swLoadSeconds "${swPolicy}" executables ${TARGET_NAME} loadSeconds)
	sw_assertTestLabels("sw_addTestExecutable(${TARGET_NAME})" ${swArea})

	# 선택 키 — asanOptions 없음 = 공통 ASan 옵션만.
	set(swAsanOptions "")
	sw_readOptionalJsonValue(swAsanCount "${swPolicy}" LENGTH executables ${TARGET_NAME} asanOptions)
	if(DEFINED swAsanCount AND swAsanCount GREATER 0)
		math(EXPR swLastAsan "${swAsanCount} - 1")
		foreach(swIndex RANGE 0 ${swLastAsan})
			sw_requireJsonValue(swOption "${swPolicy}" executables ${TARGET_NAME} asanOptions ${swIndex})
			list(APPEND swAsanOptions "${swOption}")
		endforeach()
	endif()

	# 비용표 둘: `caseCosts`(nogpu 갈래 · 갈래 없는 실행)와 `hostCaseCosts`(`_HostOnly` 조각). 따로 두는 까닭: 가장 비싼 케이스가
	# 그 갈래의 제한 시간을 정하므로, 분 단위 호스트 케이스가 몇 초짜리 nogpu 조각의 제한 시간을 늘리지 않게 한다. 키가 없으면 비용 없는 고른 분배다.
	foreach(swCostKey IN ITEMS caseCosts hostCaseCosts)
		set(swKeyCosts "")
		set(swKeyMax 0)
		sw_readOptionalJsonValue(swCostCount "${swPolicy}" LENGTH executables ${TARGET_NAME} ${swCostKey})
		if(DEFINED swCostCount AND swCostCount GREATER 0)
			math(EXPR swLastCost "${swCostCount} - 1")
			foreach(swIndex RANGE 0 ${swLastCost})
				sw_requireJsonValue(swCaseName "${swPolicy}" MEMBER executables ${TARGET_NAME} ${swCostKey} ${swIndex})
				sw_requireJsonValue(swCaseSeconds "${swPolicy}" executables ${TARGET_NAME} ${swCostKey} ${swCaseName})
				if(swKeyCosts)
					string(APPEND swKeyCosts ",")
				endif()
				string(APPEND swKeyCosts "${swCaseName}=${swCaseSeconds}")
				if(swCaseSeconds GREATER swKeyMax)
					set(swKeyMax ${swCaseSeconds})
				endif()
			endforeach()
		endif()
		if(swCostKey STREQUAL "caseCosts")
			set(swCosts "${swKeyCosts}")
			set(swMaxCaseSeconds ${swKeyMax})
		else()
			set(swHostCosts "${swKeyCosts}")
			set(swHostMaxCaseSeconds ${swKeyMax})
		endif()
	endforeach()

	if(NOT DEFINED swShards)
		math(EXPR swShards "(${swSeconds} + ${swTargetShardSeconds} - 1) / ${swTargetShardSeconds}")
	endif()
	if(swShards LESS 1)
		set(swShards 1)
	endif()
	sw_computeTestTimeout(swTimeout ${swSeconds} ${swShards} ${swMaxCaseSeconds})
	math(EXPR swShardCost "(${swSeconds} + ${swShards} - 1) / ${swShards}")

	# 같은 `Bin` 에 산출물을 쓰는 호스트 실행은 정책의 `hostResourceLock` 으로 서로 비켜 간다(호스트 조각 · 레이스 묶음이 같이 쓴다).
	# 선택 키다 — 키가 없으면 "다른 항목과 겹쳐도 되는 실행" 이라는 뜻이다(EngineTest 호스트 조각은 App 을 띄우지 않는다).
	sw_readOptionalJsonValue(swHostLock "${swPolicy}" executables ${TARGET_NAME} hostResourceLock)
	set(swHostLockArgs "")
	if(DEFINED swHostLock)
		set(swHostLockArgs RESOURCE_LOCK ${swHostLock})
	endif()

	set(swBaseLabels "unit;${swArea}")
	if(NOT swHostSplit)
		sw_registerTestShards(${TARGET_NAME} ${TARGET_NAME} ${swShards}
			LABELS "${swBaseLabels};nogpu" TIMEOUT ${swTimeout} COSTS "${swCosts}" COST ${swShardCost} ASAN_OPTIONS ${swAsanOptions})
	else()
		sw_registerTestShards(${TARGET_NAME}_NoGPU ${TARGET_NAME} ${swShards}
			ARGS --host_suites=exclude LABELS "${swBaseLabels};nogpu" TIMEOUT ${swTimeout} COSTS "${swCosts}" COST ${swShardCost} ASAN_OPTIONS ${swAsanOptions})
		# 창을 띄우고 GPU 를 잡으므로 GPU 칸 하나를 받는다(동시에 `gpuSlots` 개까지 — 공유 GPU 잠금과 같은 수).
		# 조각마다 호스트 케이스가 하나는 있어야 한다(`--host_suites=only` 가 아무것도 고르지 않으면 진다).
		# 갈라 내면 호스트 갈래의 초 · 조각 수는 필수다 — nogpu 의 seconds 와 뜻이 다르다(분 단위 시나리오).
		sw_requireJsonValue(swHostSeconds "${swPolicy}" executables ${TARGET_NAME} hostSeconds)
		sw_requireJsonValue(swHostShards "${swPolicy}" executables ${TARGET_NAME} hostShards)
		sw_computeTestTimeout(swHostTimeout ${swHostSeconds} ${swHostShards} ${swHostMaxCaseSeconds})
		math(EXPR swHostShardCost "(${swHostSeconds} + ${swHostShards} - 1) / ${swHostShards}")
		# 이 PC 에 있어야 할 백엔드가 서지 못하면 건너뛰지 않고 진다(정책 `requiredBackends`).
		sw_getRequiredBackendsArgument(swRequiredBackends requiredBackends)
		sw_registerTestShards(${TARGET_NAME}_HostOnly ${TARGET_NAME} ${swHostShards} GPU ${swHostLockArgs} COST ${swHostShardCost}
			ARGS --host_suites=only ${swRequiredBackends} LABELS "${swBaseLabels};hostgpu" TIMEOUT ${swHostTimeout} COSTS "${swHostCosts}" ASAN_OPTIONS ${swAsanOptions})
	endif()

	# 부하 · 시간 예산 검증(`SW_TEST_LOAD_SUITE`) — 혼자 돌아야 숫자가 뜻이 있어 직렬이다. 예산을 넘은 케이스는 Release 에서만 진다
	# (Debug 는 경합 검출기 · 할당 추적이 시간을 부풀리고, Shipping 은 측정 로그가 없다 — 다른 구성은 정확성만 보고 예산은 요약에 찍기만 한다).
	if(DEFINED swLoadSeconds)
		sw_computeTestTimeout(swLoadTimeout ${swLoadSeconds} 1 0)
		set(swLoadArgs --host_suites=all --load_suites=only)
		if(CMAKE_BUILD_TYPE STREQUAL "Release")
			list(APPEND swLoadArgs --enforce_budgets)
		endif()
		sw_registerTestRun(${TARGET_NAME}_Load ${TARGET_NAME} RUN_SERIAL
			ARGS ${swLoadArgs} LABELS "${swBaseLabels};load" TIMEOUT ${swLoadTimeout} ASAN_OPTIONS ${swAsanOptions})
	endif()

	# 레이스 묶음(`race`) — 스레드 경로를 지나는 케이스(`raceCases`, `Suite.Case` · `Suite.*`)만 호스트 스위트까지 넣어 한 실행으로 돈다.
	# Debug 에서만 등록한다: `DataRaceDetector` 는 `SW_DEBUG`(Debug 구성)에서만 켜지므로 다른 구성에서는 같은 케이스를 nogpu · hostgpu 가 이미 돈다.
	# 호스트 실행 파일(`hostSplit`)이면 호스트 조각과 같은 GPU 칸 · 잠금을 받는다. 라벨 없는 `ctest` 에서는 이 케이스들이 한 번 더 돈다(묶음이 작아 받아들인다).
	# 선택 키 — raceCases 없음 = 레이스 묶음에 넣을 스레드 경로가 없음. 있으면 raceSeconds(그 한 실행의 Debug 초)는 필수다.
	sw_readOptionalJsonValue(swRaceCount "${swPolicy}" LENGTH executables ${TARGET_NAME} raceCases)
	if(DEFINED swRaceCount AND swRaceCount GREATER 0 AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT SW_SHIPPING_BUILD)
		set(swRaceFilter "")
		math(EXPR swLastRace "${swRaceCount} - 1")
		foreach(swIndex RANGE 0 ${swLastRace})
			sw_requireJsonValue(swRaceCase "${swPolicy}" executables ${TARGET_NAME} raceCases ${swIndex})
			if(swRaceFilter)
				string(APPEND swRaceFilter ",")
			endif()
			string(APPEND swRaceFilter "${swRaceCase}")
		endforeach()
		sw_requireJsonValue(swRaceSeconds "${swPolicy}" executables ${TARGET_NAME} raceSeconds)
		sw_computeTestTimeout(swRaceTimeout ${swRaceSeconds} 1 0)
		set(swRaceExtra "")
		set(swRaceRequired "")
		if(swHostSplit)
			set(swRaceExtra GPU ${swHostLockArgs})
			sw_getRequiredBackendsArgument(swRaceRequired requiredBackends)
		endif()
		sw_registerTestRun(${TARGET_NAME}_Race ${TARGET_NAME} ${swRaceExtra} COST ${swRaceSeconds}
			ARGS --host_suites=all "--test_filter=${swRaceFilter}" ${swRaceRequired} LABELS "${swBaseLabels};race" TIMEOUT ${swRaceTimeout} ASAN_OPTIONS ${swAsanOptions})
	endif()

	# 소프트웨어 렌더러 묶음(`swrender`, C-41 4단계) — GPU 없는 CI 러너가 소프트웨어 래스터라이저(Windows WARP · 리눅스 lavapipe/llvmpipe + xvfb)로
	# 도는 호스트 케이스(`softwareRenderCases`)만. `SW_RHI_SOFTWARE_ADAPTER=1` 로 디바이스가 소프트웨어 어댑터를 고르고, 정책 `softwareRequiredBackends`
	# 의 백엔드가 서지 못하면 진다. 그림 비교(골든)는 하지 않는다 — 소프트웨어 렌더러 전용 골든은 두지 않는다(결정 기록 5-2 · 5-12).
	# 선택 키 — 없으면 이 실행 파일에 소프트웨어 렌더러로 도는 케이스가 없다. 있으면 softwareRenderSeconds 는 필수다.
	sw_readOptionalJsonValue(swSoftwareCount "${swPolicy}" LENGTH executables ${TARGET_NAME} softwareRenderCases)
	if(DEFINED swSoftwareCount AND swSoftwareCount GREATER 0 AND swHostSplit)
		set(swSoftwareFilter "")
		math(EXPR swLastSoftware "${swSoftwareCount} - 1")
		foreach(swIndex RANGE 0 ${swLastSoftware})
			sw_requireJsonValue(swSoftwareCase "${swPolicy}" executables ${TARGET_NAME} softwareRenderCases ${swIndex})
			if(swSoftwareFilter)
				string(APPEND swSoftwareFilter ",")
			endif()
			string(APPEND swSoftwareFilter "${swSoftwareCase}")
		endforeach()
		sw_requireJsonValue(swSoftwareSeconds "${swPolicy}" executables ${TARGET_NAME} softwareRenderSeconds)
		sw_computeTestTimeout(swSoftwareTimeout ${swSoftwareSeconds} 1 0)
		sw_getRequiredBackendsArgument(swSoftwareRequired softwareRequiredBackends)
		sw_registerTestRun(${TARGET_NAME}_SoftwareRender ${TARGET_NAME} GPU ${swHostLockArgs} COST ${swSoftwareSeconds}
			ARGS --host_suites=all "--test_filter=${swSoftwareFilter}" ${swSoftwareRequired} LABELS "${swBaseLabels};swrender" TIMEOUT ${swSoftwareTimeout}
			ASAN_OPTIONS ${swAsanOptions})
		set_property(TEST ${TARGET_NAME}_SoftwareRender APPEND PROPERTY ENVIRONMENT "SW_RHI_SOFTWARE_ADAPTER=1")
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
#   RUN_SERIAL · SKIP_RETURN_CODE · GPU · RESOURCE_LOCK(`sw_registerTestRun` 과 같은 뜻)
# ------------------------------------------------------------------------------
function(sw_registerScriptTest TEST_NAME)
	cmake_parse_arguments(ARG "RUN_SERIAL;GPU" "SCRIPT;TIMEOUT;SKIP_RETURN_CODE;WORKING_DIRECTORY;RESOURCE_LOCK" "ARGS;LABELS" ${ARGN})
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
	if(ARG_GPU)
		set_tests_properties(${TEST_NAME} PROPERTIES RESOURCE_GROUPS "gpu:1")
	endif()
	if(ARG_RESOURCE_LOCK)
		set_tests_properties(${TEST_NAME} PROPERTIES RESOURCE_LOCK ${ARG_RESOURCE_LOCK})
	endif()
	if(DEFINED ARG_SKIP_RETURN_CODE)
		set_tests_properties(${TEST_NAME} PROPERTIES SKIP_RETURN_CODE ${ARG_SKIP_RETURN_CODE})
	endif()
endfunction()
