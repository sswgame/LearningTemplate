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
#    구성별로 실제 쓰이는 것만 배포한다. 예전엔 무조건 복사해서 Shipping 산출물에
#    셰이더 컴파일러와 검증 레이어가 그대로 따라 들어갔다.
# ------------------------------------------------------------------------------

# DXC(dxcompiler/dxil)는 캐시 미스 시의 런타임 HLSL 컴파일 폴백에만 쓰인다. 그 폴백은
# ShaderCache 에서 #if !defined(SW_SHIPPING) 으로 막혀 있어(Shipping 은 미리 구운 셰이더가
# 없으면 오류) Shipping 산출물에는 필요 없다.
if(NOT SW_SHIPPING_BUILD)
	sw_copyDxcDlls(App)
endif()

# Vulkan 검증 레이어는 VulkanRHIDevice 가 SW_DEBUG 에서만 켠다 — Debug 구성에만 배포한다.
# (딸려오는 mimalloc 도 검증 레이어 의존성일 뿐이다.)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	sw_copyVulkanValidationRuntime(App)
endif()

sw_emitRuntimeCopies(App)
