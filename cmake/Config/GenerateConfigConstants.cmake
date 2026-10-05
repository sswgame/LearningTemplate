# ==============================================================================
# @file cmake/Config/GenerateConfigConstants.cmake
# @brief Constants.py 의 상수를 읽고(LoadConfigConstants), 그것으로 C++ 헤더 넷(ConfigConstants.h · PackFormat.gen.h ·
#        CookContract.gen.h · ShippingHostDefaults.h)을 만듭니다. 최상위 CMakeLists 가 project() 전에 한 번 include 한다.
# ==============================================================================
# 상수만 필요하면 이 파일이 아니라 `LoadConfigConstants.cmake` 를 include 한다(그 파일 머리말 참고).
include_guard(GLOBAL)

# 1~3. Constants.py 의 상수를 CMake 변수로 (SW_GENERATED_CMAKE_VARS 도 거기서 정한다)
include("${CMAKE_CURRENT_LIST_DIR}/LoadConfigConstants.cmake")

# 3-1. 활성 게임의 프리셋(Config/Game/<SW_ACTIVE_GAME>.json) — 팩 루트 · gamesettings · 시작 씬. 게임을 바꾸는 것은 프리셋을 바꾸는 것이다.
set(SW_FILE_RUNTIME_GAME_CONFIG "${SW_DIR_RUNTIME_GAME_PRESET}/${SW_ACTIVE_GAME}.json")
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${SW_FILE_RUNTIME_GAME_CONFIG}")
	message(FATAL_ERROR
		"SW_ACTIVE_GAME='${SW_ACTIVE_GAME}' has no game preset ${SW_FILE_RUNTIME_GAME_CONFIG}\n"
		"  Add it next to the other presets in ${SW_DIR_RUNTIME_GAME_PRESET}/ (_packRoot, _gameSettingsFile, _startupScene)")
endif()
# 3-2. 전용 서버 운영 설정(Config/Server/<SW_ACTIVE_GAME>.json, ServerConfig) — 굽지 않고 Server 가 디스크에서 읽는다. 서버가 없는 게임은 파일이 없어도 된다.
set(SW_FILE_RUNTIME_SERVER_CONFIG "${SW_DIR_RUNTIME_SERVER_PRESET}/${SW_ACTIVE_GAME}.json")
# 프리셋을 고치면 생성 헤더(Shipping 기본값)도 다시 만든다.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/${SW_FILE_RUNTIME_GAME_CONFIG}")

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

# 6. Shipping/Dev 폴백용 호스트 기본값 생성 (커밋된 Engine/Game Config JSON)
set(SW_SHIPPING_HOST_DEFAULTS_H "${CMAKE_BINARY_DIR}/generated/sw/config/ShippingHostDefaults.h")
sw_executePythonScript("Scripts/generate/GenerateShippingHostDefaults.py"
	ARGS "${SW_SHIPPING_HOST_DEFAULTS_H}" "${SW_FILE_RUNTIME_GAME_CONFIG}"
	REQUIRED
)
