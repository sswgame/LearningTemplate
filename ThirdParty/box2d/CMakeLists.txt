# ==============================================================================
# @file ThirdParty/box2d/CMakeLists.txt
# @brief Box2D v3 — 2D 물리 백엔드. 헤더는 `Source/Engine/Physics/Box2D/` 에서만 include 한다(엔진 쪽 인터페이스 뒤에 감춘다).
# ==============================================================================

sw_addVcpkgConfigLib(
	NAME box2d
	PACKAGE box2d
	CONFIG_TARGET box2d::box2d
)
