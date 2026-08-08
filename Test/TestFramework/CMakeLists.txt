# ==============================================================================
# @file Test/TestFramework/CMakeLists.txt
# @brief 모든 단위 테스트에서 공통으로 사용하는 기본 테스트 프레임워크 래퍼 모듈
# ==============================================================================

sw_add_library(TestFramework
    TYPE STATIC
    NO_INSTALL
)

target_link_libraries(TestFramework PUBLIC Core)
target_include_directories(TestFramework PUBLIC "${CMAKE_SOURCE_DIR}/Test")
sw_set_log_tag(TestFramework Test)
sw_configure_pch(TestFramework)
