# ==============================================================================
# @file Test/ReflectionTest/CMakeLists.txt
# @brief 엔진 리플렉션 및 자동 소스 코드 생성 툴 기능 단위 테스트 스크립트
# ==============================================================================

# ReflectionTest 실행 파일 타겟 생성
sw_add_executable(ReflectionTest LINK_LIBRARIES Core TestFramework)
sw_set_log_tag(ReflectionTest Test)
sw_configure_pch(ReflectionTest)

# [Reflection] 타겟을 위해 ReflectionParser 구동 커스텀 스텝 등록
# 매크로를 통해 `TestSampleActor.h`를 파싱하고 .gen.cpp 코드를 생성하여 타겟에 자동 병합
sw_add_reflection_step(ReflectionTest
    HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/TestSampleActor.h"
    INCLUDES
        "${CMAKE_SOURCE_DIR}/Source"
)

# BUILD_TESTING 옵션 켜짐 시 CTest에 단위 테스트 타겟으로 추가
if(BUILD_TESTING)
    add_test(NAME ReflectionTest COMMAND ReflectionTest)
endif()
