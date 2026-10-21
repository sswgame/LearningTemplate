# ==============================================================================
# @file ThirdParty/harfbuzz/CMakeLists.txt
# @brief HarfBuzz 셰이퍼(MIT) — 복합 문자 빌드(SW_ENABLE_COMPLEX_TEXT)에만 붙는다. 헤더는 Source/Engine/Text/HarfBuzz 에서만 include 한다.
#        폰트 바이트로 자기 면을 만들므로 FreeType 연동 기능(hb-ft)은 쓰지 않는다(vcpkg.json 에서 기본 기능을 끈다).
# ==============================================================================

if(NOT SW_ENABLE_COMPLEX_TEXT)
	return()
endif()

sw_addVcpkgPackage(
	NAME harfbuzz
	CONFIG_TARGET harfbuzz::harfbuzz
)
