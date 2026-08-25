# ==============================================================================
# @file cmake/internal/ConfigConstants.cmake
# @brief Python의 ConfigHelper.py에 대응하는 CMake 내부 상수(경로, JSON 키 등) 선언
# ==============================================================================

# 1. 파이썬 실행 헬퍼 포함 (sw_executePythonScript)
include("${CMAKE_CURRENT_LIST_DIR}/Python.cmake")

# 2. ConfigHelper.py를 읽어 CMake 상수를 자동 생성하는 스크립트 실행
# (Python이 Single Source of Truth가 됨)
set(SW_GENERATED_CMAKE_VARS "${CMAKE_BINARY_DIR}/generated/sw/config/ConfigVars.cmake")
sw_executePythonScript("Scripts/setup/GenerateCMakeConstants.py" 
    ARGS "${SW_GENERATED_CMAKE_VARS}"
    REQUIRED
)

# 3. 방금 파이썬이 생성한 CMake 변수들을 현재 스코프에 인클루드
include("${SW_GENERATED_CMAKE_VARS}")

# 4. 가져온 CMake 변수들을 바탕으로 C++ 헤더(ConfigConstants.h) 생성
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/ConfigConstants.h.in"
    "${CMAKE_BINARY_DIR}/generated/sw/config/ConfigConstants.h"
)

# 5. Shipping/Dev 폴백용 호스트 기본값 베이크 (커밋된 Engine/Game Config JSON)
set(SW_SHIPPING_HOST_DEFAULTS_H "${CMAKE_BINARY_DIR}/generated/sw/config/ShippingHostDefaults.h")
sw_executePythonScript("Scripts/BakeShippingHostDefaults.py"
	ARGS "${SW_SHIPPING_HOST_DEFAULTS_H}"
	REQUIRED
)
