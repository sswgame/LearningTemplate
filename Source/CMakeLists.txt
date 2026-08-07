# ==============================================================================
# @file Source/CMakeLists.txt
# @brief Source 하위 프로젝트 (Game은 ReflectionParser 이후 루트에서 등록)
# ==============================================================================

sw_discover_projects("${CMAKE_CURRENT_SOURCE_DIR}" EXCLUDE Game)
