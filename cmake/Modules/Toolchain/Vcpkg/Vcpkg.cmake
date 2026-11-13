# ==============================================================================
# @file cmake/Modules/Toolchain/Vcpkg/Vcpkg.cmake
# @brief vcpkg 통합 모듈: root 탐색 · triplet · manifest hash/stamp · install gate · INTERFACE
#
# VcpkgGate.cmake(project() 전)에서 include. 역할:
# 1. vcpkg 루트 탐색 (Vcpkg.py / 환경 변수)
# 2. VCPKG_TARGET_TRIPLET 자동 결정
# 3. install 게이트 — 읽기 전용(사용자가 끈 설치 · 소스 트리 밖 설치 폴더, VcpkgInstallPolicy.cmake)이면 설치 · 스탬프 쓰기를 하지 않는다.
#    그 밖에는 스탬프(매니페스트 해시)가 같으면 바로 skip, 다르면 `vcpkg install --dry-run` 계획으로:
#    지을 것이 없으면 skip, 이미 깔린 포트를 지우게 되면 멈추고 경고, 빠진 포트가 있을 때만 install
# 4. sw_toolchain_vcpkg INTERFACE (SW_VCPKG 정의만 — 서드파티 include 는 각 라이브러리 타깃이 준다)
#
# project() 이후 루트가 sw_vcpkgWriteManifestStamp() 를 호출해
# 다음 configure에서 불필요한 재설치를 건너뛴다.
#
# @note 런타임 헬퍼(sw_copy_vcpkg_*)는 cmake/internal/VcpkgRuntime.cmake
# VcpkgRuntime은 BuildLayout.cmake의 sw_queueRuntimeCopy에 의존하므로 internal에 유지
# ==============================================================================
include("${CMAKE_CURRENT_LIST_DIR}/../../../Environment/PythonUtils.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../Config/LoadConfigConstants.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/VcpkgTriplets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/VcpkgInstallPolicy.cmake")

# 캐시의 VCPKG_MANIFEST_INSTALL 은 사용자 말뿐이다 — 게이트가 정한 값은 project() 뒤 sw_vcpkgWriteManifestStamp 가 캐시에서 지운다.
# 사용자 OFF 는 SW_VCPKG_USER_INSTALL 로 남아 다음 구성에도 읽기 전용이고, -DVCPKG_MANIFEST_INSTALL=ON 으로만 풀린다.
# 옛 게이트가 남긴 캐시(SW_VCPKG_GATE_REV 없이 SW_VCPKG_LAST_TREE 만 있음)의 값은 게이트 판단이라 사용자 말로 읽지 않는다.
if(DEFINED CACHE{VCPKG_MANIFEST_INSTALL} AND (DEFINED SW_VCPKG_GATE_REV OR NOT DEFINED SW_VCPKG_LAST_TREE))
    if(VCPKG_MANIFEST_INSTALL)
        unset(SW_VCPKG_USER_INSTALL CACHE)
    else()
        set(SW_VCPKG_USER_INSTALL OFF CACHE INTERNAL "사용자가 끈 vcpkg 설치 — 이 빌드 폴더는 설치 폴더를 읽기만 한다")
    endif()
endif()

# ------------------------------------------------------------------------------
# 0) sw_vcpkgHashFileNormalized — 줄끝(CRLF/LF)을 같게 보고 파일 내용의 SHA256 을 구한다
# 스탬프는 main 체크아웃과 워크트리가 나눠 쓴다. 두 체크아웃의 줄끝이 다르면(core.autocrlf — main 은 LF, 워크트리는 CRLF 로 나온 적이 있다)
# 내용이 같아도 해시가 달라 configure 마다 스탬프가 번갈아 덮이고, 그때마다 바뀐 것 없이 설치가 돌았다.
# ------------------------------------------------------------------------------
function(sw_vcpkgHashFileNormalized FILE_PATH OUT_VAR)
    file(READ "${FILE_PATH}" swContent)
    string(REPLACE "\r\n" "\n" swContent "${swContent}")
    string(SHA256 swHash "${swContent}")
    set(${OUT_VAR} "${swHash}" PARENT_SCOPE)
