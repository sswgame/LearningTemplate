# ==============================================================================
# @file Test/TestFramework/CMakeLists.txt
# @brief 모든 단위 테스트에서 공통으로 사용하는 기본 테스트 프레임워크 래퍼
# ==============================================================================

file(GLOB_RECURSE TF_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

add_library(TestFramework STATIC ${TF_SOURCES})

target_include_directories(TestFramework PUBLIC "${CMAKE_SOURCE_DIR}/Test" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(TestFramework
    PUBLIC
        Engine
    PRIVATE
        sw_global_options
)
if(sw_flag_libraries)
    target_link_libraries(TestFramework PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(TestFramework PRIVATE "SW_LOG_TAG=\"Test\"")
target_precompile_headers(TestFramework PRIVATE "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
set_target_properties(TestFramework PROPERTIES FOLDER "Test")
