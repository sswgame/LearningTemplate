# ==============================================================================
# @file cmake/Config/BuildOptions.cmake
# @brief SW Engine 전역 기능 빌드 옵션(SW_*) 및 워크스페이스 메타데이터 정의
#
# [CMake 네이밍 및 개발 규칙]:
# - sw_camelCase      : CMake function() / macro() 헬퍼 함수 (예: sw_addModuleLibrary, sw_addGameModule)
# - sw_snake_case     : 프로젝트 내부 변수 및 INTERFACE 타겟명 (예: sw_flag_libraries)
# - SW_UPPER_SNAKE    : option() / CACHE 빌드 옵션 및 C++ 컴파일 매크로 (예: SW_ENABLE_PCH, SW_SHIPPING_BUILD)
# - camelCase         : 함수/매크로 내부 로컬 변수
# 이 파일은 옵션(SW_*)과 메타데이터만 둔다 — 타깃 함수는 cmake/Engine/, 찾은 도구 경로 캐시는 찾는 파일이 둔다.
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 워크스페이스 메타데이터 · C++ 표준
# ------------------------------------------------------------------------------
set(sw_workspace_name "Workspace")
set(sw_project_version "1.0.0")

# 최소 C++17 표준 요구. 필요 시 -DSW_CPP_STANDARD=20 (또는 23).
set(SW_CPP_STANDARD 17 CACHE STRING "C++ 표준(17 이상 — 20 · 23)")

# ------------------------------------------------------------------------------
# 2) 핵심 빌드 및 아키텍처 기능 옵션 (SW_* / option) — 논리 그룹별 알파벳 정렬
# ------------------------------------------------------------------------------
# 2-1) 빌드 모드 및 엔진 기능 옵션
option(SW_BUILD_GAME "GameFramework 및 게임 모듈(SWGame DLL/정적 링크) 빌드" ON)

# 아래 옵션들의 기본값이 이 값에 따라 갈리므로 가장 먼저 선언한다.
option(SW_SHIPPING_BUILD "배포용 단일 실행 파일 정적 링크 빌드 (Editor 모듈 제외 및 최고 성능 최적화)" OFF)

# 빌드 타깃 종류 — 언리얼 TargetType(Game · Client · Server)과 같은 자리. 모듈 매니페스트의 `_listTarget` 과 코드 매크로
# (`SW_WITH_CLIENT_CODE` · `SW_WITH_SERVER_CODE`, `cmake/Engine/BuildLayout.cmake`)가 이 값을 따른다.
# Game   — 클라이언트 + 서버 코드 전부(개발 · 에디터 · 한 프로세스 서버/클라 테스트 · 리슨 서버). App 과 Server 를 짓는다.
# Client — 서버 전용 모듈이 없는 플레이어 배포본. App 만 짓는다.
# Server — 클라이언트 전용 모듈(에디터 · RHI 백엔드)과 창 · GPU · 오디오 장치 코드가 없는 전용 서버. Server 만 짓는다.
# 비우면 Shipping 은 Client, 그 밖은 Game 이다. 캐시 값은 그대로 두고 같은 이름의 보통 변수로 가린다 — 아래 모든 곳이 해석된 값을 본다.
set(SW_TARGET_TYPE "" CACHE STRING "빌드 타깃 종류: Game | Client | Server (비우면 Shipping=Client, 그 밖=Game)")
set_property(CACHE SW_TARGET_TYPE PROPERTY STRINGS "" Game Client Server)

if(SW_TARGET_TYPE STREQUAL "")
	if(SW_SHIPPING_BUILD)
		set(SW_TARGET_TYPE Client)
	else()
		set(SW_TARGET_TYPE Game)
	endif()
endif()

# 리눅스 클라이언트의 Wayland 창 계층(`Engine/Window/Linux/Wayland`). 켜면 wayland-client · wayland-cursor · xkbcommon · wayland-protocols ·
# wayland-scanner 가 모두 있어야 한다(없으면 구성이 선다). 실행에서 고르는 것은 환경 변수 `SW_WINDOW_BACKEND=wayland` 이다(기본은 X11).
# 확인은 헤드리스 weston 이 있는 리눅스 환경에서만 한다 — 이 PC 에서는 리눅스 빌드를 하지 않는다.
option(SW_ENABLE_WAYLAND "리눅스 클라이언트에 Wayland 창 계층을 짓는다" OFF)

