# ==============================================================================
# @file cmake/Dependencies.cmake
# @brief ThirdParty 서브트리 등록
# ==============================================================================

add_library(sw_third_party_includes INTERFACE)
set(sw_libraries sw_third_party_includes)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty")
    add_subdirectory(ThirdParty)
endif()
