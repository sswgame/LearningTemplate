# ==============================================================================
# @file cmake/internal/Targets.cmake
# @brief 엔진 내부: 소스 수집 + sw_add_library / sw_add_executable
# ==============================================================================

# 디렉터리 하위의 .c/.cpp/.h/.hpp/.inl 을 CONFIGURE_DEPENDS 로 수집합니다.
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

# 현재 소스 디렉터리 소스를 모으고, ARG_SOURCES 추가 / ARG_EXCLUDE 정규식 제외를 적용합니다.
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

# include/link/컴파일 옵션 등 공통 타겟 프로퍼티를 설정합니다.
#
# Include 정책:
# - PUBLIC:  타겟 자신의 소스 디렉터리 (로컬 헤더)
# - PRIVATE: ${CMAKE_SOURCE_DIR}/Source , Resource (이 타겟 컴파일용)
# - Source/ 를 PUBLIC 으로 재export 하지 않음 → Core / RuntimeAPI 가 제공
#   (Core 헤더의 "Core/..." 경로는 Core의 PUBLIC include 로 전파)
macro(sw_setup_target_properties TARGET_NAME ARG_INCLUDE_DIRECTORIES ARG_LINK_LIBRARIES)
	target_include_directories(${TARGET_NAME}
		PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
		PRIVATE "${CMAKE_SOURCE_DIR}/Source"
		PRIVATE "${CMAKE_SOURCE_DIR}/Resource"
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

# 라이브러리 타겟을 추가합니다. TYPE/LINK_LIBRARIES/EXCLUDE/SOURCES/INCLUDE_DIRECTORIES 지원.
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

# 실행 파일 타겟을 추가합니다. LINK_LIBRARIES/EXCLUDE/SOURCES/INCLUDE_DIRECTORIES 지원.
function(sw_add_executable TARGET_NAME)
    cmake_parse_arguments(ARG "" "" "LINK_LIBRARIES;EXCLUDE;SOURCES;INCLUDE_DIRECTORIES" ${ARGN})

    sw_prepare_target_sources(TARGET_SOURCES ARG_SOURCES ARG_EXCLUDE)
    add_executable(${TARGET_NAME} ${TARGET_SOURCES})
    sw_setup_target_properties(${TARGET_NAME} ARG_INCLUDE_DIRECTORIES ARG_LINK_LIBRARIES)
endfunction()

# 타겟별 compile definition(SW_EXPORTS/SW_IMPORTS 등)이 다르므로 REUSE_FROM 대신
# 동일 pch.h를 타겟마다 따로 컴파일한다.
function(sw_configure_pch TARGET_NAME)
    if(NOT SW_ENABLE_PCH)
        return()
    endif()
    set(_sw_pch "${CMAKE_SOURCE_DIR}/Source/Core/pch.h")
    if(EXISTS "${_sw_pch}")
        target_precompile_headers(${TARGET_NAME} PRIVATE "${_sw_pch}")
    endif()
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

# ---------------------------------------------------------------------------
# 런타임 파일 복사: POST_BUILD COMMENT가 ; 로 이어지지 않도록 한 번에 emit
# ---------------------------------------------------------------------------
# SRC_FILE을 TARGET의 SW_RUNTIME_COPY_FILES 프로퍼티에 큐잉합니다 (아직 POST_BUILD 미생성).
function(sw_queue_runtime_copy TARGET_NAME SRC_FILE)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "sw_queue_runtime_copy: target '${TARGET_NAME}' does not exist")
    endif()
    if(NOT SRC_FILE OR NOT EXISTS "${SRC_FILE}")
        return()
    endif()
    set_property(TARGET ${TARGET_NAME} APPEND PROPERTY SW_RUNTIME_COPY_FILES "${SRC_FILE}")
endfunction()

# 큐잉된 런타임 복사를 하나의 POST_BUILD로 emit하고 요약 COMMENT를 붙입니다.
function(sw_emit_runtime_copies TARGET_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "sw_emit_runtime_copies: target '${TARGET_NAME}' does not exist")
    endif()

    get_property(_already TARGET ${TARGET_NAME} PROPERTY SW_RUNTIME_COPIES_EMITTED)
    if(_already)
        return()
    endif()

    get_property(_files TARGET ${TARGET_NAME} PROPERTY SW_RUNTIME_COPY_FILES)
    if(NOT _files)
        return()
    endif()

    # 중복 제거 (같은 DLL을 여러 헬퍼가 큐잉할 수 있음)
    list(REMOVE_DUPLICATES _files)

    set(_names "")
    set(_commands "")
    foreach(_src IN LISTS _files)
        get_filename_component(_name "${_src}" NAME)
        list(APPEND _names "${_name}")
        list(APPEND _commands
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_src}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/${_name}"
        )
    endforeach()

    list(JOIN _names ", " _summary)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        ${_commands}
        COMMENT "[${TARGET_NAME}] Runtime deps: ${_summary}"
        VERBATIM
    )
    set_property(TARGET ${TARGET_NAME} PROPERTY SW_RUNTIME_COPIES_EMITTED TRUE)
endfunction()
