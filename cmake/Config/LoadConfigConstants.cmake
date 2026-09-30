# ==============================================================================
# @file cmake/Config/LoadConfigConstants.cmake
# @brief Scripts/common/Constants.py 의 상수를 CMake 변수(SW_DIR_* · SW_FILE_* · SW_KEY_* · SW_SCRIPT_*)로 읽어 들입니다.
# ==============================================================================
# 상수만 필요한 곳(툴체인 탐색 `sw_findLlvmBin` · vcpkg 연동 · vcpkg 포트 툴체인)은 이것을 include 한다. C++ 헤더까지 만드는
# `GenerateConfigConstants.cmake` 를 include 하면 헤더 생성기가 따라 돈다 — 예전에는 세 곳이 그것을 include 해서 configure 한 번에
# 파이썬 생성기 셋이 세 번씩(아홉 번) 돌았고, `sw_findLlvmBin` 은 부를 때마다 함수 안에서 다시 돌렸으며, vcpkg 포트를 빌드할 때마다
# 포트 빌드 폴더에 엔진 헤더를 써 넣었다.
#
# 가드는 GLOBAL 이다 — 한 configure 에서 한 번. 그래서 **파일 스코프에서 include 해야 한다**(함수 안에서 처음 include 되면 상수가
# 그 함수 스코프에만 생기고, 뒤의 include 는 가드에 걸려 아무 것도 하지 않는다).
include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/../Environment/PythonUtils.cmake")

# Constants.py 가 정본이다. 파이썬이 `set(SW_...)` 목록을 찍고 CMake 는 그것을 읽는다.
set(SW_GENERATED_CMAKE_VARS "${CMAKE_BINARY_DIR}/generated/sw/config/ConfigVars.cmake")
sw_executePythonScript("Scripts/generate/GenerateCMakeConstants.py"
	ARGS "${SW_GENERATED_CMAKE_VARS}"
	REQUIRED
)
include("${SW_GENERATED_CMAKE_VARS}")
