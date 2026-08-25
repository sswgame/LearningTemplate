# ==============================================================================
# @file Test/EngineTest/CMakeLists.txt
# @brief Engine 단위 테스트
# ==============================================================================

file(GLOB_RECURSE ENGINETEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

add_executable(EngineTest ${ENGINETEST_SOURCES})

target_include_directories(EngineTest PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(EngineTest
    PRIVATE
        Engine
        TestFramework
        GameFramework
        sw_global_options
)
if(sw_flag_libraries)
    target_link_libraries(EngineTest PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(EngineTest PRIVATE "SW_LOG_TAG=\"Test\"")
target_precompile_headers(EngineTest PRIVATE "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
set_target_properties(EngineTest PROPERTIES FOLDER "Test")

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
        RUN_SERIAL TRUE
        TIMEOUT 90
    )

    add_test(
        NAME EngineTest_NoGPU
        COMMAND EngineTest --test_filter=-RHITest.*,-WindowTest.*,-ShaderCompilerTest.*,-LiveShaderTest.*,-RenderPassTest.FrameRenderer*
    )
    set_tests_properties(EngineTest_NoGPU PROPERTIES
        WORKING_DIRECTORY "${sw_output_directory}/Bin"
        LABELS "unit;engine;nogpu"
        RUN_SERIAL TRUE
        TIMEOUT 30
    )
endif()
