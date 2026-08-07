# ==============================================================================
# @file Test/CoreSmokeTest/CMakeLists.txt
# @brief Core 단일 라이브러리 스모크 테스트
# ==============================================================================

sw_add_executable(CoreSmokeTest LINK_LIBRARIES Core TestFramework)
sw_set_log_tag(CoreSmokeTest Test)
sw_configure_pch(CoreSmokeTest)

if(BUILD_TESTING)
    add_test(NAME CoreSmokeTest COMMAND CoreSmokeTest)
endif()
