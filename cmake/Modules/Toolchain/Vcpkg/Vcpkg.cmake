# ==============================================================================
# @file cmake/Modules/Toolchain/Vcpkg/Vcpkg.cmake
# @brief vcpkg 통합 모듈: root 탐색 · triplet · manifest hash/stamp · install gate · INTERFACE
#
# VcpkgGate.cmake(project() 전)에서 include. 역할:
# 1. vcpkg 루트 탐색 (SetupVcpkg.py / 환경 변수)
# 2. VCPKG_TARGET_TRIPLET 자동 결정
# 3. install 게이트 — 스탬프(매니페스트 해시)가 같으면 바로 skip, 다르면 `vcpkg install --dry-run` 계획으로:
#    지을 것이 없으면 skip, 이미 깔린 포트를 지우게 되면 멈추고 경고(공유 설치 폴더), 빠진 포트가 있을 때만 install
# 4. sw_toolchain_vcpkg INTERFACE (include 경로 + SW_VCPKG)
#
# project() 이후 루트가 sw_vcpkgWriteManifestStamp() 를 호출해
# 다음 configure에서 불필요한 재설치를 건너뛴다.
#
# @note 런타임 헬퍼(sw_copy_vcpkg_*)는 cmake/internal/VcpkgRuntime.cmake
# VcpkgRuntime은 BuildLayout.cmake의 sw_queueRuntimeCopy에 의존하므로 internal에 유지
# ==============================================================================
include("${CMAKE_CURRENT_LIST_DIR}/../../../Environment/PythonUtils.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../Config/LoadConfigConstants.cmake")

# ------------------------------------------------------------------------------
# 1) sw_vcpkgComputeManifestHash — manifest + overlay port/triplet SHA256
# 포함: vcpkg.json, vcpkg-configuration.json, ThirdParty/*/vcpkg-port,
# cmake/Modules/Toolchain/Vcpkg 오버레이 triplet (.cmake). 없으면 빈 문자열
# ------------------------------------------------------------------------------
function(sw_vcpkgComputeManifestHash OUT_VAR)
    set(swSrcDir "${CMAKE_CURRENT_SOURCE_DIR}")

    if(DEFINED CMAKE_SOURCE_DIR AND NOT CMAKE_SOURCE_DIR STREQUAL "")
        set(swSrcDir "${CMAKE_SOURCE_DIR}")
    endif()

    # 앞부분(매니페스트 두 파일)은 레거시 스탬프와 같다 — 두 벌로 적지 않고 그것에 오버레이를 덧붙인다.
    sw_vcpkgComputeManifestHashLegacy(hash)

    file(GLOB_RECURSE swOverlayPortFiles
        "${swSrcDir}/ThirdParty/*/vcpkg-port/portfile.cmake"
        "${swSrcDir}/ThirdParty/*/vcpkg-port/vcpkg.json"
        "${swSrcDir}/ThirdParty/*/vcpkg-port/*.patch"
        "${swSrcDir}/ThirdParty/*/vcpkg-port/*/portfile.cmake"
        "${swSrcDir}/ThirdParty/*/vcpkg-port/*/vcpkg.json"
        "${swSrcDir}/ThirdParty/*/vcpkg-port/*/*.patch"
    )
    list(SORT swOverlayPortFiles)

    foreach(overlayFile IN LISTS swOverlayPortFiles)
        if(EXISTS "${overlayFile}")
            file(SHA256 "${overlayFile}" hOverlay)
            string(APPEND hash "${hOverlay}")
        endif()
    endforeach()

    file(GLOB swOverlayTripletFiles
        "${swSrcDir}/cmake/Modules/Toolchain/Vcpkg/*.cmake"
    )
    list(SORT swOverlayTripletFiles)

    foreach(tripletFile IN LISTS swOverlayTripletFiles)
        get_filename_component(tripletName "${tripletFile}" NAME)

        # 실제 패키지 빌드에 영향을 주는 타겟 트립릿 파일(*-windows.cmake, *-linux.cmake, *-osx.cmake)만 스탬프 해시에 포함
        if(NOT tripletName MATCHES "-(windows|linux|osx)\\.cmake$")
            continue()
        endif()

        if(EXISTS "${tripletFile}")
            file(SHA256 "${tripletFile}" hTriplet)
            string(APPEND hash "${hTriplet}")
        endif()
    endforeach()

    set(${OUT_VAR} "${hash}" PARENT_SCOPE)
