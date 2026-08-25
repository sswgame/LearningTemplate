# ==============================================================================
# @file CMakeLists.txt
# @brief SW Engine 최상위 루트 빌드 스크립트
# ==============================================================================
#
# cmake/
#   UserConfig.cmake       — SW_* 옵션
#   VcpkgGate.cmake        — SW_USE_VCPKG 시 Vcpkg.cmake (≠ CMAKE_TOOLCHAIN_FILE)
#   LoadFlagModules.cmake  — Compiler/Platform/… INTERFACE
#   Modules/               — 플래그 · Toolchain/Vcpkg
#   internal/Targets.cmake — 타겟/설치/출력/Shipping/ThirdParty 헬퍼 (단일 진입)
#   internal/              — SetupEnvironment, Reflection, VcpkgRuntime, AuxTargets …
#
# ==============================================================================

cmake_minimum_required(VERSION 3.24)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# 1) project() 이전 — 옵션·툴체인·vcpkg 루트
#    UserConfig: SW_* / sw_workspace_name
#    SetupEnvironment: clang-cl, SDK, vcpkg 경로
#    VcpkgGate: 매니페스트 게이트 (CMAKE_TOOLCHAIN_FILE 자체가 아님)
# ------------------------------------------------------------------------------
include(cmake/UserConfig.cmake)
include(cmake/internal/ConfigConstants.cmake)
include(cmake/internal/SetupEnvironment.cmake)
include(cmake/VcpkgGate.cmake)

project(
	${sw_workspace_name}
	VERSION ${sw_project_version}
	LANGUAGES C CXX
)

# ------------------------------------------------------------------------------
# 2) project() 직후 — 아카이브 도구 재바인딩, vcpkg 스탬프
#    최소 LLVM 키트에 llvm-lib가 없으면 MSVC lib.exe로 돌린다.
# ------------------------------------------------------------------------------
include(cmake/internal/ClangClWindowsTools.cmake)
sw_vcpkgWriteManifestStamp()

# ------------------------------------------------------------------------------
# 3) 플래그 INTERFACE · 타겟 헬퍼 · DLL 복사 · 리플렉션 생성
# ------------------------------------------------------------------------------
include(cmake/LoadFlagModules.cmake)
include(cmake/internal/Targets.cmake)
include(cmake/internal/VcpkgRuntime.cmake)
include(cmake/internal/Reflection.cmake)

# 모든 엔진 타겟이 공유하는 include 루트 (Source/, Resource/)
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

include(cmake/internal/AuxTargets.cmake)

# ------------------------------------------------------------------------------
# 4) 서브디렉터리 순서 (앞 타겟을 뒤가 링크/생성에 씀)
#    Source        — Core STATIC (Tools/Engine이 Core를 링크)
#    Tools         — ReflectionParser (Engine .gen.cpp 생성에 필요)
#    Engine → RuntimeAPI → Editor → App
#    GameFramework / Games — 옵션
#    Test          — 맨 마지막
# ------------------------------------------------------------------------------
add_subdirectory(Source)
add_subdirectory(Tools)
add_subdirectory(Source/Engine)
add_subdirectory(Source/RuntimeAPI)
add_subdirectory(Source/Editor)
add_subdirectory(Source/App)

# RHI_* 는 MODULE 플러그인. App이 런타임에 로드하므로 빌드 순서만 묶는다 (링크하지 않음).
if(TARGET App)
	foreach(rhiMod IN ITEMS RHI_DX11 RHI_DX12 RHI_GL RHI_Vulkan)
		if(TARGET ${rhiMod})
			add_dependencies(App ${rhiMod})
		endif()
	endforeach()
endif()

if(SW_BUILD_GAMEFRAMEWORK OR SW_BUILD_GAME)
	add_subdirectory(Source/GameFramework)
endif()
if(SW_BUILD_GAME)
	add_subdirectory(Source/Games)
endif()

# Shipping: SWGame을 App에 정적 링크 및 CookAssets 자동 선행 실행
# Dev: delay-load 이므로 링크하지 않고, DLL이 App보다 먼저 빌드되게만 한다.
if(TARGET App)
	if(SW_SHIPPING_BUILD)
		if(TARGET SWGame)
			target_link_libraries(App PRIVATE SWGame)
		endif()
		if(TARGET CookAssets)
			add_dependencies(App CookAssets)
		endif()
	else()
		if(TARGET SWGame)
			get_property(dynMods GLOBAL PROPERTY SW_DYNAMIC_MODULES)
			foreach(mod IN LISTS dynMods)
				if(TARGET ${mod})
					add_dependencies(App ${mod})
				endif()
			endforeach()
		endif()
	endif()
endif()

# ------------------------------------------------------------------------------
# 5) 테스트 — GPU 불필요 lint는 nogpu 라벨
# ------------------------------------------------------------------------------
if(SW_ENABLE_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test")
	include(CTest)
	set(CTEST_TEST_TIMEOUT 60)
	enable_testing()
	add_subdirectory(Test)
	if(TARGET CheckEngineLayers AND Python3_Interpreter_FOUND)
		add_test(
			NAME CheckEngineLayers
			COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/${SW_SCRIPT_LINT_CHECK_ENGINE_LAYERS}"
				--root "${CMAKE_SOURCE_DIR}"
		)
		set_tests_properties(CheckEngineLayers PROPERTIES LABELS "lint" TIMEOUT 15)
	endif()

	if(TARGET CheckIncludeOrder AND Python3_Interpreter_FOUND)
		add_test(
			NAME CheckIncludeOrder
			COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Scripts/lint/CheckIncludeOrder.py"
				--root "${CMAKE_SOURCE_DIR}"
		)
		set_tests_properties(CheckIncludeOrder PROPERTIES LABELS "lint" TIMEOUT 15)
	endif()
	if(TARGET CheckSourceGlob AND Python3_Interpreter_FOUND)
		add_test(
			NAME CheckSourceGlob
			COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/${SW_SCRIPT_LINT_CHECK_SOURCE_GLOB}"
				--root "${CMAKE_SOURCE_DIR}" --build "${CMAKE_BINARY_DIR}" --active-game "${SW_ACTIVE_GAME}"
		)
		set_tests_properties(CheckSourceGlob PROPERTIES LABELS "lint" TIMEOUT 15)
	endif()
endif()
