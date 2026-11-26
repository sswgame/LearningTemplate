# ctest `ReflectionParser.ReflectionStep` 가 구성하는 작은 프로젝트 — `sw_add_reflection_step`(cmake/Modules/Build/ReflectionCodeGen.cmake)의 구성 · 생성 경로를 실제로 돌린다.
# 자리 표시자 심기 · .cpp 의 OBJECT_DEPENDS · 후보 검사 명령 · 타깃 응답 파일(링크한 타깃의 정의 · include 자리 전이)은 구성 때만 드러나 파서 사례가 지나지 않는다.
# 파서는 짓지 않는다(IMPORTED 자리 표시). 컴파일러는 찾기만 하고 링크하지 않는다(try_compile 은 정적 라이브러리).
# C 를 함께 켠다. file(GENERATE) 는 켠 언어마다 평가하므로, 언어별 정의가 있는 타깃의 응답 파일이 C 와 CXX 에서 갈리는 꼴을 여기서 잡는다.
# 입력: SW_SOURCE_ROOT · SW_GENERATED_CMAKE_VARS
cmake_minimum_required(VERSION 3.24)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
project(ReflectionStepCheck LANGUAGES C CXX)

include("${SW_GENERATED_CMAKE_VARS}")
include("${SW_SOURCE_ROOT}/cmake/Modules/Build/ReflectionCodeGen.cmake")

# 파서 대신 — 단계는 파서 타깃이 있는지와 그 실행 파일 자리만 본다.
add_executable(ReflectionParser IMPORTED)
set_property(TARGET ReflectionParser PROPERTY IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/ReflectionParser.fake")
# 대상 매크로를 내는 전역 옵션(실제 구성의 sw_global_options 자리).
add_library(sw_global_options INTERFACE)
target_compile_definitions(sw_global_options INTERFACE SW_PLATFORM_WINDOWS)

# 링크한 타깃의 정의 · include 자리가 응답 파일로 이어지는지 본다(전이).
add_library(fixture_dependency INTERFACE)
target_compile_definitions(fixture_dependency INTERFACE SW_FIXTURE_DEPENDENCY_DEFINE=1)
target_include_directories(fixture_dependency INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/dependency_include")

add_library(fixture_module STATIC "${CMAKE_CURRENT_SOURCE_DIR}/Plain.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Source.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/PlainC.c")
target_link_libraries(fixture_module PRIVATE fixture_dependency)
target_compile_definitions(fixture_module PRIVATE SW_FIXTURE_MODULE_DEFINE=1 "$<$<COMPILE_LANGUAGE:CXX>:SW_FIXTURE_CXX_ONLY=1>" "$<$<COMPILE_LANGUAGE:C>:SW_FIXTURE_C_ONLY=1>")
sw_add_reflection_step(fixture_module
	MODULE FixtureModule
	HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/Fixture.h" "${CMAKE_CURRENT_SOURCE_DIR}/Source.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Plain.cpp"
)
