# ==============================================================================
# @file Source/GameFramework/Kits/CMakeLists.txt
# @brief GameFramework 장르 키트 (Overworld / TurnBattle / ActionCombat / Farming / Shooter / ThemePark)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 장르 키트 — 서로 독립, 모두 GameFramework를 링크
# ------------------------------------------------------------------------------
add_subdirectory(Overworld)
add_subdirectory(TurnBattle)
add_subdirectory(ActionCombat)
add_subdirectory(Farming)
add_subdirectory(Shooter)
add_subdirectory(ThemePark)
