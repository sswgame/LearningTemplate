# ==============================================================================
# @file Source/App/CMakeLists.txt
# @brief 메인 애플리케이션(App) 빌드 및 패키징 스크립트
# ==============================================================================

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

add_executable(App ${APP_SOURCES})
set_target_properties(App PROPERTIES FOLDER "Source/App")

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
sw_configurePch(App "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

sw_addReflectionStep(App
    INCLUDES "${CMAKE_SOURCE_DIR}/Source"
    HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/AppConfig.h"
)

# ------------------------------------------------------------------------------
# 2) 런타임 DLL 복사 — DXC, Vulkan validation(+mimalloc)
# ------------------------------------------------------------------------------
sw_copyDxcDlls(App)
sw_copyVulkanValidationRuntime(App)
sw_emitRuntimeCopies(App)
