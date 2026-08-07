# ==============================================================================
# @file cmake/internal/Targets.cmake
# @brief 엔진 내부: 소스 수집 + sw_add_library / sw_add_executable
# ==============================================================================

function(sw_collect_sources DIR_PATH OUT_SOURCES)
    file(
        GLOB_RECURSE sources_list
        CONFIGURE_DEPENDS
        "${DIR_PATH}/*.cpp"
        "${DIR_PATH}/*.c"
    )
    file(
        GLOB_RECURSE headers_list
        CONFIGURE_DEPENDS
        "${DIR_PATH}/*.h"
        "${DIR_PATH}/*.hpp"
        "${DIR_PATH}/*.inl"
    )
    list(APPEND sources_list ${headers_list})
    set(${OUT_SOURCES} ${sources_list} PARENT_SCOPE)
endfunction()

macro(sw_prepare_target_sources OUT_SOURCES ARG_SOURCES ARG_EXCLUDE)
    sw_collect_sources("${CMAKE_CURRENT_SOURCE_DIR}" ${OUT_SOURCES})
    if(${ARG_SOURCES})
        list(APPEND ${OUT_SOURCES} ${${ARG_SOURCES}})
    endif()
    if(${ARG_EXCLUDE})
        foreach(ex_pattern IN LISTS ${ARG_EXCLUDE})
            list(FILTER ${OUT_SOURCES} EXCLUDE REGEX "${ex_pattern}")
        endforeach()
    endif()
endmacro()

macro(sw_setup_target_properties TARGET_NAME ARG_INCLUDE_DIRECTORIES ARG_LINK_LIBRARIES)
    target_include_directories(${TARGET_NAME}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
        PUBLIC "${CMAKE_SOURCE_DIR}/Source"
        PUBLIC "${CMAKE_SOURCE_DIR}/Resource"
    )

    if(${ARG_INCLUDE_DIRECTORIES})
        target_include_directories(${TARGET_NAME} PUBLIC ${${ARG_INCLUDE_DIRECTORIES}})
    endif()

    set(link_libs "")
    if(${ARG_LINK_LIBRARIES})
        list(APPEND link_libs ${${ARG_LINK_LIBRARIES}})
    endif()
    if(sw_flag_libraries)
        list(APPEND link_libs ${sw_flag_libraries})
    endif()

    if(link_libs)
        target_link_libraries(${TARGET_NAME} PRIVATE ${link_libs})
    endif()

    file(RELATIVE_PATH rel_path "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(folder_path "${rel_path}" DIRECTORY)
    if(folder_path)
        set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "${folder_path}")
    endif()

    sw_install_target(${TARGET_NAME})
endmacro()

function(sw_add_library TARGET_NAME)
    cmake_parse_arguments(ARG "" "TYPE" "LINK_LIBRARIES;EXCLUDE;SOURCES;INCLUDE_DIRECTORIES" ${ARGN})

    set(LIB_TYPE STATIC)
    if(ARG_TYPE)
        set(LIB_TYPE ${ARG_TYPE})
    elseif(${ARGC} GREATER 1 AND NOT "${ARGV1}" MATCHES "^(LINK_LIBRARIES|EXCLUDE|SOURCES|TYPE|INCLUDE_DIRECTORIES)$")
        set(LIB_TYPE ${ARGV1})
    endif()

    sw_prepare_target_sources(TARGET_SOURCES ARG_SOURCES ARG_EXCLUDE)
    add_library(${TARGET_NAME} ${LIB_TYPE} ${TARGET_SOURCES})
    sw_setup_target_properties(${TARGET_NAME} ARG_INCLUDE_DIRECTORIES ARG_LINK_LIBRARIES)
endfunction()

function(sw_add_executable TARGET_NAME)
    cmake_parse_arguments(ARG "" "" "LINK_LIBRARIES;EXCLUDE;SOURCES;INCLUDE_DIRECTORIES" ${ARGN})

    sw_prepare_target_sources(TARGET_SOURCES ARG_SOURCES ARG_EXCLUDE)
    add_executable(${TARGET_NAME} ${TARGET_SOURCES})
    sw_setup_target_properties(${TARGET_NAME} ARG_INCLUDE_DIRECTORIES ARG_LINK_LIBRARIES)
endfunction()

# LiveReload: MODULE DLL을 App.exe와 같은 Bin에 배치 (멀티컨픽 하위폴더 무시)
function(sw_set_module_bin_output TARGET_NAME)
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${sw_output_directory}/Bin"
        LIBRARY_OUTPUT_DIRECTORY "${sw_output_directory}/Bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${sw_output_directory}/Bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${sw_output_directory}/Bin"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${sw_output_directory}/Bin"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${sw_output_directory}/Bin"
    )
endfunction()
