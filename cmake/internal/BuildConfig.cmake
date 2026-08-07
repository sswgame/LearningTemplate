# ==============================================================================
# @file cmake/internal/BuildConfig.cmake
# @brief 엔진 내부: Development(MODULE) vs Shipping(STATIC) + DLL export 헬퍼
# ==============================================================================

# 단일 설정 생성기: CMAKE_BUILD_TYPE
# 멀티 설정 생성기: SW_SHIPPING_BUILD 캐시 (기본 OFF = 개발 DLL)
if(CMAKE_CONFIGURATION_TYPES)
    option(SW_SHIPPING_BUILD "Static Core/SWGame shipping build (no Editor/Game DLLs)" OFF)
else()
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(SW_SHIPPING_BUILD TRUE)
    else()
        set(SW_SHIPPING_BUILD FALSE)
    endif()
endif()

if(SW_SHIPPING_BUILD)
    # CMake: SW_SHIPPING_BUILD / C++: SW_SHIPPING
    add_compile_definitions(SW_SHIPPING)
    message(STATUS "[BuildConfig] Shipping build: Core/SWGame STATIC, Editor DLL disabled")
else()
    message(STATUS "[BuildConfig] Development build: Core SHARED, Editor/SWGame MODULE DLLs")
endif()

function(sw_configure_core_dll_exports TARGET_NAME LIB_TYPE)
    if(LIB_TYPE STREQUAL "SHARED")
        target_compile_definitions(${TARGET_NAME} PRIVATE SW_EXPORTS)
        target_compile_definitions(${TARGET_NAME} INTERFACE SW_IMPORTS)
        if(WIN32)
            set_target_properties(${TARGET_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
        endif()
    endif()
endfunction()

function(sw_configure_module_dll_exports TARGET_NAME LIB_TYPE)
    if(LIB_TYPE STREQUAL "MODULE")
        target_compile_definitions(${TARGET_NAME} PRIVATE SW_MODULE_EXPORTS)
    endif()
endfunction()

# 로그 출력 태그 (호출 모듈 TU에 컴파일 타임으로 박힘 — 전역 _target 덮어쓰기 방지)
function(sw_set_log_tag TARGET_NAME TAG)
    target_compile_definitions(${TARGET_NAME} PRIVATE "SW_LOG_TAG=\"${TAG}\"")
endfunction()
