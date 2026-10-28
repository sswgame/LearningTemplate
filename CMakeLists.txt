# ==============================================================================
# @file ModuleManager/CMakeLists.txt
# @brief SW Engine 최상위 루트 빌드 스크립트
# @note cmake/ 의 계층과 각 파일의 역할은 cmake/README.md 가 원본이다.
# ==============================================================================

cmake_minimum_required(VERSION 3.24)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# 1) project() 이전 — 옵션·상수·개발환경·vcpkg 연동
# (Ninja 링크 잡풀은 BuildOptions.cmake 5)에서 컴파일 풀과 함께 설정)
# ------------------------------------------------------------------------------
include(cmake/Config/BuildOptions.cmake)
include(cmake/Config/GenerateConfigConstants.cmake)
include(cmake/Environment/DetectToolchain.cmake)
# vcpkg 매니페스트 게이트 — 실제 툴체인은 그 안에서 CMAKE_TOOLCHAIN_FILE 로 확정한다(project() 전이어야 한다).
if(SW_USE_VCPKG)
	include(cmake/Modules/Toolchain/Vcpkg/Vcpkg.cmake)
endif()

project(
	${sw_workspace_name}
	VERSION ${sw_project_version}
	LANGUAGES C CXX
)

# ------------------------------------------------------------------------------
# 2) project() 직후 — Windows 빌드 도구 사후 바인딩, vcpkg 스탬프
# ------------------------------------------------------------------------------
include(cmake/Environment/ToolchainBinaries.cmake)
include(cmake/Environment/FindWindowsTools.cmake)
sw_bindClangClWindowsTools()
sw_bindPinnedArchiver()
sw_vcpkgWriteManifestStamp()

# ------------------------------------------------------------------------------
# 3) 컴파일 플래그 · 모듈 빌드 규칙 · 런타임 DLL 복사 · 리플렉션 코드젠 · 에셋/도구 타겟
# ------------------------------------------------------------------------------
# BuildLayout 은 include 되는 순간 출력 경로와 sw_global_options 를 정하므로 타깃 규칙(UnbuiltSources · ModuleTargets)보다 먼저다.
# RuntimeDependencies 는 BuildLayout 의 sw_queueRuntimeCopy 를 쓴다. 나머지는 함수만 정의한다(파일마다 맡는 일은 cmake/README.md).
include(cmake/Modules/LoadCompileFlags.cmake)
include(cmake/Engine/BuildLayout.cmake)
include(cmake/Engine/UnbuiltSources.cmake)
include(cmake/Engine/TargetCompileRules.cmake)
include(cmake/Engine/EngineABIStamp.cmake)
include(cmake/Engine/ModuleRegistry.cmake)
include(cmake/Engine/ModuleTargets.cmake)
include(cmake/Engine/ExecutableTargets.cmake)
include(cmake/Engine/ModuleManifest.cmake)
include(cmake/Engine/ModuleActivation.cmake)
include(cmake/Engine/ThirdPartyLibs.cmake)
include(cmake/Engine/RuntimeDependencies.cmake)
include(cmake/Engine/ReflectionCodeGen.cmake)
include(cmake/Engine/RHIBackends.cmake)

# 모든 엔진 타겟이 공유하는 include 루트 (Source/, Content/, generated/)
add_library(sw_third_party_includes INTERFACE)
add_library(sw_public_source_includes INTERFACE)
target_include_directories(sw_public_source_includes INTERFACE
	"${CMAKE_CURRENT_SOURCE_DIR}/Source"
	"${CMAKE_CURRENT_SOURCE_DIR}/Content"
	"${CMAKE_BINARY_DIR}/generated"
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty")
	add_subdirectory(ThirdParty)
endif()

include(cmake/Engine/AssetAndToolTargets.cmake)

# ------------------------------------------------------------------------------
# 4) 서브디렉터리 빌드 및 의존성 구성
# ------------------------------------------------------------------------------
# 모듈 매니페스트를 먼저 해석한다 — 모듈을 만드는 함수가 "켜져 있는가" 를 묻는다(`cmake/Engine/ModuleManifest.cmake`).
sw_resolveModuleManifests()

# 순서: Core → ReflectionParser(Core 를 링크한다) → Engine(파서가 있어야 리플렉션 단계가 선다) → ModuleAPI → Editor → ModuleManager → App · Server → GameFramework/Games.
add_subdirectory(Source/Core)
add_subdirectory(Source/Programs/ReflectionParser)
add_subdirectory(Source/Engine)
add_subdirectory(Source/ModuleAPI)
add_subdirectory(Source/Editor)
add_subdirectory(Source/ModuleManager)
add_subdirectory(Source/App)
add_subdirectory(Source/Server)

# 끈 것은 소스 검사 게이트에 "이 구성이 짓지 않는다" 로 적는다.
if(SW_BUILD_GAMEFRAMEWORK OR SW_BUILD_GAME)
	add_subdirectory(Source/GameFramework)
else()
	sw_declareUnbuiltDirectory("${CMAKE_SOURCE_DIR}/Source/GameFramework")
endif()

if(SW_BUILD_GAME)
	add_subdirectory(Source/Games)
else()
	sw_declareUnbuiltDirectory("${CMAKE_SOURCE_DIR}/Source/Games")
endif()

# 온라인 부하 테스트 봇 — 개발 도구. 클라이언트 · 서버 키트가 함께 있는 개발 구성(Game 타깃)에서만 짓는다(배포물 · Client · Server 타깃에 없다).
if(NOT SW_SHIPPING_BUILD AND SW_TARGET_TYPE STREQUAL "Game" AND (SW_BUILD_GAMEFRAMEWORK OR SW_BUILD_GAME))
	add_subdirectory(Source/Programs/OnlineLoadBot)
endif()

# 단위 테스트(Test/ — 링크하는 것마다 실행 파일 하나, 각각 CTest 항목). 배포 구성은 짓지 않는다. 시나리오(Content/**/automation/)는 App 이 돌린다.
if(NOT SW_SHIPPING_BUILD)
	enable_testing()
	add_subdirectory(Test)
endif()

# 여기까지 오면 동적 모듈이 전부 만들어졌다 — 레지스트리에 빠진 것이 없는지 먼저 확인한다.
# (빠지면 App 이 그 DLL 보다 먼저 빌드될 수 있고, 증상은 "가끔 실패" 로만 나타난다.)
sw_verifyDynamicModuleRegistry()
# 동적 모듈을 모두 링크한 뒤에만 다시 쓰는 스탬프 — 바깥 빌드의 핫 리로드 신호(`LiveReloadManager`).
sw_addModuleBuildStamp()

# App · Server 타겟의 RHI 플러그인, 게임 모듈(SWGame), 에셋 쿠킹 빌드 순서 및 링크 의존성 자동 구성(없는 타깃은 건너뛴다)
sw_configureAppDependencies(App)
sw_configureAppDependencies(Server)

# 이 구성이 일부러 짓지 않는 소스(배포의 에디터 · 핫 리로드 · 고르지 않은 RHI 백엔드 …)를 빌드 트리에 적는다 — 구성 끝에 목록 파일로 쓴다.
sw_writeUnbuiltSourceList()

# 모든 타깃이 선 뒤 — `Source/` 아래 폴더를 include 경로로 준 타깃이 있으면 구성을 세운다(접두 없는 include 가 층 게이트를 비껴가는 길).
sw_verifySourceIncludeRoots()
