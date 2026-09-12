# ==============================================================================
# @file Test/SmokeTest/CMakeLists.txt
# @brief Dev: MODULE DLL fill*API 로드 스모크 / Shipping: 정적 fillGameAPI 스모크
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) SmokeTest 타겟 정의 및 의존성 구성
# ------------------------------------------------------------------------------
set(smokeLibs Engine RuntimeAPI)
if(SW_SHIPPING_BUILD)
	list(APPEND smokeLibs SWGame)
endif()

# 핫리로드 · 모듈 백그라운드 컴파일은 Dev 에만 있다 — Shipping 스모크는 정적 fillGameAPI 경로만 본다.
# (TestSmoke.cpp 도 같은 기준으로 갈라져 있다: 리로드 케이스는 #if !defined(SW_SHIPPING) 안이다.)
set(swSmokeDevSources "")
if(NOT SW_SHIPPING_BUILD)
	set(swSmokeDevSources
		${CMAKE_SOURCE_DIR}/Source/App/Module/LiveReloadManager.cpp
		${CMAKE_SOURCE_DIR}/Source/App/Module/ModuleCompiler.cpp
	)
endif()

sw_addTestExecutable(SmokeTest
	RUN_SERIAL
	SOURCES
		TestSmoke.cpp
		${swSmokeDevSources}
	LIBS
		${smokeLibs}
	LABELS
		"unit;module;nogpu"
	TIMEOUT
		180
	# ASan 의 전역 오버플로 검사를 이 스위트에서만 끈다.
	#
	# 이 ASan 런타임은 **DLL 을 내려도 그 모듈의 전역 등록을 지우지 않는다.** 측정값:
	# report_globals=2 로 찍어 보면 이 스위트 한 번에 "Added Global" 5241건, 제거 0건이다.
	# 그래서 모듈을 내린 뒤 다음 모듈이 그 주소 범위에 매핑되면, 새 DLL 이 **자기 전역을**
	# 읽는데도 앞 모듈이 남긴 레드존을 밟아 global-buffer-overflow 로 보고된다(실제로 잡힌
	# 것은 RHI_DX12 의 정적 초기화가 SW_LOG_CALLER 의 __FILE__ 를 읽는 자리였다). 게다가
	# 보고하려고 주소를 설명하는 순간 이미 언매핑된 모듈의 디스크립터를 역참조해
	# "nested bug in the same thread, aborting" 으로 프로세스가 죽는다 — 예전에 보이던
	# LoadLibrary err=1114 와 맨 세그폴트가 이것이다.
	#
	# 이 스위트는 존재 이유가 모듈을 올렸다 내리는 것이라 우회할 방법이 없다. 전역 검사만
	# 끄면 힙·스택·use-after-free 는 그대로 잡히고 19개 케이스가 전부 돈다(예전에는 스위트째
	# Disabled 였다). 다른 4개 테스트는 전역 검사를 켠 채로 남는다.
	ASAN_OPTIONS
		report_globals=0
)

target_include_directories(SmokeTest PRIVATE "${CMAKE_SOURCE_DIR}/Source/App")

if(NOT SW_SHIPPING_BUILD)
	target_compile_definitions(SmokeTest PRIVATE
		"SW_TEST_MODULE_DIR=\"${sw_output_directory}/Bin\""
	)
	if(TARGET SWGame)
		add_dependencies(SmokeTest SWGame)
	endif()
	if(TARGET EditorModule)
		add_dependencies(SmokeTest EditorModule)
	endif()
	if(TARGET RHI_DX11)
		add_dependencies(SmokeTest RHI_DX11)
	endif()
endif()
