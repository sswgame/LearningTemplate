# ==============================================================================
# @file Source/CMakeLists.txt
# @brief 초기 Source 타겟(Core만). Engine/RuntimeAPI/Editor/App은 Tools 이후.
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) Tools 이전 — Core만
#    Engine은 ReflectionParser가 필요하므로 루트가 Tools 이후 명시 호출:
#    Source → Tools → Engine → RuntimeAPI → Editor → App → (GameFramework / Games)
# ------------------------------------------------------------------------------
add_subdirectory(Core)
