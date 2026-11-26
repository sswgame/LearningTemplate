# ctest `ReflectionParser.TargetArgs` 가 구성하는 작은 프로젝트 — `sw_write_reflection_target_args`(cmake/Modules/Build/ReflectionCodeGen.cmake)를 실제 file(GENERATE) 로 돌린다.
# 그 호출의 인자 꼴(OUTPUT · CONTENT · TARGET 차례)과 생성기 식은 구성 · 생성 때만 드러나 파서 사례가 지나지 않는다. 컴파일러는 쓰지 않는다(LANGUAGES NONE).
# 입력: SW_SOURCE_ROOT(저장소) · SW_GENERATED_CMAKE_VARS(프로젝트 상수 ConfigVars.cmake) · CHECK_OUTPUT(쓸 응답 파일)
cmake_minimum_required(VERSION 3.24)
project(ReflectionTargetArgsCheck LANGUAGES NONE)

include("${SW_GENERATED_CMAKE_VARS}")
include("${SW_SOURCE_ROOT}/cmake/Modules/Build/ReflectionCodeGen.cmake")

add_library(fixture_target INTERFACE)
set_property(TARGET fixture_target PROPERTY COMPILE_DEFINITIONS "SW_FIXTURE_FLAG=1" "SW_FIXTURE_TEXT=\"text\"")
set_property(TARGET fixture_target PROPERTY INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/fixture_include")
set_property(TARGET fixture_target PROPERTY CXX_STANDARD 17)
set_property(TARGET fixture_target PROPERTY COMPILE_OPTIONS "/GR-")
sw_write_reflection_target_args(fixture_target "${CHECK_OUTPUT}")
