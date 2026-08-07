# ==============================================================================
# @file Source/App/CMakeLists.txt
# @brief 메인 애플리케이션(App) 빌드 및 패키징 스크립트
# ==============================================================================

set(APP_LINK_LIBS
    Core
    RuntimeAPI
    sw_graphics_libs
)

sw_add_executable(App
    LINK_LIBRARIES
        ${APP_LINK_LIBS}
)

sw_set_log_tag(App Engine)
sw_configure_pch(App)

# SWGame 링크/의존은 루트 CMakeLists에서 Tools 이후 연결
if(NOT SW_SHIPPING_BUILD AND TARGET EditorModule)
    add_dependencies(App EditorModule)
endif()

sw_copy_dxc_dlls(App)

sw_copy_vcpkg_shared_lib(App "VkLayer_khronos_validation")
sw_copy_vcpkg_shared_lib(App "mimalloc")
sw_copy_vcpkg_shared_lib(App "mimalloc-redirect")
sw_copy_vcpkg_file(App "VkLayer_khronos_validation.json")
