# ==============================================================================
# @file ThirdParty/imgui-node-editor/vcpkg-port/imgui-node-editor/portfile.cmake
# @brief vcpkg 내장 포트(imgui-node-editor 0.9.3#4)의 오버레이 — 패치 하나를 더 얹는다
#
# 왜 오버레이인가: 업스트림 `crude_json.cpp` 가 `std::terminate()` 를 부르면서 `<exception>` 을
# include 하지 않는다. 예전 표준 라이브러리는 `<string>` 같은 헤더가 그것을 딸려 주어 넘어갔지만
# libc++ 19+ 는 그런 전이 include 를 끊었고, GitHub `macos-14` 러너의 brew LLVM(23.x) 에서
# `no member named 'terminate' in namespace 'std'` 로 포트 빌드가 선다 — 그래서 macOS CI 는
# configure 단계(vcpkg install)에서 죽었다. 업스트림 vcpkg 에는 아직 고침이 없어
# (2026-09-21 기준 port-version 4) 여기서 `fix-missing-exception-include.patch` 를 더한다.
#
# 이 폴더의 나머지 파일(CMakeLists.txt · 다른 세 패치 · vcpkg.json)은 내장 포트를 그대로 복사한
# 것이다. `Vcpkg.cmake` 가 `ThirdParty/*/vcpkg-port` 를 `VCPKG_OVERLAY_PORTS` 로 올리고, 매니페스트
# 스탬프 해시에도 이 폴더의 portfile · vcpkg.json · *.patch 가 들어가므로 여기를 고치면 재설치된다.
# 업스트림이 같은 고침을 받으면 이 오버레이는 지운다.
# ==============================================================================

vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO thedmd/imgui-node-editor
    REF v${VERSION}
    SHA512 83573b6ed776095837373bc95be1c1f5b85e9c5fae2145647f9cb6fdc17d3889edce716ac9e27c1bbde56f00803a66db98ca856910e6e0ce8714d3c5ce3f7c3f
    HEAD_REF master
    PATCHES
        fix-vec2-math-operators.patch
        remove-getkeyindex.patch # GetKeyIndex() is a no-op since 1.87; see https://github.com/ocornut/imgui/issues/5979#issuecomment-1345349492
        fix-imgui-v1.92.5.patch
        fix-missing-exception-include.patch # std::terminate() needs <exception>; libc++ 19+ no longer includes it transitively
)

file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")
file(REMOVE_RECURSE "${SOURCE_PATH}/external/imgui")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS_DEBUG
        -DIMGUI_NODE_EDITOR_SKIP_HEADERS=ON
)

vcpkg_cmake_install()

vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(PACKAGE_NAME unofficial-${PORT} CONFIG_PATH share/unofficial-${PORT})

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
