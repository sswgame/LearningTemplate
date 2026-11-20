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
# 계획을 얻지 못하면 구성 오류 — 예전에는 그때 설치를 켜 공유 설치 폴더의 포트를 지울 수 있었다. OUT_REMOVE · OUT_BUILD: 지울 · 지을(다시 지을 포함) 포트 목록
# ------------------------------------------------------------------------------
function(sw_vcpkg_plan_manifest_install src_dir OUT_REMOVE OUT_BUILD)
    set(vcpkg_exe "${SW_VCPKG_ROOT}/vcpkg${CMAKE_HOST_EXECUTABLE_SUFFIX}")

    if(NOT DEFINED SW_VCPKG_ROOT OR SW_VCPKG_ROOT STREQUAL "" OR NOT EXISTS "${vcpkg_exe}")
        message(FATAL_ERROR "[vcpkg] Cannot plan the manifest install: vcpkg executable '${vcpkg_exe}' not found (SW_VCPKG_ROOT='${SW_VCPKG_ROOT}')")
    endif()

    set(args
        install --dry-run
        "--triplet=${VCPKG_TARGET_TRIPLET}"
        "--host-triplet=${VCPKG_HOST_TRIPLET}"
        "--x-install-root=${VCPKG_INSTALLED_DIR}"
        "--x-manifest-root=${src_dir}"
    )

    # 실제 설치(vcpkg 툴체인)와 같은 기능 집합으로 계획한다 — 빼면 선택 기능의 포트가 "지울 것" 으로 잡힌다.
    foreach(feature IN LISTS VCPKG_MANIFEST_FEATURES)
        list(APPEND args "--x-feature=${feature}")
    endforeach()

    if(VCPKG_MANIFEST_NO_DEFAULT_FEATURES)
        list(APPEND args "--x-no-default-features")
    endif()

    foreach(overlay_port IN LISTS VCPKG_OVERLAY_PORTS)
        list(APPEND args "--overlay-ports=${overlay_port}")
    endforeach()

    # VCPKG_OVERLAY_TRIPLETS 는 아래 4) 절이 늘 캐시에 정해 둔다.
    foreach(overlay_triplet IN LISTS VCPKG_OVERLAY_TRIPLETS)
        list(APPEND args "--overlay-triplets=${overlay_triplet}")
    endforeach()

    # 계획 읽기는 영어 머리 줄에 기댄다 — 언어를 고정한다(vcpkg 는 VSLANG 의 LCID 로 메시지 언어를 고른다, 1033 = en-US).
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env VSLANG=1033 "${vcpkg_exe}" ${args}
        WORKING_DIRECTORY "${src_dir}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )

    if(NOT result EQUAL 0)
        message(FATAL_ERROR "[vcpkg] Cannot plan the manifest install (vcpkg install --dry-run exited ${result}):\n${output}\n${error}")
    endif()

    sw_vcpkg_parse_install_plan("${output}" remove build)
    set(${OUT_REMOVE} "${remove}" PARENT_SCOPE)
    set(${OUT_BUILD} "${build}" PARENT_SCOPE)
endfunction()

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
    sw_vcpkg_plan_manifest_install("${src_dir}" plan_remove plan_build)

    if(plan_remove)
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
