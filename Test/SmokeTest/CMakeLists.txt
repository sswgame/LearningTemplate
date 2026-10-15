# ==============================================================================
# @file Test/SmokeTest/CMakeLists.txt
# @brief Dev: MODULE DLL export*API 로드 스모크 / Shipping: 정적 exportGameAPI 스모크
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) SmokeTest 타겟 정의 및 의존성 구성
# ------------------------------------------------------------------------------
set(smokeLibs Engine RuntimeAPI)
if(SW_SHIPPING_BUILD)
	get_property(swActiveGameTarget GLOBAL PROPERTY SW_ACTIVE_GAME_TARGET) # SWGame_<활성 게임>(`sw_addGameModule`)
	list(APPEND smokeLibs ${swActiveGameTarget})
endif()

# 핫리로드 · 모듈 백그라운드 컴파일은 Dev 에만 있다 — Shipping 스모크는 정적 exportGameAPI 경로만 본다.
# (TestSmoke.cpp 도 같은 기준으로 갈라져 있다: 리로드 케이스는 #if !defined(SW_SHIPPING) 안이다.)
set(swSmokeDevSources "")
if(NOT SW_SHIPPING_BUILD)
	set(swSmokeDevSources
		${CMAKE_SOURCE_DIR}/Source/ModuleHost/LiveReloadManager.cpp
		${CMAKE_SOURCE_DIR}/Source/ModuleHost/ModuleDependencyGraph.cpp
		${CMAKE_SOURCE_DIR}/Source/ModuleHost/ModuleShadowCopy.cpp
		${CMAKE_SOURCE_DIR}/Source/ModuleHost/DeferredImageUnloadQueue.cpp
		${CMAKE_SOURCE_DIR}/Source/ModuleHost/ModuleCallGuard.cpp
		${CMAKE_SOURCE_DIR}/Source/ModuleHost/ModuleImagePatch.cpp
		${CMAKE_SOURCE_DIR}/Source/ModuleHost/ShadowCopyName.cpp
		${CMAKE_SOURCE_DIR}/Source/ModuleHost/ModuleCatalogLoader.cpp
	)
endif()

# `ModuleHost` 는 구성과 무관하게 가져온다 — 검사하는 것이 "디바이스가 없을 때의 호스트" 라
# 배포 구성에서도 같은 자리를 지나가야 한다(그쪽은 게임이 정적 링크라 경로가 오히려 더 깊다).
#
# `RUN_SERIAL` 은 **일부러 없다**. 다른 테스트와 함께 돌아도 되는 근거: `Bin` 에 쓰는 파일이 없고(임시 파일은 케이스 폴더
# `%TEMP%/sw_<pid>/…`), 로그는 이름이 실행마다 다르고, Vulkan 파이프라인 캐시는 원자적으로 쓴다. 직렬로 묶으면
# `ctest -L "nogpu|lint" -j 4` 의 끝이 이 테스트와 EngineTest_NoGPU 가 혼자 도는 구간이 된다. GPU 를 잡는 `_HostOnly` 는 그대로
# 직렬이다(`sw_addTestExecutable`).
# 테스트 소스는 이 폴더를 훑는다 — `Dev/` 의 케이스(핫 리로드 · 매니페스트 · 의존 그래프 · 지연 언로드)는 개발 구성에서만 짓는다.
# 조각 · 시간 · ASan 옵션(report_globals=0 — 이유는 정책의 `_asanOptionsReason`)은 Test/TestPolicy.json.
sw_addTestExecutable(SmokeTest
	EXTRA_SOURCES
		${CMAKE_SOURCE_DIR}/Source/ModuleHost/ModuleHost.cpp
		# App 전용 층(에디터 인스턴스 · RHI 교체 뒤 재초기화)도 같은 테스트가 본다 — App 은 실행 파일이라 소스로 가져온다.
		${CMAKE_SOURCE_DIR}/Source/App/EditorModuleHost.cpp
		# `ModuleCompiler` 는 배포 구성에도 들어간다 — `ModuleHost` 의 소스 규칙과 같다(ModuleHost 는 배포에서
		# 핫 리로드 파일만 뺀다). `ModuleHost::shutdown` 이 구성과 무관하게 이 심볼을 부른다.
		${CMAKE_SOURCE_DIR}/Source/ModuleHost/ModuleCompiler.cpp
		${swSmokeDevSources}
	LIBS
		${smokeLibs}
)

if(NOT SW_SHIPPING_BUILD)
	target_compile_definitions(SmokeTest PRIVATE
		"SW_TEST_MODULE_DIR=\"${CMAKE_BINARY_DIR}/Bin/Modules\""
	)
	# 이 테스트는 모듈 DLL 을 실제로 올렸다 내린다 — 백엔드 · 게임 · 에디터가 옆에 있어야 한다. 이름을 적지 않고 레지스트리에 묻는다.
	sw_addDynamicModuleDependencies(SmokeTest KINDS rhi game editor)
endif()
