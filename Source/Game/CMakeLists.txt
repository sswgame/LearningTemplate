# ==============================================================================
# @file Source/Game/CMakeLists.txt
# @brief 게임 로직 모듈 (개발: MODULE / shipping: STATIC)
# ==============================================================================

if(SW_SHIPPING_BUILD)
    set(SWGAME_LIB_TYPE STATIC)
else()
    set(SWGAME_LIB_TYPE MODULE)
endif()

sw_add_library(SWGame ${SWGAME_LIB_TYPE}
    LINK_LIBRARIES
        Core
        RuntimeAPI
        sw_graphics_libs
)

sw_configure_module_dll_exports(SWGame ${SWGAME_LIB_TYPE})
sw_set_log_tag(SWGame Game)
sw_configure_pch(SWGame)

if(NOT SW_SHIPPING_BUILD)
    sw_set_module_bin_output(SWGame)
endif()

sw_add_reflection_step(SWGame
    HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/SWGameTypes.h"
    INCLUDES "${CMAKE_SOURCE_DIR}/Source"
)
