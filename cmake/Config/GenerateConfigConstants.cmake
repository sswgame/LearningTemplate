# ==============================================================================
# @file cmake/Config/GenerateConfigConstants.cmake
# @brief configure 생성 파일 — 생성기 다섯을 한 프로세스로 돌리고(ConfigVars · PackFormat · CookContract · ShippingHostDefaults · LintTargets),
#        상수를 CMake 변수로 읽고, 게임 프리셋 경로를 정하고, ConfigConstants.h 를 만든다. 최상위 CMakeLists 가 project() 전에 한 번 include 한다.
# ==============================================================================
# 상수만 필요하면 이 파일이 아니라 `LoadConfigConstants.cmake` 를 include 한다(그 파일 머리말 참고).
# 생성 파일은 모두 `<빌드>/generated/sw/config/` 에 있다. C++ 쪽 이름은 그 폴더 기준 include(`sw/config/PackFormat.gen.h` …)라 CMake 변수로 들지 않는다.
include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# 1) configure 생성기 다섯을 한 프로세스로 — 생성기마다 파이썬을 띄우면 configure 의 3 할 가까이가 파이썬 시작 · import 였다.
#    규칙 · 출력은 각 생성기 한 자리(GenerateConfigureFiles.py 는 차례로 부를 뿐). ToolchainVars 는 SetupEnvironment 뒤라 DetectToolchain 이 따로 부른다.
#    - PackFormat.gen.h   : Config/Engine/PackFormat.json(.pack 바이너리 포맷) → C++. offsetof/sizeof static_assert 로 계약과 C++ 이 어긋나면 컴파일이 깨진다.
#    - CookContract.*     : Config/Engine/CookContract.json(RHI 백엔드 · 쿡 접미사) → C++ X-macro 와 CMake(RHI 백엔드의 빌드 칸 — RhiBackends.cmake 가 읽는다).
#    - ShippingHostDefaults.h : 엔진 설정 + 게임 프리셋 → Shipping/Dev 폴백 호스트 기본값(프리셋이 없으면 건너뛴다 — 아래 3) 이 멈춘다).
#    두 JSON 표는 Python 쿠커(CookAssets.py)도 같은 파일을 읽는다.
# ------------------------------------------------------------------------------
include("${CMAKE_CURRENT_LIST_DIR}/../Environment/PythonUtils.cmake")
sw_executePythonScript("Scripts/generate/GenerateConfigureFiles.py"
	ARGS "${CMAKE_BINARY_DIR}/generated/sw/config" --game "${SW_ACTIVE_GAME}"
	REQUIRED
)
set(SW_CONFIGURE_FILES_GENERATED ON)

# ------------------------------------------------------------------------------
# 2) Constants.py 의 상수를 CMake 변수로(SW_DIR_* · SW_FILE_* …) · 쿠킹 표의 CMake 쪽
# ------------------------------------------------------------------------------
include("${CMAKE_CURRENT_LIST_DIR}/LoadConfigConstants.cmake")
include("${CMAKE_BINARY_DIR}/generated/sw/config/CookContract.cmake")

# ------------------------------------------------------------------------------
# 3) 활성 게임의 프리셋(Config/Game/<SW_ACTIVE_GAME>.json — 팩 루트)과 전용 서버 운영 설정(Config/Server/<SW_ACTIVE_GAME>.json, ServerConfig)
#    게임을 바꾸는 것은 프리셋을 바꾸는 것이다. 서버 설정은 굽지 않고 Server 가 디스크에서 읽으며, 서버가 없는 게임은 파일이 없어도 된다.
# ------------------------------------------------------------------------------
set(SW_FILE_RUNTIME_GAME_CONFIG "${SW_DIR_RUNTIME_GAME_PRESET}/${SW_ACTIVE_GAME}.json")
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${SW_FILE_RUNTIME_GAME_CONFIG}")
	message(FATAL_ERROR
		"SW_ACTIVE_GAME='${SW_ACTIVE_GAME}' has no game preset ${SW_FILE_RUNTIME_GAME_CONFIG}\n"
		"  Add it next to the other presets in ${SW_DIR_RUNTIME_GAME_PRESET}/ (_packRoot)")
endif()
set(SW_FILE_RUNTIME_SERVER_CONFIG "${SW_DIR_RUNTIME_SERVER_PRESET}/${SW_ACTIVE_GAME}.json")

# 입력이 바뀌면 configure 를 다시 돌려 생성 파일을 새로 만든다 — 프리셋 · 엔진 설정이 빠지면 Shipping 이 옛 값을 굽는다.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
	"${CMAKE_SOURCE_DIR}/${SW_FILE_RUNTIME_GAME_CONFIG}"
	"${CMAKE_SOURCE_DIR}/${SW_FILE_RUNTIME_ENGINE_CONFIG}"
	"${CMAKE_SOURCE_DIR}/Config/Engine/PackFormat.json"
	"${CMAKE_SOURCE_DIR}/Config/Engine/CookContract.json"
)

# ------------------------------------------------------------------------------
# 4) ConfigConstants.h — 위 변수로 C++ 경로 · 키 상수를 만든다(경로 조립은 템플릿 ConfigConstants.h.in 이 한다)
# ------------------------------------------------------------------------------
configure_file(
	"${CMAKE_CURRENT_LIST_DIR}/ConfigConstants.h.in"
	"${CMAKE_BINARY_DIR}/generated/sw/config/ConfigConstants.h"
)
