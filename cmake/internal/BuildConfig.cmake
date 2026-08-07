# ==============================================================================
# @file cmake/internal/BuildConfig.cmake
# @brief 엔진 내부: Development(MODULE) vs Shipping(STATIC) + DLL export 헬퍼
# ==============================================================================

# SW_SHIPPING_BUILD 는 빌드 타입(Debug/Release)과 독립입니다.
# - OFF (기본): Core SHARED + Editor/SWGame MODULE (핫리로드 가능). Release여도 Dev 레이아웃.
# - ON: Core/SWGame STATIC, Editor MODULE 비활성. 보통 Ninja-Shipping preset과 함께 사용.
option(SW_SHIPPING_BUILD "Static Core/SWGame shipping layout (no Editor MODULE / no hot-reload)" OFF)

if(SW_SHIPPING_BUILD)
	# CMake: SW_SHIPPING_BUILD / C++: SW_SHIPPING
	add_compile_definitions(SW_SHIPPING)
	# Shipping links all RHI backends into Core — no RHI_* module load path.
	set(SW_RHI_AS_MODULES OFF CACHE BOOL "Build RHI backends as MODULE plugins (forced OFF for shipping)" FORCE)
	message(STATUS "[BuildConfig] Shipping build: Core/SWGame STATIC, Editor DLL disabled (SW_SHIPPING_BUILD=ON)")
	message(STATUS "[BuildConfig] SW_RHI_AS_MODULES forced OFF for shipping")
else()
	message(STATUS "[BuildConfig] Development build: Core SHARED, Editor/SWGame MODULE DLLs (SW_SHIPPING_BUILD=OFF, type=${CMAKE_BUILD_TYPE})")
endif()

# Core SHARED DLL: SW_EXPORTS(빌드)/SW_IMPORTS(소비) 및 Windows export-all 설정.
function(sw_configure_core_dll_exports TARGET_NAME LIB_TYPE)
	if(LIB_TYPE STREQUAL "SHARED")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_EXPORTS)
		target_compile_definitions(${TARGET_NAME} INTERFACE SW_IMPORTS)
		if(WIN32)
			set_target_properties(${TARGET_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
		endif()
	endif()
endfunction()

# Editor/Game MODULE DLL: SW_MODULE_EXPORTS 컴파일 정의를 붙입니다.
function(sw_configure_module_dll_exports TARGET_NAME LIB_TYPE)
	if(LIB_TYPE STREQUAL "MODULE")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_MODULE_EXPORTS)
	endif()
endfunction()

# 로그 출력 태그 (호출 모듈 TU에 컴파일 타임으로 박힘 — 전역 _target 덮어쓰기 방지)
function(sw_set_log_tag TARGET_NAME TAG)
	target_compile_definitions(${TARGET_NAME} PRIVATE "SW_LOG_TAG=\"${TAG}\"")
endfunction()
