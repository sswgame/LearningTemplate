# ==============================================================================
# @file ThirdParty/acl/vcpkg-port/nfrechette-acl/portfile.cmake
# @brief Animation Compression Library(헤더만). vcpkg 의 내장 `acl` 은 이름만 같은 POSIX 접근 제어 목록 라이브러리라
#        다른 이름의 오버레이로 둔다(내장 `acl` 을 가리면 그것에 기대는 리눅스 포트가 깨진다).
# ==============================================================================

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO nfrechette/acl
    REF v2.1.0
    SHA512 50f49cda92acf135e316919599332c96f04c92747986153454e067db95c6d7d06951007e7f48b42084cf389ef09c5210297e76a2a5b7bd86c1551b74093bdc03
    HEAD_REF develop
)

file(INSTALL "${SOURCE_PATH}/includes/acl" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
