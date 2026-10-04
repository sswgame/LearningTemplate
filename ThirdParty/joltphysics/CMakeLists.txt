# ==============================================================================
# @file ThirdParty/joltphysics/CMakeLists.txt
# @brief Jolt Physics — 3D 물리 백엔드. 헤더는 `Source/Engine/Physics/Jolt/` 에서만 include 한다(엔진 쪽 인터페이스 뒤에 감춘다).
#        vcpkg x64-windows 는 공유 라이브러리(JPH_SHARED_LIBRARY)이고, 설정 정의(JPH_USE_AVX2 등)는 임포트 타깃이 실어 준다.
# ==============================================================================

sw_addVcpkgConfigLib(
	NAME joltphysics
	PACKAGE Jolt
	CONFIG_TARGET Jolt::Jolt
)
