# ==============================================================================
# @file Source/GameFramework/CMakeLists.txt
# @brief 장르 비의존 게임플레이 프레임워크 (Dev: SHARED / Shipping: STATIC)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) GameFramework — 모듈 라이브러리 기본값은 sw_addModuleLibrary(cmake/Engine/ModuleTargets.cmake). Kits/ 는 키트마다 따로 짓는다.
# ------------------------------------------------------------------------------
sw_skipInactiveModule(GameFramework swSkipGameFramework)
if(swSkipGameFramework)
	return()
endif()

sw_globSourceTree(listGameFrameworkSource RECURSE "*.cpp" "*.c" "*.h" "*.hpp")
list(FILTER listGameFrameworkSource EXCLUDE REGEX "Kits/")
# 에디터 확장(Plugin/Editor/)은 따로 짓는 모듈이다(GF_EditorPlugin_<이름>) - GameFramework DLL(배포본 포함)에 섞지 않는다.
list(FILTER listGameFrameworkSource EXCLUDE REGEX "Plugin/")
# 클라이언트 전용 소스(Client/ — HUD · 마커 · 로딩 화면)는 전용 서버가 짓지 않는다(cmake/Engine/ExcludedSources.cmake).
# 서버 전용 소스(Server/ — 온라인 서버 조립, 서비스 호스트 · 메모리 저장소 · 캐시 · 버스 구현)는 클라이언트 구성이 짓지 않는다.
if(SW_TARGET_TYPE STREQUAL "Server")
	sw_removeExcludedSources(listGameFrameworkSource ${SW_CLIENT_ONLY_SOURCE_PATTERNS})
elseif(SW_TARGET_TYPE STREQUAL "Client")
	sw_removeExcludedSources(listGameFrameworkSource ${SW_SERVER_ONLY_SOURCE_PATTERNS})
endif()

# 리플렉션 대상 헤더는 소스와 **같은 목록**에서 고른다. 주의: 폴더를 이름으로 적어 모으면 최상위 폴더를 하나 더 만들 때 그 안의 REFLECT() 타입이
# 조용히 등록되지 않는다 — 컴파일은 되고 역직렬화만 실패하므로 찾기 어렵다. (자동 훑기를 쓰지 않는 이유: 폴더 재귀가 Kits/ 까지 내려간다.)
set(listGameFrameworkHeader ${listGameFrameworkSource})
list(FILTER listGameFrameworkHeader INCLUDE REGEX "\\.(h|hpp)$")

