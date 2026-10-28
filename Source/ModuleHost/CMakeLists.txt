# ==============================================================================
# @file Source/ModuleHost/CMakeLists.txt
# @brief App · Server 가 같이 쓰는 모듈 호스트(ModuleHost) 정적 라이브러리 빌드 스크립트
# ==============================================================================

# ------------------------------------------------------------------------------
# ModuleHost — 모듈 호스트(ModuleHost · LiveReloadManager · ModuleCompiler · 매니페스트 해석).
# 플레이어 실행 파일(App, Source/App)과 전용 서버(Server, Source/Server)가 같이 링크한다. include 경로는 `ModuleHost/...` 다.
# ------------------------------------------------------------------------------
file(GLOB_RECURSE MODULE_HOST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

# 핫 리로드는 Dev 전용이다. Shipping 은 모듈을 정적으로 링크하므로 갈아 끼울 것이 없다.
# 코드가 실행되지 않는 것만으로는 부족하고, 바이너리에서 아예 빠져야 한다.
# 빼는 것은 소스 검사 게이트에도 적힌다(`sw_excludeUnbuiltSources`).
#
# 핫 리로드 파일 목록은 여기 한 곳이 정본이다. 배포 제외 정규식도, SmokeTest 가 Dev 에서 가져가는 소스도(전역 속성
# `SW_MODULEHOST_HOT_RELOAD_SOURCES`) 이 목록에서 나온다 — 파일을 더하면 이 줄만 고친다.
set(swHotReloadNames
	LiveReloadManager ModuleDependencyGraph ModuleShadowCopy DeferredImageUnloadQueue
	ModuleImagePatch ModuleCallGuard ShadowCopyName)
set(swHotReloadSources "")
foreach(swName IN LISTS swHotReloadNames)
	list(APPEND swHotReloadSources "${CMAKE_CURRENT_SOURCE_DIR}/${swName}.cpp")
endforeach()
set_property(GLOBAL PROPERTY SW_MODULEHOST_HOT_RELOAD_SOURCES "${swHotReloadSources}")

if(SW_SHIPPING_BUILD)
	string(JOIN "|" swHotReloadAlternation ${swHotReloadNames})
	sw_excludeUnbuiltSources(MODULE_HOST_SOURCES "/Source/ModuleHost/(${swHotReloadAlternation})\\.(cpp|h)$")
endif()

add_library(ModuleHost STATIC ${MODULE_HOST_SOURCES})
set_target_properties(ModuleHost PROPERTIES FOLDER "Source/ModuleHost")
target_link_libraries(ModuleHost
	PUBLIC
		Engine
		RuntimeAPI
	PRIVATE
		sw_global_options
)
target_compile_definitions(ModuleHost PRIVATE "SW_LOG_TAG=\"Engine\"" SW_APP_INTERNAL)
sw_configurePCH(ModuleHost "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
