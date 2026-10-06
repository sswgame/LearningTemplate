# ==============================================================================
# @file ThirdParty/recastnavigation/CMakeLists.txt
# @brief Recast & Detour(zlib) — 3D 내비메시 생성 · 길찾기 · 군중. vcpkg 는 정적 라이브러리만 낸다(Recast · Detour · DetourCrowd ·
#        DetourTileCache · DebugUtils). 헤더는 엔진의 내비게이션 백엔드 폴더에서만 include 한다(엔진 쪽 인터페이스 뒤에 감춘다).
# ==============================================================================

sw_addVcpkgPackage(
	NAME recastnavigation
	PACKAGE recastnavigation
	CONFIG_TARGET RecastNavigation::Detour
)
if(TARGET RecastNavigation::Recast)
	target_link_libraries(recastnavigation INTERFACE RecastNavigation::Recast RecastNavigation::DetourCrowd RecastNavigation::DetourTileCache)
endif()
