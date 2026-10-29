# ==============================================================================
# @file ThirdParty/directxtex/CMakeLists.txt
# @brief DirectXTex — DDS · BC 텍스처 처리(에디터의 텍스처 파이프라인). EditorModule 이 `Microsoft::DirectXTex` 로 쓴다.
# ==============================================================================

# 한 번 찾고 GLOBAL 로 올린다(CMake 3.24). 예전에는 두 타겟의 CMakeLists 가 각자 find_package 해서 configure 마다 vcpkg 설정 스크립트를
# 두 번(0.24 s 씩) 돌렸다. `sw_addVcpkgPackage` 로 감싸지 않는 이유: 쓰는 쪽이 vcpkg 타깃 이름(`Microsoft::DirectXTex`)을 그대로 쓰고
# 그 타깃을 GLOBAL 로 올려야 해서다. 에디터가 없는 배포 구성은 찾지 않는다(개발 구성은 REQUIRED — 없으면 configure 가 멈춘다).
if(NOT SW_SHIPPING_BUILD)
	find_package(directxtex CONFIG REQUIRED GLOBAL)
endif()
