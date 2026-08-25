vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO TyomaVader/ImGuiNotify
    REF v0.0.3
    SHA512 99e9ddc205f4c8f679584272e355087f749da4cbdaa0f4b1bdbb390a763f191b0df08ec1474324e67c9f40f6376816e9294746eeb2fe97067aac864eebe654a7
    HEAD_REF master
)

# ImGuiNotify is header-only
file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/include")

file(GLOB_RECURSE NOTIFY_HEADERS "${SOURCE_PATH}/*ImGuiNotify.hpp" "${SOURCE_PATH}/*fa_solid_900.h" "${SOURCE_PATH}/*IconsFontAwesome6.h")
file(GLOB_RECURSE NOTIFY_FONTS "${SOURCE_PATH}/*fa-solid-900.ttf")

file(INSTALL ${NOTIFY_HEADERS} DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL ${NOTIFY_FONTS} DESTINATION "${CURRENT_PACKAGES_DIR}/include")

# Install copyright (use LICENSE file from source if exists)
if(EXISTS "${SOURCE_PATH}/LICENSE")
    vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
endif()
