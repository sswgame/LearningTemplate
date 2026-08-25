# ==============================================================================
# @file cmake/Modules/BuildType/Release.cmake
# @brief Release CONFIG 매크로 및 Shipping LTO(ThinLTO) 연동
# ==============================================================================

add_library(sw_build_release INTERFACE)
target_compile_definitions(sw_build_release INTERFACE
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:SW_RELEASE>
)

list(APPEND sw_flag_libraries sw_build_release)

# Shipping 배포 빌드 시 ThinLTO (링크 타임 최적화) 전역 활성화
if(SW_SHIPPING_BUILD AND SW_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipoSupported OUTPUT ipoError)
    if(ipoSupported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "[Release] Shipping ThinLTO (INTERPROCEDURAL_OPTIMIZATION) ENABLED")
    else()
        message(WARNING "[Release] ThinLTO is not supported by compiler: ${ipoError}")
    endif()
endif()
