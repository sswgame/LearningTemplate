# ==============================================================================
# @file ThirdParty/sheenbidi/CMakeLists.txt
# @brief SheenBidi 양방향 알고리즘(Apache 2.0 — 배포물에 고지 필요) — 복합 문자 빌드(SW_ENABLE_COMPLEX_TEXT)에만 붙는다.
#        헤더는 Source/Engine/Text/SheenBidi 에서만 include 한다. 임포트 타깃 이름은 포트판마다 다를 수 있어 후보를 둘 적는다(먼저 있는 것).
# ==============================================================================

if(NOT SW_ENABLE_COMPLEX_TEXT)
	return()
endif()

sw_addVcpkgPackage(
	NAME sheenbidi
	PACKAGE SheenBidi
	CONFIG_TARGET SheenBidi::SheenBidi sheenbidi::sheenbidi
)