# 리눅스 클라이언트의 AT-SPI 스크린 리더 브리지(`Engine/UI/Accessibility/Linux`). 켜면 libdbus-1(pkg-config `dbus-1`)이 있어야 한다(없으면 구성이 선다).
# 확인은 Orca + at-spi2 를 올린 세션에서만 한다. 확인 전이라 기본은 끈다.
option(SW_ENABLE_ATSPI "리눅스 클라이언트에 AT-SPI 접근성 브리지를 짓는다" OFF)

# 리눅스 X11 입력 주입기(`Engine/Window/Linux/XTestInputInjector`, 개발 명령 `input.xtest`). 선택 의존이다 — 켰는데 libxtst(pkg-config `xtst`)가 없으면
# 구성을 세우지 않고 기능만 끈 채 "XTest 주입기 없음"을 알린다(`XTestInputInjector::isCompiledIn()` false). 패키지: libxtst-dev.
option(SW_ENABLE_XTEST "리눅스 클라이언트에 XTest 입력 주입기를 짓는다(libxtst 가 없으면 기능을 끈다)" OFF)

# 자동화 런타임(`Engine/Automation/Runner` 의 시나리오 실행기 · 세션 · 판정 · 보고 — `-scenario` 가 쓴다). 기본은 켠다: 배포 구성도 GPU 검증을 배포 구성 App 으로 돌린다.
# 끄면 그 소스를 Engine 이 짓지 않고 App 은 구동기 없는 구현(`AppAutomationHostOff.cpp`)을 지어 `-scenario` 는 기동 오류다. 단계 등록 · 감시 값 표(`Engine/Automation/Scenario` · `Engine/Automation/Watch`)는 남는다 — 기능이 자기 폴더에서 등록하는 형식이다.
option(SW_ENABLE_AUTOMATION "자동화 시나리오 런타임(Engine/Automation/Runner)을 짓는다(끄면 -scenario 불가)" ON)
# 전용 서버 타깃은 자동화 런타임을 짓지 않는다 — 실행기는 창 · RHI · 렌더러(사진 우편함 `ScreenshotRequestQueue`)를 모는 클라이언트 구동기라
# 서버가 짓지 않는 `Renderer/` 를 링크한다(언리얼도 스크린샷 비교 테스트는 클라이언트 쪽이다). 서버 확인은 `-gv_serverExitAfterTicks` 와 두 프로세스 체인이 한다.
# 캐시 값은 두고 같은 이름의 보통 변수로 가린다(SW_TARGET_TYPE 과 같은 방식).
if(SW_TARGET_TYPE STREQUAL "Server")
	set(SW_ENABLE_AUTOMATION OFF)
endif()

if(NOT SW_TARGET_TYPE MATCHES "^(Game|Client|Server)$")
	message(FATAL_ERROR "SW_TARGET_TYPE='${SW_TARGET_TYPE}' — one of Game, Client, Server")
endif()

if(SW_TARGET_TYPE STREQUAL "Server" AND NOT SW_BUILD_GAME)
	message(FATAL_ERROR "SW_TARGET_TYPE=Server needs SW_BUILD_GAME=ON — the dedicated server runs the game module")
endif()

# 배포 빌드가 Engine 에 정적으로 넣는 RHI 백엔드 하나 — 쿠킹 표(Config/Engine/CookContract.json rhi_backends)의 이름 그대로(별칭 · 소문자는 구성 실패).
# 기본은 표의 default_rhi_backend, Windows 밖은 Vulkan(project() 전이라 WIN32 대신 CMAKE_HOST_WIN32 — 교차 컴파일은 지원하지 않는다).
# Dev 는 백엔드 넷을 모두 모듈로 짓고 이 값을 읽지 않는다. 이 플랫폼에 없는 백엔드면 구성이 선다(cmake/Engine/RHIBackends.cmake).
include("${CMAKE_CURRENT_LIST_DIR}/JsonValue.cmake")
sw_requireJSONValue(swContractDefaultRHI "${CMAKE_CURRENT_LIST_DIR}/../../Config/Engine/CookContract.json" default_rhi_backend)

