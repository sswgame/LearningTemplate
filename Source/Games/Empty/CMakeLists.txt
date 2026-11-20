# ==============================================================================
# @file Source/Games/Empty/CMakeLists.txt
# @brief 엔진 기능 쇼케이스 · 자동화 시험대인 최소 SWGame — 키트는 콘텐츠가 쓰는 것만 매니페스트에 둔다(콘텐츠가 쓰는 컴포넌트의 모듈은 게임 의존이다, 언리얼 .uproject Plugins)
# ==============================================================================

sw_add_game_module(SWGame)

# 핫 리로드 확인 판 머리 — 시나리오 단계 `<RebuildModule variant>`(Config/Dev/ModuleRebuildVariants.json)가 덮고 시나리오 끝에 되돌린다. 추적 소스를 고치지 않으려고
# 빌드 트리에 둔다. `file(CONFIGURE)` 는 글이 다를 때만 쓴다 — 중단된 실행이 남긴 판도 다음 구성이 기본 글로 되돌린다(같으면 다시 짓지 않는다).
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/sw/dev/HotReloadProbeVariant.h"
	CONTENT "#pragma once\n#define SW_HOTRELOAD_PROBE_STEP 0\n")
