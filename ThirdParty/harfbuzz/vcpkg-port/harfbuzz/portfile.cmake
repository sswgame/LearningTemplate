# ==============================================================================
# @file ThirdParty/harfbuzz/vcpkg-port/harfbuzz/portfile.cmake
# @brief vcpkg 내장 harfbuzz 포트(14.5.1, meson)와 같은 소스를 CMake 로 짓는다.
#        이유: meson 1.9 는 clang-cl 링커를 이름 `lld-link` 로 실행하는데, vcpkg 가 포트 빌드의 PATH 를 걸러 LLVM bin 이 빠져 구성이 서지 못한다
#        (2026-10-22 확인 — `FileNotFoundError` 로 lld-link 를 못 찾음). triplet 에서 PATH 를 고치면 triplet 해시가 바뀌어 모든 포트가 다시 빌드된다.
#        HarfBuzz 의 CMake 빌드는 커뮤니티 유지 지원이지만 셰이퍼 코어(hb-shape)만 필요한 이 저장소에는 충분하다: 이 포트는 기능을 두지 않고
#        FreeType · ICU · GLib · 부분 집합 · 래스터 · 벡터 · GPU · 유틸리티를 모두 끈다(폰트 바이트로 자기 면을 만든다 — Source/Engine/Text/HarfBuzz).
#        업스트림 meson 빌드가 clang-cl 에서 서거나 vcpkg 가 포트 PATH 를 고치면 이 오버레이를 지운다.
# ==============================================================================

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO harfbuzz/harfbuzz
    REF "${VERSION}"
    SHA512 18cd352c3879c8b44f150c9a3171f3790c3c641e161ae1f5591c8156cf8c551aa45c7873cb6ddc9138e2fdc6c331d6812c21e6a210eb1385dd2813a738317593
    HEAD_REF master
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DHB_BUILD_UTILS=OFF
        -DHB_BUILD_SUBSET=OFF
        -DHB_BUILD_RASTER=OFF
        -DHB_BUILD_VECTOR=OFF
        -DHB_BUILD_GPU=OFF
        -DHB_HAVE_FREETYPE=OFF
        -DHB_HAVE_GLIB=OFF
        -DHB_HAVE_ICU=OFF
        -DHB_BUILD_GPU_DEMO=OFF
        -DHB_HAVE_CAIRO=OFF
        -DHB_HAVE_CORETEXT=OFF
        -DHB_HAVE_DIRECTWRITE=OFF
        -DHB_HAVE_GDI=OFF
        -DHB_HAVE_UNISCRIBE=OFF
        -DHB_HAVE_GRAPHITE2=OFF
        -DHB_HAVE_GOBJECT=OFF
        -DHB_HAVE_INTROSPECTION=OFF
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME harfbuzz CONFIG_PATH lib/cmake/harfbuzz)
vcpkg_fixup_pkgconfig()
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
