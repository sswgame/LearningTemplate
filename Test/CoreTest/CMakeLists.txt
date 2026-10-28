# ==============================================================================
# @file Test/CoreTest/CMakeLists.txt
# @brief Core 단위 테스트 — Source/Core 만 링크한다(Engine 이상을 include 하면 링크되지 않는다). 파일은 소스 경로를 따른다(Core/String/X → String/TestX.cpp).
# ==============================================================================

sw_addTestExecutable(CoreTest LINK Core LABELS nogpu)
