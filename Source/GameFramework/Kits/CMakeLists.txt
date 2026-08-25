# ==============================================================================
# @file Source/GameFramework/Kits/CMakeLists.txt
# @brief GameFramework 장르 키트 (Overworld / TurnBattle / ActionCombat)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 장르 키트 — 서로 독립, 모두 GameFramework를 링크
# ------------------------------------------------------------------------------
add_subdirectory(Overworld)
add_subdirectory(TurnBattle)
add_subdirectory(ActionCombat)
