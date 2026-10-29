# ==============================================================================
# @file ThirdParty/meshoptimizer/CMakeLists.txt
# @brief meshoptimizer — 인덱스 버퍼 정점 캐시 · 오버드로 최적화(에디터의 모델 임포터). EditorModule 이 링크한다.
# ==============================================================================

sw_addVcpkgPackage(
	NAME meshoptimizer
	PACKAGE meshoptimizer
	CONFIG_TARGET meshoptimizer::meshoptimizer
)
