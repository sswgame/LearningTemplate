# ==============================================================================
# @file Test/PythonTest/CMakeLists.txt
# @brief 파이썬 도구의 단위 시험(nogpu)과 빌드된 App 을 돌리는 QA 러너(hostgpu · soak · perf)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 단위 시험 — 이 폴더의 `Test*.py` 하나 = CTest 항목 하나(`PythonTest_<이름>`)
#
# 목록을 적지 않는다. 파일을 놓으면 그것이 시험이다(린트 게이트와 같은 규칙). 시험은 표준 `unittest` 만 쓰고
# 저장소 루트에서 돈다 — GPU · 빌드 산출물이 필요 없어 CI 가 도는 `nogpu` 다.
# ------------------------------------------------------------------------------
file(GLOB listPythonTestScript CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Test*.py")
foreach(scriptPath IN LISTS listPythonTestScript)
	get_filename_component(scriptName "${scriptPath}" NAME_WE)
	sw_registerScriptTest(PythonTest_${scriptName}
		SCRIPT "Test/PythonTest/${scriptName}.py"
		LABELS "nogpu;unit;python"
		TIMEOUT 120
	)
endforeach()

# ------------------------------------------------------------------------------
# 2) QA 러너 — 빌드된 App 을 띄운다(`Scripts/qa/`)
#
# - `QaGoldenImages` (hostgpu): 활성 게임(`SW_ACTIVE_GAME`)의 자동 플레이를 네 백엔드로 그려 `Test/Golden/Games/<게임>/` 의 기준과
#   배경을 뺀 지표로 견준다. 기준이 없는 백엔드 · 이 기계에서 못 도는 백엔드는 건너뛴다(로그에 남긴다). 직렬 — 창과 GPU 를 잡는다.
# - `QaSoak` (soak): 자동 플레이를 오래 돌려 메모리 · 핸들 증가와 프레임 p99 를 본다. 분 단위라 `-L soak` 로 고른다(Shipping 없음).
# - `QaPerfRegression` (perf): Release 에서만 — 기준(`Test/Perf/Baseline/`)과 p50 · p99 를 견준다. 기계마다 기준이 다르다.
# ------------------------------------------------------------------------------
if(TARGET App)
	sw_registerScriptTest(QaGoldenImages
		SCRIPT "Scripts/qa/GoldenImages.py"
		ARGS --app "$<TARGET_FILE:App>" --game "${SW_ACTIVE_GAME}" --diff-dir "${sw_output_directory}/GoldenDiff"
		WORKING_DIRECTORY "${sw_output_directory}/Bin"
		LABELS "hostgpu;app;qa"
		TIMEOUT 900
		RUN_SERIAL
		SKIP_RETURN_CODE 77
	)

	# soak 은 프로파일 표(Info 로그)를 읽는다 — Shipping 은 Info 로그를 빼고 지으므로 등록하지 않는다.
	if(NOT SW_SHIPPING_BUILD)
		sw_registerScriptTest(QaSoak
			SCRIPT "Scripts/qa/Soak.py"
			ARGS --app "$<TARGET_FILE:App>" --game "${SW_ACTIVE_GAME}" --minutes 2
			WORKING_DIRECTORY "${sw_output_directory}/Bin"
			LABELS "soak;qa"
			TIMEOUT 600
			RUN_SERIAL
			SKIP_RETURN_CODE 77
		)
	endif()

	if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT SW_SHIPPING_BUILD)
		sw_registerScriptTest(QaPerfRegression
			SCRIPT "Scripts/qa/PerfRegression.py"
			ARGS --app "$<TARGET_FILE:App>" --game "${SW_ACTIVE_GAME}"
			WORKING_DIRECTORY "${sw_output_directory}/Bin"
			LABELS "perf;qa"
			TIMEOUT 900
			RUN_SERIAL
			SKIP_RETURN_CODE 77
		)
	endif()
endif()
