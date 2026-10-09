# ==============================================================================
# @file cmake/Config/BuildOptions.cmake
# @brief SW Engine 전역 기능 빌드 옵션(SW_*) 및 워크스페이스 메타데이터 정의
#
# [CMake 네이밍 및 개발 규칙]:
# - sw_camelCase      : CMake function() / macro() 헬퍼 함수 (예: sw_addModuleLibrary, sw_addGameModule)
# - sw_snake_case     : 프로젝트 내부 변수 및 INTERFACE 타겟명 (예: sw_flag_libraries)
# - SW_UPPER_SNAKE    : option() / CACHE 빌드 옵션 및 C++ 컴파일 매크로 (예: SW_ENABLE_PCH, SW_SHIPPING_BUILD)
# - camelCase         : 함수/매크로 내부 로컬 변수
# 이 파일은 옵션(SW_*)과 메타데이터만 둔다 — 타깃 함수는 cmake/Engine/, 찾은 도구 경로 캐시(SW_SCCACHE_EXE 등)는 찾는 파일이 둔다.
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
option(SW_BUILD_DOCS "Doxygen 코드 문서화 생성 타겟 추가" OFF)
option(SW_BUILD_GAME "GameFramework 및 게임 모듈(SWGame DLL/정적 링크) 빌드" ON)

# 아래 옵션들의 기본값이 이 값에 따라 갈리므로 가장 먼저 선언한다.
option(SW_SHIPPING_BUILD "배포용 단일 실행 파일 정적 링크 빌드 (Editor 모듈 제외 및 최고 성능 최적화)" OFF)

# 빌드 타깃 종류 — 언리얼 TargetType(Game · Client · Server)과 같은 자리. 모듈 매니페스트의 `_listTarget` 과 코드 매크로
# (`SW_WITH_CLIENT_CODE` · `SW_WITH_SERVER_CODE`, `cmake/Engine/BuildLayout.cmake`)가 이 값을 따른다.
# Game   — 클라이언트 + 서버 코드 전부(개발 · 에디터 · 한 프로세스 서버/클라 시험 · 리슨 서버). App 과 Server 를 짓는다.
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

if(NOT SW_TARGET_TYPE MATCHES "^(Game|Client|Server)$")
	message(FATAL_ERROR "SW_TARGET_TYPE='${SW_TARGET_TYPE}' — one of Game, Client, Server")
endif()

if(SW_TARGET_TYPE STREQUAL "Server" AND NOT SW_BUILD_GAME)
	message(FATAL_ERROR "SW_TARGET_TYPE=Server needs SW_BUILD_GAME=ON — the dedicated server runs the game module")
endif()

# 배포 빌드가 Engine 에 정적으로 넣는 RHI 백엔드 하나 — 쿠킹 표(Config/Engine/CookContract.json rhi_backends)의 이름 그대로(별칭 · 소문자는 구성 실패).
# 기본은 표의 default_rhi_backend, Windows 밖은 Vulkan(project() 전이라 WIN32 대신 CMAKE_HOST_WIN32 — 교차 컴파일은 지원하지 않는다).
# Dev 는 백엔드 넷을 모두 모듈로 짓고 이 값을 읽지 않는다. 이 플랫폼에 없는 백엔드면 구성이 선다(cmake/Engine/RhiBackends.cmake).
file(READ "${CMAKE_CURRENT_LIST_DIR}/../../Config/Engine/CookContract.json" swCookContractText)
string(JSON swContractDefaultRhi GET "${swCookContractText}" default_rhi_backend)

if(CMAKE_HOST_WIN32)
	set(SW_SHIPPING_RHI_BACKEND "${swContractDefaultRhi}" CACHE STRING "Shipping 이 Engine 에 정적 링크할 RHI 백엔드(쿠킹 표 이름)")
else()
	set(SW_SHIPPING_RHI_BACKEND "Vulkan" CACHE STRING "Shipping 이 Engine 에 정적 링크할 RHI 백엔드(쿠킹 표 이름)")