if(CMAKE_HOST_WIN32)
	set(SW_SHIPPING_RHI_BACKEND "${swContractDefaultRHI}" CACHE STRING "Shipping 이 Engine 에 정적 링크할 RHI 백엔드(쿠킹 표 이름)")
else()
	set(SW_SHIPPING_RHI_BACKEND "Vulkan" CACHE STRING "Shipping 이 Engine 에 정적 링크할 RHI 백엔드(쿠킹 표 이름)")
endif()

set_property(CACHE SW_SHIPPING_RHI_BACKEND PROPERTY STRINGS DirectX11 DirectX12 Vulkan OpenGL)

option(SW_BUILD_GAMEFRAMEWORK "Source/GameFramework 및 게임 장르별 키트 라이브러리 빌드" ON)

# 끄면 활성 게임(과 그 밖의 켜진 비키트 모듈)이 의존으로 닿지 않는 키트를 짓지 않는다. 기본은 끈다 — 언리얼이 프로젝트가 켠 플러그인만 짓는 것과 같다(일상 빌드 시간).
# 모든 키트가 컴파일되는지는 켜 두는 Ninja-Release 프리셋이 본다.
option(SW_BUILD_UNUSED_KITS "활성 게임이 쓰지 않는 장르 키트도 빌드(끄면 게임 의존으로 닿는 키트만)" OFF)
option(SW_ENABLE_PCH "빌드 속도 단축을 위한 프리컴파일드 헤더(PCH) 사용" ON)

option(SW_ENABLE_SANITIZER "Address/UB Sanitizer 컴파일러 플래그 모듈 활성화" OFF)

# 어떤 새니타이저인가. address(ASan+UBSan, 기본) 와 thread(TSan)는 함께 켤 수 없다 — 런타임이 서로 다른 섀도 메모리를 쓴다.
# TSan 은 GNU/Clang(리눅스)만 된다(clang-cl 은 지원하지 않는다).
set(SW_SANITIZER_KIND "address" CACHE STRING "SW_ENABLE_SANITIZER 가 켤 새니타이저: address | thread")
set_property(CACHE SW_SANITIZER_KIND PROPERTY STRINGS address thread)

# 커버리지 안내 퍼징(libFuzzer) — 엔진에 커버리지 계측(`-fsanitize=fuzzer-no-link`)을 건다(퍼저 대상은 테스트 프레임워크를 다시 만들 때 정한다). 리눅스 clang + ASan 만
# (Windows 의 clang_rt.fuzzer 는 정적 CRT 뿐이라 엔진과 링크되지 않는다).
option(SW_ENABLE_FUZZING "리눅스 clang + ASan 에서 엔진 커버리지 계측(libFuzzer)" OFF)

option(SW_ENABLE_UNITY_BUILD "대형 라이브러리 타겟에 CMake UNITY_BUILD(여러 소스를 합쳐 컴파일) 사용" OFF)
# 유니티 범위 확대 측정용(빌드 속도 계획 5단계): Core_objects 와 GameFramework 키트의 묶음 크기. 0 이면 묶지 않는다.
# SW_ENABLE_UNITY_BUILD 가 꺼져 있으면 값과 상관없이 묶지 않는다(`sw_setUnityBuild`). 측정이 끝나 값을 정하면 이 변수를 지우고 호출부에 숫자를 쓴다.
set(SW_UNITY_BATCH_CORE 0 CACHE STRING "Core_objects 유니티 묶음 크기(0=묶지 않음)")
set(SW_UNITY_BATCH_KIT 0 CACHE STRING "GameFramework 키트 유니티 묶음 크기(0=묶지 않음)")
option(SW_REQUIRE_REFLECTION "Engine/SWGame 등 리플렉션 타겟에 ReflectionParser 및 libclang 필수 요구" ON)

