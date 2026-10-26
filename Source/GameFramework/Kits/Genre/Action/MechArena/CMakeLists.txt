# ==============================================================================
# @file Source/GameFramework/Kits/Genre/Action/MechArena/CMakeLists.txt
# @brief 3인칭 팀 기체 대전 키트(SD건담 캡슐파이터 장르) — 기체 카탈로그 · 부스트 오버히트 · 다운치 · 록온 유도 · 근접 콤보 · 변형 · 스킬 · 코스트 게이지 · 상태 직렬화
# ==============================================================================

sw_addGameFrameworkKit(GF_MechArena)
if(NOT TARGET GF_MechArena)
	return()
endif()
# 판 걸음(`step`)과 상태 직렬화는 서버와 리플레이가 같은 입력으로 다시 도는 시뮬레이션이다 — 구성마다 같은 비트여야 한다(`sw_markDeterministicSources`).
# 카탈로그 읽기는 계산이 아니라 넣지 않는다.
sw_markDeterministicSources(GF_MechArena
	MechArenaWorld.cpp
	MechArenaWorldCombat.cpp
	MechArenaWorldState.cpp
	MechArenaWorldStep.cpp
)
