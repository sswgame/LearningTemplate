# ==============================================================================
# @file Test/CoreTest/CMakeLists.txt
# @brief Core 단위 테스트
# ==============================================================================

file(GLOB_RECURSE CORETEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

add_executable(CoreTest ${CORETEST_SOURCES})

target_include_directories(CoreTest PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(CoreTest
    PRIVATE
        TestFramework
        sw_global_options
)
if(sw_flag_libraries)
    target_link_libraries(CoreTest PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(CoreTest PRIVATE "SW_LOG_TAG=\"Test\"")
target_precompile_headers(CoreTest PRIVATE "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
set_target_properties(CoreTest PROPERTIES FOLDER "Test")

# ------------------------------------------------------------------------------
# 2) ctest — GPU 불필요. 라벨 unit;core;nogpu
# ------------------------------------------------------------------------------
if(BUILD_TESTING)
    add_test(NAME CoreTest COMMAND CoreTest)
    set_tests_properties(CoreTest PROPERTIES
        WORKING_DIRECTORY "${sw_output_directory}/Bin"
        LABELS "unit;core;nogpu"
        TIMEOUT 30
    )
endif()
