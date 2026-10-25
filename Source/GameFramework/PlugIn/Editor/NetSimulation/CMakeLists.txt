# ==============================================================================
# @file Source/GameFramework/PlugIn/Editor/NetSimulation/CMakeLists.txt
# @brief 한 프로세스 재생 에디터 확장 — Multiplayer 창의 In process 탭(서버 월드 1 + 클라이언트 월드 N 을 NetSimHarness 로 한 프로세스에서, 월드마다 미리보기 뷰포트) (Dev 전용, 매니페스트 GF_EditorPlugIn_NetSimulation.module.json)
# ==============================================================================

# 본보기 게임(Sample/)은 화면 없는 게임 로직이라 EngineTest 가 직접 컴파일한다 - 게임 목록에 올리는 일(NetSimSampleRegistration.cpp)은 확장 DLL 의 몫이라 뺀다.
sw_addEditorExtensionModel(GF_EditorPlugIn_NetSimulation SOURCES Sample/NetSimSampleGame.cpp)
sw_addEditorExtension(GF_EditorPlugIn_NetSimulation)
