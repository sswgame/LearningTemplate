# ==============================================================================
# @file Test/ReflectionTest/CMakeLists.txt
# @brief 엔진 리플렉션 및 자동 소스 코드 생성 툴 기능 단위 테스트
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) ReflectionTest — TestSampleActor.h 코드젠 후 실행
# ------------------------------------------------------------------------------
sw_addExecutable(ReflectionTest
    NO_INSTALL
    LINK_LIBRARIES Engine TestFramework
    EXCLUDE "^.*Fail.*\\.cpp$"
)
sw_setLogTag(ReflectionTest Test)
sw_configurePch(ReflectionTest)

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
    )

    # ------------------------------------------------------------------------------
    # 3) Compile Fail Test: REFLECT() on Component with member variables
    # ------------------------------------------------------------------------------
    sw_addExecutable(CompileFail_ComponentVariable
        NO_INSTALL
        LINK_LIBRARIES Engine TestFramework
    )
    sw_setLogTag(CompileFail_ComponentVariable Test)
    sw_configurePch(CompileFail_ComponentVariable)
    sw_addReflectionStep(CompileFail_ComponentVariable
        HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/FailComponent.h"
        INCLUDES "${CMAKE_SOURCE_DIR}/Source" "${CMAKE_SOURCE_DIR}/Test"
    )
    set_target_properties(CompileFail_ComponentVariable PROPERTIES EXCLUDE_FROM_ALL TRUE)

    add_test(NAME CompileFail_ComponentVariable 
        COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target CompileFail_ComponentVariable
    )
    set_tests_properties(CompileFail_ComponentVariable PROPERTIES 
        WILL_FAIL TRUE
        LABELS "unit;reflection;compile_fail"
    )
endif()
