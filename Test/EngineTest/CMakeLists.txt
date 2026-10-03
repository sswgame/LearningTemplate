# ==============================================================================
# @file Test/EngineTest/CMakeLists.txt
# @brief Engine 단위 테스트
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) EngineTest 타겟 정의 및 런타임 의존성 복사
#
# `RUN_SERIAL` 은 **일부러 없다**(2026-10-01 에 걷었다). 다른 테스트와 함께 돌면 안 될 이유를 찾았지만 없었다 — `Bin` 에 쓰는 파일이
# 없고(실행 전후로 `Bin` 을 통째로 견줬다. 임시 파일은 케이스 폴더 `%TEMP%/sw_<pid>/…`), 로그는 이름이 실행마다 다르고, Vulkan 파이프라인
# 캐시는 원자적으로 쓴다. 직렬로 묶여 있는 동안 `ctest -L "nogpu|lint" -j 4` 의 끝이 이 둘(EngineTest_NoGPU · SmokeTest)이 혼자 도는
# 22 초였다 — 풀자 44 초 → 24 초(다섯 번 다 통과). GPU 를 잡는 `_HostOnly` 는 그대로 직렬이다(`sw_addTestExecutable`).
# ------------------------------------------------------------------------------
sw_addTestExecutable(EngineTest
	HOST_SPLIT
	LIBS
		Engine
		GameFramework
		GF_Overworld
		GF_TurnBattle
		GF_ActionCombat
		GF_Farming
		GF_ThemePark
		GF_Voxel
		GF_CityBuilder
		GF_RealTimeStrategy
		GF_NetClientServer
		GF_NetLockstep
		GF_NetTurnRelay
		GF_NetMmo
		GF_Rhythm
		GF_TacticsSrpg
		GF_CardGame
		GF_OpenWorldWestern
		GF_WitcherRpg
		GF_CreatureLife
	LABELS
		"unit;engine"
	TIMEOUT
		180
)

# RHI Vulkan Debug validation 경로와 동일 Bin 을 쓰므로 layer + mimalloc 배포.
#
# DXC 는 **배포 빌드에서도** 테스트 옆에 둔다. App 과 달리 테스트 바이너리는 Bin 이 아니라
# TestBin 으로 나가므로(sw_addTestExecutable) 배포물에 셰이더 컴파일러가 섞이지 않는다.
# 없으면 ShaderBindingContractTest.ReflectionNamesAreUniformAcrossBackends 가 DXIL 리플렉션을
# 못 얻어 "forwardlit_ps g_SwMaterials 원소 없음: dx12" 로 실패한다 — 셰이더가 아니라 도구가
# 없어서 나는 실패라 원인을 찾기 어렵다.
sw_copyDxcDlls(EngineTest)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	sw_copyVulkanValidationRuntime(EngineTest)
endif()
sw_emitRuntimeCopies(EngineTest)

# RHI MODULE은 런타임 로드 — Engine과 순환 없이 테스트 빌드 시 플러그인 재빌드
#
# 백엔드 이름을 여기 적지 않는다. `sw_addRhiBackendModule` 이 타겟을 만들면서 등록한 것을 묻는다
# (`cmake/Engine/TargetRules.cmake` 의 RHI 백엔드 레지스트리) — 예전에는 이 넷이 세 곳에 글자
# 그대로 있었고, 백엔드가 늘면 조용히 빠졌다.
if(SW_RHI_AS_MODULES)
	sw_getDynamicModules(listRhiBackend KINDS rhi)
	foreach(rhiMod IN LISTS listRhiBackend)
		add_dependencies(EngineTest ${rhiMod})
	endforeach()
endif()

# ------------------------------------------------------------------------------
# 2) ctest — `HOST_SPLIT` 이 `EngineTest_NoGPU` · `EngineTest_HostOnly` 를 등록한다
#
# CI 러너가 못 돌리는 스위트(GPU · 디스플레이 · DXC)는 **자기 파일에서** `SW_TEST_REQUIRES_HOST( 스위트, "이유" )`
# 로 선언한다. `_NoGPU` 는 그 선언을 뺀 집합(`--host_suites=exclude`, 라벨 `nogpu` — CI 가 도는 것),
# `_HostOnly` 는 그 나머지(`--host_suites=only`, 라벨 `hostgpu`)다. GPU 가 있는 개발자는 배포 구성에서 이렇게 돈다:
#
#   ctest --test-dir build/Ninja-Shipping -L hostgpu --output-on-failure
#
# 예전에는 스위트 이름을 이 파일에 **두 번** 적었다(빼는 필터 · 고르는 필터). 그보다 전에는 케이스 이름을 하나씩
# 적었는데, 2026-09-08 에 들어온 픽셀 리드백 테스트 여섯이 그 규칙을 비켜가 그대로 CI 로 들어갔다 — 리눅스는 X11
# 디스플레이가 없어 조용히 스킵되지만, Windows 는 DX11/DX12 가 Basic Render Driver(WARP)로 **초기화에 성공해서**
# 픽셀 검증이 실제로 돌고 진다. 그리고 `HostOnly` 가 없던 때에는 그 집합을 따로 고를 방법이 없어 아무도 돌지 않았고,
# `RenderPassGpuTest` 두 건이 Shipping 에서만 지는 채로 숨어 있었다(docs/06_Backlog.md 2026-09-17).
# ------------------------------------------------------------------------------