# 2-2) 도구 및 vcpkg 부트스트랩 옵션
option(SW_LLVM_AUTO_BOOTSTRAP "LLVM이 없을 때 Llvm.py를 통해 Tools/LLVM에 최소 clang-cl+libclang 키트 자동 다운로드 허용" ON)
option(SW_USE_VCPKG "vcpkg 패키지 매니저 연동 및 툴체인 사용" ON)
option(SW_VCPKG_AUTO_BOOTSTRAP "vcpkg가 없을 때 Scripts/setup/Vcpkg.py를 통해 Tools/vcpkg로 자동 git clone 허용" ON)
option(SW_VCPKG_FORCE_INSTALL "설치 트리 및 스탬프가 일치해도 vcpkg manifest install 강제 실행" OFF)

# ------------------------------------------------------------------------------
# 3) 디버깅 및 진단 도구 옵션
# ------------------------------------------------------------------------------
option(SW_ENABLE_DEADLOCK_DETECTION "sw::Mutex 잠금 순서를 실시간 추적하여 데드락 사이클 탐지 (성능 저하 주의)" OFF)
option(SW_ENABLE_LTO "Shipping 배포 빌드 시 ThinLTO(링크 타임 최적화) 활성화" ON)
option(SW_ENABLE_STL_CONTAINER "컨테이너의 기본 할당자를 엔진 할당자 대신 std::allocator 로 바꾼다(컨테이너 형식은 두 구성에서 같다)" OFF)
option(SW_ENABLE_TIME_TRACE "Clang 컴파일 시간 프로파일링(-ftime-trace JSON 출력)" OFF)

# Release · Shipping 의 디버그 정보 — lines(함수 · 줄 표만: 크래시 스택이 이름 · 줄을 낸다) | full(지역 변수까지, 오브젝트 · 캐시 · 링크가 크게 는다) | none.
# 링크는 어느 값이든 서명(Windows RSDS · 리눅스 build-id)을 적는다 — 덤프를 그 빌드의 심볼과 짝짓는 열쇠다.
set(SW_RELEASE_DEBUG_INFO "lines" CACHE STRING "Release/Shipping debug info: lines | full | none")
set_property(CACHE SW_RELEASE_DEBUG_INFO PROPERTY STRINGS lines full none)

if(NOT SW_RELEASE_DEBUG_INFO MATCHES "^(lines|full|none)$")
	message(FATAL_ERROR "SW_RELEASE_DEBUG_INFO must be lines, full or none (got '${SW_RELEASE_DEBUG_INFO}')")
endif()

# Tracy 프로파일러 클라이언트(엔진 프로파일러의 두 번째 출력, Source/Engine/Profiling). Shipping 은 언제나 뺀다.
# Windows 는 TracyClient.dll 을 지연 로드한다 — `-gv_tracy` 로 켜기 전에는 DLL 도, 수집 스레드도 없다.
# 리눅스(vcpkg 정적 라이브러리)는 지연 로드가 없어 링크하면 기동부터 수집 스레드 · 리슨 소켓이 선다 — 그래서 기본 꺼짐이다.
if(WIN32 AND NOT SW_SHIPPING_BUILD)
	set(swTracyDefault ON)
else()
	set(swTracyDefault OFF)
endif()

option(SW_ENABLE_TRACY "Tracy 프로파일러 클라이언트 링크(개발 빌드, Shipping 은 무시)" ${swTracyDefault})

