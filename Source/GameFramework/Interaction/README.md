# Interaction — 상호작용 · 스마트 오브젝트 · 집기

플레이어와 AI 가 오브젝트를 "쓰는" 공통 틀입니다. 종류(안내 문구 · 입력 방식 · 시간 · 거리 · 조건)는 데이터이고, 장르를 가리지 않아 기반에 있습니다.
시험: `EngineTest` 의 `InteractionTest`(`Test/EngineTest/TestInteraction.cpp`), 누르고 있기의 진행 규칙은 `WorldSystemsTest`(`InteractionProgress`).

| 타입 | 하는 일 |
|------|--------|
| `InteractionCatalog` | `<Interactions>` XML — 상호작용 정의(`InteractionDef` — 단계 · 거리 · 시야각 · 시야 · 쿨다운 · 필요/금지 태그 · 맞춤 마커 · 강조 · 권한)와 스마트 오브젝트 자리(`SmartObjectDef`). `findShared` 가 경로마다 한 번 읽는다 |
| `InteractionSelector` | 후보 고르기 — 거리 · 시야각 안, 우선도 → 가까운 순, 시야(가림)는 가까운 순으로 묻는다. 2D 는 XY 평면 |
| `InteractionSession` | 한 번의 진행 — `Press` · `Hold`(떼면 취소, 진행은 처음부터 — `InteractionProgress`) · `Mash`(누를 때마다 오르고 쉬면 준다), 여러 단계 |
| `InteractionProgress` | 진행형 상호작용(여럿이 붙기 · 끊김 · 퇴행 · 스킬 체크 — 발전기 수리 · 동료 부활). 예전 `World/` 에서 옮겼다 |
| `InteractableComponent` | 쓸 수 있는 오브젝트 — 종류 id, 맞춤 지점(`computeAlignmentPoint` — 소켓 표의 마커), 쿨다운, 강조 요청 깃발(`getHighlightRequest` — 렌더러 · UI 가 읽는다), 완료 처리 |
| `InteractorComponent` | 쓰는 쪽 — 후보 모으기 · 고르기 · 진행, UI 안내(`InteractionPrompt`), 2D/3D(`InteractionSpace`) |
| `SmartObjectComponent` · `SmartObjectSlots` | 오브젝트 위 자리(벤치 · 엄폐 · 작업대 · 숨는 곳)를 플레이어와 AI 가 같은 함수로 차지 · 비움(태그로 빈자리 찾기) |
| `GrabberComponent` · `IGrabPhysics` | 집기 · 던지기 · 붙이기. 물리 백엔드 서비스가 없으면 트랜스폼 폴백(`TransformGrabPhysics`) |
| `IInteractionAuthority` · `InteractionCompletedEvent` | 네트워크 권한 훅(권한 `Server` 는 시작 전 허락), 완료 이벤트("game" 채널) |

## 흐름

1. 게임 입력이 틱 전에 `InteractorComponent::setInput( 버튼 )` 을 부릅니다.
2. 하는 쪽 틱(`PostPhysics`): 씬의 `InteractableComponent` 중 켜짐 · 쿨다운 끝 · 태그 조건(`matchesTags( required, forbidden )`)을 지난 것을 후보로 모아
   고르고, 고른 대상에 강조 요청을 세웁니다(이전 대상은 내림). 새로 누르면 진행을 시작합니다(권한 훅이 걸려 있고 권한이 `Server` 면 먼저 허락).
3. 진행 중에는 대상을 바꾸지 않습니다. 대상이 사라지거나 고를 거리의 1.25 배 밖이면 취소합니다.
4. 끝나면 틱 뒤에 대상의 `completeInteraction` — 쿨다운, 같은 오브젝트의 `GimmickSensorComponent` 에 사용 알림(기믹 `Interaction` 센서 → 회로),
   `InteractionCompletedEvent`, 권한 훅의 `notifyInteractionCompleted`.

## 함정 · 다음에 붙일 것

- **맞춤 지점은 정의의 `alignment` 마커**를 대상 오브젝트의 소켓 · 마커 표(`SocketSetComponent` · `*.sockets.xml`, 마커의 +Z 가 하는 쪽이 볼 방향)에서 찾습니다.
  없으면 오브젝트 원점 · 앞입니다. 상호작용을 시작하면 하는 쪽의 `MotionWarpingComponent` 에 그 자리 · 요를 마커 이름(없으면 `Interaction`)의 워프 목표로 넣어,
  상호작용 클립의 `MotionWarp` 창이 손을 문고리에 맞춥니다.
- 후보는 하는 쪽마다 틱에 씬의 `InteractableComponent` 를 훑습니다(`forEachComponentOfType` — 오브젝트 수에 비례). 하는 쪽이 많아지면 공간 격자 등록부로 바꿉니다.
- 시야는 `WorldQuery`(물리 백엔드 서비스, 없으면 `PhysicsWorld` AABB 폴백 — 트리거는 막지 않는다)입니다.
- 강조 요청은 깃발뿐입니다 — 외곽선 · 감각 모드 패스는 렌더러가 `getHighlightRequest` 를 읽어 그릴 때 붙습니다.
