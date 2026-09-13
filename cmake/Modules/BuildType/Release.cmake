# ==============================================================================
# @file cmake/Modules/BuildType/Release.cmake
# @brief Release CONFIG 매크로 및 Shipping LTO(ThinLTO) 연동
# ==============================================================================

add_library(sw_build_release INTERFACE)
target_compile_definitions(sw_build_release INTERFACE
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:SW_RELEASE>
)

list(APPEND sw_flag_libraries sw_build_release)

# LTO(IPO)는 **여기서 켜지 않는다.** Shipping 도 CMAKE_BUILD_TYPE 이 Release 라
# `cmake/Engine/BuildLayout.cmake` 의 IPO 블록이 이미 같은 판정을 하고 켠다 — 예전엔 두 곳이 각자
# `check_ipo_supported` 를 부르고 각자 메시지를 찍어서, 같은 사실을 두 번 말하고 한쪽만 고치면
# 어긋나는 자리였다. 판정과 활성화는 BuildLayout 한 곳이 소유한다.