endfunction()

# 레거시 스탬프(vcpkg.json + vcpkg-configuration.json만) 해시. 공식 마이그레이션용.
function(sw_vcpkgComputeManifestHashLegacy OUT_VAR)
    set(swSrcDir "${CMAKE_CURRENT_SOURCE_DIR}")

    if(DEFINED CMAKE_SOURCE_DIR AND NOT CMAKE_SOURCE_DIR STREQUAL "")
        set(swSrcDir "${CMAKE_SOURCE_DIR}")
    endif()

    set(hash "")

    if(EXISTS "${swSrcDir}/vcpkg.json")
        file(SHA256 "${swSrcDir}/vcpkg.json" h1)
        string(APPEND hash "${h1}")
    endif()

    if(EXISTS "${swSrcDir}/vcpkg-configuration.json")
        file(SHA256 "${swSrcDir}/vcpkg-configuration.json" h2)
        string(APPEND hash "${h2}")
    endif()

    set(${OUT_VAR} "${hash}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# 1b) sw_vcpkgPlanManifestInstall — 이 매니페스트를 공유 설치 폴더에 깔면 무엇이 바뀌는지(`vcpkg install --dry-run`)
# 설치 폴더 · 스탬프는 워크트리 모두가 나눠 쓴다. 해시만 다르다고 설치하면 옛 매니페스트의 워크트리가 다른 워크트리가 쓰는
# 포트를 지운다 — 그래서 설치 여부는 계획(지울 것 · 지을 것)으로 정한다.
# OUT_STATUS: OK(계획을 얻음) · FAILED(vcpkg 를 돌리지 못함). OUT_REMOVE · OUT_BUILD: 지울 · 지을(다시 지을 포함) 포트 목록
# ------------------------------------------------------------------------------
function(sw_vcpkgPlanManifestInstall swSrcDir OUT_STATUS OUT_REMOVE OUT_BUILD)
    set(${OUT_STATUS} "FAILED" PARENT_SCOPE)
    set(${OUT_REMOVE} "" PARENT_SCOPE)
    set(${OUT_BUILD} "" PARENT_SCOPE)

    if(NOT DEFINED sw_vcpkg_root OR sw_vcpkg_root STREQUAL "")
        return()
    endif()

    if(CMAKE_HOST_WIN32)
        set(swVcpkgExe "${sw_vcpkg_root}/vcpkg.exe")
    else()
        set(swVcpkgExe "${sw_vcpkg_root}/vcpkg")
    endif()

    if(NOT EXISTS "${swVcpkgExe}")
        return()
    endif()

    set(swArgs
        install --dry-run
        "--triplet=${VCPKG_TARGET_TRIPLET}"
        "--host-triplet=${VCPKG_HOST_TRIPLET}"
        "--x-install-root=${VCPKG_INSTALLED_DIR}"
        "--x-manifest-root=${swSrcDir}"
    )

    foreach(overlayPort IN LISTS VCPKG_OVERLAY_PORTS)
        list(APPEND swArgs "--overlay-ports=${overlayPort}")
    endforeach()

    set(swOverlayTriplets "${VCPKG_OVERLAY_TRIPLETS}")

    if(swOverlayTriplets STREQUAL "" AND DEFINED ENV{VCPKG_OVERLAY_TRIPLETS})
        set(swOverlayTriplets "$ENV{VCPKG_OVERLAY_TRIPLETS}")
    endif()

    if(swOverlayTriplets STREQUAL "")
        set(swOverlayTriplets "${swSrcDir}/cmake/Modules/Toolchain/Vcpkg")
    endif()

    foreach(overlayTriplet IN LISTS swOverlayTriplets)
        list(APPEND swArgs "--overlay-triplets=${overlayTriplet}")
    endforeach()

    execute_process(
        COMMAND "${swVcpkgExe}" ${swArgs}
        WORKING_DIRECTORY "${swSrcDir}"
        RESULT_VARIABLE swResult
        OUTPUT_VARIABLE swOutput
        ERROR_VARIABLE swError
    )

    if(NOT swResult EQUAL 0)
        message(STATUS "[vcpkg] dry-run failed (${swResult}): ${swError}")
        return()
    endif()

    # 절마다 머리 줄("The following packages will be removed:" 등) 아래에 포트가 한 줄씩 온다.
    string(REPLACE "\r" "" swOutput "${swOutput}")
    string(REPLACE "\n" ";" swLines "${swOutput}")
    set(swSection "")
    set(swRemove "")
    set(swBuild "")

    foreach(line IN LISTS swLines)
        if(line MATCHES "^The following packages will be removed")
            set(swSection "remove")
        elseif(line MATCHES "^The following packages will be (built and installed|rebuilt)")
            set(swSection "build")
        elseif(line MATCHES "^The following packages")
            set(swSection "")
        elseif(line MATCHES "^ +[*]? *([^ :]+):")
            if(swSection STREQUAL "remove")
                list(APPEND swRemove "${CMAKE_MATCH_1}")
            elseif(swSection STREQUAL "build")
                list(APPEND swBuild "${CMAKE_MATCH_1}")
            endif()
        elseif(NOT line MATCHES "^ ")
            set(swSection "")
        endif()
    endforeach()

    set(${OUT_STATUS} "OK" PARENT_SCOPE)
    set(${OUT_REMOVE} "${swRemove}" PARENT_SCOPE)
    set(${OUT_BUILD} "${swBuild}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# 2) sw_vcpkgWriteManifestStamp — project() 이후 현재 해시를 스탬프에 기록
# 다음 configure의 install gate가 스탬프와 비교해 재설치를 건너뜀
# 경로: ${VCPKG_INSTALLED_DIR}/.sw_vcpkg_manifest_sha
# ------------------------------------------------------------------------------
function(sw_vcpkgWriteManifestStamp)
    if(NOT SW_USE_VCPKG)
        return()
    endif()

    if(NOT DEFINED VCPKG_INSTALLED_DIR OR NOT DEFINED VCPKG_TARGET_TRIPLET)
        return()
    endif()

    # 설치를 거절한 configure(다른 워크트리가 쓰는 포트를 지우게 되는 옛 매니페스트)는 스탬프를 자기 해시로 덮지 않는다.
    get_property(swStampHeld GLOBAL PROPERTY SW_VCPKG_STAMP_HELD)

    if(swStampHeld)
        return()
    endif()

    set(swVcpkgTree "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

    if(NOT EXISTS "${swVcpkgTree}")
        return()
    endif()

    sw_vcpkgComputeManifestHash(swManifestHash)

    if(swManifestHash STREQUAL "")
        return()
    endif()

    set(swStamp "${VCPKG_INSTALLED_DIR}/.sw_vcpkg_manifest_sha")
    file(WRITE "${swStamp}" "${swManifestHash}")
    message(STATUS "[vcpkg] Manifest stamp updated (${VCPKG_TARGET_TRIPLET})")
endfunction()

# ------------------------------------------------------------------------------
# 3) vcpkg 루트 탐색
# 우선순위: SetupVcpkg.py → VCPKG_ROOT → VCPKG_INSTALLATION_ROOT
# ------------------------------------------------------------------------------
if(NOT DEFINED sw_vcpkg_root OR sw_vcpkg_root STREQUAL "")
    set(swFindVcpkgArgs "")

    if(SW_VCPKG_AUTO_BOOTSTRAP)
        list(APPEND swFindVcpkgArgs "--install")
    endif()

    # bootstrap 시도 중에는 QUIET 금지 — git/clone 실패가 configure 로그에 보여야 함
    if(SW_VCPKG_AUTO_BOOTSTRAP)
        sw_executePythonScript(
            "${SW_SCRIPT_SETUP_VCPKG}"
            ARGS ${swFindVcpkgArgs}
            OUTPUT_VARIABLE sw_detected_vcpkg_root
            RESULT_VARIABLE sw_find_vcpkg_result
            WARN
        )
    else()
        sw_executePythonScript(
            "${SW_SCRIPT_SETUP_VCPKG}"
            ARGS ${swFindVcpkgArgs}
            OUTPUT_VARIABLE sw_detected_vcpkg_root
            RESULT_VARIABLE sw_find_vcpkg_result
            QUIET
        )
    endif()

    if(sw_find_vcpkg_result EQUAL 0 AND NOT "${sw_detected_vcpkg_root}" STREQUAL "")
        # SetupVcpkg.py는 stdout에 경로만 출력. 상태 메시지가 섞이면 마지막 줄만 사용
        string(STRIP "${sw_detected_vcpkg_root}" sw_detected_vcpkg_root)
        string(REPLACE "\r\n" "\n" sw_detected_vcpkg_root "${sw_detected_vcpkg_root}")
        string(REPLACE "\r" "\n" sw_detected_vcpkg_root "${sw_detected_vcpkg_root}")
        string(REGEX REPLACE "\n+$" "" sw_detected_vcpkg_root "${sw_detected_vcpkg_root}")

        if(sw_detected_vcpkg_root MATCHES "\n")
            string(REGEX REPLACE "^.*\n([^\n]+)$" "\\1" sw_detected_vcpkg_root "${sw_detected_vcpkg_root}")
        endif()

        set(sw_vcpkg_root "${sw_detected_vcpkg_root}" CACHE PATH "vcpkg 루트 디렉터리" FORCE)
    endif()

    if(NOT DEFINED sw_vcpkg_root OR sw_vcpkg_root STREQUAL "")
        if(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
            set(sw_vcpkg_root "$ENV{VCPKG_ROOT}")
        elseif(DEFINED ENV{VCPKG_INSTALLATION_ROOT} AND NOT "$ENV{VCPKG_INSTALLATION_ROOT}" STREQUAL "")
            set(sw_vcpkg_root "$ENV{VCPKG_INSTALLATION_ROOT}")
        endif()
    endif()
endif()

# ------------------------------------------------------------------------------
# 4) Triplet · installed dir · overlay ports
# VCPKG_TARGET_TRIPLET은 vcpkg.cmake include 전에 정해야 적용됨
# 공유 VCPKG_INSTALLED_DIR은 ADDITIONAL_CLEAN_FILES에 넣지 않음
# (한 preset의 clean이 Debug/Release/Shipping 패키지를 전부 지움)
# overlay ports는 install 게이트 전에 보여야 함 (imgui-notify 등)
# ------------------------------------------------------------------------------
if(NOT DEFINED VCPKG_TARGET_TRIPLET OR VCPKG_TARGET_TRIPLET STREQUAL "")
    # 이 파일은 project() **앞**에서 include 되므로 CMAKE_SYSTEM_PROCESSOR 가 아직 비어 있다.
    # 그 상태로 매치를 돌리면 언제나 마지막 else() 로 떨어져 **호스트가 무엇이든 x64** 가 됐다 —
    # GitHub 의 macos-14 러너(Apple Silicon)가 `x64-osx` 이름으로 포트를 굽고 있었다. 호스트
    # 아키텍처는 project() 없이도 물을 수 있으니(OS_PLATFORM: AMD64 · x86_64 · arm64 · aarch64)
    # 프로세서가 비어 있으면 그것을 대신 본다. 대소문자는 OS 마다 달라서 낮춰서 본다.
    set(sw_vcpkg_processor "${CMAKE_SYSTEM_PROCESSOR}")

    if(sw_vcpkg_processor STREQUAL "")
        cmake_host_system_information(RESULT sw_vcpkg_processor QUERY OS_PLATFORM)
    endif()

    string(TOLOWER "${sw_vcpkg_processor}" sw_vcpkg_processor)

    if(sw_vcpkg_processor MATCHES "^(aarch64|arm64)$" OR CMAKE_GENERATOR_PLATFORM MATCHES "[Aa][Rr][Mm]64")
        set(sw_vcpkg_arch "arm64")
    elseif(sw_vcpkg_processor MATCHES "^(x86_64|amd64|x64)$" OR CMAKE_GENERATOR_PLATFORM MATCHES "[Xx]64")
        set(sw_vcpkg_arch "x64")
    elseif(sw_vcpkg_processor MATCHES "^(x86|i[3-6]86)$")
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

set(swSrcDir "${CMAKE_CURRENT_SOURCE_DIR}")

if(DEFINED CMAKE_SOURCE_DIR AND NOT CMAKE_SOURCE_DIR STREQUAL "")
    set(swSrcDir "${CMAKE_SOURCE_DIR}")
endif()

if(NOT DEFINED VCPKG_INSTALLED_DIR OR VCPKG_INSTALLED_DIR STREQUAL "")
    set(VCPKG_INSTALLED_DIR "${swSrcDir}/build/vcpkg_installed" CACHE PATH "vcpkg 설치 디렉터리" FORCE)
endif()

file(GLOB swVcpkgOverlayPorts "${swSrcDir}/ThirdParty/*/vcpkg-port")

if(swVcpkgOverlayPorts)
    set(VCPKG_OVERLAY_PORTS "${swVcpkgOverlayPorts}" CACHE STRING "vcpkg 오버레이 포트 경로(들)" FORCE)
endif()

# ------------------------------------------------------------------------------
# 5) Manifest install gate — project() 전 CACHE. vcpkg.cmake가 이를 존중
# 트리+스탬프가 일치하면 VCPKG_MANIFEST_INSTALL=OFF
# MANIFEST_MODE는 한 번 ON이면 OFF 전환을 거부하므로 항상 ON, INSTALL로만 제어
# ------------------------------------------------------------------------------
sw_vcpkgComputeManifestHash(swManifestHash)

set(swVcpkgTree "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
set(swStamp "${VCPKG_INSTALLED_DIR}/.sw_vcpkg_manifest_sha")
set(swTreeReady FALSE)

if(EXISTS "${swVcpkgTree}")
    set(swTreeReady TRUE)
endif()

set(swStampMatch FALSE)

if(swTreeReady AND EXISTS "${swStamp}" AND NOT swManifestHash STREQUAL "")
    file(READ "${swStamp}" swStampContent)
    string(STRIP "${swStampContent}" swStampContent)

    if(swStampContent STREQUAL swManifestHash)
        set(swStampMatch TRUE)
    endif()
endif()

# 스탬프가 레거시(json만)와 일치하면 overlay-aware 해시로 갱신하고 skip
if(swTreeReady AND NOT swStampMatch AND NOT swManifestHash STREQUAL "" AND NOT SW_VCPKG_FORCE_INSTALL)
    sw_vcpkgComputeManifestHashLegacy(swLegacyHash)

    if(EXISTS "${swStamp}")
        file(READ "${swStamp}" swStampContent)
        string(STRIP "${swStampContent}" swStampContent)

        if(swStampContent STREQUAL swLegacyHash)
            file(WRITE "${swStamp}" "${swManifestHash}\n")
            set(swStampMatch TRUE)
            message(STATUS "[vcpkg] Manifest stamp migrated to overlay-aware hash (install skipped)")
        endif()
    endif()
endif()

if(SW_VCPKG_FORCE_INSTALL)
    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "vcpkg 매니페스트 모드" FORCE)
    set(VCPKG_MANIFEST_INSTALL ON CACHE BOOL "매니페스트에서 vcpkg 자동 설치" FORCE)
    message(STATUS "[vcpkg] SW_VCPKG_FORCE_INSTALL=ON — manifest install enabled")
elseif(swTreeReady AND swStampMatch)
    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "vcpkg 매니페스트 모드" FORCE)
    set(VCPKG_MANIFEST_INSTALL OFF CACHE BOOL "매니페스트에서 vcpkg 자동 설치" FORCE)
    message(STATUS "[vcpkg] Installed tree matches manifest stamp — skipping install (VCPKG_MANIFEST_INSTALL=OFF)")
elseif(swTreeReady AND swManifestHash STREQUAL "")
    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "vcpkg 매니페스트 모드" FORCE)
    set(VCPKG_MANIFEST_INSTALL OFF CACHE BOOL "매니페스트에서 vcpkg 자동 설치" FORCE)
    message(STATUS "[vcpkg] Installed tree present (no manifest hash) — VCPKG_MANIFEST_INSTALL=OFF")
elseif(swTreeReady)
    # 스탬프가 다르다(이 매니페스트가 바뀌었거나, 설치 폴더를 나눠 쓰는 다른 워크트리가 덮었다). 해시만으로 설치하지 않고 계획을 본다.
    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "vcpkg 매니페스트 모드" FORCE)
    sw_vcpkgPlanManifestInstall("${swSrcDir}" swPlanStatus swPlanRemove swPlanBuild)

    if(NOT swPlanStatus STREQUAL "OK")
        set(VCPKG_MANIFEST_INSTALL ON CACHE BOOL "매니페스트에서 vcpkg 자동 설치" FORCE)
        message(STATUS "[vcpkg] Manifest changed or stamp missing — could not plan the install, install enabled")
    elseif(swPlanRemove)
        # 지우게 되면 멈춘다 — 다른 워크트리(최신 main)가 쓰는 포트일 수 있다. 빠진 포트는 find_package 에서 드러난다.
        set(VCPKG_MANIFEST_INSTALL OFF CACHE BOOL "매니페스트에서 vcpkg 자동 설치" FORCE)
        set_property(GLOBAL PROPERTY SW_VCPKG_STAMP_HELD TRUE)
        message(WARNING
            "[vcpkg] Install skipped: this manifest would remove ports from the shared installed tree:\n"
            "  ${swPlanRemove}\n"
            "Another worktree (or main) uses them. Merge the latest main (vcpkg.json) into this worktree, "
            "or configure with -DSW_VCPKG_FORCE_INSTALL=ON if removing them is intended.")
    elseif(swPlanBuild)
        set(VCPKG_MANIFEST_INSTALL ON CACHE BOOL "매니페스트에서 vcpkg 자동 설치" FORCE)
        message(STATUS "[vcpkg] Manifest needs ports that are not installed (${swPlanBuild}) — install enabled")
    else()
        set(VCPKG_MANIFEST_INSTALL OFF CACHE BOOL "매니페스트에서 vcpkg 자동 설치" FORCE)
        message(STATUS "[vcpkg] Stamp differs but every port of this manifest is already installed — skipping install (VCPKG_MANIFEST_INSTALL=OFF)")
    endif()
else()
    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "vcpkg 매니페스트 모드" FORCE)
    set(VCPKG_MANIFEST_INSTALL ON CACHE BOOL "매니페스트에서 vcpkg 자동 설치" FORCE)
    message(STATUS "[vcpkg] Installed tree missing — install enabled")
