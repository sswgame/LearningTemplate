# ==============================================================================
# @file CMakeLists.txt
# @brief SW Engine 최상위 루트 빌드 스크립트
# ==============================================================================
#
# cmake/
#   Config/      — BuildOptions, GenerateConfigConstants
#   Environment/ — DetectToolchain, VcpkgIntegration, FindWindowsTools, PythonUtils
#   Modules/     — LoadCompileFlags (Compiler, Platform, Architecture, BuildType, Options, Toolchain)
#   Engine/      — ModuleBuildRules, RuntimeDependencies, ReflectionCodeGen, AssetAndToolTargets, RhiBackendSources
#
# ==============================================================================

cmake_minimum_required(VERSION 3.24)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Ninja 빌드 시 대형 링크 작업으로 인한 메모리 스래싱 방지
if(CMAKE_GENERATOR STREQUAL "Ninja")
	include(ProcessorCount)
	ProcessorCount(sw_nproc)
	if(NOT sw_nproc EQUAL 0)
		math(EXPR sw_link_jobs "${sw_nproc} / 4")
		if(sw_link_jobs LESS 1)
			set(sw_link_jobs 1)
		elseif(sw_link_jobs GREATER 4)
			set(sw_link_jobs 4)
		endif()
		set_property(GLOBAL PROPERTY JOB_POOLS link_pool=${sw_link_jobs})
		set(CMAKE_JOB_POOL_LINK link_pool)
	endif()
endif()

# ------------------------------------------------------------------------------
# 1) project() 이전 — 옵션·상수·개발환경·vcpkg 연동
# ------------------------------------------------------------------------------
include(cmake/Config/BuildOptions.cmake)
include(cmake/Config/GenerateConfigConstants.cmake)
include(cmake/Environment/DetectToolchain.cmake)
include(cmake/Environment/VcpkgIntegration.cmake)

project(
	${sw_workspace_name}
	VERSION ${sw_project_version}
	LANGUAGES C CXX
)

# ------------------------------------------------------------------------------
# 2) project() 직후 — Windows 빌드 도구 사후 바인딩, vcpkg 스탬프
# ------------------------------------------------------------------------------
include(cmake/Environment/FindWindowsTools.cmake)
sw_bindClangClWindowsTools()
sw_vcpkgWriteManifestStamp()

# ------------------------------------------------------------------------------
# 3) 컴파일 플래그 · 모듈 빌드 규칙 · 런타임 DLL 복사 · 리플렉션 코드젠 · 에셋/도구 타겟
# ------------------------------------------------------------------------------
include(cmake/Modules/LoadCompileFlags.cmake)
include(cmake/Engine/ModuleBuildRules.cmake)
include(cmake/Engine/RuntimeDependencies.cmake)
include(cmake/Engine/ReflectionCodeGen.cmake)

# 모든 엔진 타겟이 공유하는 include 루트 (Source/, Resource/, generated/)
add_library(sw_third_party_includes INTERFACE)
add_library(sw_public_source_includes INTERFACE)
target_include_directories(sw_public_source_includes INTERFACE
	"${CMAKE_CURRENT_SOURCE_DIR}/Source"
	"${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine"
	"${CMAKE_CURRENT_SOURCE_DIR}/Source/Core"
	"${CMAKE_CURRENT_SOURCE_DIR}/Resource"
	"${CMAKE_BINARY_DIR}/generated"
)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty")
	add_subdirectory(ThirdParty)
endif()

include(cmake/Engine/AssetAndToolTargets.cmake)

# ------------------------------------------------------------------------------
# 4) 서브디렉터리 빌드 및 의존성 구성
# ------------------------------------------------------------------------------
add_subdirectory(Source)
add_subdirectory(Tools)
add_subdirectory(Source/Engine)
add_subdirectory(Source/RuntimeAPI)
add_subdirectory(Source/Editor)
add_subdirectory(Source/App)

if(SW_BUILD_GAMEFRAMEWORK OR SW_BUILD_GAME)
	add_subdirectory(Source/GameFramework)
endif()
if(SW_BUILD_GAME)
	add_subdirectory(Source/Games)
endif()

# App 타겟의 RHI 플러그인, 게임 모듈(SWGame), 에셋 쿠킹 빌드 순서 및 링크 의존성 자동 구성
sw_configureAppDependencies(App)

# ------------------------------------------------------------------------------
# 5) 테스트 — GPU 불필요 lint는 nogpu 라벨
# ------------------------------------------------------------------------------
if(SW_ENABLE_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test")
	include(CTest)
	set(CTEST_TEST_TIMEOUT 180)
	enable_testing()
	add_subdirectory(Test)
	sw_registerLintTests()
endif()
