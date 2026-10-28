# ==============================================================================
# @file Test/EngineTest/CMakeLists.txt
# @brief Engine 단위 테스트 — Engine DLL 의 GPU · 창 없이 도는 부분(순수 식 · 파일 시스템 · 형식). 파일은 소스 경로를 따른다(Engine/Graphics/LOD/X → Graphics/LOD/TestX.cpp).
# ==============================================================================

sw_addTestExecutable(EngineTest LINK Engine LABELS nogpu)
