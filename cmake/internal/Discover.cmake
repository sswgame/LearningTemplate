# ==============================================================================
# @file cmake/internal/Discover.cmake
# @brief 엔진 내부: 직속 하위 CMakeLists 자동 탐색 (ThirdParty / Test 용)
# @note 제품 그래프(Source/Tools)는 명시 add_subdirectory — 여기서 SW_BUILD_* 를 만들지 않음
# ==============================================================================

# sw_discover_projects(<dir> [EXCLUDE name1 name2 ...])
function(sw_discover_projects BASE_DIR)
    cmake_parse_arguments(ARG "" "" "EXCLUDE" ${ARGN})

    if(NOT EXISTS "${BASE_DIR}")
        return()
    endif()

    file(GLOB children RELATIVE "${BASE_DIR}" "${BASE_DIR}/*")
    foreach(child IN LISTS children)
        if(ARG_EXCLUDE AND child IN_LIST ARG_EXCLUDE)
            continue()
        endif()

        set(child_path "${BASE_DIR}/${child}")
        if(IS_DIRECTORY "${child_path}" AND EXISTS "${child_path}/CMakeLists.txt")
            add_subdirectory("${child_path}")
        endif()
    endforeach()
endfunction()
