# vcpkg toolchain setup (Scripts/vcpkg/FindVcpkg.py + manifest install gate).

include("${CMAKE_CURRENT_LIST_DIR}/../../../internal/Python.cmake")

# ##############################################################
# vcpkg 루트 탐색 (Scripts)
# ##############################################################

if(NOT DEFINED sw_vcpkg_root OR sw_vcpkg_root STREQUAL "")
    set(_sw_find_vcpkg_args "")
    if(SW_VCPKG_AUTO_BOOTSTRAP)
        list(APPEND _sw_find_vcpkg_args "--install")
    endif()

    sw_execute_python_script(
        "Scripts/vcpkg/FindVcpkg.py"
        ARGS ${_sw_find_vcpkg_args}
        OUTPUT_VARIABLE sw_detected_vcpkg_root
        RESULT_VARIABLE sw_find_vcpkg_result
        QUIET
    )
    if(sw_find_vcpkg_result EQUAL 0 AND NOT "${sw_detected_vcpkg_root}" STREQUAL "")
        set(sw_vcpkg_root "${sw_detected_vcpkg_root}" CACHE PATH "vcpkg root directory" FORCE)
    endif()

    if(NOT DEFINED sw_vcpkg_root OR sw_vcpkg_root STREQUAL "")
        if(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
            set(sw_vcpkg_root "$ENV{VCPKG_ROOT}")
        elseif(DEFINED ENV{VCPKG_INSTALLATION_ROOT} AND NOT "$ENV{VCPKG_INSTALLATION_ROOT}" STREQUAL "")
            set(sw_vcpkg_root "$ENV{VCPKG_INSTALLATION_ROOT}")
        endif()
    endif()
endif()

# VCPKG_TARGET_TRIPLET을 vcpkg.cmake include 전에 설정해야 합니다.
if(NOT DEFINED VCPKG_TARGET_TRIPLET OR VCPKG_TARGET_TRIPLET STREQUAL "")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$" OR CMAKE_GENERATOR_PLATFORM MATCHES "[Aa][Rr][Mm]64")
        set(sw_vcpkg_arch "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|x64)$" OR CMAKE_GENERATOR_PLATFORM MATCHES "[Xx]64")
        set(sw_vcpkg_arch "x64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86|i[3-6]86)$")
        set(sw_vcpkg_arch "x86")
    else()
        set(sw_vcpkg_arch "x64")
    endif()

    if(WIN32)
        set(sw_vcpkg_triplet "${sw_vcpkg_arch}-windows")
    elseif(APPLE)
        set(sw_vcpkg_triplet "${sw_vcpkg_arch}-osx")
    else()
        set(sw_vcpkg_triplet "${sw_vcpkg_arch}-linux")
    endif()

    set(VCPKG_TARGET_TRIPLET "${sw_vcpkg_triplet}" CACHE STRING "" FORCE)
    set(VCPKG_HOST_TRIPLET "${sw_vcpkg_triplet}" CACHE STRING "" FORCE)
endif()

if(NOT DEFINED VCPKG_INSTALLED_DIR OR VCPKG_INSTALLED_DIR STREQUAL "")
    set(VCPKG_INSTALLED_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed" CACHE PATH "vcpkg installed directory" FORCE)
endif()

if(EXISTS "${VCPKG_INSTALLED_DIR}")
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${VCPKG_INSTALLED_DIR}")
endif()

# ##############################################################
# Manifest install gate (project() 전 CACHE 설정 → toolchain이 존중)
# installed 트리 + manifest 해시가 같으면 OFF (configure마다 install 방지)
# ##############################################################

include("${CMAKE_CURRENT_LIST_DIR}/../../../internal/VcpkgManifestHash.cmake")
sw_vcpkg_compute_manifest_hash(_sw_manifest_hash)

set(_sw_vcpkg_tree "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
set(_sw_stamp "${VCPKG_INSTALLED_DIR}/.sw_vcpkg_manifest_sha")
set(_sw_tree_ready FALSE)
if(EXISTS "${_sw_vcpkg_tree}")
    set(_sw_tree_ready TRUE)
endif()

set(_sw_stamp_match FALSE)
if(_sw_tree_ready AND EXISTS "${_sw_stamp}" AND NOT _sw_manifest_hash STREQUAL "")
    file(READ "${_sw_stamp}" _sw_stamp_content)
    string(STRIP "${_sw_stamp_content}" _sw_stamp_content)
    if(_sw_stamp_content STREQUAL _sw_manifest_hash)
        set(_sw_stamp_match TRUE)
    endif()
endif()

if(SW_VCPKG_FORCE_INSTALL)
    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "vcpkg manifest install" FORCE)
    message(STATUS "[vcpkg] SW_VCPKG_FORCE_INSTALL=ON — manifest install enabled")
elseif(_sw_tree_ready AND _sw_stamp_match)
    set(VCPKG_MANIFEST_MODE OFF CACHE BOOL "vcpkg manifest install" FORCE)
    message(STATUS "[vcpkg] Installed tree matches manifest stamp — skipping install (VCPKG_MANIFEST_MODE=OFF)")
elseif(_sw_tree_ready AND _sw_manifest_hash STREQUAL "")
    # No manifest files — keep using installed tree without install churn
    set(VCPKG_MANIFEST_MODE OFF CACHE BOOL "vcpkg manifest install" FORCE)
    message(STATUS "[vcpkg] Installed tree present (no manifest hash) — VCPKG_MANIFEST_MODE=OFF")
else()
    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "vcpkg manifest install" FORCE)
    if(_sw_tree_ready)
        message(STATUS "[vcpkg] Manifest changed or stamp missing — install enabled")
    else()
        message(STATUS "[vcpkg] Installed tree missing — install enabled")
    endif()
endif()

if(DEFINED sw_vcpkg_root AND NOT sw_vcpkg_root STREQUAL "")
    set(VCPKG_ROOT "${sw_vcpkg_root}" CACHE PATH "vcpkg root directory" FORCE)
endif()

if(NOT TARGET sw_toolchain_vcpkg)
    add_library(sw_toolchain_vcpkg INTERFACE)
endif()

set(_vcpkg_inc_candidates
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)
if(DEFINED sw_vcpkg_root)
    list(APPEND _vcpkg_inc_candidates "${sw_vcpkg_root}/installed/${VCPKG_TARGET_TRIPLET}/include")
endif()

foreach(_inc IN LISTS _vcpkg_inc_candidates)
    if(EXISTS "${_inc}")
        target_include_directories(sw_toolchain_vcpkg INTERFACE "${_inc}")
    endif()
endforeach()

target_compile_definitions(
    sw_toolchain_vcpkg
    INTERFACE
    SW_VCPKG
)
# Consumed by sw_add_* via sw_flag_libraries (same list as Compiler/Platform modules).
if(NOT sw_toolchain_vcpkg IN_LIST sw_flag_libraries)
    list(APPEND sw_flag_libraries sw_toolchain_vcpkg)
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
