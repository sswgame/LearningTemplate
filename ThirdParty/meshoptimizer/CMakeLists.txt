# ==============================================================================
# @file ThirdParty/meshoptimizer/CMakeLists.txt
# @brief meshoptimizer — 인덱스 버퍼 정점 캐시 · 오버드로 최적화(에디터의 모델 임포터). EditorModule 과 EditorTest 가 링크한다.
# ==============================================================================

sw_addVcpkgConfigLib(
	NAME meshoptimizer
	PACKAGE meshoptimizer
	CONFIG_TARGET meshoptimizer::meshoptimizer
)
