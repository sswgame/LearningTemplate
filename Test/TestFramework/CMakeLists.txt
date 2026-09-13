# ==============================================================================
# @file Test/TestFramework/CMakeLists.txt
# @brief 모든 단위 테스트에서 공통으로 사용하는 기본 테스트 프레임워크 래퍼
# ==============================================================================

# main.cpp 는 **뺀다.** 테스트 실행 파일마다 자기 main 이 필요해서 sw_addTestExecutable 이 타겟마다
# 따로 붙인다(cmake/Engine/TargetRules.cmake). 여기 넣으면 정적 라이브러리에 main 이 들어가 버린다.
file(GLOB TF_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")
list(REMOVE_ITEM TF_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

add_library(TestFramework STATIC ${TF_SOURCES})

target_include_directories(TestFramework PUBLIC "${CMAKE_SOURCE_DIR}/Test" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(TestFramework
    PUBLIC
    Engine
    PRIVATE
    sw_global_options
)

target_compile_definitions(TestFramework PRIVATE "SW_LOG_TAG=\"Test\"")
sw_configurePch(TestFramework "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
set_target_properties(TestFramework PROPERTIES FOLDER "Test")