endif()

# ------------------------------------------------------------------------------
# 6) CMAKE_TOOLCHAIN_FILE 확정
# Preset이 Tools/vcpkg를 가리켜도, 탐색된 vcpkg root의 vcpkg.cmake를 FORCE
# ------------------------------------------------------------------------------
if(DEFINED sw_vcpkg_root AND NOT sw_vcpkg_root STREQUAL "")
    set(VCPKG_ROOT "${sw_vcpkg_root}" CACHE PATH "vcpkg 루트 디렉터리" FORCE)
    set(swToolchain "${sw_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")

    if(EXISTS "${swToolchain}")
        set(CMAKE_TOOLCHAIN_FILE "${swToolchain}" CACHE FILEPATH "vcpkg 툴체인" FORCE)
    endif()
endif()

# Preset이 없는 Tools/vcpkg 경로를 박아 둔 경우, project() 전에 원인을 명확히 끊습니다.
set(swToolchainFile "${CMAKE_TOOLCHAIN_FILE}")

if(SW_USE_VCPKG AND swToolchainFile AND NOT EXISTS "${swToolchainFile}")
    if(SW_VCPKG_AUTO_BOOTSTRAP)
        message(FATAL_ERROR
            "[vcpkg] CMAKE_TOOLCHAIN_FILE does not exist:\n"
            "  ${swToolchainFile}\n"
            "Auto-bootstrap was enabled but Tools/vcpkg is still missing.\n"
            "  Check that Git is installed, then reconfigure, or run:\n"
            "    py -3 Scripts/setup/SetupVcpkg.py --install"
        )
    else()
        message(FATAL_ERROR
            "[vcpkg] CMAKE_TOOLCHAIN_FILE does not exist:\n"
            "  ${swToolchainFile}\n"
            "Enable SW_VCPKG_AUTO_BOOTSTRAP / search_paths.vcpkg_auto_bootstrap, or run:\n"
            "    py -3 Scripts/setup/SetupVcpkg.py --install"
        )
    endif()
