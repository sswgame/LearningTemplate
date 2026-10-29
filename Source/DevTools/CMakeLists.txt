# ==============================================================================
# @file Source/DevTools/CMakeLists.txt
# @brief 개발 도구 공유 라이브러리(DevTools.dll) — 배포 게임이 쓰지 않는 개발 기능(리로드 · OS 파일 감시 · 콘솔 · HUD · 회선 흉내)을 엔진 밖에 둔다.
# ==============================================================================

# ------------------------------------------------------------------------------
# DevTools — 엔진 확장점(기동 확장 델리게이트 · 프레임 훅 `EngineFrameHooks`)에 붙는다(`DevToolsHost`). 엔진은 이 단위를 모른다.
# 링크하는 쪽: 개발 App · Server(호스트 범위에 `DevToolsHost`), EditorModule(설정 · 에셋 감시), GameFramework 의 개발 감시.
# 늘 정의하되 `EXCLUDE_FROM_ALL` 이다 — 배포 호스트는 링크하지 않으므로 짓지 않는다(배포에서도 지어지는 에디터 테스트만 링크한다).
# 공유 라이브러리다 — 여러 이미지(App · EditorModule · GameFramework)가 링크하므로 정적이면 정적 등록이 이미지마다 돈다(언리얼 Developer 모듈도 DLL).
# include 경로는 `DevTools/...` 다.
# ------------------------------------------------------------------------------
file(GLOB_RECURSE DEVTOOLS_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")
sw_filterPlatformSources(DEVTOOLS_SOURCES) # OS 파일 감시기(`FileWatch/Windows` · `FileWatch/Linux`)

add_library(DevTools SHARED ${DEVTOOLS_SOURCES})
set_target_properties(DevTools PROPERTIES
	FOLDER "Source/DevTools"
	EXCLUDE_FROM_ALL ON)
target_link_libraries(DevTools
	PUBLIC
		Engine
	PRIVATE
		sw_global_options
)
target_compile_definitions(DevTools PRIVATE "SW_LOG_TAG=\"DevTools\"" SW_TOOL_INTERNAL) # 호스트 쪽 도구라 엔진 서비스 표를 직접 찾는다
sw_configureDLLExports(DevTools SHARED DEVTOOLS)
sw_configurePCH(DevTools "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
