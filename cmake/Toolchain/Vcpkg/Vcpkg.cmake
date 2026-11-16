# ==============================================================================
# @file cmake/Toolchain/Vcpkg/Vcpkg.cmake
# @brief vcpkg 통합 모듈: root 탐색 · triplet · manifest hash/stamp · install gate · INTERFACE
#
# VcpkgGate.cmake(project() 전)에서 include. 역할:
# 1. vcpkg 루트 탐색 (vcpkg.py / 환경 변수)
# 2. VCPKG_TARGET_TRIPLET 자동 결정
# 3. install 게이트 — 읽기 전용(사용자가 끈 설치 · 소스 트리 밖 설치 폴더, VcpkgInstallPolicy.cmake)이면 설치 · 스탬프 쓰기를 하지 않는다.
#    그 밖에는 스탬프(매니페스트 해시)가 같으면 바로 skip, 다르면 `vcpkg install --dry-run` 계획으로:
#    지을 것이 없으면 skip, 이미 깔린 포트를 지우게 되면 멈추고 경고, 빠진 포트가 있을 때만 install
# 4. sw_toolchain_vcpkg INTERFACE (SW_VCPKG 정의만 — 서드파티 include 는 각 라이브러리 타깃이 준다)
#
# project() 이후 루트가 sw_vcpkg_write_manifest_stamp() 를 호출해
# 다음 configure에서 불필요한 재설치를 건너뛴다.
#
# @note 런타임 헬퍼(sw_copy_vcpkg_*)는 cmake/internal/VcpkgRuntime.cmake
# VcpkgRuntime은 BuildLayout.cmake의 sw_queue_runtime_copy에 의존하므로 internal에 유지
# ==============================================================================
include("${CMAKE_CURRENT_LIST_DIR}/../../Modules/Environment/PythonUtils.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../Modules/Config/LoadConfigConstants.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/VcpkgTriplets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/VcpkgInstallPolicy.cmake")