endif()

set_property(CACHE SW_SHIPPING_RHI_BACKEND PROPERTY STRINGS DirectX11 DirectX12 Vulkan OpenGL)

option(SW_BUILD_GAMEFRAMEWORK "Source/GameFramework 및 게임 장르별 키트 라이브러리 빌드" ON)

# 끄면 활성 게임(과 그 밖의 켜진 비키트 모듈)이 의존으로 닿지 않는 키트를 짓지 않는다. 게임별 · Shipping 프리셋이 끈다(언리얼 패키징이 프로젝트가 켠 플러그인만 담는 것과 같다).
# 켜 두는 Ninja-Debug · CI 프리셋이 모든 키트와 키트 시험을 짓는다.
option(SW_BUILD_UNUSED_KITS "활성 게임이 쓰지 않는 장르 키트도 빌드(끄면 게임 의존으로 닿는 키트만)" ON)
option(SW_ENABLE_PCH "빌드 속도 단축을 위한 프리컴파일드 헤더(PCH) 사용" ON)

option(SW_ENABLE_SANITIZER "Address/UB Sanitizer 컴파일러 플래그 모듈 활성화" OFF)

# 어떤 새니타이저인가. address(ASan+UBSan, 기본) 와 thread(TSan)는 함께 켤 수 없다 — 런타임이 서로 다른 섀도 메모리를 쓴다.
# TSan 은 GNU/Clang(리눅스)만 된다(clang-cl 은 지원하지 않는다).
set(SW_SANITIZER_KIND "address" CACHE STRING "SW_ENABLE_SANITIZER 가 켤 새니타이저: address | thread")
set_property(CACHE SW_SANITIZER_KIND PROPERTY STRINGS address thread)

# 커버리지 안내 퍼징(libFuzzer) — 엔진에 커버리지 계측(`-fsanitize=fuzzer-no-link`)을 걸고 `Test/FuzzTest/LoaderFuzzer` 를 짓는다. 리눅스 clang + ASan 만
# (Windows 의 clang_rt.fuzzer 는 정적 CRT 뿐이라 엔진과 링크되지 않는다). 프리셋 `CI-Fuzz`, 밤마다 `.github/workflows/fuzz.yml`.
option(SW_ENABLE_FUZZING "리눅스 clang + ASan 에서 libFuzzer 대상(LoaderFuzzer)과 엔진 커버리지 계측" OFF)

# 배포 빌드의 산출물 디렉터리에 테스트 실행 파일이 섞이면 안 된다. 그렇다고 Shipping 에서 테스트를
# 끄면 배포 구성이 실제로 도는지 아무도 확인하지 않게 된다(CI 는 Shipping 에서도 같은 시험 집합을 돈다).
# 테스트는 어디서든 빌드하고, Shipping 에서는 실행 파일을 Bin 이 아니라 TestBin 으로 뺀다(sw_addTestExecutable).
# 테스트가 끌고 오는 개발용 DLL 은 각 호출부의 SW_SHIPPING_BUILD 가드가 이미 막고 있다.
option(SW_ENABLE_TESTING "단위/통합 테스트 프로젝트 빌드 및 CTest 등록" ON)
option(SW_ENABLE_UNITY_BUILD "대형 라이브러리 타겟에 CMake UNITY_BUILD(여러 소스를 합쳐 컴파일) 사용" OFF)
option(SW_REQUIRE_REFLECTION "Engine/SWGame 등 리플렉션 타겟에 ReflectionParser 및 libclang 필수 요구" ON)
option(SW_USE_SCCACHE "사용 가능 시 sccache 컴파일러 캐시를 활성화하여 빌드 가속" ON)

