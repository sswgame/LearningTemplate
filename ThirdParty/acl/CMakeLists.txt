# ==============================================================================
# @file ThirdParty/acl/CMakeLists.txt
# @brief Animation Compression Library + Realtime Math(둘 다 헤더만, 오버레이 포트 `nfrechette-acl` · `nfrechette-rtm`).
#        헤더는 `Source/Engine/Animation/Codec/ACL/` 에서만 include 한다(애니메이션 코덱 인터페이스 뒤에 감춘다).
# ==============================================================================

sw_addVcpkgHeaderOnly(NAME acl PROBE acl/version.h)
