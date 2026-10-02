# ==============================================================================
# @file cmake/Config/GenerateConfigConstants.cmake
# @brief Constants.py 의 상수를 읽고(LoadConfigConstants), 그것으로 C++ 헤더 넷(ConfigConstants.h · PackFormat.gen.h ·
#        CookContract.gen.h · ShippingHostDefaults.h)을 만듭니다. 최상위 CMakeLists 가 project() 전에 한 번 include 한다.
# ==============================================================================
# 상수만 필요하면 이 파일이 아니라 `LoadConfigConstants.cmake` 를 include 한다(그 파일 머리말 참고).
include_guard(GLOBAL)

# 1~3. Constants.py 의 상수를 CMake 변수로 (SW_GENERATED_CMAKE_VARS 도 거기서 정한다)
include("${CMAKE_CURRENT_LIST_DIR}/LoadConfigConstants.cmake")

# 4. 가져온 CMake 변수들을 바탕으로 C++ 헤더(ConfigConstants.h) 생성
configure_file(
	"${CMAKE_CURRENT_LIST_DIR}/ConfigConstants.h.in"
	"${CMAKE_BINARY_DIR}/generated/sw/config/ConfigConstants.h"
)

# 5. .pack 바이너리 포맷 계약 → C++ 헤더 생성
# Config/Engine/PackFormat.json 이 단일 출처이고, 같은 파일을 Python 쿠커(CookAssets.py)가
# 읽는다. 생성물의 offsetof/sizeof static_assert 덕분에 계약과 C++ 이 어긋나면 컴파일이 깨진다.
set(SW_GENERATED_PACK_FORMAT_H "${CMAKE_BINARY_DIR}/generated/sw/config/PackFormat.gen.h")
sw_executePythonScript("Scripts/generate/GeneratePackFormat.py"
	ARGS "${SW_GENERATED_PACK_FORMAT_H}"
	REQUIRED
)

# 5b. 쿠킹 표(RHI 백엔드 · 쿡 접미사) → C++ X-macro 헤더 생성
# Config/Engine/CookContract.json 이 단일 출처이고, 같은 파일을 Python 쿠커(CookAssets.py)가 읽는다.
set(SW_GENERATED_COOK_CONTRACT_H "${CMAKE_BINARY_DIR}/generated/sw/config/CookContract.gen.h")
sw_executePythonScript("Scripts/generate/GenerateCookContract.py"
	ARGS "${SW_GENERATED_COOK_CONTRACT_H}"
	REQUIRED
)

# 계약 파일이 바뀌면 configure 를 다시 돌려 생성 헤더를 새로 만든다.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
	"${CMAKE_SOURCE_DIR}/Config/Engine/PackFormat.json"
	"${CMAKE_SOURCE_DIR}/Config/Engine/CookContract.json"
)

# 6. Shipping/Dev 폴백용 호스트 기본값 베이크 (커밋된 Engine/Game Config JSON)
set(SW_SHIPPING_HOST_DEFAULTS_H "${CMAKE_BINARY_DIR}/generated/sw/config/ShippingHostDefaults.h")
sw_executePythonScript("Scripts/generate/BakeShippingHostDefaults.py"
	ARGS "${SW_SHIPPING_HOST_DEFAULTS_H}"
	REQUIRED
)