endfunction()

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
            sw_vcpkgHashFileNormalized("${overlayFile}" hOverlay)
            string(APPEND hash "${hOverlay}")
        endif()
    endforeach()

    # 생성한 오버레이(triplet · 포트 툴체인 · 컴파일 규칙 — 4 절이 이 함수보다 먼저 만든다). 포트 ABI 와 같은 입력이다.
    file(GLOB swOverlayTripletFiles "${VCPKG_OVERLAY_TRIPLETS}/*.cmake")
    list(SORT swOverlayTripletFiles)

    foreach(tripletFile IN LISTS swOverlayTripletFiles)
        sw_vcpkgHashFileNormalized("${tripletFile}" hTriplet)
        string(APPEND hash "${hTriplet}")
    endforeach()

    # 리눅스 포트 컴파일러(DetectToolchain 이 건 CC · CXX)도 설치를 가른다 — 바뀌면 다시 계획해야 옛 컴파일러(시스템 GCC)로 지은 포트가 남지 않는다.
    # Windows 는 생성한 포트 툴체인(VcpkgTriplets.cmake)이 컴파일러를 정하고 환경 CC 는 프리셋 · 재구성마다 달라 넣지 않는다.
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        string(APPEND hash "|cc=$ENV{CC}|cxx=$ENV{CXX}")
    endif()

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
        sw_vcpkgHashFileNormalized("${swSrcDir}/vcpkg.json" h1)
        string(APPEND hash "${h1}")
    endif()

    if(EXISTS "${swSrcDir}/vcpkg-configuration.json")
        sw_vcpkgHashFileNormalized("${swSrcDir}/vcpkg-configuration.json" h2)
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

    set(swVcpkgExe "${sw_vcpkg_root}/vcpkg${CMAKE_HOST_EXECUTABLE_SUFFIX}")

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

    # VCPKG_OVERLAY_TRIPLETS 는 아래 4) 절이 늘 캐시에 정해 둔다.
    foreach(overlayTriplet IN LISTS VCPKG_OVERLAY_TRIPLETS)
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
# 경로: ${VCPKG_INSTALLED_DIR}/.sw_vcpkg_manifest_sha_<triplet>
# ------------------------------------------------------------------------------
function(sw_vcpkgWriteManifestStamp)
    if(NOT SW_USE_VCPKG)
        return()
    endif()

    # 게이트가 정한 값을 캐시에 남기지 않는다 — 다음 구성이 캐시 값을 사용자 말로 읽는다(파일 머리 참고).
    unset(VCPKG_MANIFEST_INSTALL CACHE)
    set(SW_VCPKG_GATE_REV 2 CACHE INTERNAL "vcpkg 게이트 판 — 캐시의 VCPKG_MANIFEST_INSTALL 이 사용자 말인지 가른다")

    if(NOT DEFINED VCPKG_INSTALLED_DIR OR NOT DEFINED VCPKG_TARGET_TRIPLET)
        return()
    endif()

    # 읽기 전용 구성 · 설치를 거절한 configure(다른 워크트리가 쓰는 포트를 지우게 되는 옛 매니페스트)는 스탬프를 자기 해시로 덮지 않는다.
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

    set(swStamp "${VCPKG_INSTALLED_DIR}/.sw_vcpkg_manifest_sha_${VCPKG_TARGET_TRIPLET}") # triplet 마다 — 한 폴더에 triplet 이 여럿이면 한쪽 스탬프가 다른 쪽 설치를 가렸다
    file(WRITE "${swStamp}" "${swManifestHash}")
    message(STATUS "[vcpkg] Manifest stamp updated (${VCPKG_TARGET_TRIPLET})")
endfunction()

