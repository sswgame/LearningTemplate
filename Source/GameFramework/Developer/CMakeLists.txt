# ==============================================================================
# @file Source/GameFramework/Developer/CMakeLists.txt
# @brief GameFramework 개발 전용 공유 모듈(GameFrameworkDev) — 언리얼 `DeveloperTool` 모듈 자리
# ==============================================================================

# ------------------------------------------------------------------------------
# GF 창구(`IGameSettingsOverride` · `ISnapshotFaultHook` — `SW_GF_API`, `StaticRegistrar`)의 개발 구현과 GF 의 개발 명령 · 디버그 HUD 섹션 · 감시 값 · 시나리오 단계를 정적 등록한다(GF 공개 API 만 본다). DevTools 는 GF 를 볼 수 없어
# (TargetLayers `devtools` 게이트) Runtime 안 `Dev/` 예외로 있던 것을 모듈 하나로 모았다(설계 2026-11-17 H1). 배포에서 빠지는 것은 폴더 패턴이 아니라 매니페스트다
# (`_listConfiguration: ["Dev"]` — `sw_skip_inactive_module` 이 짓지 않고 런타임 목록이 거른다). 종류는 GameFramework 라 개발 호스트가 GameFramework 다음에
# 공유 모듈로 올린다(`GameModuleManager` 의 `loadSharedModule`, 의존 순서). Runtime · 키트 · 게임은 이 폴더를 include 하지 못한다(TargetLayers).
# ------------------------------------------------------------------------------
sw_skip_inactive_module(GameFrameworkDev skip_game_framework_dev)
if(skip_game_framework_dev)
	return()
endif()

sw_glob_source_tree(list_game_framework_dev_source RECURSE "*.cpp" "*.h")
# GameFramework 와 같은 폴더 나눔이다 — 클라이언트 몫(Client/)은 전용 서버가, 서버 몫(Server/)은 클라이언트가 짓지 않는다.
sw_remove_other_target_sources(list_game_framework_dev_source)
sw_add_module_library(GameFrameworkDev
	KIND gameframework
	DEV_TYPE SHARED
	EXPORTS MODULE_SHARED # 내보내는 형식이 없다 — 정적 등록만 한다
	LOG_TAG "GameFrameworkDev"
	FOLDER "Source/GameFramework"
	SOURCES ${list_game_framework_dev_source}
	LINK_PUBLIC GameFramework
	NO_REFLECTION
)
