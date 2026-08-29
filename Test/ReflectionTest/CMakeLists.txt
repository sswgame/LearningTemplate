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
)

target_include_directories(ReflectionTest PRIVATE "${CMAKE_SOURCE_DIR}/Tools")

sw_addReflectionStep(ReflectionTest
	HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/TestSampleActor.h"
	INCLUDES
		"${CMAKE_SOURCE_DIR}/Source"
		"${CMAKE_SOURCE_DIR}/Test"
)
