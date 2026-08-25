# ==============================================================================
# @file Source/App/CMakeLists.txt
# @brief 메인 애플리케이션(App) 빌드 및 패키징 스크립트
# ==============================================================================

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

add_executable(App ${APP_SOURCES})

target_include_directories(App PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(App
    PRIVATE
        Engine
        RuntimeAPI
        sw_global_options
)
if(sw_flag_libraries)
    target_link_libraries(App PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(App PRIVATE "SW_LOG_TAG=\"Engine\"" SW_APP_INTERNAL)
target_precompile_headers(App PRIVATE "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

sw_addReflectionStep(App
    INCLUDES "${CMAKE_SOURCE_DIR}/Source"
    HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/AppConfig.h"
)

# ------------------------------------------------------------------------------
# 2) Dev: EditorModule이 App보다 먼저 빌드되게만 함 (링크하지 않음)
# ------------------------------------------------------------------------------
if(NOT SW_SHIPPING_BUILD AND TARGET EditorModule)
    add_dependencies(App EditorModule)
endif()

# RHI MODULE 플러그인 — 런타임 로드, App 빌드 시 함께 재빌드
if(SW_RHI_AS_MODULES)
    foreach(_swRhiMod IN ITEMS RHI_DX11 RHI_DX12 RHI_GL RHI_Vulkan)
        if(TARGET ${_swRhiMod})
            add_dependencies(App ${_swRhiMod})
        endif()
    endforeach()
    unset(_swRhiMod)
endif()

# ------------------------------------------------------------------------------
# 3) 런타임 DLL 복사 — DXC, Vulkan validation(+mimalloc), POST_BUILD를 한 줄로 묶음
# ------------------------------------------------------------------------------
sw_copyDxcDlls(App)
sw_copyVulkanValidationRuntime(App)
sw_emitRuntimeCopies(App)
