# ==============================================================================
# @file Source/Games/CMakeLists.txt
# @brief 활성 게임 팩 → 타겟명 SWGame (App LiveReload / Shipping 링크), 함께 짓는 게임 팩(SW_GAMES) → SWGame_<게임>
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 활성 게임 팩 — 타깃 SWGame_<게임>, 모듈 이름 · 산출물은 SWGame(`Bin/Modules/SWGame`)
# App LiveReload 는 모듈 이름을, Shipping 정적 링크는 전역 속성 SW_ACTIVE_GAME_TARGET 을 본다
# ------------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${SW_ACTIVE_GAME}/CMakeLists.txt")
	# 무엇을 고를 수 있는지, 어떻게 고치는지까지 알려준다. 여기 걸리는 흔한 경우가 **낡은 캐시**다 —
	# 게임 폴더를 옮기거나 지운 뒤에도 이미 만들어 둔 빌드 디렉터리는 옛 SW_ACTIVE_GAME 을 그대로
	# 들고 있어서, 소스에는 없는 이름으로 configure 가 선다.
	file(GLOB list_game_candidate RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
	set(list_available_game "")
	foreach(game_name IN LISTS list_game_candidate)
		if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${game_name}/CMakeLists.txt")
			list(APPEND list_available_game ${game_name})
		endif()
	endforeach()
	message(FATAL_ERROR
		"SW_ACTIVE_GAME='${SW_ACTIVE_GAME}' has no CMakeLists.txt under Source/Games/
"
		"  Available: ${list_available_game}
"
		"  Fix: cmake --preset <preset> -DSW_ACTIVE_GAME=<name> (an existing build cache keeps the old value)")
endif()

message(STATUS "[Games] SW_ACTIVE_GAME=${SW_ACTIVE_GAME} -> target SWGame_${SW_ACTIVE_GAME} (Bin/Modules/SWGame)")
add_subdirectory(${SW_ACTIVE_GAME})

# ------------------------------------------------------------------------------
# 2) 함께 짓는 게임 팩(`SW_GAMES`, Dev 전용) — 타깃 SWGame_<게임>, 산출물 Bin/Games/<게임>/SWGame(`sw_add_game_module`).
# 고르지 않은 게임 팩은 짓지 않는다 — 소스 검사 게이트에 적는다.
# ------------------------------------------------------------------------------
get_property(list_game_pack GLOBAL PROPERTY SW_GAME_PACKS)
if(list_game_pack)
	message(STATUS "[Games] SW_GAMES -> ${list_game_pack} (Bin/Games/<game>/SWGame)")
endif()
# 이번 구성이 짓지 않는 게임의 옛 산출물을 지운다 — 기동이 Bin/Games 를 훑어 옛 매니페스트를 읽으면 형식이 바뀐 뒤 멈춘다.
file(GLOB list_built_game RELATIVE "${CMAKE_BINARY_DIR}/Bin/${SW_DIR_BIN_GAMES}" "${CMAKE_BINARY_DIR}/Bin/${SW_DIR_BIN_GAMES}/*")
foreach(built_game IN LISTS list_built_game)
	if(NOT built_game IN_LIST list_game_pack)
		file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/Bin/${SW_DIR_BIN_GAMES}/${built_game}")
	endif()
endforeach()
file(GLOB list_game_folder RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(game_name IN LISTS list_game_folder)
	if(game_name STREQUAL SW_ACTIVE_GAME OR NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${game_name}")
		continue()
	endif()
	if(game_name IN_LIST list_game_pack)
		add_subdirectory(${game_name})
	else()
		sw_declare_excluded_directory("${CMAKE_CURRENT_SOURCE_DIR}/${game_name}")
	endif()
endforeach()

# 등록은 여기서 하지 않는다 — 타겟을 **만드는** `sw_add_game_module` 이 한다
# (`cmake/Engine/ModuleRegistry.cmake` 의 "동적 모듈 레지스트리"). 호출부가 등록하면
# 새 모듈이 조용히 빠진다.
