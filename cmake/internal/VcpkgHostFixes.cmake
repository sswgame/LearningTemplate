# ==============================================================================
# @file cmake/internal/VcpkgHostFixes.cmake
# @brief vcpkg 호스트 픽스 (Linux Vulkan loader symlink 등) — Scripts에 위임
# ==============================================================================

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

if(NOT DEFINED VCPKG_INSTALLED_DIR OR NOT DEFINED VCPKG_TARGET_TRIPLET)
    return()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/Python.cmake")

sw_execute_python_script(
    "Scripts/vcpkg/FixVcpkgVulkanLoader.py"
    ARGS
        "--vcpkg-installed-dir" "${VCPKG_INSTALLED_DIR}"
        "--triplet" "${VCPKG_TARGET_TRIPLET}"
    WARN
)
