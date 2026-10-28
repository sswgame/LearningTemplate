# 씬 · 프리팹 파일을 넘는 오브젝트 참조

> 상태: 구현중 · 선행: 없음 · 영역: engine · 진행: 2/3

한 씬 파일의 오브젝트가 다른 씬 파일의 오브젝트를 가리키는 참조다. 이전에는 파일 안에서만 엔티티 `id` 로 가리켰다. 형식 · 해석 · 에디터 위젯(C1 · C2)이 끝났고, 파일 사이를 옮겨도 이어지는 영속 GUID(C3)만 조건부로 남았다.

| 단계 | 내용 | 상태 |
|---|---|---|
| C1 | 참조 형식 · 해석 · `Scene::getOrAssignFileID` · `findObjectIDByFileID` | 끝 |
| C2 | 인스펙터 위젯 `SceneObjectReferencePropertyDrawer`(올라온 씬의 오브젝트 콤보) | 끝 |
| C3 | 파일 사이로 옮겨도 이어지는 영속 GUID | 남음(조건부: 오브젝트를 파일 사이에서 옮겨 같은 오브젝트로 이어야 할 때) |

## 설계 결정

- 참조는 `SceneObjectReference`(`Engine/Scene/SceneObjectReference.h`) = (씬 리소스 경로, 그 파일의 엔티티 파일 id). 언리얼 `FSoftObjectPath` 의 패키지 경로 + 하위 오브젝트 경로 자리다.
- 오브젝트마다 영속 GUID 를 싣지 않는다. 파일 id 가 이미 그 파일 안의 영속 키이고(한 번 준 id 는 오브젝트가 사라져도 다시 주지 않는다), 그래서 씬 파일 형식이 바뀌지 않는다.
- 해석 결과는 형식으로 가른다(`SceneObjectResolveResult`): `Resolved` · `Unset` · `SceneNotLoaded`(정상, 하위 레벨 · 셀이 내려감) · `ObjectMissing`(데이터 결함 또는 내려간 셀). 경로 비교는 구분자 · 대소문자를 맞춘다.
- `makeReference` 는 저장한 적 없는(경로 없는) 씬을 거절한다.

## C3 를 미룬 이유

에디터에 오브젝트를 파일 사이로 옮기는 명령이 없다. 프리팹 적용 · 되돌리기 · 풀기는 모두 정체성을 지키며 그 자리에서 고친다. 옮기는 곳은 `adoptChildPrefabInstances`(새 프리팹 만들기) 하나이고 이때 씬 자식 인스턴스가 부품으로 넘어가 다시 스폰된다. 그래서 다음을 구조로 막았다.

- 다시 스폰되는 부품은 `getOrAssignFileID` 가 0 을 돌려 참조를 아예 못 만든다.
- 옮기기 전에 이미 있는 참조는 `adoptChildPrefabInstances` 가 `SceneObjectReferenceUtil::findReferenceTo` 로 찾아(컨테이너 칸 · 리플렉션 구조체 안까지 읽기 전용 수집기로 훑음) 있으면 어느 참조인지 적어 옮김을 거절한다. 구조체 타입은 필수 인자 `const TypeRegistry&` 로 받으므로 레지스트리가 없어 구조체 안을 못 보고 통과할 수 없고, 씬 · 작업 공간 같은 필수 값을 못 얻으면 검사를 건너뛰지 않고 거절한다.
- 훑지 않는 곳: 컴포넌트가 아닌 `GameObject` · `Scene` 수준 데이터(담을 자리가 없다), 프리팹 파일 안에 저장된 인스턴스 덮어쓰기 값(프리팹이 특정 씬 오브젝트를 가리키는 데이터가 아직 없다. 생기면 더한다).
- 같은 모양의 다른 에디터 명령은 없다(`registerNestedPart` 를 부르는 곳은 이 함수뿐). GUID 는 그런 참조를 쓰는 데이터가 생길 때 정한다.
