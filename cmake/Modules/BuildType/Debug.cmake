# ==============================================================================
# @file cmake/Modules/BuildType/Debug.cmake
# @brief Debug CONFIG 매크로 (플래그 INTERFACE)
# @note 단일 설정(Ninja): CMAKE_BUILD_TYPE이 CONFIG와 일치
# 다중 설정(VS): genex가 활성 구성을 선택
# ==============================================================================

add_library(sw_build_debug INTERFACE)
target_compile_definitions(sw_build_debug INTERFACE $<$<CONFIG:Debug>:SW_DEBUG>)
list(APPEND sw_flag_libraries sw_build_debug)
