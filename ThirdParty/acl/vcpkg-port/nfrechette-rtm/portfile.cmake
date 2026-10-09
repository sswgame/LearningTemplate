# ==============================================================================
# @file ThirdParty/acl/vcpkg-port/nfrechette-rtm/portfile.cmake
# @brief Realtime Math(헤더만) — ACL 이 include 하는 수학 라이브러리. vcpkg 내장 포트가 없어 오버레이로 둔다.
#        ACL v2.1.0 의 서브모듈(external/rtm)은 v2.2.0 을 가리키지만, 같은 2.x 판의 최신 v2.3.1 을 쓴다(ACL 이 쓰는 API 는 2.x 안에서 그대로다).
# ==============================================================================

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO nfrechette/rtm
    REF v2.3.1
    SHA512 95cea5b892d2ba02944a579fca912fc0eb68e74b1ca25fc13bb3584dc5be8b576279ae7f3a4f9b40928f38665f905099a854593eab95dc380e7c39e7c1008c50
    HEAD_REF develop
)

file(INSTALL "${SOURCE_PATH}/includes/rtm" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
