# ==============================================================================
# @file Source/Games/CMakeLists.txt
# @brief 활성 게임 팩 → 타겟명 SWGame (App LiveReload / Shipping 링크)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 활성 게임 팩 — 타겟명은 항상 SWGame
#    App LiveReload / Shipping 정적 링크가 이 이름을 본다
# ------------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${SW_ACTIVE_GAME}/CMakeLists.txt")
	message(FATAL_ERROR "SW_ACTIVE_GAME='${SW_ACTIVE_GAME}' has no CMakeLists.txt under Source/Games/")
endif()

message(STATUS "[Games] SW_ACTIVE_GAME=${SW_ACTIVE_GAME} -> target SWGame")
add_subdirectory(${SW_ACTIVE_GAME})
set_property(GLOBAL APPEND PROPERTY SW_DYNAMIC_MODULES SWGame)
