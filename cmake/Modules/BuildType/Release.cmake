# ==============================================================================
# @file cmake/Modules/BuildType/Release.cmake
# @brief Release CONFIG 매크로 (플래그 INTERFACE)
# @note IPO는 Targets.cmake의 sw_enableTargetIpo로 핵심 타겟에만 적용
# ==============================================================================

add_library(sw_build_release INTERFACE)
target_compile_definitions(sw_build_release INTERFACE
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:SW_RELEASE>
)

list(APPEND sw_flag_libraries sw_build_release)
