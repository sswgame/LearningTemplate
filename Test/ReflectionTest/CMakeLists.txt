# ==============================================================================
# @file Test/ReflectionTest/CMakeLists.txt
# @brief 엔진 리플렉션 및 자동 소스 코드 생성 툴 기능 단위 테스트
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) ReflectionTest — TestSampleActor.h 코드젠 후 실행
# ------------------------------------------------------------------------------
file(GLOB_RECURSE REFTEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")
list(FILTER REFTEST_SOURCES EXCLUDE REGEX "^.*Fail.*\\.cpp$")

add_executable(ReflectionTest ${REFTEST_SOURCES})

target_include_directories(ReflectionTest PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ReflectionTest
    PRIVATE
        Engine
        TestFramework
        sw_global_options
)
if(sw_flag_libraries)
    target_link_libraries(ReflectionTest PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(ReflectionTest PRIVATE "SW_LOG_TAG=\"Test\"")
target_precompile_headers(ReflectionTest PRIVATE "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
set_target_properties(ReflectionTest PROPERTIES FOLDER "Test")

sw_addReflectionStep(ReflectionTest
    HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/TestSampleActor.h"
    INCLUDES
        "${CMAKE_SOURCE_DIR}/Source"
        "${CMAKE_SOURCE_DIR}/Test"
)

# ------------------------------------------------------------------------------
# 2) ctest — Bin에서 실행 (생성 산출물 옆)
# ------------------------------------------------------------------------------
if(BUILD_TESTING)
    add_test(NAME ReflectionTest COMMAND ReflectionTest)
    set_tests_properties(ReflectionTest PROPERTIES
        WORKING_DIRECTORY "${sw_output_directory}/Bin"
        LABELS "unit;reflection;nogpu"
        TIMEOUT 30
    )

    # ------------------------------------------------------------------------------
    # 3) Compile Fail Test: REFLECT() on Component with member variables
    # ------------------------------------------------------------------------------
    add_executable(CompileFail_ComponentVariable "${CMAKE_CURRENT_SOURCE_DIR}/FailComponent.cpp")
    target_include_directories(CompileFail_ComponentVariable PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(CompileFail_ComponentVariable
        PRIVATE
            Engine
            TestFramework
            sw_global_options
    )
    if(sw_flag_libraries)
        target_link_libraries(CompileFail_ComponentVariable PRIVATE ${sw_flag_libraries})
    endif()
    target_compile_definitions(CompileFail_ComponentVariable PRIVATE "SW_LOG_TAG=\"Test\"")
    target_precompile_headers(CompileFail_ComponentVariable PRIVATE "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
    set_target_properties(CompileFail_ComponentVariable PROPERTIES EXCLUDE_FROM_ALL TRUE FOLDER "Test")

    sw_addReflectionStep(CompileFail_ComponentVariable
        HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/FailComponent.h"
        INCLUDES "${CMAKE_SOURCE_DIR}/Source" "${CMAKE_SOURCE_DIR}/Test"
    )

    add_test(NAME CompileFail_ComponentVariable 
        COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target CompileFail_ComponentVariable
    )
    set_tests_properties(CompileFail_ComponentVariable PROPERTIES 
        WILL_FAIL TRUE
        LABELS "unit;reflection;compile_fail"
        RUN_SERIAL TRUE
        TIMEOUT 30
    )
endif()
