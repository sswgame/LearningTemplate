# ==============================================================================
# @file Test/CoreSmokeTest/CMakeLists.txt
# @brief Core 단일 라이브러리 스모크 테스트
# ==============================================================================

sw_add_executable(CoreSmokeTest
    NO_INSTALL
    LINK_LIBRARIES Core TestFramework
)
sw_set_log_tag(CoreSmokeTest Test)
sw_configure_pch(CoreSmokeTest)

# nogpu: Core link + basic invariants — no window/RHI
if(BUILD_TESTING)
    add_test(NAME CoreSmokeTest COMMAND CoreSmokeTest)
    set_tests_properties(CoreSmokeTest PROPERTIES
        WORKING_DIRECTORY "${sw_output_directory}/Bin"
        LABELS "unit;core;nogpu"
    )
endif()
