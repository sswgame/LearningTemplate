# ==============================================================================
# @file Test/TestFramework/CMakeLists.txt
# @brief 모든 단위 테스트에서 공통으로 사용하는 기본 테스트 프레임워크 래퍼 모듈
# ==============================================================================

# TestFramework 정적 라이브러리(STATIC) 생성
sw_add_library(TestFramework STATIC)

# 엔진 메인 모듈인 Core에 대한 퍼블릭 종속성 부여
target_link_libraries(TestFramework PUBLIC Core)
sw_set_log_tag(TestFramework Test)
