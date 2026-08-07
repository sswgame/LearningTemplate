# vcpkg toolchain setup.

# ##############################################################
#
# Python 스크립트(Scripts/FindVcpkg.py)를 실행하여 vcpkg 루트 경로를 동적으로 탐색합니다.
#
# ##############################################################

if(NOT DEFINED sw_vcpkg_root OR sw_vcpkg_root STREQUAL "")
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Scripts/FindVcpkg.py"
            OUTPUT_VARIABLE sw_detected_vcpkg_root
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE sw_find_vcpkg_result
        )
        if(sw_find_vcpkg_result EQUAL 0 AND NOT "${sw_detected_vcpkg_root}" STREQUAL "")
            set(sw_vcpkg_root "${sw_detected_vcpkg_root}" CACHE PATH "vcpkg root directory" FORCE)
        endif()
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
# [vcpkg 빌드 속도 최적화]
# 이미 vcpkg 의존성 바이너리(build/vcpkg_installed/x64-windows)가 생성되어 있는 경우,
# CMake 설정(Configure) 단계마다 vcpkg install 및 네트워크 도구 다운로드가 재실행되는
# 빌드 지연 오버헤드를 완전 방지하기 위해 VCPKG_MANIFEST_MODE를 OFF로 조건부 설정합니다.
# 만약 신규 의존성 패키지를 다시 다운로드/빌드하려면 build/vcpkg_installed 폴더를 삭제하면 됩니다.
# ##############################################################
# Standard vcpkg manifest mode enabled for automatic dependency management

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
if(NOT sw_toolchain_vcpkg IN_LIST sw_module_libraries)
    list(APPEND sw_module_libraries sw_toolchain_vcpkg)
endif()

# ##############################################################
#
# Vcpkg 헬퍼 매크로 및 함수
#
# ##############################################################

# Vcpkg가 사용하는 타겟 Triplet과 설치 경로를 계산하여 OUT_INC_DIRS와 OUT_BIN_DIRS 리스트에 반환합니다.
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

# 헤더 전용 라이브러리를 Vcpkg로부터 연결할 때 사용하는 헬퍼 함수
function(sw_link_vcpkg_header_only_target TARGET_NAME)
    sw_get_vcpkg_paths(_inc_dirs _bin_dirs)
    foreach(_dir IN LISTS _inc_dirs)
        if(EXISTS "${_dir}")
            target_include_directories(${TARGET_NAME} SYSTEM INTERFACE "${_dir}")
        endif()
    endforeach()
    target_include_directories(${TARGET_NAME} SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
endfunction()

# Vcpkg에서 설치된 파일(DLL, SO, JSON 등)을 타겟의 출력 디렉터리로 복사하는 헬퍼 함수
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
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_found_file}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/${FILE_NAME}"
            COMMENT "[${TARGET_NAME}] Copying Vcpkg File: ${FILE_NAME}..."
            VERBATIM
        )
    endif()
endfunction()

# 크로스 플랫폼 공유 라이브러리(dll, so, dylib) 복사 헬퍼 함수
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
