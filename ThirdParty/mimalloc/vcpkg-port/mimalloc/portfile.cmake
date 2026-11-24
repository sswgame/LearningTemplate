# ==============================================================================
# @file ThirdParty/mimalloc/vcpkg-port/mimalloc/portfile.cmake
# @brief vcpkg 내장 mimalloc 포트(3.5.3)와 같고 아래 셋만 다르다. 엔진 힙(SW_MEMORY_BACKEND)과 Windows 의 Vulkan 검증 레이어가 쓴다.
#        1) 모든 triplet 에서 정적 라이브러리 — Windows DLL 은 `MI_OVERRIDE` 를 꺼도 mimalloc-redirect.dll 을 임포트해 프로세스의 ucrt malloc 을 통째로 갈아 끼운다.
#           Engine 이 그 DLL 을 링크하면 서드파티 힙까지 바뀐다. 언리얼도 mimalloc 을 정적으로 링크하고 엔진 입구(FMalloc)만 바꾼다. 배포할 DLL 도 없다.
#        2) Debug 는 `MI_DEBUG=FULL` 대신 `ON` — FULL 은 할당마다 힙 전체 불변식을 다시 봐 엔진 전체 할당을 얹을 수 없다. ON 은 이중 해제 · 넘쳐 쓰기(패딩)를
#           본다(CRT 디버그 힙 자리). 표본 보호 페이지 · 표본 프로파일러는 끈다 — 엔진이 쓰지 않는다(MemoryProfiler 가 센다).
#        3) `MI_NO_OPT_ARCH=ON` — arm64 기본값(-march=armv8.1-a)은 Android 10 의 armv8.0 기기에서 SIGILL 로 죽는다. 엔진(Architecture/ARM64.cmake)도 armv8.0 이다.
#        vcpkg 를 올릴 때 내장 포트와 다시 맞춘다(SHA512 · 패치).
# ==============================================================================
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO microsoft/mimalloc
    REF "v${VERSION}"
    SHA512 ef24d926153eb81c95e6c79c335eed12c6e14523a2dd817faddc3f666909a244eab5f4be43cc01a6d8c8d62b5695d29fb535b3762128cdef50a7ee55302b2886
    HEAD_REF dev3
    PATCHES
        pkgconfig-cxx.diff
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        override    MI_OVERRIDE
        secure      MI_SECURE
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DMI_USE_CXX=ON
        -DMI_BUILD_OBJECT=OFF
        -DMI_BUILD_TESTS=OFF
        -DMI_BUILD_STATIC=ON
        -DMI_BUILD_SHARED=OFF
        -DMI_INSTALL_TOPLEVEL=ON
        -DMI_NO_OPT_ARCH=ON
        -DMI_GUARDED=OFF
        -DMI_PROFILE=OFF
    OPTIONS_DEBUG
        -DMI_DEBUG=ON
)

vcpkg_cmake_install()

vcpkg_copy_pdbs()

file(COPY
    "${CMAKE_CURRENT_LIST_DIR}/vcpkg-cmake-wrapper.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/usage"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
)
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/mimalloc)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_fixup_pkgconfig()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
