# ==============================================================================
# @file Source/Games/CMakeLists.txt
# @brief 활성 게임 팩 → 타겟명 SWGame (App LiveReload / Shipping 링크)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 활성 게임 팩 — 타겟명은 항상 SWGame
# App LiveReload / Shipping 정적 링크가 이 이름을 본다
# ------------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${SW_ACTIVE_GAME}/CMakeLists.txt")
	# 무엇을 고를 수 있는지, 어떻게 고치는지까지 알려준다. 여기 걸리는 흔한 경우가 **낡은 캐시**다 —
	# 게임 폴더를 옮기거나 지운 뒤에도 이미 만들어 둔 빌드 디렉터리는 옛 SW_ACTIVE_GAME 을 그대로
	# 들고 있어서, 소스에는 없는 이름으로 configure 가 선다(실제로 Ninja-Debug-ASAN 이 'Demo' 였다).
	file(GLOB _swListGameCandidate RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
	set(_swListAvailableGame "")
	foreach(_swGameName IN LISTS _swListGameCandidate)
		if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_swGameName}/CMakeLists.txt")
			list(APPEND _swListAvailableGame ${_swGameName})
		endif()
	endforeach()
	message(FATAL_ERROR
		"SW_ACTIVE_GAME='${SW_ACTIVE_GAME}' has no CMakeLists.txt under Source/Games/
"
		"  Available: ${_swListAvailableGame}
"
		"  Fix: cmake --preset <preset> -DSW_ACTIVE_GAME=<name> (an existing build cache keeps the old value)")
endif()

message(STATUS "[Games] SW_ACTIVE_GAME=${SW_ACTIVE_GAME} -> target SWGame")
add_subdirectory(${SW_ACTIVE_GAME})
set_property(GLOBAL APPEND PROPERTY SW_DYNAMIC_MODULES SWGame)
