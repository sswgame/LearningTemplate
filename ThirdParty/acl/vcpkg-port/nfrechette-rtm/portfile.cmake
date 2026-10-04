# ==============================================================================
# @file ThirdParty/acl/vcpkg-port/nfrechette-rtm/portfile.cmake
# @brief Realtime Math(헤더만) — ACL 이 include 하는 수학 라이브러리. vcpkg 내장 포트가 없어 오버레이로 둔다.
#        버전은 ACL v2.1.0 의 서브모듈(external/rtm)이 가리키는 v2.2.0 에 맞춘다.
# ==============================================================================

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO nfrechette/rtm
    REF v2.2.0
    SHA512 0aae699e5bb73132461402d83b119b834f650860e8cd82bb791d975d18c111ae4c0a5846ca99a89091c13628d4685e4a406c8c34d88b16472d431c9ccc9dd2e6
    HEAD_REF develop
)

file(INSTALL "${SOURCE_PATH}/includes/rtm" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
