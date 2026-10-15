# ==============================================================================
# @file Test/ReflectionTest/CMakeLists.txt
# @brief 엔진 리플렉션 및 자동 소스 코드 생성 툴 기능 단위 테스트
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) ReflectionTest — TestSampleActor.h 코드젠 후 실행
# ------------------------------------------------------------------------------
# 파서를 프로세스로 띄우는 ReflectionParserTest 가 실행 시간의 거의 전부다 — 샤드 · 시간은 Test/TestPolicy.json.
sw_addTestExecutable(ReflectionTest
	LIBS
		Engine
)

target_include_directories(ReflectionTest PRIVATE "${CMAKE_SOURCE_DIR}/Tools")

sw_addReflectionStep(ReflectionTest
	HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/TestSampleActor.h"
	INCLUDES
		"${CMAKE_SOURCE_DIR}/Source"
		"${CMAKE_SOURCE_DIR}/Test"
)
