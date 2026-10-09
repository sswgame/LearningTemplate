# ==============================================================================
# @file Source/GameFramework/Kits/Feature/Network/NetDestruction/CMakeLists.txt
# @brief 파괴 네트워킹 키트 — 사건(신뢰 순서 · 번호) · 덩어리 자세 보간 · 늦은 참가 스냅숏 · 해시 어긋남 바로잡기 (파괴를 쓰는 네트워크 게임만 링크)
# ==============================================================================

sw_addGameFrameworkKit(GF_NetDestruction)
# 사건 · 스냅숏 · 해시 대조를 만든다 — 구성마다 같은 비트여야 한다(엔진 Destruction/ 와 같은 이유, `sw_markDeterministicSources`).
sw_markDeterministicSources(GF_NetDestruction DestructionReplication.cpp)
