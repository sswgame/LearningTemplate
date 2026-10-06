# ==============================================================================
# @file Test/CoreTest/CMakeLists.txt
# @brief Core 단위 테스트
# ==============================================================================

sw_addTestExecutable(CoreTest
	LABELS "unit;core;nogpu"
	TIMEOUT 60
	# 스트림 전송 · 보안 · 저장 계약 시험이 들어온 뒤 한 실행이 부하 없는 이 PC 에서 27 초라 30 초 한도에 붙었다 — 두 조각으로 가른다
	# (`CoreTest_Shard1` · `_Shard2`, 케이스는 스위트 안에서 번갈아 나뉜다).
	SHARDS 2
)
