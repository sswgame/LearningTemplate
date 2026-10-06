# ==============================================================================
# @file Test/SmokeTest/CMakeLists.txt
# @brief Dev: MODULE DLL export*Api 로드 스모크 / Shipping: 정적 exportGameApi 스모크
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) SmokeTest 타겟 정의 및 의존성 구성
# ------------------------------------------------------------------------------
set(smokeLibs Engine RuntimeAPI)
if(SW_SHIPPING_BUILD)
	list(APPEND smokeLibs SWGame)
endif()

# 핫리로드 · 모듈 백그라운드 컴파일은 Dev 에만 있다 — Shipping 스모크는 정적 exportGameApi 경로만 본다.
# (TestSmoke.cpp 도 같은 기준으로 갈라져 있다: 리로드 케이스는 #if !defined(SW_SHIPPING) 안이다.)
set(swSmokeDevSources "")
if(NOT SW_SHIPPING_BUILD)
	set(swSmokeDevSources
		${CMAKE_SOURCE_DIR}/Source/App/Module/LiveReloadManager.cpp
		# 패처는 파일 바이트만 다루므로 Windows 에서도 합성 ELF 로 잰다(리눅스 핫 리로드의 SONAME 고유화).
		TestModuleImagePatch.cpp
		TestModuleCallGuard.cpp
		TestShadowCopyName.cpp
	)
endif()

# `ModuleHost` 는 구성과 무관하게 가져온다 — 검사하는 것이 "디바이스가 없을 때의 호스트" 라
# 배포 구성에서도 같은 자리를 지나가야 한다(그쪽은 게임이 정적 링크라 경로가 오히려 더 깊다).
#
# `RUN_SERIAL` 은 **일부러 없다**. 다른 테스트와 함께 돌아도 되는 근거: `Bin` 에 쓰는 파일이 없고(임시 파일은 케이스 폴더
# `%TEMP%/sw_<pid>/…`), 로그는 이름이 실행마다 다르고, Vulkan 파이프라인 캐시는 원자적으로 쓴다. 직렬로 묶으면
# `ctest -L "nogpu|lint" -j 4` 의 끝이 이 테스트와 EngineTest_NoGPU 가 혼자 도는 구간이 된다. GPU 를 잡는 `_HostOnly` 는 그대로
# 직렬이다(`sw_addTestExecutable`).
sw_addTestExecutable(SmokeTest
	SOURCES
		TestSmoke.cpp
		TestModuleHost.cpp
		${CMAKE_SOURCE_DIR}/Source/App/Module/ModuleHost.cpp
		# `ModuleCompiler` 는 배포 구성에도 들어간다 — `App` 의 소스 규칙과 같다(App 은 배포에서
		# `LiveReloadManager` 만 뺀다). `ModuleHost::shutdown` 이 구성과 무관하게 이 심볼을 부른다.
		${CMAKE_SOURCE_DIR}/Source/App/Module/ModuleCompiler.cpp
		${swSmokeDevSources}
	LIBS
		${smokeLibs}
	LABELS
		"unit;module;nogpu"
	TIMEOUT
		180
	# ASan 의 전역 오버플로 검사를 이 스위트에서만 끈다.
	#
	# 이 ASan 런타임은 **DLL 을 내려도 그 모듈의 전역 등록을 지우지 않는다**(report_globals=2 로
	# 찍으면 "Added Global" 만 쌓이고 제거는 0건이다). 그래서 모듈을 내린 뒤 다음 모듈이 그 주소
	# 범위에 매핑되면, 새 DLL 이 **자기 전역을** 읽는데도 앞 모듈이 남긴 레드존을 밟아
	# global-buffer-overflow 로 보고된다(예: RHI_DX12 의 정적 초기화가 SW_LOG_CALLER 의 __FILE__ 를
	# 읽는 자리). 게다가 보고하려고 주소를 설명하는 순간 이미 언매핑된 모듈의 디스크립터를
	# 역참조해 "nested bug in the same thread, aborting" 으로 프로세스가 죽는다 — 겉으로는
	# LoadLibrary err=1114 나 맨 세그폴트로 보인다.
	#
	# 이 스위트는 존재 이유가 모듈을 올렸다 내리는 것이라 우회할 방법이 없다. 전역 검사만
	# 끄면 힙·스택·use-after-free 는 그대로 잡히고 케이스가 전부 돈다. 다른 테스트 실행 파일은
	# 전역 검사를 켠 채로 남는다.
	ASAN_OPTIONS
		report_globals=0
)

target_include_directories(SmokeTest PRIVATE "${CMAKE_SOURCE_DIR}/Source/App")

if(NOT SW_SHIPPING_BUILD)
	target_compile_definitions(SmokeTest PRIVATE
		"SW_TEST_MODULE_DIR=\"${CMAKE_BINARY_DIR}/Bin\""
	)
	# 이 테스트는 모듈 DLL 을 실제로 올렸다 내린다 — 백엔드 · 게임 · 에디터가 옆에 있어야 한다. 이름을 적지 않고 레지스트리에 묻는다.
	sw_addDynamicModuleDependencies(SmokeTest KINDS rhi game editor)
endif()
