# ==============================================================================
# @file Source/Games/Demo/CMakeLists.txt
# @brief Demo 게임 MODULE (오버월드 + 턴배틀 + 액션컴뱃 룸)
# ==============================================================================

# 리플렉션 대상 헤더 스캔
file(GLOB_RECURSE DEMO_GAME_HEADERS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# 게임 팩 타겟(SWGame) 구성
sw_addGameModule(SWGame
	KITS
		GF_Overworld
		GF_TurnBattle
		GF_ActionCombat
	HEADERS
		${DEMO_GAME_HEADERS}
)
