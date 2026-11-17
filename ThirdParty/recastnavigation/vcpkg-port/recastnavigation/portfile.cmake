# ==============================================================================
# @file ThirdParty/recastnavigation/vcpkg-port/recastnavigation/portfile.cmake
# @brief vcpkg 내장 recastnavigation 포트(1.6.0#1)와 같고, `RECASTNAVIGATION_DT_POLYREF64=ON` 만 다르다.
#        내장 포트는 32 비트 폴리 참조(타일 · 폴리 22 비트, 세대 10 비트)로 지어 타일 자리가 2^14 를 넘는 월드가 서지 않았다(감사 S-6).
#        켜면 Detour 가 64 비트 참조(세대 16 · 타일 28 · 폴리 20 비트)를 쓰고, 공개 컴파일 정의(DT_POLYREF64)가 내보낸 대상에 실려 엔진도 같은 폭으로 짓는다
#        (엔진 쪽 `RecastQueryUtil.h` 의 static_assert 가 확인한다). 언리얼도 64 비트 참조(`NavNodeRef`)로 짓는다.
#        참조 폭은 타일 바이트(링크 배열)를 바꾼다 — 쿠킹본의 백엔드 판(`NavMeshBackend::getBackendFormatVersion`)에 실려 옛 쿠킹본은 거절된다.
#        업스트림 포트가 옵션을 기능으로 내주거나 vcpkg 를 올릴 때 이 오버레이를 다시 본다.
# ==============================================================================
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO recastnavigation/recastnavigation
    REF v${VERSION}
    SHA512 7567aaa78219cc490a6f76210fba1f130f0c17aeaa06432ab1207e0fd03404abe31042e8b03971aa0d04ad65d39469f13575fe0072fb920c38581d39568b70fb
    HEAD_REF master
    PATCHES
        fix-detail-mesh-edge-detection.patch #Upstream fix https://github.com/recastnavigation/recastnavigation/pull/657
)
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DRECASTNAVIGATION_DEMO=OFF
        -DRECASTNAVIGATION_TESTS=OFF
        -DRECASTNAVIGATION_EXAMPLES=OFF
        -DRECASTNAVIGATION_DT_POLYREF64=ON
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/recastnavigation)

vcpkg_fixup_pkgconfig()

vcpkg_copy_pdbs()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/License.txt")
