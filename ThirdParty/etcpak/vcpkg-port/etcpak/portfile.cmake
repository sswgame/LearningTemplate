# 프로젝트 오버레이: vcpkg 기본 저장소에 etcpak 포트가 없다. 원래 빌드는 응용 프로그램이고 libpng · Tracy(CPM 으로 내려받는다) · -march=native 를 끌어와,
# 블록 압축 · 풀기 소스만 정적 라이브러리로 짓는 CMakeLists 를 넣는다(텍스처 쿠커 `TextureBlockEncoder` 가 쓴다).
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO wolfpld/etcpak
    REF ${VERSION}
    SHA512 be64eb61a2a0119858ed4548b2ed187c5bad572317b82ebf1090b9ff91b62728a6aa086c6fabc6d6b43f08eb90ced92b4985c6b558a1afa5607c20668906dec3
    HEAD_REF master
)

file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}/vcpkg-build")

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}/vcpkg-build")
vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(PACKAGE_NAME unofficial-${PORT} CONFIG_PATH share/unofficial-${PORT})

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
