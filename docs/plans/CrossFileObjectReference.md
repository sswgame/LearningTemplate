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
| C3 | 오브젝트를 파일 사이로 옮겨도 이어지는 영속 GUID | 남음(조건부 — 오브젝트를 파일 사이에서 옮겨 같은 오브젝트로 이어야 할 때. 에디터에 파일 사이로 옮기는 명령은 없다 — `EditorSceneCommands::pasteObjects` · `duplicate` 는 새 오브젝트를 만들고 잘라내기는 없다). 2026-10-25 프리팹 경로를 읽었다: 프리팹 적용(`applyToPrefab`) · 되돌리기(`revertInstance`) · 다른 인스턴스 갱신(`updateInstance`)은 오브젝트를 그 자리에서 고치고 정체성(`captureIdentity`)을 지켜 파일을 넘지 않고, 풀기(`unlinkPrefab`)는 씬 안에서 부품을 엔티티로 올릴 뿐이다. 옮기는 곳은 하나다 — `adoptChildPrefabInstances`(새 프리팹 만들기, 개발 명령)가 씬 자식 인스턴스를 부품으로 넘기면 그 오브젝트는 씬 파일에 엔티티로 저장되지 않고 다시 스폰된다. 그래서 다시 스폰되는 부품은 `Scene::getOrAssignFileID` 가 0 을 돌려 참조를 만들 수 없게 막았다(끊어질 참조를 처음부터 못 쓰게 한다). 옮기기 전에 만든 참조는 `adoptChildPrefabInstances` 가 옮기기 전에 `SceneObjectReferenceUtil::findReferenceTo`(`SceneManager::collectAllScenes` 가 주는 로드된 씬 + 미리보기 씬의 `SceneObjectReference` 칸과 `vector<SceneObjectReference>` 같은 컨테이너 칸과 리플렉션 구조체 안에 든 칸까지 훑는다. `ReflectedValueWalk` 는 바이트로 복사하는 잎을 제자리에서 바꾸는 쓰기 도구라 문자열을 가진 이 타입을 못 받아 쓰지 않고, 같은 단계(구조체 · 시퀀스 · 맵 키와 값 · 중첩)를 읽기 전용 수집기 `ReferenceCollector` 로 내려간다. 구조체 타입은 `findReferenceTo` 가 필수 인자 `const TypeRegistry&` 로 받아 찾는다 — 레지스트리가 없어 구조체 안을 못 보고 통과하는 상태를 만들 수 없다. 호출처는 `editor::getSubsystem<TypeRegistry>()` 를 넘긴다(에디터는 `EngineServices.h` 를 포함하지 못한다). `adoptChildPrefabInstances` 도 씬 · 씬 매니저 · 작업 공간 · 루트 중 하나라도 얻지 못하면 검사를 건너뛰고 옮기지 않고, `AdoptResult::_rejectReason` 과 로그로 거절한다(`AdoptRejectsWhenReferenceCheckIsImpossible`). 훑지 않는 곳과 근거: ① 컴포넌트가 아닌 `GameObject` · `Scene` 수준 데이터 — 둘 다 리플렉션 프로퍼티가 없어(`SW_PROPERTY` 없음) `SceneObjectReference` 를 담을 자리가 없다, ② 프리팹 파일 안에 저장된 인스턴스 덮어쓰기 값 — 올라온 인스턴스는 덮어쓰기가 이미 컴포넌트 칸에 적용돼 로드된 컴포넌트로 훑리고, 내려가 있는 프리팹 파일의 XML 을 파싱해 보는 것은 큰 일이며 프리팹이 특정 씬 오브젝트를 가리키는 데이터가 아직 없다. 그런 데이터가 생기면 그때 더한다)로 찾아, 있으면 아무것도 옮기지 않고 어느 참조인지 적어 거절한다(`AdoptResult`, 개발 명령은 오류 응답). 같은 모양의 다른 에디터 명령은 `EditorSceneCommands` · 프리팹 편집 경로에서 찾지 못했다(`registerNestedPart` 를 부르는 에디터 코드는 이 함수뿐). 테스트: `SceneTest.NestedPartGetsNoFileIDWhileOrdinaryObjectsDo`, `EditorInspectorCommandsTest.*`. 참조를 따라가는 GUID 는 그런 참조를 쓰는 데이터가 생길 때 정한다 |
