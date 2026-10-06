# ==============================================================================
# @file ThirdParty/freetype/CMakeLists.txt
# @brief FreeType(FTL — 배포물 고지는 THIRD_PARTY_NOTICES). 글리프 윤곽 읽기 · SDF 래스터화를 Engine 의 글자 백엔드
#        (`Source/Engine/Text/FreeType`)만 쓴다 — 경계는 `CheckThirdPartyIsolation` 이 지킨다. 에디터 ImGui 는 imgui 포트가 같은 포트를 따로 링크한다.
#        vcpkg 설정 파일이 임포트 타깃 `freetype`(x64-windows 는 freetype.dll, x64-linux 는 정적)과 `Freetype::Freetype` 를 만든다.
# ==============================================================================

find_package(freetype CONFIG REQUIRED)

# 임포트 타깃은 이 디렉터리에만 보인다 — Engine 이 링크하므로 전역으로 올린다(openssl · tracy 와 같은 방식).
# 뒤에 오는 imgui 의 설정 파일은 이미 있는 전역 타깃을 보고 다시 만들지 않는다.
foreach(swFreetypeTarget IN ITEMS freetype Freetype::Freetype)
	if(NOT TARGET ${swFreetypeTarget})
		message(FATAL_ERROR "[ThirdParty] freetype config has no target ${swFreetypeTarget} — check build/vcpkg_installed/<triplet>/share/freetype")
	endif()
	set_target_properties(${swFreetypeTarget} PROPERTIES IMPORTED_GLOBAL TRUE)
endforeach()
message(STATUS "[ThirdParty] FreeType ${freetype_VERSION}")
