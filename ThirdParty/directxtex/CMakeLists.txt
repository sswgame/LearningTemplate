# ==============================================================================
# @file ThirdParty/directxtex/CMakeLists.txt
# @brief DirectXTex — DDS · BC 텍스처 처리(에디터의 텍스처 파이프라인). EditorModule 과 EditorTest 가 `Microsoft::DirectXTex` 로 쓴다.
# ==============================================================================

# 한 번 찾고 GLOBAL 로 올린다(CMake 3.24). 예전에는 두 타겟의 CMakeLists 가 각자 find_package 해서 configure 마다 vcpkg 설정 스크립트를
# 두 번(0.24 s 씩) 돌렸다. `sw_addVcpkgConfigLib` 를 쓰지 않는 이유: 그 함수는 못 찾으면 빈 타겟으로 조용히 대신하는데, 여기는 원래
# REQUIRED 였다(없으면 configure 가 멈춰야 한다). EditorTest 는 Shipping 에서도 빌드되므로 조건 없이 찾는다.
find_package(directxtex CONFIG REQUIRED GLOBAL)
