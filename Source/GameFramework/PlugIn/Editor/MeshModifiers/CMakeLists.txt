# ==============================================================================
# @file Source/GameFramework/PlugIn/Editor/MeshModifiers/CMakeLists.txt
# @brief 메시 모디파이어 스택 에디터 확장 — 모디파이어 스택 패널 · 변형 미리보기 · 사이드카 저장 · 굽기 (Dev 전용, 매니페스트 GF_EditorPlugIn_MeshModifiers.module.json)
# @details 모디파이어는 Modifiers/ 아래 .cpp 하나씩이고 GLOB_RECURSE 로 모인다. 순수 로직(Stack/ · Modifiers/)은 EngineTest 가 직접 컴파일한다.
# ==============================================================================

sw_addEditorExtension(GF_EditorPlugIn_MeshModifiers)
