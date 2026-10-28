# ==============================================================================
# @file Source/GameFramework/Plugin/Editor/CMakeLists.txt
# @brief GameFramework 자체와 여러 키트를 잇는 에디터 확장 모듈 모음 (Dev 전용 — 각 폴더가 매니페스트 GF_EditorPlugin_<이름>.module.json 하나와 모듈 하나). 키트 하나의 확장은 그 키트 폴더의 Editor/ 에 둔다(sw_addGameFrameworkKit 이 짓는다)
# ==============================================================================

add_subdirectory(Base)
add_subdirectory(AdminLogin)
add_subdirectory(MeshModifiers)
add_subdirectory(CameraPresets)
add_subdirectory(CharacterEditor)
