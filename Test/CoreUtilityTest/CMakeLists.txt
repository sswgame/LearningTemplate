# ==============================================================================
# @file Test/CoreUtilityTest/CMakeLists.txt
# @brief Core utility unit tests
# ==============================================================================

sw_add_executable(CoreUtilityTest LINK_LIBRARIES Core TestFramework)
sw_set_log_tag(CoreUtilityTest Test)
sw_configure_pch(CoreUtilityTest)

if(BUILD_TESTING)
    add_test(NAME CoreUtilityTest COMMAND CoreUtilityTest)
    set_tests_properties(CoreUtilityTest PROPERTIES LABELS "unit;core")

    # Headless-friendly subset (skips GPU/window suites via framework filter)
    add_test(
        NAME CoreUtilityTest_NoGPU
        COMMAND CoreUtilityTest --test_filter=-RHITest.*,-WindowTest.*,-MaterialTest.*,-ShaderCompilerTest.*,-LiveShaderTest.*
    )
    set_tests_properties(CoreUtilityTest_NoGPU PROPERTIES LABELS "unit;core;nogpu")
endif()