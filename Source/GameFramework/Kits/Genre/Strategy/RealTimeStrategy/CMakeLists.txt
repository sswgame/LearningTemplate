# ==============================================================================
# @file Source/GameFramework/Kits/Genre/Strategy/RealTimeStrategy/CMakeLists.txt
# @brief 실시간 전략 키트(스타크래프트 장르) — 유닛 카탈로그 · 자원 · 보급 · 명령 · 채취 · 건설 · 생산 · 전투 · 전장의 안개 · AI · 1 대 1 한 판 디렉터(모습 · 선택 입력은 Client/)
# ==============================================================================

sw_add_game_framework_kit(GF_RealTimeStrategy)

# 락스텝 판 걸음(Playground 의 2 인 대전이 도는 1 대 1 한 판 `SkirmishMatch`)은 입력만 보내고 받는 쪽이 같은 계산을 한다 — 구성 · 묶음 경계와 상관없이 같은 비트여야 한다
# (`sw_mark_deterministic_sources` — Release 의 FMA 축약이 인라인 · 유니티 묶음에 따라 달라질 수 있다). 키트를 짓지 않는 구성(Debug)에는 타깃이 없다.
if(TARGET GF_RealTimeStrategy)
	sw_mark_deterministic_sources(GF_RealTimeStrategy RTSWorld.cpp RTSCommand.cpp RTSCommanderAI.cpp FogOfWar.cpp SkirmishMatch.cpp)
endif()