# 캐시의 VCPKG_MANIFEST_INSTALL 은 사용자 말뿐이다 — 게이트가 정한 값은 project() 뒤 sw_vcpkg_write_manifest_stamp 가 캐시에서 지운다.
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
# 0) sw_vcpkg_hash_file_normalized — 줄끝(CRLF/LF)을 같게 보고 파일 내용의 SHA256 을 구한다
# 스탬프는 main 체크아웃과 워크트리가 나눠 쓴다. 두 체크아웃의 줄끝이 다르면(core.autocrlf — main 은 LF, 워크트리는 CRLF 로 나온 적이 있다)
# 내용이 같아도 해시가 달라 configure 마다 스탬프가 번갈아 덮이고, 그때마다 바뀐 것 없이 설치가 돌았다.
# ------------------------------------------------------------------------------
function(sw_vcpkg_hash_file_normalized FILE_PATH OUT_VAR)
    file(READ "${FILE_PATH}" content)
    string(REPLACE "\r\n" "\n" content "${content}")
    string(SHA256 hash "${content}")
    set(${OUT_VAR} "${hash}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# 1) sw_vcpkg_compute_manifest_hash — manifest + overlay port/triplet SHA256
# 포함: vcpkg.json, vcpkg-configuration.json, ThirdParty/*/vcpkg-port,
# cmake/Toolchain/Vcpkg 오버레이 triplet (.cmake). 없으면 빈 문자열
# ------------------------------------------------------------------------------
function(sw_vcpkg_compute_manifest_hash OUT_VAR)
    set(src_dir "${CMAKE_CURRENT_SOURCE_DIR}")

    if(DEFINED CMAKE_SOURCE_DIR AND NOT CMAKE_SOURCE_DIR STREQUAL "")
        set(src_dir "${CMAKE_SOURCE_DIR}")
    endif()

    # 앞부분(매니페스트 두 파일)은 레거시 스탬프와 같다 — 두 벌로 적지 않고 그것에 오버레이를 덧붙인다.
    sw_vcpkg_compute_manifest_hash_legacy(hash)

    file(GLOB_RECURSE overlay_port_files
        "${src_dir}/ThirdParty/*/vcpkg-port/portfile.cmake"
        "${src_dir}/ThirdParty/*/vcpkg-port/vcpkg.json"
        "${src_dir}/ThirdParty/*/vcpkg-port/*.patch"
        "${src_dir}/ThirdParty/*/vcpkg-port/*/portfile.cmake"
        "${src_dir}/ThirdParty/*/vcpkg-port/*/vcpkg.json"
        "${src_dir}/ThirdParty/*/vcpkg-port/*/*.patch"
    )
    list(SORT overlay_port_files)

    foreach(overlay_file IN LISTS overlay_port_files)
        if(EXISTS "${overlay_file}")
            sw_vcpkg_hash_file_normalized("${overlay_file}" overlay)
            string(APPEND hash "${overlay}")
        endif()
    endforeach()

    # 생성한 오버레이(triplet · 포트 툴체인 · 컴파일 규칙 — 4 절이 이 함수보다 먼저 만든다). 포트 ABI 와 같은 입력이다.
    file(GLOB overlay_triplet_files "${VCPKG_OVERLAY_TRIPLETS}/*.cmake")
    list(SORT overlay_triplet_files)

    foreach(triplet_file IN LISTS overlay_triplet_files)
        sw_vcpkg_hash_file_normalized("${triplet_file}" triplet)
        string(APPEND hash "${triplet}")
    endforeach()

    # 리눅스 포트 컴파일러(DetectToolchain 이 건 CC · CXX)도 설치를 가른다 — 바뀌면 다시 계획해야 옛 컴파일러(시스템 GCC)로 지은 포트가 남지 않는다.
    # Windows 는 생성한 포트 툴체인(VcpkgTriplets.cmake)이 컴파일러를 정하고 환경 CC 는 프리셋 · 재구성마다 달라 넣지 않는다.
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        string(APPEND hash "|cc=$ENV{CC}|cxx=$ENV{CXX}")
    endif()

    set(${OUT_VAR} "${hash}" PARENT_SCOPE)
endfunction()

# 레거시 스탬프(vcpkg.json + vcpkg-configuration.json만) 해시. 공식 마이그레이션용.
function(sw_vcpkg_compute_manifest_hash_legacy OUT_VAR)
    set(src_dir "${CMAKE_CURRENT_SOURCE_DIR}")

    if(DEFINED CMAKE_SOURCE_DIR AND NOT CMAKE_SOURCE_DIR STREQUAL "")
        set(src_dir "${CMAKE_SOURCE_DIR}")
    endif()

    set(hash "")

    if(EXISTS "${src_dir}/vcpkg.json")
        sw_vcpkg_hash_file_normalized("${src_dir}/vcpkg.json" h1)
        string(APPEND hash "${h1}")
    endif()

    if(EXISTS "${src_dir}/vcpkg-configuration.json")
        sw_vcpkg_hash_file_normalized("${src_dir}/vcpkg-configuration.json" h2)
        string(APPEND hash "${h2}")
    endif()

    set(${OUT_VAR} "${hash}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# 1b) sw_vcpkg_plan_manifest_install — 이 매니페스트를 공유 설치 폴더에 깔면 무엇이 바뀌는지(`vcpkg install --dry-run`)
# 설치 폴더 · 스탬프는 워크트리 모두가 나눠 쓴다. 해시만 다르다고 설치하면 옛 매니페스트의 워크트리가 다른 워크트리가 쓰는
# 포트를 지운다 — 그래서 설치 여부는 계획(지울 것 · 지을 것)으로 정한다.
# OUT_STATUS: OK(계획을 얻음) · FAILED(vcpkg 를 돌리지 못함). OUT_REMOVE · OUT_BUILD: 지울 · 지을(다시 지을 포함) 포트 목록
# ------------------------------------------------------------------------------
function(sw_vcpkg_plan_manifest_install src_dir OUT_STATUS OUT_REMOVE OUT_BUILD)
    set(${OUT_STATUS} "FAILED" PARENT_SCOPE)
    set(${OUT_REMOVE} "" PARENT_SCOPE)
    set(${OUT_BUILD} "" PARENT_SCOPE)

    if(NOT DEFINED SW_VCPKG_ROOT OR SW_VCPKG_ROOT STREQUAL "")
        return()
    endif()

    set(vcpkg_exe "${SW_VCPKG_ROOT}/vcpkg${CMAKE_HOST_EXECUTABLE_SUFFIX}")

    if(NOT EXISTS "${vcpkg_exe}")
        return()
    endif()

    set(args
        install --dry-run
        "--triplet=${VCPKG_TARGET_TRIPLET}"
        "--host-triplet=${VCPKG_HOST_TRIPLET}"
        "--x-install-root=${VCPKG_INSTALLED_DIR}"
        "--x-manifest-root=${src_dir}"
    )

    foreach(overlay_port IN LISTS VCPKG_OVERLAY_PORTS)
        list(APPEND args "--overlay-ports=${overlay_port}")
    endforeach()

    # VCPKG_OVERLAY_TRIPLETS 는 아래 4) 절이 늘 캐시에 정해 둔다.
    foreach(overlay_triplet IN LISTS VCPKG_OVERLAY_TRIPLETS)
        list(APPEND args "--overlay-triplets=${overlay_triplet}")
    endforeach()

    execute_process(
        COMMAND "${vcpkg_exe}" ${args}
        WORKING_DIRECTORY "${src_dir}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )

    if(NOT result EQUAL 0)
        message(STATUS "[vcpkg] dry-run failed (${result}): ${error}")
        return()
    endif()

    # 절마다 머리 줄("The following packages will be removed:" 등) 아래에 포트가 한 줄씩 온다.
    string(REPLACE "\r" "" output "${output}")
    string(REPLACE "\n" ";" lines "${output}")
    set(section "")
    set(remove "")
    set(build "")

    foreach(line IN LISTS lines)
        if(line MATCHES "^The following packages will be removed")
            set(section "remove")
        elseif(line MATCHES "^The following packages will be (built and installed|rebuilt)")
            set(section "build")
        elseif(line MATCHES "^The following packages")
            set(section "")
        elseif(line MATCHES "^ +[*]? *([^ :]+):")
            if(section STREQUAL "remove")
                list(APPEND remove "${CMAKE_MATCH_1}")
            elseif(section STREQUAL "build")
                list(APPEND build "${CMAKE_MATCH_1}")
            endif()
        elseif(NOT line MATCHES "^ ")
            set(section "")
        endif()
    endforeach()

    set(${OUT_STATUS} "OK" PARENT_SCOPE)
    set(${OUT_REMOVE} "${remove}" PARENT_SCOPE)
    set(${OUT_BUILD} "${build}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# 2) sw_vcpkg_write_manifest_stamp — project() 이후 현재 해시를 스탬프에 기록
# 다음 configure의 install gate가 스탬프와 비교해 재설치를 건너뜀
# 경로: ${VCPKG_INSTALLED_DIR}/.sw_vcpkg_manifest_sha_<triplet>
# ------------------------------------------------------------------------------
function(sw_vcpkg_write_manifest_stamp)
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
    get_property(stamp_held GLOBAL PROPERTY SW_VCPKG_STAMP_HELD)

    if(stamp_held)
        return()
    endif()

    set(vcpkg_tree "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

    if(NOT EXISTS "${vcpkg_tree}")
        return()
    endif()

    sw_vcpkg_compute_manifest_hash(manifest_hash)

    if(manifest_hash STREQUAL "")
        return()
    endif()

    set(stamp "${VCPKG_INSTALLED_DIR}/.sw_vcpkg_manifest_sha_${VCPKG_TARGET_TRIPLET}") # triplet 마다 — 한 폴더에 triplet 이 여럿이면 한쪽 스탬프가 다른 쪽 설치를 가렸다
    file(WRITE "${stamp}" "${manifest_hash}")
    message(STATUS "[vcpkg] Manifest stamp updated (${VCPKG_TARGET_TRIPLET})")
endfunction()

# ------------------------------------------------------------------------------
# 3) vcpkg 루트 탐색
# 우선순위: vcpkg.py → VCPKG_ROOT → VCPKG_INSTALLATION_ROOT
# ------------------------------------------------------------------------------
if(NOT DEFINED SW_VCPKG_ROOT OR SW_VCPKG_ROOT STREQUAL "")
    set(find_vcpkg_args "")

    if(SW_VCPKG_AUTO_BOOTSTRAP)
        list(APPEND find_vcpkg_args "--install")
    endif()

    # bootstrap 시도 중에는 QUIET 금지 — git/clone 실패가 configure 로그에 보여야 함
    if(SW_VCPKG_AUTO_BOOTSTRAP)
        sw_execute_python_script(
            "${SW_SCRIPT_SETUP_VCPKG}"
            ARGS ${find_vcpkg_args}
            OUTPUT_VARIABLE SW_DETECTED_VCPKG_ROOT
            RESULT_VARIABLE SW_FIND_VCPKG_RESULT
            WARN
        )
    else()
        sw_execute_python_script(
            "${SW_SCRIPT_SETUP_VCPKG}"
            ARGS ${find_vcpkg_args}
            OUTPUT_VARIABLE SW_DETECTED_VCPKG_ROOT
            RESULT_VARIABLE SW_FIND_VCPKG_RESULT
            QUIET
        )
    endif()

    if(SW_FIND_VCPKG_RESULT EQUAL 0 AND NOT "${SW_DETECTED_VCPKG_ROOT}" STREQUAL "")
        # Vcpkg.py는 stdout에 경로만 출력. 상태 메시지가 섞이면 마지막 줄만 사용
        string(STRIP "${SW_DETECTED_VCPKG_ROOT}" SW_DETECTED_VCPKG_ROOT)
        string(REPLACE "\r\n" "\n" SW_DETECTED_VCPKG_ROOT "${SW_DETECTED_VCPKG_ROOT}")
        string(REPLACE "\r" "\n" SW_DETECTED_VCPKG_ROOT "${SW_DETECTED_VCPKG_ROOT}")
        string(REGEX REPLACE "\n+$" "" SW_DETECTED_VCPKG_ROOT "${SW_DETECTED_VCPKG_ROOT}")

        if(SW_DETECTED_VCPKG_ROOT MATCHES "\n")
            string(REGEX REPLACE "^.*\n([^\n]+)$" "\\1" SW_DETECTED_VCPKG_ROOT "${SW_DETECTED_VCPKG_ROOT}")
        endif()

        set(SW_VCPKG_ROOT "${SW_DETECTED_VCPKG_ROOT}" CACHE PATH "vcpkg 루트 디렉터리" FORCE)
    endif()

    if(NOT DEFINED SW_VCPKG_ROOT OR SW_VCPKG_ROOT STREQUAL "")
        if(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
            set(SW_VCPKG_ROOT "$ENV{VCPKG_ROOT}")
        elseif(DEFINED ENV{VCPKG_INSTALLATION_ROOT} AND NOT "$ENV{VCPKG_INSTALLATION_ROOT}" STREQUAL "")
            set(SW_VCPKG_ROOT "$ENV{VCPKG_INSTALLATION_ROOT}")
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
foreach(cross_env IN ITEMS EMSDK ANDROID_NDK_HOME)
    if(DEFINED ENV{${cross_env}} AND NOT "$ENV{${cross_env}}" STREQUAL "")
        set(SW_CROSS_ENV_${cross_env} "$ENV{${cross_env}}" CACHE INTERNAL "configure-time ${cross_env} (restored on regeneration)")
    elseif(DEFINED SW_CROSS_ENV_${cross_env})
        set(ENV{${cross_env}} "${SW_CROSS_ENV_${cross_env}}")
    endif()
endforeach()

if(NOT DEFINED VCPKG_TARGET_TRIPLET OR VCPKG_TARGET_TRIPLET STREQUAL "")
    # 이 파일은 project() **앞**에서 include 되므로 CMAKE_SYSTEM_PROCESSOR 가 아직 비어 있다.
    # 그 상태로 매치를 돌리면 언제나 마지막 else() 로 떨어져 **호스트가 무엇이든 x64** 가 됐다 —
    # 아키텍처는 project() 없이도 물을 수 있으니(OS_PLATFORM: AMD64 · x86_64 · arm64 · aarch64)
    # 프로세서가 비어 있으면 그것을 대신 본다. 대소문자는 OS 마다 달라서 낮춰서 본다.
    set(SW_VCPKG_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")

    if(SW_VCPKG_PROCESSOR STREQUAL "")
        cmake_host_system_information(RESULT SW_VCPKG_PROCESSOR QUERY OS_PLATFORM)
    endif()

    string(TOLOWER "${SW_VCPKG_PROCESSOR}" SW_VCPKG_PROCESSOR)

    if(SW_VCPKG_PROCESSOR MATCHES "^(aarch64|arm64)$" OR CMAKE_GENERATOR_PLATFORM MATCHES "[Aa][Rr][Mm]64")
        set(SW_VCPKG_ARCH "arm64")
    elseif(SW_VCPKG_PROCESSOR MATCHES "^(x86_64|amd64|x64)$" OR CMAKE_GENERATOR_PLATFORM MATCHES "[Xx]64")
        set(SW_VCPKG_ARCH "x64")
    elseif(SW_VCPKG_PROCESSOR MATCHES "^(x86|i[3-6]86)$")
        set(SW_VCPKG_ARCH "x86")
    else()
        set(SW_VCPKG_ARCH "x64")
    endif()

    # 대상 OS 는 이름으로 고른다 — `else()` 에 Linux 를 두면 Android · Emscripten 툴체인도 조용히 linux 패키지를 받는다.
    # `project()` 앞이라 교차 툴체인이 없으면 CMAKE_SYSTEM_NAME 이 비어 있다 — 그때는 호스트와 같은 대상이다.
    # 정의되지 않은 변수를 따옴표 없이 비교하면 이름 글자 그대로 비교된다(`if(CMAKE_SYSTEM_NAME STREQUAL "")` 는 늘 거짓) — 값을 지역에 받아 비교한다.
    # 그 틀린 비교가 새 빌드 폴더의 첫 구성(Linux-Debug-ASan)만 멈췄다. 있는 폴더는 캐시의 VCPKG_TARGET_TRIPLET 로 이 길을 지나지 않는다.
    set(SW_VCPKG_TARGET_SYSTEM "${CMAKE_SYSTEM_NAME}")
    if(WIN32 OR (SW_VCPKG_TARGET_SYSTEM STREQUAL "" AND CMAKE_HOST_WIN32))
        set(SW_VCPKG_TRIPLET "${SW_VCPKG_ARCH}-windows")
    elseif(SW_VCPKG_TARGET_SYSTEM STREQUAL "Linux" OR (SW_VCPKG_TARGET_SYSTEM STREQUAL "" AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux"))
        set(SW_VCPKG_TRIPLET "${SW_VCPKG_ARCH}-linux")
    else()
        message(FATAL_ERROR "[vcpkg] no default triplet for target '${SW_VCPKG_TARGET_SYSTEM}' (host '${CMAKE_HOST_SYSTEM_NAME}') - set VCPKG_TARGET_TRIPLET in the preset (arm64-android, wasm32-emscripten-pthread)")
    endif()

    set(VCPKG_TARGET_TRIPLET "${SW_VCPKG_TRIPLET}" CACHE STRING "" FORCE)
    set(VCPKG_HOST_TRIPLET "${SW_VCPKG_TRIPLET}" CACHE STRING "" FORCE)
endif()

set(src_dir "${CMAKE_CURRENT_SOURCE_DIR}")

if(DEFINED CMAKE_SOURCE_DIR AND NOT CMAKE_SOURCE_DIR STREQUAL "")
    set(src_dir "${CMAKE_SOURCE_DIR}")
endif()

if(NOT DEFINED VCPKG_INSTALLED_DIR OR VCPKG_INSTALLED_DIR STREQUAL "")
    set(VCPKG_INSTALLED_DIR "${src_dir}/build/vcpkg_installed" CACHE PATH "vcpkg 설치 디렉터리" FORCE)
endif()

# 패키지 트리(설치 폴더/triplet)가 지난 구성과 다르면 옛 트리를 가리키는 캐시 항목(find_package · find_path 결과 — ZLIB_INCLUDE_DIR · PNG_LIBRARY · *_DIR …)을
# 지운다. 남겨 두면 find 가 캐시 값을 믿고 다시 찾지 않아, 지워진 옛 폴더를 가리키는 IMPORTED 타깃으로 생성이 멈췄다(Android 설치 폴더 분리 · 웹 pthread triplet 때 두 번).
set(vcpkg_tree_now "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
if(DEFINED SW_VCPKG_LAST_TREE AND NOT SW_VCPKG_LAST_TREE STREQUAL vcpkg_tree_now)
    set(vcpkg_tree_old "${SW_VCPKG_LAST_TREE}") # 기록 변수도 옛 트리 값이라 아래에서 함께 지워진다 — 먼저 받아 둔다
    get_cmake_property(cache_names CACHE_VARIABLES)
    set(cleared_count 0)
    foreach(cache_name IN LISTS cache_names)
        get_property(cache_value CACHE "${cache_name}" PROPERTY VALUE)
        string(FIND "${cache_value}" "${vcpkg_tree_old}/" old_tree_at)
        if(NOT old_tree_at EQUAL -1 OR cache_value STREQUAL vcpkg_tree_old)
            unset("${cache_name}" CACHE)
            math(EXPR cleared_count "${cleared_count} + 1")
        endif()
    endforeach()
    message(STATUS "[vcpkg] package tree changed (${vcpkg_tree_old} -> ${vcpkg_tree_now}) - cleared ${cleared_count} cached paths into the old tree")
endif()
set(SW_VCPKG_LAST_TREE "${vcpkg_tree_now}" CACHE INTERNAL "지난 구성의 vcpkg 패키지 트리 — 바뀌면 옛 트리를 가리키는 캐시를 지운다")

# 바이너리 캐시는 환경 변수로만 전달된다. CMakePresets.json 의 `environment` 는 `cmake --preset` 으로 구성할 때만 걸리고,
# ninja 가 부르는 재구성(`cmake --regenerate-during-build`)과 `cmake -S -B` 에는 없다 — 그러면 vcpkg 가 기본 캐시(%LOCALAPPDATA%)를 보고
# 포트 전체를 소스에서 다시 짓는다(35 개 포트 · 20 분, 새 결과는 저장소 캐시에 안 쌓인다). 프리셋과 같은 값을 여기서도 정한다.
if(NOT DEFINED ENV{VCPKG_BINARY_SOURCES} OR "$ENV{VCPKG_BINARY_SOURCES}" STREQUAL "")
    set(ENV{VCPKG_BINARY_SOURCES} "clear;files,${src_dir}/build/vcpkg_cache,readwrite")
    message(STATUS "[vcpkg] VCPKG_BINARY_SOURCES not set in the environment — using ${src_dir}/build/vcpkg_cache")
endif()

# 오버레이 triplet 은 구성마다 빌드 폴더에 생성한다(VcpkgTriplets.cmake — 포트 ABI 가 풀린 도구 · 플래그만 보게). 재구성(ninja 의
# `--regenerate-during-build` · `cmake -S -B`)도 이 줄을 지나 같은 폴더를 가리킨다 — 빠지면 vcpkg 가 기본 triplet(MSVC)으로 공유 설치 폴더의
# 포트 전체를 다시 깐다(2026-10-18). 환경 변수 VCPKG_OVERLAY_TRIPLETS 는 vcpkg 실행 파일도 읽으므로 비운다(다른 오버레이가 섞이지 않게).
sw_vcpkg_write_overlay_triplets(generated_triplet_dir)
set(VCPKG_OVERLAY_TRIPLETS "${generated_triplet_dir}" CACHE STRING "vcpkg 오버레이 triplet 경로(생성)" FORCE)
unset(ENV{VCPKG_OVERLAY_TRIPLETS})

file(GLOB vcpkg_overlay_ports "${src_dir}/ThirdParty/*/vcpkg-port")

if(vcpkg_overlay_ports)
    set(VCPKG_OVERLAY_PORTS "${vcpkg_overlay_ports}" CACHE STRING "vcpkg 오버레이 포트 경로(들)" FORCE)
endif()

# ------------------------------------------------------------------------------
# 5) Manifest install gate — project() 전 CACHE. vcpkg.cmake가 이를 존중
# 트리+스탬프가 일치하면 VCPKG_MANIFEST_INSTALL=OFF
# MANIFEST_MODE는 한 번 ON이면 OFF 전환을 거부하므로 항상 ON, INSTALL로만 제어
# ------------------------------------------------------------------------------
sw_vcpkg_compute_manifest_hash(manifest_hash)

set(vcpkg_tree "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
set(stamp "${VCPKG_INSTALLED_DIR}/.sw_vcpkg_manifest_sha_${VCPKG_TARGET_TRIPLET}") # triplet 마다 — 한 폴더에 triplet 이 여럿이면 한쪽 스탬프가 다른 쪽 설치를 가렸다
set(tree_ready FALSE)

# 폴더만 보지 않는다 — 매니페스트 설치가 다른 triplet 의 포트를 지우면 빈 폴더가 남아 스탬프만 맞고 헤더가 없었다(2026-11-09).
file(GLOB vcpkg_shared_ports LIST_DIRECTORIES true "${vcpkg_tree}/share/*")
if(EXISTS "${vcpkg_tree}" AND vcpkg_shared_ports)
    set(tree_ready TRUE)
endif()

set(stamp_match FALSE)

if(tree_ready AND EXISTS "${stamp}" AND NOT manifest_hash STREQUAL "")
    file(READ "${stamp}" stamp_content)
    string(STRIP "${stamp_content}" stamp_content)

    if(stamp_content STREQUAL manifest_hash)
        set(stamp_match TRUE)
    endif()
endif()

sw_vcpkg_install_read_only_reason("${src_dir}" "${VCPKG_INSTALLED_DIR}" "${SW_VCPKG_USER_INSTALL}" read_only_reason)

# 스탬프가 레거시(json만)와 일치하면 overlay-aware 해시로 갱신하고 skip
if(read_only_reason STREQUAL "" AND tree_ready AND NOT stamp_match AND NOT manifest_hash STREQUAL "" AND NOT SW_VCPKG_FORCE_INSTALL)
    sw_vcpkg_compute_manifest_hash_legacy(legacy_hash)

    if(EXISTS "${stamp}")
        file(READ "${stamp}" stamp_content)
        string(STRIP "${stamp_content}" stamp_content)

        if(stamp_content STREQUAL legacy_hash)
            file(WRITE "${stamp}" "${manifest_hash}\n")
            set(stamp_match TRUE)
            message(STATUS "[vcpkg] Manifest stamp migrated to overlay-aware hash (install skipped)")
        endif()
    endif()
endif()

if(NOT read_only_reason STREQUAL "")
    # 읽기 전용 — 폴백 없이 멈출 것은 멈춘다. 매니페스트와 다른 포트는 find_package 에서 드러난다.
    if(SW_VCPKG_FORCE_INSTALL)
        message(FATAL_ERROR "[vcpkg] SW_VCPKG_FORCE_INSTALL=ON but the installed tree is read-only: ${read_only_reason}")
    endif()

    if(NOT tree_ready)
        message(FATAL_ERROR
            "[vcpkg] No installed packages at ${vcpkg_tree}, and this configure may not install there: ${read_only_reason}
"
            "Copy an installed tree into this source tree's build/vcpkg_installed, or configure without those options.")
    endif()

    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "vcpkg 매니페스트 모드" FORCE)
    set(VCPKG_MANIFEST_INSTALL OFF CACHE BOOL "매니페스트에서 vcpkg 자동 설치" FORCE)
    set_property(GLOBAL PROPERTY SW_VCPKG_STAMP_HELD TRUE)
    message(STATUS "[vcpkg] Read-only installed tree (${read_only_reason}) — no install, no stamp")
elseif(SW_VCPKG_FORCE_INSTALL)
    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "vcpkg 매니페스트 모드" FORCE)
    set(VCPKG_MANIFEST_INSTALL ON CACHE BOOL "매니페스트에서 vcpkg 자동 설치" FORCE)
    message(STATUS "[vcpkg] SW_VCPKG_FORCE_INSTALL=ON — manifest install enabled")
elseif(tree_ready AND stamp_match)
    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "vcpkg 매니페스트 모드" FORCE)
    set(VCPKG_MANIFEST_INSTALL OFF CACHE BOOL "매니페스트에서 vcpkg 자동 설치" FORCE)
    message(STATUS "[vcpkg] Installed tree matches manifest stamp — skipping install (VCPKG_MANIFEST_INSTALL=OFF)")
elseif(tree_ready AND manifest_hash STREQUAL "")
    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "vcpkg 매니페스트 모드" FORCE)
    set(VCPKG_MANIFEST_INSTALL OFF CACHE BOOL "매니페스트에서 vcpkg 자동 설치" FORCE)
    message(STATUS "[vcpkg] Installed tree present (no manifest hash) — VCPKG_MANIFEST_INSTALL=OFF")
elseif(tree_ready)
    # 스탬프가 다르다(이 매니페스트가 바뀌었거나, 설치 폴더를 나눠 쓰는 다른 워크트리가 덮었다). 해시만으로 설치하지 않고 계획을 본다.
    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "vcpkg 매니페스트 모드" FORCE)
    sw_vcpkg_plan_manifest_install("${src_dir}" plan_status plan_remove plan_build)

    if(NOT plan_status STREQUAL "OK")
        set(VCPKG_MANIFEST_INSTALL ON CACHE BOOL "매니페스트에서 vcpkg 자동 설치" FORCE)
        message(STATUS "[vcpkg] Manifest changed or stamp missing — could not plan the install, install enabled")
    elseif(plan_remove)
        # 지우게 되면 멈춘다 — 다른 워크트리(최신 main)가 쓰는 포트일 수 있다. 빠진 포트는 find_package 에서 드러난다.
        set(VCPKG_MANIFEST_INSTALL OFF CACHE BOOL "매니페스트에서 vcpkg 자동 설치" FORCE)
        set_property(GLOBAL PROPERTY SW_VCPKG_STAMP_HELD TRUE)
        message(WARNING
            "[vcpkg] Install skipped: this manifest would remove ports from the shared installed tree:\n"
            "  ${plan_remove}\n"
            "Another worktree (or main) uses them. Merge the latest main (vcpkg.json) into this worktree, "
            "or configure with -DSW_VCPKG_FORCE_INSTALL=ON if removing them is intended.")
    elseif(plan_build)
        set(VCPKG_MANIFEST_INSTALL ON CACHE BOOL "매니페스트에서 vcpkg 자동 설치" FORCE)
        message(STATUS "[vcpkg] Manifest needs ports that are not installed (${plan_build}) — install enabled")
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
if(DEFINED SW_VCPKG_ROOT AND NOT SW_VCPKG_ROOT STREQUAL "")
    set(VCPKG_ROOT "${SW_VCPKG_ROOT}" CACHE PATH "vcpkg 루트 디렉터리" FORCE)
    set(toolchain "${SW_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

    if(EXISTS "${toolchain}")
        set(CMAKE_TOOLCHAIN_FILE "${toolchain}" CACHE FILEPATH "vcpkg 툴체인" FORCE)
    endif()
endif()

# Preset이 없는 Tools/vcpkg 경로를 박아 둔 경우, project() 전에 원인을 명확히 끊습니다.
set(toolchain_file "${CMAKE_TOOLCHAIN_FILE}")

if(SW_USE_VCPKG AND toolchain_file AND NOT EXISTS "${toolchain_file}")
    if(SW_VCPKG_AUTO_BOOTSTRAP)
        message(FATAL_ERROR
            "[vcpkg] CMAKE_TOOLCHAIN_FILE does not exist:\n"
            "  ${toolchain_file}\n"
            "Auto-bootstrap was enabled but Tools/vcpkg is still missing.\n"
            "  Check that Git is installed, then reconfigure, or run:\n"
            "    py -3 Scripts/setup/vcpkg.py --install"
        )
    else()
        message(FATAL_ERROR
            "[vcpkg] CMAKE_TOOLCHAIN_FILE does not exist:\n"
            "  ${toolchain_file}\n"
            "Enable SW_VCPKG_AUTO_BOOTSTRAP / search_paths.vcpkg_auto_bootstrap, or run:\n"
            "    py -3 Scripts/setup/vcpkg.py --install"
        )
    endif()
endif()

set(canonical_vcpkg "${CMAKE_SOURCE_DIR}/Tools/vcpkg")
cmake_path(NORMAL_PATH canonical_vcpkg)

if(DEFINED SW_VCPKG_ROOT AND NOT SW_VCPKG_ROOT STREQUAL "")
    set(resolved_vcpkg "${SW_VCPKG_ROOT}")
    cmake_path(NORMAL_PATH resolved_vcpkg)

    if(NOT resolved_vcpkg STREQUAL canonical_vcpkg)
        message(STATUS
            "[vcpkg] Using ${resolved_vcpkg} "
            "(project default would be ${canonical_vcpkg})"
        )
    endif()
endif()

# ------------------------------------------------------------------------------
# 7) sw_toolchain_vcpkg INTERFACE — SW_VCPKG 정의만
# SW_FLAG_LIBRARIES에 넣어 sw_add_* 타겟이 PRIVATE 링크
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

if(NOT sw_toolchain_vcpkg IN_LIST SW_FLAG_LIBRARIES)
    list(APPEND SW_FLAG_LIBRARIES sw_toolchain_vcpkg)
endif()
