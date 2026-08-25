# ==============================================================================
# @file Test/EngineTest/CMakeLists.txt
# @brief Engine 단위 테스트
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) EngineTest — GPU 테스트와 nogpu 필터를 따로 등록
# ------------------------------------------------------------------------------
sw_addExecutable(EngineTest
    NO_INSTALL
    LINK_LIBRARIES Engine TestFramework GameFramework
)
sw_setLogTag(EngineTest Test)
sw_configurePch(EngineTest)

# RHI Vulkan Debug validation 경로와 동일 Bin 을 쓰므로 layer + mimalloc 배포
sw_copyDxcDlls(EngineTest)
sw_copyVulkanValidationRuntime(EngineTest)
sw_emitRuntimeCopies(EngineTest)

# RHI MODULE은 런타임 로드 — Engine과 순환 없이 테스트 빌드 시 플러그인 재빌드
if(SW_RHI_AS_MODULES)
    foreach(_swRhiMod IN ITEMS RHI_DX11 RHI_DX12 RHI_GL RHI_Vulkan)
        if(TARGET ${_swRhiMod})
            add_dependencies(EngineTest ${_swRhiMod})
        endif()
    endforeach()
    unset(_swRhiMod)
endif()

# ------------------------------------------------------------------------------
# 2) ctest — 전체 스위트 + NoGPU 필터 (RHI/Window/Shader 등 제외)
# ------------------------------------------------------------------------------
if(BUILD_TESTING)
    add_test(NAME EngineTest COMMAND EngineTest)
    set_tests_properties(EngineTest PROPERTIES
        WORKING_DIRECTORY "${sw_output_directory}/Bin"
        LABELS "unit;engine"
    )

    add_test(
        NAME EngineTest_NoGPU
        COMMAND EngineTest --test_filter=-RHITest.*,-WindowTest.*,-ShaderCompilerTest.*,-LiveShaderTest.*,-RenderPassTest.FrameRenderer*
    )
    set_tests_properties(EngineTest_NoGPU PROPERTIES
        WORKING_DIRECTORY "${sw_output_directory}/Bin"
        LABELS "unit;engine;nogpu"
    )
endif()
