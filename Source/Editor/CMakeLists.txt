# ==============================================================================
# @file Source/Editor/CMakeLists.txt
# @brief 인게임 에디터 모듈 (개발 빌드 MODULE DLL)
# ==============================================================================

if(NOT SW_SHIPPING_BUILD)
    sw_add_library(EditorModule MODULE
        LINK_LIBRARIES
            Core
            RuntimeAPI
            sw_graphics_libs
            imgui
    )

    sw_configure_module_dll_exports(EditorModule MODULE)
    sw_set_log_tag(EditorModule Editor)
    sw_set_module_bin_output(EditorModule)
endif()
