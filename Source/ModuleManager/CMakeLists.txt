# ==============================================================================
# @file Source/ModuleManager/CMakeLists.txt
# @brief App · Server 가 같이 쓰는 모듈 관리(ModuleManager) 정적 라이브러리 — 게임 · 에디터 모듈의 수명, 매니페스트 해석, 모듈 컴파일, 핫 리로드(LiveReload/)
# ==============================================================================

# ------------------------------------------------------------------------------
# ModuleManager — 모듈 관리(GameModuleManager · ModuleCompiler · 매니페스트 해석, LiveReload/ 는 Dev 전용 핫 리로드).
# 플레이어 실행 파일(App, Source/App)과 전용 서버(Server, Source/Server)가 같이 링크한다. include 경로는 `ModuleManager/...` 다.
# ------------------------------------------------------------------------------
file(GLOB_RECURSE MODULE_HOST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

# 핫 리로드는 Dev 전용이다. Shipping 은 모듈을 정적으로 링크하므로 갈아 끼울 것이 없다.
# 코드가 실행되지 않는 것만으로는 부족하고, 바이너리에서 아예 빠져야 한다.
# 빼는 것은 소스 검사 게이트에도 적힌다(`sw_excludeUnbuiltSources`).
#
# 핫 리로드는 `LiveReload/` 폴더 하나다 — Shipping 은 폴더째 뺀다(파일 이름을 적은 목록이 없다. 핫 리로드 파일은 그 폴더에 둔다).
if(SW_SHIPPING_BUILD)
	sw_excludeUnbuiltSources(MODULE_HOST_SOURCES "/Source/ModuleManager/LiveReload/.*\\.(cpp|h)$")
endif()

add_library(ModuleManager STATIC ${MODULE_HOST_SOURCES})
set_target_properties(ModuleManager PROPERTIES FOLDER "Source/ModuleManager")
target_link_libraries(ModuleManager
	PUBLIC
		Engine
		RuntimeAPI
	PRIVATE
		sw_global_options
)
target_compile_definitions(ModuleManager PRIVATE "SW_LOG_TAG=\"Engine\"" SW_APP_INTERNAL)
sw_configurePCH(ModuleManager "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
