# ==============================================================================
# @file cmake/UserConfig.cmake
# @brief 사용자가 바꿀 수 있는 빌드 옵션 / 워크스페이스 메타데이터
# @note 툴체인 세부(compiler/vcpkg overlay)는 CMakePresets.json + cmake/Modules/Toolchain/
#
# 네이밍 규칙 (사용자 가독성):
#   SW_*            — -D 로 켜고 끄는 feature option (option())
#   sw_snake_case   — 함수/매크로, 프로젝트 메타/경로/리스트 변수
#   _snake_case     — 함수 내부 로컬
#   CMAKE_* / VCPKG_* — 업스트림 계약 (변경하지 않음)
# ==============================================================================

set(sw_workspace_name "Workspace")
set(sw_project_version "1.0.0")
# Minimum C++17; override with -Dsw_cpp_standard=20 (or 23) if desired.
if(NOT DEFINED sw_cpp_standard)
	set(sw_cpp_standard 17)
endif()
set(sw_output_directory "${CMAKE_CURRENT_SOURCE_DIR}/build")

# --- Feature options (모두 SW_* / option) ---
option(SW_USE_VCPKG "Use vcpkg package integration" ON)
option(SW_ENABLE_TESTING "Build and register unit tests" ON)
option(SW_ENABLE_SANITIZER "Enable sanitizer flag module" OFF)
option(SW_ENABLE_PCH "Enable precompiled headers (Core pch.h)" ON)
option(SW_ENABLE_UNITY_BUILD "Enable unity build flag module" OFF)
option(SW_BUILD_DOCS "Generate Doxygen docs target when available" OFF)
option(SW_AUTO_CHANGELOG "Add AutoChangelog target (manual build; not part of default ALL)" OFF)
option(SW_RHI_AS_MODULES "Build DX11/DX12 RHI backends as MODULE DLLs (Core loads via createRHIDevice)" ON)
option(SW_BUILD_GAME "Build Source/Game (SWGame module)" ON)
# SW_SHIPPING_BUILD 는 cmake/internal/BuildConfig.cmake 에서 option() — Release 와 독립.

# 예전 소문자 CACHE 토글 → 새 SW_* 로 이관 후 제거
macro(sw_migrate_legacy_bool_cache LEGACY_NAME NEW_NAME)
	if(DEFINED ${LEGACY_NAME})
		set(${NEW_NAME} "${${LEGACY_NAME}}" CACHE BOOL "" FORCE)
		unset(${LEGACY_NAME} CACHE)
	endif()
endmacro()
sw_migrate_legacy_bool_cache(sw_use_vcpkg SW_USE_VCPKG)
sw_migrate_legacy_bool_cache(sw_enable_testing SW_ENABLE_TESTING)
sw_migrate_legacy_bool_cache(sw_enable_sanitizer SW_ENABLE_SANITIZER)
sw_migrate_legacy_bool_cache(sw_enable_pch SW_ENABLE_PCH)
sw_migrate_legacy_bool_cache(sw_enable_unity_build SW_ENABLE_UNITY_BUILD)
sw_migrate_legacy_bool_cache(sw_build_Game SW_BUILD_GAME)

set(CMAKE_COMPILE_PDB_NAME "compile")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CMAKE_BUILD_PARALLEL_LEVEL 은 환경변수/빌드 옵션이지 캐시 변수가 아님.
unset(CMAKE_BUILD_PARALLEL_LEVEL CACHE)

# Compiler / platform / architecture INTERFACE libs accumulate here.
# Must be initialized BEFORE Toolchain so vcpkg can append without being wiped later.
set(sw_flag_libraries "")
