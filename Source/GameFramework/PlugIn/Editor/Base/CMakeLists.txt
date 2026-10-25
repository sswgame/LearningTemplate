# ==============================================================================
# @file Source/GameFramework/PlugIn/Editor/Base/CMakeLists.txt
# @brief GameFramework 기반(장르 비의존)의 에디터 확장 — 기믹 회로 그래프 편집 창 · 어빌리티 런타임 상태 창 · 행동 트리 편집 창 (Dev 전용, 매니페스트 GF_EditorPlugIn_Base.module.json)
# ==============================================================================

# 화면 없는 편집 모델(기믹 회로 그래프 · 행동 트리 · 어빌리티 디버그 · NPC 일정 표 행)은 EngineTest 가 직접 컴파일한다.
sw_addEditorExtensionModel(GF_EditorPlugIn_Base SOURCES
	Gimmick/GimmickGraphModel.cpp
	Gimmick/GimmickGraphBridge.cpp
	AI/BehaviorTreeEditModel.cpp
	Ability/AbilityDebugModel.cpp
	Schedule/ScheduleTimelineRows.cpp)
sw_addEditorExtension(GF_EditorPlugIn_Base)
