# ==============================================================================
# @file cmake/internal/VcpkgRuntime.cmake
# @brief project() 이후: vcpkg include/bin 경로 조회 + 런타임 DLL 복사 헬퍼
# @note Targets.cmake 의 sw_queue_runtime_copy 이후에 include 할 것
# ==============================================================================

if(NOT SW_USE_VCPKG)
    return()
endif()

macro(sw_get_vcpkg_paths OUT_INC_DIRS OUT_BIN_DIRS)
    set(_vcpkg_path "${sw_vcpkg_root}")
    if(NOT _vcpkg_path)
        set(_vcpkg_path "${VCPKG_ROOT}")
    endif()

    if(NOT DEFINED VCPKG_TARGET_TRIPLET OR VCPKG_TARGET_TRIPLET STREQUAL "")
        if(WIN32)
            set(_triplet "x64-windows")
        elseif(APPLE)
            set(_triplet "x64-osx")
        else()
            set(_triplet "x64-linux")
        endif()
    else()
        set(_triplet "${VCPKG_TARGET_TRIPLET}")
    endif()

    set(${OUT_INC_DIRS} "")
    set(${OUT_BIN_DIRS} "")

    if(DEFINED VCPKG_INSTALLED_DIR)
        list(APPEND ${OUT_INC_DIRS} "${VCPKG_INSTALLED_DIR}/${_triplet}/include")
        list(APPEND ${OUT_BIN_DIRS} "${VCPKG_INSTALLED_DIR}/${_triplet}/bin")
        list(APPEND ${OUT_BIN_DIRS} "${VCPKG_INSTALLED_DIR}/${_triplet}/lib")
    endif()

    if(_vcpkg_path)
        list(APPEND ${OUT_INC_DIRS} "${_vcpkg_path}/installed/${_triplet}/include")
        list(APPEND ${OUT_BIN_DIRS} "${_vcpkg_path}/installed/${_triplet}/bin")
        list(APPEND ${OUT_BIN_DIRS} "${_vcpkg_path}/installed/${_triplet}/lib")
    endif()
endmacro()

function(sw_link_vcpkg_header_only_target TARGET_NAME)
    sw_get_vcpkg_paths(_inc_dirs _bin_dirs)
    foreach(_dir IN LISTS _inc_dirs)
        if(EXISTS "${_dir}")
            target_include_directories(${TARGET_NAME} SYSTEM INTERFACE "${_dir}")
        endif()
    endforeach()
    target_include_directories(${TARGET_NAME} SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
endfunction()

function(sw_copy_vcpkg_file TARGET_NAME FILE_NAME)
    sw_get_vcpkg_paths(_inc_dirs _bin_dirs)
    set(_found_file "")

    foreach(_dir IN LISTS _bin_dirs)
        if(EXISTS "${_dir}/${FILE_NAME}")
            set(_found_file "${_dir}/${FILE_NAME}")
            break()
        endif()
    endforeach()

    if(_found_file)
        sw_queue_runtime_copy(${TARGET_NAME} "${_found_file}")
    endif()
endfunction()

function(sw_copy_vcpkg_shared_lib TARGET_NAME LIB_BASE_NAME)
    if(WIN32)
        set(_lib_name "${LIB_BASE_NAME}.dll")
    elseif(APPLE)
        set(_lib_name "lib${LIB_BASE_NAME}.dylib")
    else()
        set(_lib_name "lib${LIB_BASE_NAME}.so")
    endif()

    sw_copy_vcpkg_file(${TARGET_NAME} "${_lib_name}")
endfunction()
