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
	# 파서를 프로세스로 띄우는 ReflectionParserTest 가 실행 시간의 거의 전부다(케이스마다 파서 실행 1~4 번, 실행 하나에 기동 · 종료
	# 0.5~1 초 + CoreMinimal.h 파싱 0.5 초 — 기계가 바쁘면 2~3 배). 둘로 가르면 부하가 걸린 기계에서 조각마다 30 초를 넘어 넷으로 가른다
	# (ReflectionTest_Shard1 ~ _Shard4, 케이스는 스위트 안에서 번갈아 나뉜다).
	SHARDS
		4
)

target_include_directories(ReflectionTest PRIVATE "${CMAKE_SOURCE_DIR}/Tools")

sw_addReflectionStep(ReflectionTest
	HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/TestSampleActor.h"
	INCLUDES
		"${CMAKE_SOURCE_DIR}/Source"
		"${CMAKE_SOURCE_DIR}/Test"
)
