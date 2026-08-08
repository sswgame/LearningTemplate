# ==============================================================================
# @file cmake/internal/VcpkgManifestStamp.cmake
# @brief project() 이후: installed 트리가 있으면 현재 manifest 해시를 스탬프에 기록
# ==============================================================================

if(NOT SW_USE_VCPKG)
    return()
endif()

if(NOT DEFINED VCPKG_INSTALLED_DIR OR NOT DEFINED VCPKG_TARGET_TRIPLET)
    return()
endif()

set(_sw_vcpkg_tree "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
if(NOT EXISTS "${_sw_vcpkg_tree}")
    return()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/VcpkgManifestHash.cmake")
sw_vcpkg_compute_manifest_hash(_sw_manifest_hash)

if(_sw_manifest_hash STREQUAL "")
    return()
endif()

set(_sw_stamp "${VCPKG_INSTALLED_DIR}/.sw_vcpkg_manifest_sha")
file(WRITE "${_sw_stamp}" "${_sw_manifest_hash}")
message(STATUS "[vcpkg] Manifest stamp updated (${VCPKG_TARGET_TRIPLET})")
