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
# 구성별 가드는 App 과 같다 — 배포 빌드에 셰이더 컴파일러/검증 레이어를 흘리지 않는다.
if(NOT SW_SHIPPING_BUILD)
	sw_copyDxcDlls(EngineTest)
endif()
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
# ------------------------------------------------------------------------------
if(BUILD_TESTING)
	add_test(
		NAME EngineTest_NoGPU
		COMMAND EngineTest --test_filter=-RHITest.*,-WindowTest.*,-ShaderCompilerTest.*,-LiveShaderTest.*,-RenderPassTest.FrameRenderer*
	)
	set_tests_properties(EngineTest_NoGPU PROPERTIES
		WORKING_DIRECTORY "${sw_output_directory}/Bin"
		LABELS "unit;engine;nogpu"
		RUN_SERIAL TRUE
		TIMEOUT 180
	)
endif()