# 2-2) 도구 및 vcpkg 부트스트랩 옵션
option(SW_LLVM_AUTO_BOOTSTRAP "LLVM이 없을 때 SetupLlvm.py를 통해 Tools/LLVM에 최소 clang-cl+libclang 키트 자동 다운로드 허용" ON)
option(SW_USE_VCPKG "vcpkg 패키지 매니저 연동 및 툴체인 사용" ON)
option(SW_VCPKG_AUTO_BOOTSTRAP "vcpkg가 없을 때 Scripts/setup/SetupVcpkg.py를 통해 Tools/vcpkg로 자동 git clone 허용" ON)
option(SW_VCPKG_FORCE_INSTALL "설치 트리 및 스탬프가 일치해도 vcpkg manifest install 강제 실행" OFF)

# ------------------------------------------------------------------------------
# 3) 디버깅 및 진단 도구 옵션
# ------------------------------------------------------------------------------
option(SW_ENABLE_DEADLOCK_DETECTION "sw::Mutex 잠금 순서를 실시간 추적하여 데드락 사이클 탐지 (성능 저하 주의)" OFF)
option(SW_ENABLE_LTO "Shipping 배포 빌드 시 ThinLTO(링크 타임 최적화) 활성화" ON)
option(SW_ENABLE_STL_CONTAINER "엔진 커스텀 할당자 대신 std::allocator를 사용하도록 설정" OFF)
option(SW_ENABLE_TIME_TRACE "Clang 컴파일 시간 프로파일링(-ftime-trace JSON 출력)" OFF)

# Release · Shipping 의 디버그 정보 — lines(함수 · 줄 표만: 크래시 스택이 이름 · 줄을 낸다) | full(지역 변수까지, 오브젝트 · 캐시 · 링크가 크게 는다) | none.
# 링크는 어느 값이든 서명(Windows RSDS · 리눅스 build-id)을 적는다 — 덤프를 그 빌드의 심볼과 짝짓는 열쇠다.
set(SW_RELEASE_DEBUG_INFO "lines" CACHE STRING "Release/Shipping debug info: lines | full | none")
set_property(CACHE SW_RELEASE_DEBUG_INFO PROPERTY STRINGS lines full none)

if(NOT SW_RELEASE_DEBUG_INFO MATCHES "^(lines|full|none)$")
	message(FATAL_ERROR "SW_RELEASE_DEBUG_INFO must be lines, full or none (got '${SW_RELEASE_DEBUG_INFO}')")
endif()

# Tracy 프로파일러 클라이언트(엔진 프로파일러의 두 번째 출력, Source/Engine/Utility/Profiling). Shipping 은 언제나 뺀다.
# Windows 는 TracyClient.dll 을 지연 로드한다 — `-gv_tracy` 로 켜기 전에는 DLL 도, 수집 스레드도 없다.
# 리눅스(vcpkg 정적 라이브러리)는 지연 로드가 없어 링크하면 기동부터 수집 스레드 · 리슨 소켓이 선다 — 그래서 기본 꺼짐이다.
if(WIN32 AND NOT SW_SHIPPING_BUILD)
	set(swTracyDefault ON)
else()
	set(swTracyDefault OFF)
endif()

option(SW_ENABLE_TRACY "Tracy 프로파일러 클라이언트 링크(개발 빌드, Shipping 은 무시)" ${swTracyDefault})

# 활성화할 대상 게임 팩 선택 (Source/Games/ 하위 디렉터리 이름)
set(SW_ACTIVE_GAME "Empty" CACHE STRING "활성화할 Source/Games 게임 팩 — 게임을 바꿀 때는 이 값이 아니라 프리셋(Ninja-Debug-<게임>)을 고른다")
file(GLOB swGamePresetFiles RELATIVE "${CMAKE_SOURCE_DIR}/Config/Game" "${CMAKE_SOURCE_DIR}/Config/Game/*.json")
string(REPLACE ".json" "" swGameNames "${swGamePresetFiles}")
set_property(CACHE SW_ACTIVE_GAME PROPERTY STRINGS ${swGameNames})

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
