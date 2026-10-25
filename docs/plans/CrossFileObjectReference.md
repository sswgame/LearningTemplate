# 씬 · 프리팹 파일을 넘는 오브젝트 참조

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: G-5 · 진행: 2/3

파일 안에서는 엔티티 `id` 로 가리키고, 파일을 넘는 데이터 · 코드가 0 이었습니다. 2026-10-22 사용자 결정으로 착수 조건(레벨 스트리밍 · 하위 레벨 · 다른 씬을 가리키는 데이터)을 갈음했습니다.

## 1. 정한 것

- 참조는 `SceneObjectReference`(`Engine/Scene/SceneObjectReference.h`) = (씬 리소스 경로, 그 파일의 엔티티 파일 id) 입니다. 언리얼 `FSoftObjectPath` 의 패키지 경로 + 하위 오브젝트 경로 자리입니다.
- 후보 절은 "오브젝트마다 영속 GUID" 를 적었지만 싣지 않았습니다. 파일 id 가 이미 그 파일 안의 영속 키입니다 — 한 번 준 id 는 오브젝트가 사라져도 다시 주지 않습니다(`Scene::computeNextFileID`, `collectSavedIDMap` 과 `getOrAssignFileID` 가 함께 씀). 그래서 씬 파일 형식을 바꾸지 않습니다. 오브젝트를 다른 파일로 옮기는 일(잘라 붙이기)은 새 오브젝트이며, 그것을 같은 오브젝트로 이어야 하면 그때 GUID 를 정합니다(C3).
- 해석 결과는 형식으로 가릅니다(`SceneObjectResolveResult`): `Resolved` · `Unset` · `SceneNotLoaded`(정상 — 하위 레벨 · 셀이 내려감) · `ObjectMissing`(데이터 결함 또는 내려간 셀). 경로 비교는 구분자 · 대소문자를 맞춥니다.
- 만들기 `SceneObjectReferenceUtil::makeReference` 는 저장한 적 없는(경로 없는) 씬을 거절합니다.

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| C1 | 참조 형식 · 해석 · `Scene::getOrAssignFileID` · `findObjectIDByFileID` · 테스트 `SceneTest.CrossSceneReferenceResolvesThroughFileID` | 끝(2026-10-23 Debug 빌드 경고 0, `SceneTest` 전체 통과) |
| C2 | 에디터 인스펙터에서 다른 씬의 오브젝트를 고르는 위젯(`SceneObjectReference` 칸)과 그 에디터 시나리오 | 끝(코드 · 시나리오, 실행 확인은 매니페스트 id `scene-reference-scenario`) — `SceneObjectReferencePropertyDrawer`(올라와 있는 씬의 오브젝트 콤보, 저장한 적 없는 씬은 이유를 보이고 건너뜀), 테스트 컴포넌트 `EditorSceneReferenceProbeComponent`, 탐침 `Editor.SelectedSceneReference`, 시나리오 `editor/scenereference` |
| C3 | 오브젝트를 파일 사이로 옮겨도 이어지는 영속 GUID | 남음(조건부 — 잘라 붙이기로 오브젝트를 파일 사이에서 옮기는 일이 생길 때. 지금은 파일 id 가 영속 키라 요구가 없다. 2026-10-25 코드 확인: 에디터에 파일 사이로 옮기는 명령이 없다 — `EditorSceneCommands::pasteObjects` · `duplicate` 는 새 오브젝트를 만들고 잘라내기는 없다. 그래서 C3 은 착수하지 않는다) |
