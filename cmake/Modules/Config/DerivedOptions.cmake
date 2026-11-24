# ==============================================================================
# @file cmake/Modules/Config/DerivedOptions.cmake
# @brief 구성(대상 OS · 다른 옵션 · 쿠킹 표)에서 유도하는 옵션 — 캐시는 auto, 푼 값은 보통 변수(AutoOption.cmake). 대상 OS 를 알아야 해 project() 뒤다
# @note 구성과 무관한 상수 기본값 옵션은 BuildOptions.cmake 에 둔다. SW_HOST_BUILD_DIR 은 쓰는 자리(HostTools.cmake)가 같은 함수로 푼다.
# ==============================================================================
include("${CMAKE_CURRENT_LIST_DIR}/AutoOption.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/JsonValue.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/MemoryBackend.cmake")

# 빌드 타깃 종류 — 언리얼 TargetType(Game · Client · Server)과 같은 자리. 모듈 매니페스트의 `_listTarget` 과 코드 상수
# (`sw::build::kWithClientCode` · `kWithServerCode` — `SW_TARGET_NAME`, `cmake/Modules/Build/BuildLayout.cmake`)가 이 값을 따른다.
# Game   — 클라이언트 + 서버 코드 전부(개발 · 에디터 · 한 프로세스 서버/클라 테스트 · 리슨 서버). App 과 Server 를 짓는다.
# Client — 서버 전용 모듈이 없는 플레이어 배포본. App 만 짓는다.
# Server — 클라이언트 전용 모듈(에디터 · RHI 백엔드)과 창 · GPU · 오디오 장치 코드가 없는 전용 서버. Server 만 짓는다.
# auto 는 정적 모듈 구성(Shipping · 웹 · Android 개발)이면 Client, 그 밖은 Game 이다.
set(derived_option "Game")
if(SW_STATIC_MODULES)
	set(derived_option "Client")
endif()
sw_resolve_auto_option(SW_TARGET_TYPE "${derived_option}" "빌드 타깃 종류: auto(정적 모듈 구성=Client, 그 밖=Game) | Game | Client | Server" Game Client Server)

if(SW_TARGET_TYPE STREQUAL "Server" AND NOT SW_BUILD_GAME)
	message(FATAL_ERROR "SW_TARGET_TYPE=Server needs SW_BUILD_GAME=ON — the dedicated server runs the game module")
endif()

# 정적 모듈 구성이 Engine 에 정적으로 넣는 RHI 백엔드 하나 — 쿠킹 표(Config/Engine/CookContract.json rhi_backends)의 이름 그대로(별칭 · 소문자는 구성 실패 — RHIBackends.cmake).
# auto 는 Windows 가 표의 default_rhi_backend, Linux · Android 가 Vulkan, 웹이 WebGPU 다. 모르는 대상이면 구성이 선다(새 플랫폼이 Vulkan 을 조용히 받지 않게).
# Dev 는 백엔드 넷을 모두 모듈로 짓고 이 값을 읽지 않는다. 이 플랫폼에 없는 백엔드면 구성이 선다(cmake/Modules/Build/RHIBackends.cmake).
if(WIN32)
	sw_require_json_value(derived_option "${CMAKE_SOURCE_DIR}/Config/Engine/CookContract.json" default_rhi_backend)
elseif(CMAKE_SYSTEM_NAME MATCHES "^(Linux|Android)$")
	set(derived_option "Vulkan")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
	set(derived_option "WebGPU")
else()
	message(FATAL_ERROR "[DerivedOptions] no default RHI backend for target '${CMAKE_SYSTEM_NAME}' - name it here")
endif()
sw_resolve_auto_option(SW_SHIPPING_RHI_BACKEND "${derived_option}" "정적 모듈 구성이 Engine 에 정적 링크할 RHI 백엔드: auto | 쿠킹 표 이름")

# Tracy 프로파일러 클라이언트(엔진 프로파일러의 두 번째 출력, Source/Engine/Profiling). Shipping 은 언제나 뺀다.
# Windows 는 TracyClient.dll 을 지연 로드한다 — `-gv_tracy` 로 켜기 전에는 DLL 도, 수집 스레드도 없다.
# 리눅스(vcpkg 정적 라이브러리)는 지연 로드가 없어 링크하면 기동부터 수집 스레드 · 리슨 소켓이 선다 — 그래서 기본 꺼짐이다.
# 정적 모듈 구성(웹 · Android 개발)은 Tracy 를 잇지 않는다 — DevTools 의 Tracy 링크 방식이 Windows · 리눅스만 정해져 있다.
set(derived_option OFF)
if(WIN32 AND NOT SW_STATIC_MODULES)
	set(derived_option ON)
endif()
sw_resolve_auto_option(SW_ENABLE_TRACY "${derived_option}" "Tracy 프로파일러 클라이언트 링크(개발 빌드, Shipping 은 무시): auto(Windows 동적 모듈 구성만 ON) | ON | OFF" ON OFF)

# 힙 백엔드 — `Memory` 아래의 `MemoryBackend`(Source/Core/Memory/MemoryBackend.h) 구현을 고른다. 고른 구현 파일 하나만 링크된다(가상 호출 없음).
# 백엔드를 견줄 때만 `-DSW_MEMORY_BACKEND=crt` 를 준다(언리얼 -ansimalloc 자리 — 전용 프리셋은 두지 않는다).
sw_resolve_memory_backend("${CMAKE_SYSTEM_NAME}" "${SW_ENABLE_SANITIZER}")

unset(derived_option)
message(STATUS "[DerivedOptions] target=${SW_TARGET_TYPE} shipping_rhi=${SW_SHIPPING_RHI_BACKEND} tracy=${SW_ENABLE_TRACY} heap=${SW_MEMORY_BACKEND}")
