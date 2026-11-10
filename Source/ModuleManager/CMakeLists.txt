# ==============================================================================
# @file Source/ModuleManager/CMakeLists.txt
# @brief App · Server 가 같이 쓰는 모듈 관리(ModuleManager) 라이브러리 — 게임 · 에디터 모듈의 수명, 매니페스트 해석, 호스트 부팅 열(Host/)
# ==============================================================================

# ------------------------------------------------------------------------------
# ModuleManager — 모듈 관리(GameModuleManager · 매니페스트 해석 · HostLaunch). 플레이어 실행 파일(App)과 전용 서버(Server)가 같이 링크한다.
# include 경로는 `ModuleManager/...` 다.
#
# 개발 구성은 공유 라이브러리다 — 핫 리로드 · 호스트 개발 계층(DevTools.dll 의 `DevTools/ModuleReload` · `DevTools/Host`)이 이 라이브러리 위에 서서
# 창구(`ILiveReloadManager` · `IHostDevLayer` · `IServerDebugControl`)를 구현한다. 정적이면 호스트와 DevTools 가 한 벌씩 들어 정적 등록 · 상태가 둘이 된다
# (언리얼 Core 의 ModuleManager 가 DLL 이고 Developer 모듈 HotReload 가 그 위에 서는 자리). 의존은 DevTools → ModuleManager 한 방향이다(TargetLayers).
# 배포는 정적이다 — 모듈이 정적 링크라 갈아 끼울 것이 없고 DevTools 도 짓지 않는다. 개발 전용 폴더(`Dev/`)는 없다(테스트 `Tests/` 만 배포가 뺀다).
# ------------------------------------------------------------------------------
file(GLOB_RECURSE MODULE_HOST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

if(SW_SHIPPING_BUILD)
	sw_excludeUnbuiltSources(MODULE_HOST_SOURCES ${SW_DEV_ONLY_SOURCE_PATTERNS})
	set(swModuleManagerLibType STATIC)
else()
	set(swModuleManagerLibType SHARED)
endif()

add_library(ModuleManager ${swModuleManagerLibType} ${MODULE_HOST_SOURCES})
sw_applyTargetLayerGate(ModuleManager modulemanager)
set_target_properties(ModuleManager PROPERTIES FOLDER "Source/ModuleManager")
target_link_libraries(ModuleManager
	PUBLIC
		Engine
		ModuleAPI
	PRIVATE
		sw_global_options
)
target_compile_definitions(ModuleManager PRIVATE "SW_LOG_TAG=\"Engine\"" "SW_REGISTRAR_MODULE=\"ModuleManager\"" SW_APP_INTERNAL)
sw_configureDLLExports(ModuleManager ${swModuleManagerLibType} MODULEMANAGER)
sw_configurePCH(ModuleManager "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
