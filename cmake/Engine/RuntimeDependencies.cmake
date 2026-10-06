# ==============================================================================
# @file cmake/Engine/RuntimeDependencies.cmake
# @brief project() 이후: vcpkg include/bin 경로 조회 + 런타임 DLL 복사 헬퍼
# @note BuildLayout.cmake 의 sw_queueRuntimeCopy 이후에 include 할 것
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) sw_getVcpkgPaths — vcpkg 설치 트리의 include · bin/lib 폴더 후보
# vcpkg 를 쓰지 않는 구성(SW_USE_VCPKG=OFF)이면 둘 다 빈 목록이다 — 그래서 아래 함수들은 늘 정의돼 있다.
# ------------------------------------------------------------------------------
function(sw_getVcpkgPaths OUT_INC_DIRS OUT_BIN_DIRS)
    set(listIncludeDir "")
    set(listBinDir "")
    if(SW_USE_VCPKG AND VCPKG_TARGET_TRIPLET)
        foreach(installedRoot "${VCPKG_INSTALLED_DIR}" "${sw_vcpkg_root}/installed")
            if(installedRoot AND NOT installedRoot STREQUAL "/installed")
                list(APPEND listIncludeDir "${installedRoot}/${VCPKG_TARGET_TRIPLET}/include")
                list(APPEND listBinDir "${installedRoot}/${VCPKG_TARGET_TRIPLET}/bin" "${installedRoot}/${VCPKG_TARGET_TRIPLET}/lib")
            endif()
        endforeach()
    endif()
    set(${OUT_INC_DIRS} ${listIncludeDir} PARENT_SCOPE)
    set(${OUT_BIN_DIRS} ${listBinDir} PARENT_SCOPE)
endfunction()

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
# sw_copyVcpkgSharedLib: 플랫폼별 .dll / .so
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
    set(libName "${CMAKE_SHARED_LIBRARY_PREFIX}${LIB_BASE_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}")

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

# ------------------------------------------------------------------------------
# 5) sw_copyTracyRuntime — TracyClient.dll(Engine 이 지연 로드한다)
# vcpkg 의 Debug DLL 은 debug/bin/Debug/ 에 있어 applocal 이 찾지 못한다. 임포트 타깃이 아는 구성별 위치를 그대로 옮긴다.
# Tracy 를 링크하지 않은 구성(Shipping · SW_ENABLE_TRACY=OFF · 리눅스 정적)은 아무것도 하지 않는다.
# ------------------------------------------------------------------------------
function(sw_copyTracyRuntime TARGET_NAME)
    if(SW_SHIPPING_BUILD OR NOT SW_ENABLE_TRACY OR NOT WIN32 OR NOT TARGET Tracy::TracyClient)
        return()
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        get_target_property(tracyDll Tracy::TracyClient IMPORTED_LOCATION_DEBUG)
    else()
        get_target_property(tracyDll Tracy::TracyClient IMPORTED_LOCATION_RELEASE)
    endif()

    if(tracyDll)
        sw_queueRuntimeCopy(${TARGET_NAME} "${tracyDll}")
    endif()
endfunction()
