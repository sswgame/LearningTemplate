# ==============================================================================
# @file ThirdParty/zlib/CMakeLists.txt
# @brief zlib — RFC 1950/1951 (deflate) 표준 구현
# @note 리소스 팩(.pack)의 압축 해제에 쓴다. 예전에는 ResourcePackReader 안에 자체
#       인플레이터를 들고 있었는데 동적 허프만을 지원하지 않는 등 온전하지 않았다.
# ==============================================================================

sw_addVcpkgConfigLib(
	NAME zlib
	PACKAGE ZLIB
	CONFIG_TARGET ZLIB::ZLIB
)
