# ==============================================================================
# @file Test/CoreTest/CMakeLists.txt
# @brief Core 단위 테스트
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) CoreTest — TestFramework만 링크 (nogpu)
# ------------------------------------------------------------------------------
sw_addExecutable(CoreTest
    TestMemoryProfiler.cpp
    TestDeadlockDetector.cpp
    TestDataRaceDetector.cpp
    TestObjectHandle.cpp
    NO_INSTALL
    LINK_LIBRARIES TestFramework
)
sw_setLogTag(CoreTest Test)
sw_configurePch(CoreTest)

# ------------------------------------------------------------------------------
# 2) ctest — GPU 불필요. 라벨 unit;core;nogpu
# ------------------------------------------------------------------------------
if(BUILD_TESTING)
    add_test(NAME CoreTest COMMAND CoreTest)
    set_tests_properties(CoreTest PROPERTIES
        WORKING_DIRECTORY "${sw_output_directory}/Bin"
        LABELS "unit;core;nogpu"
    )
endif()
