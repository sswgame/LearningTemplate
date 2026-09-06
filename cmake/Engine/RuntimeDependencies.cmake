# ==============================================================================
# @file cmake/Engine/RuntimeDependencies.cmake
# @brief project() 이후: vcpkg include/bin 경로 조회 + 런타임 DLL 복사 헬퍼
# @note ModuleBuildRules.cmake 의 sw_queueRuntimeCopy 이후에 include 할 것
# ==============================================================================

if(NOT SW_USE_VCPKG)
    return()
endif()

# ------------------------------------------------------------------------------
# 1) sw_getVcpkgPaths — installed include / bin / lib 경로
# OUT_INC_DIRS, OUT_BIN_DIRS. triplet 미정이면 OS 기본값
# ------------------------------------------------------------------------------
macro(sw_getVcpkgPaths OUT_INC_DIRS OUT_BIN_DIRS)
    set(vcpkgPath "${sw_vcpkg_root}")

    if(NOT vcpkgPath)
        set(vcpkgPath "${VCPKG_ROOT}")
    endif()

    if(NOT DEFINED VCPKG_TARGET_TRIPLET OR VCPKG_TARGET_TRIPLET STREQUAL "")
        if(WIN32)
            set(triplet "x64-windows")
        elseif(APPLE)
            set(triplet "x64-osx")
        else()
            set(triplet "x64-linux")
        endif()
    else()
        set(triplet "${VCPKG_TARGET_TRIPLET}")
    endif()

    set(${OUT_INC_DIRS} "")
    set(${OUT_BIN_DIRS} "")

    if(DEFINED VCPKG_INSTALLED_DIR)
        list(APPEND ${OUT_INC_DIRS} "${VCPKG_INSTALLED_DIR}/${triplet}/include")
        list(APPEND ${OUT_BIN_DIRS} "${VCPKG_INSTALLED_DIR}/${triplet}/bin")
        list(APPEND ${OUT_BIN_DIRS} "${VCPKG_INSTALLED_DIR}/${triplet}/lib")
    endif()

    if(vcpkgPath)
        list(APPEND ${OUT_INC_DIRS} "${vcpkgPath}/installed/${triplet}/include")
        list(APPEND ${OUT_BIN_DIRS} "${vcpkgPath}/installed/${triplet}/bin")
        list(APPEND ${OUT_BIN_DIRS} "${vcpkgPath}/installed/${triplet}/lib")
    endif()
endmacro()

# ------------------------------------------------------------------------------
# 2) sw_linkVcpkgHeaderOnlyTarget — vcpkg include를 SYSTEM INTERFACE로
# ------------------------------------------------------------------------------
function(sw_linkVcpkgHeaderOnlyTarget TARGET_NAME)
    sw_getVcpkgPaths(incDirs binDirs)

    foreach(dir IN LISTS incDirs)
        if(EXISTS "${dir}")
            target_include_directories(${TARGET_NAME} SYSTEM INTERFACE "${dir}")
        endif()
    endforeach()

    target_include_directories(${TARGET_NAME} SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
endfunction()

# ------------------------------------------------------------------------------
# 3) 런타임 복사 큐 — 실제 POST_BUILD는 sw_emitRuntimeCopies
# sw_copyVcpkgFile: bin 디렉터리의 지정 파일
# sw_copyVcpkgSharedLib: 플랫폼별 .dll / .dylib / .so
# ------------------------------------------------------------------------------
# vcpkg bin의 지정 파일을 런타임 복사 큐에 넣습니다.
function(sw_copyVcpkgFile TARGET_NAME FILE_NAME)
    sw_getVcpkgPaths(incDirs binDirs)
    set(foundFile "")

    foreach(dir IN LISTS binDirs)
        if(EXISTS "${dir}/${FILE_NAME}")
            set(foundFile "${dir}/${FILE_NAME}")
            break()
        endif()
    endforeach()

    if(foundFile)
        sw_queueRuntimeCopy(${TARGET_NAME} "${foundFile}")
    endif()
endfunction()

# 플랫폼 접두사/접미사를 붙여 sw_copyVcpkgFile에 위임합니다.
function(sw_copyVcpkgSharedLib TARGET_NAME LIB_BASE_NAME)
    if(WIN32)
        set(libName "${LIB_BASE_NAME}.dll")
    elseif(APPLE)
        set(libName "lib${LIB_BASE_NAME}.dylib")
    else()
        set(libName "lib${LIB_BASE_NAME}.so")
    endif()

    sw_copyVcpkgFile(${TARGET_NAME} "${libName}")
endfunction()

# ------------------------------------------------------------------------------
# 4) sw_copyVulkanValidationRuntime — Khronos validation + vcpkg mimalloc 의존성
# VkLayer_khronos_validation.dll 은 mimalloc.dll 에 링크되어 있음.
# layer만 복사하면 LoadLibrary(126) → vkCreateInstance LAYER_NOT_PRESENT(-6).
# ------------------------------------------------------------------------------
function(sw_copyVulkanValidationRuntime TARGET_NAME)
    sw_copyVcpkgSharedLib(${TARGET_NAME} "VkLayer_khronos_validation")
    sw_copyVcpkgFile(${TARGET_NAME} "VkLayer_khronos_validation.json")

    # vcpkg vulkan-validationlayers 가 mimalloc 을 쓰면 같이 배포 (없으면 no-op)
    sw_copyVcpkgSharedLib(${TARGET_NAME} "mimalloc")
    sw_copyVcpkgSharedLib(${TARGET_NAME} "mimalloc-redirect")
endfunction()
