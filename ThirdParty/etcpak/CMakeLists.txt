# ==============================================================================
# @file ThirdParty/etcpak/CMakeLists.txt
# @brief etcpak(BSD-3-Clause) — 텍스처 쿠커의 ETC2 RGBA8 · EAC R11 · RG11 인코딩과 그 풀기(`TextureBlockEncoder` · 테스트). 에디터만 링크한다(배포물에 싣지 않는다).
#        vcpkg 기본 저장소에 포트가 없어 오버레이 포트(`vcpkg-port/etcpak`)가 압축 · 풀기 소스만 정적 라이브러리로 짓는다.
# ==============================================================================

# 에디터를 짓지 않는 정적 모듈 구성(배포 · 웹 · Android)은 찾지 않는다 — vcpkg 도 windows | linux 에만 깐다.
if(SW_STATIC_MODULES)
	return()
endif()

sw_add_vcpkg_package(
	NAME etcpak
	PACKAGE unofficial-etcpak
	CONFIG_TARGET unofficial::etcpak::etcpak
)
