# ctest `ReflectionParser.TargetArgs` 가 구성하는 작은 프로젝트 — `sw_write_reflection_target_args`(cmake/Modules/Build/ReflectionCodeGen.cmake)를 실제 file(GENERATE) 로 돌린다.
# 그 호출의 인자 꼴(OUTPUT · CONTENT · TARGET 차례)과 생성기 식은 구성 · 생성 때만 드러나 파서 사례가 지나지 않는다.
# C 와 CXX 를 켜고 두 언어 소스가 섞인 타깃을 쓴다. file(GENERATE) 는 켠 언어마다 평가하므로, 언어별 정의 · 옵션이 응답 파일을 갈라 놓는 꼴이 여기서 드러난다.
# 컴파일러는 찾기만 하고 링크하지 않는다(try_compile 은 정적 라이브러리).
# 입력: SW_SOURCE_ROOT(저장소) · SW_GENERATED_CMAKE_VARS(프로젝트 상수 ConfigVars.cmake) · CHECK_OUTPUT(쓸 응답 파일)
cmake_minimum_required(VERSION 3.24)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
project(ReflectionTargetArgsCheck LANGUAGES C CXX)

include("${SW_GENERATED_CMAKE_VARS}")
include("${SW_SOURCE_ROOT}/cmake/Modules/Build/ReflectionCodeGen.cmake")

add_library(fixture_target STATIC "${CMAKE_CURRENT_SOURCE_DIR}/Fixture.c" "${CMAKE_CURRENT_SOURCE_DIR}/Fixture.cpp")
set_property(TARGET fixture_target PROPERTY COMPILE_DEFINITIONS "SW_FIXTURE_FLAG=1" "SW_FIXTURE_TEXT=\"text\"" "$<$<COMPILE_LANGUAGE:CXX>:SW_FIXTURE_CXX_ONLY=1>" "$<$<COMPILE_LANGUAGE:C>:SW_FIXTURE_C_ONLY=1>")
set_property(TARGET fixture_target PROPERTY INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/fixture_include")
set_property(TARGET fixture_target PROPERTY CXX_STANDARD 17)
set_property(TARGET fixture_target PROPERTY COMPILE_OPTIONS "/GR-" "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>" "$<$<COMPILE_LANGUAGE:C>:/TC>")
sw_write_reflection_target_args(fixture_target "${CHECK_OUTPUT}")
