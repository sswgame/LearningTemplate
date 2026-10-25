# ==============================================================================
# @file Source/GameFramework/PlugIn/Editor/CharacterEditor/CMakeLists.txt
# @brief 캐릭터 편집 에디터 확장 — 소켓 파일 편집 · 외형 선택 편집 모델과 도구 패널 (Dev 전용, 매니페스트 GF_EditorPlugIn_CharacterEditor.module.json)
# @details 화면 없는 모델(Model/)은 EngineTest 가 직접 컴파일한다(이 파일의 sw_addEditorExtensionModel 이 Model/*.cpp 를 올린다).
# ==============================================================================

# 화면 없는 편집 모델(Model/)은 폴더째 EngineTest 가 직접 컴파일한다.
sw_addEditorExtensionModel(GF_EditorPlugIn_CharacterEditor SOURCES Model/*.cpp)
sw_addEditorExtension(GF_EditorPlugIn_CharacterEditor)
