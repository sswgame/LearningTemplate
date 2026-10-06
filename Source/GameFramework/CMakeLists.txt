# ==============================================================================
# @file Source/GameFramework/CMakeLists.txt
# @brief 장르 비의존 게임플레이 프레임워크 (Dev: SHARED / Shipping: STATIC)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) GameFramework — 모듈 라이브러리 기본값은 sw_addModuleLibrary(cmake/Engine/TargetRules.cmake). Kits/ 는 키트마다 따로 짓는다.
# ------------------------------------------------------------------------------
sw_skipInactiveModule(GameFramework swSkipGameFramework)
if(swSkipGameFramework)
	return()
endif()

file(GLOB_RECURSE listGameFrameworkSource CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")
list(FILTER listGameFrameworkSource EXCLUDE REGEX "Kits/")

# 리플렉션 대상 헤더는 소스와 **같은 목록**에서 고른다. 주의: 폴더를 이름으로 적어 모으면 최상위 폴더를 하나 더 만들 때 그 안의 REFLECT() 타입이
# 조용히 등록되지 않는다 — 컴파일은 되고 역직렬화만 실패하므로 찾기 어렵다. (자동 훑기를 쓰지 않는 이유: 폴더 재귀가 Kits/ 까지 내려간다.)
set(listGameFrameworkHeader ${listGameFrameworkSource})
list(FILTER listGameFrameworkHeader INCLUDE REGEX "\\.(h|hpp)$")

sw_addModuleLibrary(GameFramework
	KIND gameframework
	DEV_TYPE SHARED
	EXPORTS GF
	LOG_TAG "GameFramework"
	FOLDER "Source/GameFramework"
	SOURCES ${listGameFrameworkSource}
	LINK_PUBLIC Engine sw_public_source_includes
	UNITY_BATCH 8
	REFLECTION_HEADERS ${listGameFrameworkHeader}
)
# 로컬 저장의 장치 키(Base/Online/Local/LocalDeviceKeyProvider)는 Windows DPAPI 로 감싼다.
if(WIN32)
	target_link_libraries(GameFramework PRIVATE crypt32)
endif()

add_subdirectory(Kits)
