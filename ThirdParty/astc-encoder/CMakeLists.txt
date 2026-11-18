# ==============================================================================
# @file ThirdParty/astc-encoder/CMakeLists.txt
# @brief Arm astc-encoder(Apache-2.0) 코어 코덱 — 텍스처 쿠커의 ASTC 4x4 인코딩(`TextureBlockEncoder`). 에디터만 링크한다(배포물에 싣지 않는다).
#        vcpkg 기본 저장소에 포트가 없어 오버레이 포트(`vcpkg-port/astc-encoder`)가 코어 소스만 정적 라이브러리로 짓는다.
# ==============================================================================

# 에디터를 짓지 않는 정적 모듈 구성(배포 · 웹 · Android)은 찾지 않는다 — vcpkg 도 windows | linux 에만 깐다.
if(SW_STATIC_MODULES)
	return()
endif()

sw_add_vcpkg_package(
	NAME astcenc
	PACKAGE unofficial-astc-encoder
	CONFIG_TARGET unofficial::astc-encoder::astcenc
)