# 복합 문자 셰이핑 — HarfBuzz(셰이퍼, MIT) + SheenBidi(전체 UAX #9 수준, Apache 2.0). 엔진 인터페이스(ITextShaper · TextBidi) 뒤에 있고
# 헤더는 Source/Engine/Text/HarfBuzz · Text/SheenBidi 에서만 include 한다. 켜면 두 패키지가 필수다(못 찾으면 구성이 선다).
# 기본 꺼짐: vcpkg 설치 트리를 워크트리끼리 나눠 쓰므로 패키지는 main 에서 먼저 설치한다. 설치한 뒤 ON 으로 바꾼다.
option(SW_ENABLE_COMPLEX_TEXT "HarfBuzz + SheenBidi 복합 문자 셰이핑(아랍어 연결형 · 전체 양방향). 끄면 단순 셰이퍼 · 단순 양방향" OFF)
# 두 패키지는 vcpkg.json 의 선택 기능 `complex-text` 에 있다 — 기능 없이 켜면 패키지가 설치되지 않은 채 find_package 에서야 선다. 이유를 말하고 먼저 선다.
if(SW_ENABLE_COMPLEX_TEXT AND NOT "complex-text" IN_LIST VCPKG_MANIFEST_FEATURES)
	message(FATAL_ERROR "SW_ENABLE_COMPLEX_TEXT=ON needs the vcpkg manifest feature: configure with -DVCPKG_MANIFEST_FEATURES=complex-text")
endif()

# 활성화할 대상 게임 팩 선택 (Source/Games/ 하위 디렉터리 이름)
set(SW_ACTIVE_GAME "Empty" CACHE STRING "활성 게임(Source/Games 폴더 이름) — Bin/Modules/SWGame · Shipping 이 굽는 게임. Dev 는 실행할 때 -game=<게임> 으로 다른 게임도 고른다")
file(GLOB swGamePresetFiles RELATIVE "${CMAKE_SOURCE_DIR}/Config/Game" "${CMAKE_SOURCE_DIR}/Config/Game/*.json")
string(REPLACE ".json" "" swGameNames "${swGamePresetFiles}")
set_property(CACHE SW_ACTIVE_GAME PROPERTY STRINGS ${swGameNames})

# 활성 게임과 함께 지을 게임 팩(Dev 전용) — 비우면 활성 게임만, `ALL` 이면 Source/Games 전부, 아니면 이름 목록(`NileCity;Shooter3D`).
# 함께 짓는 게임은 `SWGame_<게임>` 타깃으로 `Bin/Games/<게임>/SWGame` 에 나오고 엔진 · 프레임워크 · 키트는 한 번만 컴파일된다(`sw_addGameModule`).
# `SW_BUILD_UNUSED_KITS=OFF` 이면 고른 게임들이 의존으로 닿는 키트의 합집합만 짓는다. Shipping 은 활성 게임 하나를 정적으로 링크하므로 이 값을 보지 않는다.
set(SW_GAMES "" CACHE STRING "활성 게임과 함께 지을 게임 팩(Dev 전용): 비움 | ALL | 이름 목록")

# ------------------------------------------------------------------------------
# 4) 컴파일 PDB · compile_commands.json · 플래그 INTERFACE 초기화
# ------------------------------------------------------------------------------
set(CMAKE_COMPILE_PDB_NAME "compile")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

unset(CMAKE_BUILD_PARALLEL_LEVEL CACHE)
set(sw_flag_libraries "")

# ------------------------------------------------------------------------------
# 5) Ninja 빌드 작업 풀(Job Pools) — 컴파일(CPU 풀가동) vs 링크(메모리/디스크 제한) 분리
# ------------------------------------------------------------------------------
if(CMAKE_GENERATOR MATCHES "Ninja")
	cmake_host_system_information(RESULT cpuCount QUERY NUMBER_OF_LOGICAL_CORES)

	if(NOT cpuCount OR cpuCount LESS 2)
		set(cpuCount 4)
	endif()

	# 링크 동시 실행 개수는 코어 수의 1/4 (최소 2, 최대 4)로 제한하여 RAM 부족/페이징 스래싱 방지
	math(EXPR linkJobs "${cpuCount} / 4")

	if(linkJobs LESS 2)
		set(linkJobs 2)
	elseif(linkJobs GREATER 4)
		set(linkJobs 4)
	endif()

	set_property(GLOBAL PROPERTY JOB_POOLS compile_job_pool=${cpuCount} link_job_pool=${linkJobs})
	set(CMAKE_JOB_POOL_COMPILE compile_job_pool)
	set(CMAKE_JOB_POOL_LINK link_job_pool)
endif()
