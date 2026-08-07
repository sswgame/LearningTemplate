# ==============================================================================
# @file cmake/internal/Discover.cmake
# @brief 엔진 내부: 하위 CMakeLists 자동 탐색
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
            string(TOUPPER "${child}" _child_upper)
            string(REPLACE "-" "_" _child_upper "${_child_upper}")
            string(REPLACE "." "_" _child_upper "${_child_upper}")
            set(_opt_name "SW_BUILD_${_child_upper}")
            option(${_opt_name} "Build project ${child}" ON)
            # 예전 sw_build_<Name> 캐시 이관
            if(DEFINED sw_build_${child})
                set(${_opt_name} "${sw_build_${child}}" CACHE BOOL "" FORCE)
                unset(sw_build_${child} CACHE)
            endif()
            if(${_opt_name})
                add_subdirectory("${child_path}")
            endif()
        endif()
    endforeach()
endfunction()
