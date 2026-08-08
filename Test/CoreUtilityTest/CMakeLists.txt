# ==============================================================================
# @file Test/CoreUtilityTest/CMakeLists.txt
# @brief Core utility unit tests
# ==============================================================================

sw_add_executable(CoreUtilityTest
    NO_INSTALL
    LINK_LIBRARIES Core TestFramework
)
sw_set_log_tag(CoreUtilityTest Test)
sw_configure_pch(CoreUtilityTest)

if(BUILD_TESTING)
    add_test(NAME CoreUtilityTest COMMAND CoreUtilityTest)
    set_tests_properties(CoreUtilityTest PROPERTIES LABELS "unit;core")

    add_test(
        NAME CoreUtilityTest_NoGPU
        COMMAND CoreUtilityTest --test_filter=-RHITest.*,-WindowTest.*,-ShaderCompilerTest.*,-LiveShaderTest.*,-MaterialTest.MaterialColorModification,-MaterialTest.MaterialInstanceOverride,-MaterialTest.MaterialShaderReflectionValidation
    )
    set_tests_properties(CoreUtilityTest_NoGPU PROPERTIES LABELS "unit;core;nogpu")
endif()