# ------------------------------------------------------------------------------
# 3) vcpkg 루트 탐색
# 우선순위: Vcpkg.py → VCPKG_ROOT → VCPKG_INSTALLATION_ROOT
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
        # Vcpkg.py는 stdout에 경로만 출력. 상태 메시지가 섞이면 마지막 줄만 사용
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
# overlay ports는 install 게이트 전에 보여야 함
# ------------------------------------------------------------------------------
# 교차 툴체인 환경(EMSDK · ANDROID_NDK_HOME) — 프리셋의 environment 는 `cmake --preset` 에만 실리고, 빌드 중 재구성(ninja 가 부르는 cmake)에는 없다.
# 그러면 vcpkg 포트 툴체인(scripts/toolchains/android.cmake · emscripten.cmake)이 NDK · emsdk 를 못 찾아 재구성이 선다 — 구성 때 값을 캐시에 남기고 재구성에서 되살린다.
foreach(swCrossEnv IN ITEMS EMSDK ANDROID_NDK_HOME)
    if(DEFINED ENV{${swCrossEnv}} AND NOT "$ENV{${swCrossEnv}}" STREQUAL "")
        set(SW_CROSS_ENV_${swCrossEnv} "$ENV{${swCrossEnv}}" CACHE INTERNAL "configure-time ${swCrossEnv} (restored on regeneration)")
    elseif(DEFINED SW_CROSS_ENV_${swCrossEnv})
        set(ENV{${swCrossEnv}} "${SW_CROSS_ENV_${swCrossEnv}}")
    endif()
endforeach()

