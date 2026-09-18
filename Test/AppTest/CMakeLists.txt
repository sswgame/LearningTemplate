# ==============================================================================
# @file Test/AppTest/CMakeLists.txt
# @brief App(런처) 테스트 — 혼자 도는 정책(nogpu) + 실기동 스모크(hostgpu)
# ==============================================================================

# `App` 은 실행 파일이라 링크할 라이브러리가 없다. 그래서 테스트가 볼 수 있는 것은 **파일 단위로
# 가져온 소스**뿐이고, 여기 담는 것은 엔진 상태 없이 혼자 도는 정책 코드로 한정한다(프레임 시간
# 정책 등). 그 경계를 지켜야 이 타깃이 "App 을 통째로 세우는 두 번째 자리" 가 되지 않는다.
#
# **기동 전체는 프로세스를 띄워서 본다**(`TestAppSmoke.cpp`) — `EngineLoop` 은 어떤 단위 테스트도
# 돌리지 않으므로, 그 그물은 실제 `App.exe` 를 돌려 보는 것뿐이다. 그 집합은 GPU·창이 필요하니
# 아래에서 `hostgpu` 로 갈라 둔다.
#
# 소스를 명시하는 이유도 같다. 글롭이면 App 소스를 하나 끌어올 때마다 그 의존성이 조용히 따라온다.
sw_addTestExecutable(AppTest
	SOURCES
		TestAppSmoke.cpp
		TestFrameTimeline.cpp
		${CMAKE_SOURCE_DIR}/Source/App/Frame/FrameTimeline.cpp
	LIBS
		Engine
	LABELS
		# `nogpu` 는 여기 없다 — 아래 `AppTest_NoGPU` 가 스모크를 빼고 그 라벨을 단다.
		"unit;app"
	TIMEOUT
		180
)

# 스모크는 **빌드된 App 을 띄운다** — 타깃이 옆에 서 있어야 한다.
if(TARGET App)
	add_dependencies(AppTest App)
endif()
if(TARGET EditorModule)
	add_dependencies(AppTest EditorModule)
endif()
if(TARGET SWGame)
	add_dependencies(AppTest SWGame)
endif()
sw_getDynamicModules(listRhiBackend KINDS rhi)
foreach(rhiMod IN LISTS listRhiBackend)
	add_dependencies(AppTest ${rhiMod})
endforeach()

if(BUILD_TESTING)
	# --------------------------------------------------------------------------
	# ctest — NoGPU: 스모크만 뺀 나머지 (CI 가 도는 집합)
	# --------------------------------------------------------------------------
	add_test(
		NAME AppTest_NoGPU
		COMMAND AppTest --test_filter=-AppSmokeTest.*
	)
	set_tests_properties(AppTest_NoGPU PROPERTIES
		WORKING_DIRECTORY "${sw_output_directory}/Bin"
		LABELS "unit;app;nogpu"
		TIMEOUT 60
	)
	sw_applySanitizerTestProperties(AppTest_NoGPU)

	# --------------------------------------------------------------------------
	# ctest — HostOnly: 위 필터가 빼는 **바로 그 집합** (백로그 0절의 실기동 게이트)
	#
	# 이름을 두 번 적는 것은 맞다 — 위는 빼는 목록, 여기는 고르는 목록이라 형태가 다르다.
	# 대신 **갈라지지 못하게 막는다**: `CheckTestSuites.py` 가 두 필터와 `SW_TEST_REQUIRES_HOST`
	# 마커를 모두 대조한다.
	#
	# `RUN_SERIAL` 인 이유: 창을 띄우고 GPU 를 잡는다. 다른 GPU 테스트와 겹치면 서로를 느리게
	# 만들고, 드라이버에 따라 서로의 디바이스 생성을 막는다.
	# --------------------------------------------------------------------------
	add_test(
		NAME AppTest_HostOnly
		COMMAND AppTest --test_filter=AppSmokeTest.*
	)
	set_tests_properties(AppTest_HostOnly PROPERTIES
		WORKING_DIRECTORY "${sw_output_directory}/Bin"
		LABELS "unit;app;hostgpu"
		RUN_SERIAL TRUE
		TIMEOUT 300
	)
	sw_applySanitizerTestProperties(AppTest_HostOnly)
endif()
