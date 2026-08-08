# ==============================================================================
# @file cmake/internal/VcpkgManifestHash.cmake
# @brief vcpkg.json (+ vcpkg-configuration.json) SHA256 결합 해시
# ==============================================================================

# sw_vcpkg_compute_manifest_hash(<out_var>)
function(sw_vcpkg_compute_manifest_hash OUT_VAR)
    set(_hash "")
    if(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg.json")
        file(SHA256 "${CMAKE_SOURCE_DIR}/vcpkg.json" _h1)
        string(APPEND _hash "${_h1}")
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg-configuration.json")
        file(SHA256 "${CMAKE_SOURCE_DIR}/vcpkg-configuration.json" _h2)
        string(APPEND _hash "${_h2}")
    endif()
    set(${OUT_VAR} "${_hash}" PARENT_SCOPE)
endfunction()
