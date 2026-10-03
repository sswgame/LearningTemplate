# ==============================================================================
# @file Source/GameFramework/Kits/CMakeLists.txt
# @brief GameFramework 장르 키트 (장르별 — 목록은 README)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 장르 키트 — 서로 독립, 모두 GameFramework를 링크
# ------------------------------------------------------------------------------
add_subdirectory(Overworld)
add_subdirectory(TurnBattle)
add_subdirectory(ActionCombat)
add_subdirectory(Farming)
add_subdirectory(ThemePark)
add_subdirectory(Voxel)
add_subdirectory(CityBuilder)
add_subdirectory(RealTimeStrategy)
add_subdirectory(NetClientServer)
add_subdirectory(NetLockstep)
add_subdirectory(NetTurnRelay)
add_subdirectory(NetMmo)
