# ==============================================================================
# @file cmake/internal/Discover.cmake
# @brief 직속 하위 CMakeLists 자동 탐색 (ThirdParty 용)
# @note Source / Tools / Test 는 명시 add_subdirectory
# ==============================================================================

# sw_discover_projects(<dir>)
function(sw_discover_projects BASE_DIR)
    if(NOT EXISTS "${BASE_DIR}")
        return()
    endif()

    file(GLOB children RELATIVE "${BASE_DIR}" "${BASE_DIR}/*")
    foreach(child IN LISTS children)
        set(child_path "${BASE_DIR}/${child}")
        if(IS_DIRECTORY "${child_path}" AND EXISTS "${child_path}/CMakeLists.txt")
            add_subdirectory("${child_path}")
        endif()
    endforeach()
endfunction()
