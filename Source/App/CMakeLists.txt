# ==============================================================================
# @file Source/App/CMakeLists.txt
# @brief 메인 애플리케이션(App) 빌드 및 패키징 스크립트
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) App 실행 파일 — Engine + RuntimeAPI만 링크 (GameFramework는 delay-load)
#    SWGame 링크/의존은 루트 CMakeLists가 Tools 이후 연결
# ------------------------------------------------------------------------------
sw_addExecutable(App
    LINK_LIBRARIES
        Engine
        RuntimeAPI
)

sw_addReflectionStep(App
    INCLUDES "${CMAKE_SOURCE_DIR}/Source"
    HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/AppConfig.h"
)

sw_setLogTag(App Engine)
sw_configurePch(App)

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

target_compile_definitions(App PRIVATE SW_APP_INTERNAL)
