# ==============================================================================
# @file ThirdParty/implot/CMakeLists.txt
# @brief ImPlot — Editor 전용 (런타임 경로로 전파하지 않음)
# ==============================================================================

sw_addVcpkgStaticLib(
	NAME implot
	HEADER implot.h
	LIB_BASENAME implot
	WARN "[implot] vcpkg package not found — Editor plot panels will fail to link"
)
