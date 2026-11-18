# 프로젝트 오버레이: vcpkg 기본 저장소에 astc-encoder 포트가 없다. 원래 빌드는 명령줄 도구 · ISA 별 실행 파일을 짓고 라이브러리를 설치하지 않아,
# 코어 코덱 소스만 정적 라이브러리로 짓는 CMakeLists 를 넣는다(텍스처 쿠커 `TextureBlockEncoder` 가 쓴다).
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ARM-software/astc-encoder
    REF ${VERSION}
    SHA512 1dcaca86b22e95c49e3c44b94a5132683a2441c130f0e1bb79a9278467e101e4a6c6f8b00dacf62a2d268b2af9873c9153c826491ba8def83f2477bc0ad36277
    HEAD_REF main
)

file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}/vcpkg-build")

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}/vcpkg-build")
vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(PACKAGE_NAME unofficial-${PORT} CONFIG_PATH share/unofficial-${PORT})

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
