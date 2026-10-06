# ==============================================================================
# @file ThirdParty/cgltf/CMakeLists.txt
# @brief cgltf — 헤더 하나짜리 glTF 2.0 파서(에디터의 모델 임포터). CMake 설정이 없어 vcpkg include 를 붙인 INTERFACE 타깃으로 쓴다.
#        구현(`CGLTF_IMPLEMENTATION`)은 `Source/Editor/Common/Asset/ModelImporter.cpp` 한 곳에서 펼친다.
# ==============================================================================

sw_addVcpkgHeaderOnly(NAME cgltf PROBE cgltf.h)
