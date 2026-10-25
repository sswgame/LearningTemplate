# ==============================================================================
# @file Test/FuzzTest/CMakeLists.txt
# @brief 커버리지 안내 퍼저(libFuzzer) — `SW_ENABLE_FUZZING`(리눅스 clang + ASan, 프리셋 CI-Fuzz)에서만 짓는다
# ==============================================================================

# 테스트 실행 파일이 아니다(CTest 에 올리지 않는다) — 밤마다 `.github/workflows/fuzz.yml` 이 대상마다 돌린다. 대상 표는 시드 고정 변이(`LoaderFuzzTest`)와
# 같은 `Test/EngineTest/LoaderFuzzTargets.cpp` 다. 엔진은 테스트 하네스와 같은 기동(`TestHostRuntime`)으로 한 번 세운다.
if(NOT SW_ENABLE_FUZZING)
	return()
endif()

# 하네스 기동은 `sw_addTestHostRuntime` 이 정본이다(테스트 실행 파일과 같은 함수). 아래 둘은 EngineTest 폴더의 소스를 가져오는 것으로,
# 그 폴더는 EngineTest 가 훑는 곳이라 "선언" 이 없다 — 파일 이름 두 개가 이 목록의 유일한 자리이고 없어지면 구성이 멈춘다(add_executable).
add_executable(LoaderFuzzer
	LoaderFuzzer.cpp
	"${CMAKE_SOURCE_DIR}/Test/EngineTest/LoaderFuzzTargets.cpp"
	"${CMAKE_SOURCE_DIR}/Test/EngineTest/ResourcePakTestUtil.cpp"
)
sw_addTestHostRuntime(LoaderFuzzer)
set_target_properties(LoaderFuzzer PROPERTIES FOLDER "Test")
add_dependencies(AllTests LoaderFuzzer)
target_include_directories(LoaderFuzzer PRIVATE "${CMAKE_SOURCE_DIR}/Test")
target_link_libraries(LoaderFuzzer
	PRIVATE
	TestFramework
	Engine
	GameFramework
	sw_global_options
)
target_link_options(LoaderFuzzer PRIVATE -fsanitize=fuzzer)
target_compile_definitions(LoaderFuzzer PRIVATE "SW_LOG_TAG=\"Test\"")
sw_configurePch(LoaderFuzzer "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
