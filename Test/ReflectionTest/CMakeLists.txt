# ==============================================================================
# @file Test/ReflectionTest/CMakeLists.txt
# @brief 엔진 리플렉션 및 자동 소스 코드 생성 툴 기능 단위 테스트
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) ReflectionTest — TestSampleActor.h 코드젠 후 실행
# ------------------------------------------------------------------------------
file(GLOB_RECURSE REFTEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")
list(FILTER REFTEST_SOURCES EXCLUDE REGEX "^.*Fail.*\\.cpp$")

sw_addTestExecutable(ReflectionTest
	SOURCES
		${REFTEST_SOURCES}
	LIBS
		Engine
	LABELS
		"unit;reflection;nogpu"
	TIMEOUT
		30
	# 파서를 차례로 띄우는 ReflectionParserTest 가 실행 시간의 거의 전부다(21 케이스 · ~21 초, 나머지 125 케이스는 0.1 초).
	# 하나로 두면 제한 30 초에 붙어 부하가 걸리면 넘었다 — 둘로 갈라 병렬로 돈다(ReflectionTest_Shard1 · _Shard2).
	SHARDS
		2
)

target_include_directories(ReflectionTest PRIVATE "${CMAKE_SOURCE_DIR}/Tools")

sw_addReflectionStep(ReflectionTest
	HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/TestSampleActor.h"
	INCLUDES
		"${CMAKE_SOURCE_DIR}/Source"
		"${CMAKE_SOURCE_DIR}/Test"
)
