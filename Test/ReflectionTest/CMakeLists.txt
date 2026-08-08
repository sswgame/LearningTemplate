# ==============================================================================
# @file Test/ReflectionTest/CMakeLists.txt
# @brief 엔진 리플렉션 및 자동 소스 코드 생성 툴 기능 단위 테스트 스크립트
# ==============================================================================

sw_add_executable(ReflectionTest
    NO_INSTALL
    LINK_LIBRARIES Core TestFramework
)
sw_set_log_tag(ReflectionTest Test)
sw_configure_pch(ReflectionTest)

sw_add_reflection_step(ReflectionTest
    HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/TestSampleActor.h"
    INCLUDES
        "${CMAKE_SOURCE_DIR}/Source"
        "${CMAKE_SOURCE_DIR}/Test"
)

if(BUILD_TESTING)
    add_test(NAME ReflectionTest COMMAND ReflectionTest)
    set_tests_properties(ReflectionTest PROPERTIES
        WORKING_DIRECTORY "${sw_output_directory}/Bin"
        LABELS "unit;reflection;nogpu"
    )
endif()