# ------------------------------------------------------------------------------
# 1-1) 층 — Runtime 의 파일마다 층(Config/Rules/Layers/GameFrameworkLayers.toml → configure 가 GameFrameworkLayers.cmake 로)을 정하고, 층의 .cpp 는
#      아래 2) 의 층 부분(`GameFrameworkLayer<N>`)이 짓는다. GameFramework 타깃 자신은 헤더 · 리플렉션 산출물 · Runtime 밖(Client · 루트) 소스만 든다.
# ------------------------------------------------------------------------------
include("${CMAKE_BINARY_DIR}/generated/sw/config/GameFrameworkLayers.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Config/Rules/Layers/GameFrameworkLayers.toml")
set(listGameFrameworkLayerSource "")
foreach(tier IN LISTS SW_GAMEFRAMEWORK_TIER_LIST)
	# 이 구성이 짓는 것(listGameFrameworkSource — 서버 · 클라이언트 전용을 뺀 것)만 남긴다.
	list(TRANSFORM SW_GAMEFRAMEWORK_TIER_${tier}_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/" OUTPUT_VARIABLE listTierSource)
	set(listTierExcluded ${listTierSource})
	list(REMOVE_ITEM listTierExcluded ${listGameFrameworkSource})
	if(listTierExcluded)
		list(REMOVE_ITEM listTierSource ${listTierExcluded})
	endif()
	list(APPEND listGameFrameworkLayerSource ${listTierSource})
	# 개발 전용 소스는 모듈 라이브러리(sw_addModuleLibrary)가 빼는 것과 같게 부분에서도 뺀다.
	if(SW_SHIPPING_BUILD)
		sw_removeExcludedSources(listTierSource ${SW_DEV_ONLY_SOURCE_PATTERNS})
	endif()
	set(swGameFrameworkTierSource_${tier} ${listTierSource})
endforeach()
set(listGameFrameworkModuleSource ${listGameFrameworkSource})
if(listGameFrameworkLayerSource)
	list(REMOVE_ITEM listGameFrameworkModuleSource ${listGameFrameworkLayerSource})
endif()

sw_addModuleLibrary(GameFramework
	KIND gameframework
	DEV_TYPE SHARED
	EXPORTS GF
	LOG_TAG "GameFramework"
	FOLDER "Source/GameFramework"
	SOURCES ${listGameFrameworkModuleSource}
	LINK_PUBLIC Engine sw_public_source_includes
	REFLECTION_HEADERS ${listGameFrameworkHeader}
)
# 결정적 시뮬레이션 키트(`GF_MechArena` 의 판 걸음)가 부르는 Runtime 의 줄 밖 실수 함수 — 부르는 TU 만 표시하면 여기 축약이 구성마다 비트를 바꾼다
# (`sw_markDeterministicSources`). 격투(`GF_Fighting`)가 부르는 Runtime(FrameData · InputCommandBuffer · RoundSeries)는 실수 계산이 없어 넣지 않는다.
sw_markDeterministicSources(GameFramework
	Runtime/Character/Combat/Health/ResourceGauge.cpp
	Runtime/Character/Combat/Health/HealthStateMachine.cpp
	Runtime/Character/Combat/LockOnSelector.cpp
	Runtime/Character/Combat/Projectile/ProjectileDef.cpp
	Runtime/Character/Combat/Projectile/ProjectileSimulation.cpp
	Runtime/Character/Combat/Weapon/Weapon.cpp
	# 락스텝 RTS(`GF_RealTimeStrategy`)가 판 걸음 안에서 부르는 격자 길찾기 · 흐름장
	Runtime/Character/Navigation/FlowField.cpp
	Runtime/Character/Navigation/FlowFieldCache.cpp
	Runtime/Character/Navigation/GridPathSolver.cpp
	Runtime/Character/Navigation/GridPathfinder.cpp
	Runtime/Character/Navigation/GridReachability.cpp
	Runtime/Character/Navigation/NavGrid.cpp
)
# 로컬 저장의 장치 키(Runtime/Foundation/Framework/Save/Local/LocalDeviceKeyProvider)는 Windows DPAPI 로 감싼다.
if(WIN32)
	target_link_libraries(GameFramework PRIVATE crypt32)
endif()

# 네트워크 보안 — OpenSSL 3(TLS 1.3 · X25519 · AEAD · HKDF · Argon2id). 헤더는 Runtime/Online/Security/OpenSSL 에서만 include 하고
# 링크도 여기서만 한다 — Engine.dll 은 libssl · libcrypto 를 모른다. Core · 키트 · 게임은 Core/Network/Security/INetSecurityProvider.h 와
# Runtime/Online/Security/NetSecurity.h 만 본다. x64-windows 는 DLL 두 개(libssl-3-x64 · libcrypto-3-x64), 리눅스는 정적.
target_link_libraries(GameFramework PRIVATE openssl)

# ------------------------------------------------------------------------------
# 2) 층 부분 — 층마다 OBJECT 라이브러리 `GameFrameworkLayer<N>`(오브젝트는 GameFramework 이미지 안, `sw_addModulePart`). 부분 N 은 N 보다 위 층의 헤더마다
#    같은 경로의 `#error` 껍데기를 자기 include 자리 맨 앞에 둔다(`sw_addLayerStubs`) — 엔진 층과 같은 방식이다.
#    GameFramework 설정(정의 · 링크 · 결정성 표시)이 모두 끝난 뒤에 둔다 — 부분은 그 값을 구성 때 복사한다.
# ------------------------------------------------------------------------------
# 링크 목록은 부분을 만들기 전에 한 번 읽는다 — `sw_addModulePart` 가 부분을 GameFramework 에 링크하므로 뒤에 읽으면 앞 부분이 섞인다.
get_target_property(swGameFrameworkLinkLibraries GameFramework LINK_LIBRARIES)
foreach(tier IN LISTS SW_GAMEFRAMEWORK_TIER_LIST)
	if(NOT swGameFrameworkTierSource_${tier})
		continue()   # 이 구성이 짓지 않는 층
	endif()
	sw_addModulePart(GameFrameworkLayer${tier}
		OWNER GameFramework
		SOURCES ${swGameFrameworkTierSource_${tier}}
		LINK ${swGameFrameworkLinkLibraries}
	)
	sw_addLayerStubs(GameFrameworkLayer${tier} ${tier} SW_GAMEFRAMEWORK_TIER GameFramework)
endforeach()

add_subdirectory(Kits)

# 에디터 확장 - EditorModule 과 GameFramework(또는 키트)를 링크하는 Dev 전용 SHARED 모듈(`sw_addEditorExtension`, 매니페스트 GF_EditorPlugin_<이름>.module.json)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Plugin/Editor/CMakeLists.txt")
	add_subdirectory(Plugin/Editor)
endif()
