# ==============================================================================
# @file Test/EngineTest/CMakeLists.txt
# @brief Engine 단위 테스트
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) EngineTest 타겟 정의 및 런타임 의존성 복사
# ------------------------------------------------------------------------------
sw_addTestExecutable(EngineTest
	RUN_SERIAL
	LIBS
		Engine
		GameFramework
		GF_Overworld
		GF_TurnBattle
		GF_ActionCombat
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
if(SW_RHI_AS_MODULES)
	foreach(rhiMod IN ITEMS RHI_DX11 RHI_DX12 RHI_GL RHI_Vulkan)
		if(TARGET ${rhiMod})
			add_dependencies(EngineTest ${rhiMod})
		endif()
	endforeach()
endif()

# ------------------------------------------------------------------------------
# 2) ctest — NoGPU 필터 (RHI/Window/Shader 등 제외)
#
# **실제 RHI 디바이스를 만드는 케이스는 스위트 이름으로 가른다** — `RenderPassGpuTest`. 예전엔
# `-RenderPassTest.FrameRenderer*` 처럼 이름을 하나씩 적었는데, 2026-09-08 에 들어온 픽셀 리드백
# 테스트 여섯이 그 이름 규칙을 비켜가면서 그대로 CI 로 들어갔다. CI 러너에는 GPU 가 없다 —
# 리눅스는 X11 디스플레이가 없어 조용히 스킵되지만, Windows 는 DX11/DX12 가 Basic Render
# Driver(WARP)로 **초기화에 성공해서** 픽셀 검증이 실제로 돌고 진다. 이름 목록은 또 썩으므로
# 스위트로 가른다: 디바이스가 필요하면 `RenderPassGpuTest` 에 넣으면 된다.
#
# 이 케이스들은 사라지지 않는다 — GPU 가 있는 개발자가 돌리는 `EngineTest` 전체에는 그대로 있다.
# ------------------------------------------------------------------------------
if(BUILD_TESTING)
	add_test(
		NAME EngineTest_NoGPU
		COMMAND EngineTest --test_filter=-RHITest.*,-WindowTest.*,-ShaderCompilerTest.*,-LiveShaderTest.*,-RenderPassGpuTest.*
	)
	set_tests_properties(EngineTest_NoGPU PROPERTIES
		WORKING_DIRECTORY "${sw_output_directory}/Bin"
		LABELS "unit;engine;nogpu"
		RUN_SERIAL TRUE
		TIMEOUT 180
	)

	# 이 테스트는 손으로 등록하므로 sw_addTestExecutable 의 ASan 보정을 받지 못한다. 직접 부른다.
	sw_applySanitizerTestProperties(EngineTest_NoGPU)
endif()
