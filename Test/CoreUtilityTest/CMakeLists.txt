# ==============================================================================
# @file Test/CoreUtilityTest/CMakeLists.txt
# @brief Core utility unit tests
# ==============================================================================

sw_add_executable(CoreUtilityTest LINK_LIBRARIES Core TestFramework)
sw_set_log_tag(CoreUtilityTest Test)
sw_configure_pch(CoreUtilityTest)

if(BUILD_TESTING)
    add_test(NAME CoreUtilityTest COMMAND CoreUtilityTest)
endif()