endif()

set(swCanonicalVcpkg "${CMAKE_SOURCE_DIR}/Tools/vcpkg")
cmake_path(NORMAL_PATH swCanonicalVcpkg)

if(DEFINED sw_vcpkg_root AND NOT sw_vcpkg_root STREQUAL "")
    set(swResolvedVcpkg "${sw_vcpkg_root}")
    cmake_path(NORMAL_PATH swResolvedVcpkg)

    if(NOT swResolvedVcpkg STREQUAL swCanonicalVcpkg)
        message(STATUS
            "[vcpkg] Using ${swResolvedVcpkg} "
            "(project default would be ${swCanonicalVcpkg})"
        )
    endif()
endif()

# ------------------------------------------------------------------------------
# 7) sw_toolchain_vcpkg INTERFACE — include 경로 + SW_VCPKG
# sw_flag_libraries에 넣어 sw_add_* 타겟이 PRIVATE 링크
# ------------------------------------------------------------------------------
if(NOT TARGET sw_toolchain_vcpkg)
    add_library(sw_toolchain_vcpkg INTERFACE)
endif()

set(vcpkgIncCandidates
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

if(DEFINED sw_vcpkg_root)
    list(APPEND vcpkgIncCandidates "${sw_vcpkg_root}/installed/${VCPKG_TARGET_TRIPLET}/include")
endif()

foreach(inc IN LISTS vcpkgIncCandidates)
    if(EXISTS "${inc}")
        target_include_directories(sw_toolchain_vcpkg SYSTEM INTERFACE "${inc}")
    endif()
endforeach()

target_compile_definitions(
    sw_toolchain_vcpkg
    INTERFACE
    SW_VCPKG
)

if(NOT sw_toolchain_vcpkg IN_LIST sw_flag_libraries)
    list(APPEND sw_flag_libraries sw_toolchain_vcpkg)
endif()
