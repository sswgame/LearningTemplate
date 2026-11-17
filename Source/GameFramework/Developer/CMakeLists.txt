# ==============================================================================
# @file Source/GameFramework/Developer/CMakeLists.txt
# @brief GameFramework 개발 전용 공유 모듈(GameFrameworkDev) — 언리얼 `DeveloperTool` 모듈 자리
# ==============================================================================

# ------------------------------------------------------------------------------
# GF 창구(`IGameSettingsOverride` · `ISnapshotFaultHook` — `SW_GF_API`, `StaticRegistrar`)에 개발 구성만의 구현을 정적 등록한다. DevTools 는 GF 를 볼 수 없어
# (TargetLayers `devtools` 게이트) Runtime 안 `Dev/` 예외로 있던 것을 모듈 하나로 모았다(설계 2026-11-17 H1). 배포에서 빠지는 것은 폴더 패턴이 아니라 매니페스트다
# (`_listConfiguration: ["Dev"]` — `sw_skip_inactive_module` 이 짓지 않고 런타임 목록이 거른다). 종류는 GameFramework 라 개발 호스트가 GameFramework 다음에
# 공유 모듈로 올린다(`GameModuleManager` 의 `loadSharedModule`, 의존 순서). Runtime · 키트 · 게임은 이 폴더를 include 하지 못한다(TargetLayers).
# ------------------------------------------------------------------------------
sw_skip_inactive_module(GameFrameworkDev skip_game_framework_dev)
if(skip_game_framework_dev)
	return()
endif()

sw_glob_source_tree(list_game_framework_dev_source RECURSE "*.cpp" "*.h")
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