if(NOT DEFINED VCPKG_TARGET_TRIPLET OR VCPKG_TARGET_TRIPLET STREQUAL "")
    # 이 파일은 project() **앞**에서 include 되므로 CMAKE_SYSTEM_PROCESSOR 가 아직 비어 있다.
    # 그 상태로 매치를 돌리면 언제나 마지막 else() 로 떨어져 **호스트가 무엇이든 x64** 가 됐다 —
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

    # 대상 OS 는 이름으로 고른다 — `else()` 에 Linux 를 두면 Android · Emscripten 툴체인도 조용히 linux 패키지를 받는다.
    # `project()` 앞이라 교차 툴체인이 없으면 CMAKE_SYSTEM_NAME 이 비어 있다 — 그때는 호스트와 같은 대상이다.
    # 정의되지 않은 변수를 따옴표 없이 비교하면 이름 글자 그대로 비교된다(`if(CMAKE_SYSTEM_NAME STREQUAL "")` 는 늘 거짓) — 값을 지역에 받아 비교한다.
    # 그 틀린 비교가 새 빌드 폴더의 첫 구성(Linux-Debug-ASan)만 멈췄다. 있는 폴더는 캐시의 VCPKG_TARGET_TRIPLET 로 이 길을 지나지 않는다.
    set(sw_vcpkg_target_system "${CMAKE_SYSTEM_NAME}")
    if(WIN32 OR (sw_vcpkg_target_system STREQUAL "" AND CMAKE_HOST_WIN32))
        set(sw_vcpkg_triplet "${sw_vcpkg_arch}-windows")
    elseif(sw_vcpkg_target_system STREQUAL "Linux" OR (sw_vcpkg_target_system STREQUAL "" AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux"))
        set(sw_vcpkg_triplet "${sw_vcpkg_arch}-linux")
    else()
        message(FATAL_ERROR "[vcpkg] no default triplet for target '${sw_vcpkg_target_system}' (host '${CMAKE_HOST_SYSTEM_NAME}') - set VCPKG_TARGET_TRIPLET in the preset (arm64-android, wasm32-emscripten-pthread)")
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

# 패키지 트리(설치 폴더/triplet)가 지난 구성과 다르면 옛 트리를 가리키는 캐시 항목(find_package · find_path 결과 — ZLIB_INCLUDE_DIR · PNG_LIBRARY · *_DIR …)을
# 지운다. 남겨 두면 find 가 캐시 값을 믿고 다시 찾지 않아, 지워진 옛 폴더를 가리키는 IMPORTED 타깃으로 생성이 멈췄다(Android 설치 폴더 분리 · 웹 pthread triplet 때 두 번).
set(swVcpkgTreeNow "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
if(DEFINED SW_VCPKG_LAST_TREE AND NOT SW_VCPKG_LAST_TREE STREQUAL swVcpkgTreeNow)
    set(swVcpkgTreeOld "${SW_VCPKG_LAST_TREE}") # 기록 변수도 옛 트리 값이라 아래에서 함께 지워진다 — 먼저 받아 둔다
    get_cmake_property(swCacheNames CACHE_VARIABLES)
    set(swClearedCount 0)
    foreach(swCacheName IN LISTS swCacheNames)
        get_property(swCacheValue CACHE "${swCacheName}" PROPERTY VALUE)
        string(FIND "${swCacheValue}" "${swVcpkgTreeOld}/" swOldTreeAt)
        if(NOT swOldTreeAt EQUAL -1 OR swCacheValue STREQUAL swVcpkgTreeOld)
            unset("${swCacheName}" CACHE)
            math(EXPR swClearedCount "${swClearedCount} + 1")
        endif()
    endforeach()
    message(STATUS "[vcpkg] package tree changed (${swVcpkgTreeOld} -> ${swVcpkgTreeNow}) - cleared ${swClearedCount} cached paths into the old tree")
endif()
set(SW_VCPKG_LAST_TREE "${swVcpkgTreeNow}" CACHE INTERNAL "지난 구성의 vcpkg 패키지 트리 — 바뀌면 옛 트리를 가리키는 캐시를 지운다")

# 바이너리 캐시는 환경 변수로만 전달된다. CMakePresets.json 의 `environment` 는 `cmake --preset` 으로 구성할 때만 걸리고,
# ninja 가 부르는 재구성(`cmake --regenerate-during-build`)과 `cmake -S -B` 에는 없다 — 그러면 vcpkg 가 기본 캐시(%LOCALAPPDATA%)를 보고
# 포트 전체를 소스에서 다시 짓는다(35 개 포트 · 20 분, 새 결과는 저장소 캐시에 안 쌓인다). 프리셋과 같은 값을 여기서도 정한다.
if(NOT DEFINED ENV{VCPKG_BINARY_SOURCES} OR "$ENV{VCPKG_BINARY_SOURCES}" STREQUAL "")
    set(ENV{VCPKG_BINARY_SOURCES} "clear;files,${swSrcDir}/build/vcpkg_cache,readwrite")
    message(STATUS "[vcpkg] VCPKG_BINARY_SOURCES not set in the environment — using ${swSrcDir}/build/vcpkg_cache")
endif()

# 오버레이 triplet 은 구성마다 빌드 폴더에 생성한다(VcpkgTriplets.cmake — 포트 ABI 가 풀린 도구 · 플래그만 보게). 재구성(ninja 의
# `--regenerate-during-build` · `cmake -S -B`)도 이 줄을 지나 같은 폴더를 가리킨다 — 빠지면 vcpkg 가 기본 triplet(MSVC)으로 공유 설치 폴더의
# 포트 전체를 다시 깐다(2026-10-18). 환경 변수 VCPKG_OVERLAY_TRIPLETS 는 vcpkg 실행 파일도 읽으므로 비운다(다른 오버레이가 섞이지 않게).
sw_vcpkgWriteOverlayTriplets(swGeneratedTripletDir)
set(VCPKG_OVERLAY_TRIPLETS "${swGeneratedTripletDir}" CACHE STRING "vcpkg 오버레이 triplet 경로(생성)" FORCE)
unset(ENV{VCPKG_OVERLAY_TRIPLETS})

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
set(swStamp "${VCPKG_INSTALLED_DIR}/.sw_vcpkg_manifest_sha_${VCPKG_TARGET_TRIPLET}") # triplet 마다 — 한 폴더에 triplet 이 여럿이면 한쪽 스탬프가 다른 쪽 설치를 가렸다
set(swTreeReady FALSE)

# 폴더만 보지 않는다 — 매니페스트 설치가 다른 triplet 의 포트를 지우면 빈 폴더가 남아 스탬프만 맞고 헤더가 없었다(2026-11-09).
file(GLOB swVcpkgSharedPorts LIST_DIRECTORIES true "${swVcpkgTree}/share/*")
if(EXISTS "${swVcpkgTree}" AND swVcpkgSharedPorts)
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

sw_vcpkgInstallReadOnlyReason("${swSrcDir}" "${VCPKG_INSTALLED_DIR}" "${SW_VCPKG_USER_INSTALL}" swReadOnlyReason)

# 스탬프가 레거시(json만)와 일치하면 overlay-aware 해시로 갱신하고 skip
if(swReadOnlyReason STREQUAL "" AND swTreeReady AND NOT swStampMatch AND NOT swManifestHash STREQUAL "" AND NOT SW_VCPKG_FORCE_INSTALL)
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

if(NOT swReadOnlyReason STREQUAL "")
    # 읽기 전용 — 폴백 없이 멈출 것은 멈춘다. 매니페스트와 다른 포트는 find_package 에서 드러난다.
    if(SW_VCPKG_FORCE_INSTALL)
        message(FATAL_ERROR "[vcpkg] SW_VCPKG_FORCE_INSTALL=ON but the installed tree is read-only: ${swReadOnlyReason}")
    endif()

    if(NOT swTreeReady)
        message(FATAL_ERROR
            "[vcpkg] No installed packages at ${swVcpkgTree}, and this configure may not install there: ${swReadOnlyReason}
"
            "Copy an installed tree into this source tree's build/vcpkg_installed, or configure without those options.")
    endif()

    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "vcpkg 매니페스트 모드" FORCE)
    set(VCPKG_MANIFEST_INSTALL OFF CACHE BOOL "매니페스트에서 vcpkg 자동 설치" FORCE)
    set_property(GLOBAL PROPERTY SW_VCPKG_STAMP_HELD TRUE)
    message(STATUS "[vcpkg] Read-only installed tree (${swReadOnlyReason}) — no install, no stamp")
elseif(SW_VCPKG_FORCE_INSTALL)
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
            "    py -3 Scripts/setup/Vcpkg.py --install"
        )
    else()
        message(FATAL_ERROR
            "[vcpkg] CMAKE_TOOLCHAIN_FILE does not exist:\n"
            "  ${swToolchainFile}\n"
            "Enable SW_VCPKG_AUTO_BOOTSTRAP / search_paths.vcpkg_auto_bootstrap, or run:\n"
            "    py -3 Scripts/setup/Vcpkg.py --install"
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
# 7) sw_toolchain_vcpkg INTERFACE — SW_VCPKG 정의만
# sw_flag_libraries에 넣어 sw_add_* 타겟이 PRIVATE 링크
# 주의: vcpkg include 폴더를 여기서 모든 타깃에 걸지 않는다. 서드파티 헤더는 그 라이브러리 타깃(ThirdParty/<이름>)을 링크해 얻는다.
#   이 블록은 project() 전에 돌고 매니페스트 설치는 project() 안에서 일어나므로, 폴더가 있을 때만 걸면 새 러너(CI)의 첫 구성에는 빠지고
#   설치가 남아 있는 PC 에는 걸린다 — 로컬에서만 통과하는 include 누락이 그렇게 생겼다.
# ------------------------------------------------------------------------------
if(NOT TARGET sw_toolchain_vcpkg)
    add_library(sw_toolchain_vcpkg INTERFACE)
endif()

target_compile_definitions(
    sw_toolchain_vcpkg
    INTERFACE
    SW_VCPKG
)

if(NOT sw_toolchain_vcpkg IN_LIST sw_flag_libraries)
    list(APPEND sw_flag_libraries sw_toolchain_vcpkg)
endif()
