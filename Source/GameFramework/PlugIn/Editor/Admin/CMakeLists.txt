# ==============================================================================
# @file Source/GameFramework/PlugIn/Editor/Admin/CMakeLists.txt
# @brief GM 도구 키트의 에디터 확장 — GM Tool 패널 (조회 · 지급 · 제재 · 감사 열람, 서버 없이도 도는 메모리 제공자) (Dev 전용, 매니페스트 GF_EditorPlugIn_Admin.module.json)
# ==============================================================================

# 화면 없는 부분(제공자 · 세션)은 EngineTest 가 직접 컴파일한다 - 확장 DLL 은 링크하지 않는다.
sw_addEditorExtensionModel(GF_EditorPlugIn_Admin SOURCES AdminCommandProvider.cpp AdminToolSession.cpp AdminToolConnection.cpp)
sw_addEditorExtension(GF_EditorPlugIn_Admin)
