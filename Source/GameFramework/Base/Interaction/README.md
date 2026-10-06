# Interaction — 상호작용, 스마트 오브젝트, 집기

## 이것은 무엇이고 왜 있나

문을 열고, 레버를 당기고, 상자를 줍고, 벤치에 앉는 것처럼 플레이어와 AI 가 오브젝트를 "쓰는" 일의 공통 구조입니다.
어떤 상호작용인지(안내 문구, 누름인지 누르고 있기인지, 걸리는 시간, 거리, 조건)는 데이터로 적고, 고르기와 진행은 이 폴더의 코드가 합니다.
언리얼의 Smart Object 와 Lyra 의 상호작용 시스템에 해당합니다. 장르를 가리지 않으므로 기반에 있고, 2D 와 3D 를 같이 다룹니다.

## 머릿속 그림

**하는 쪽과 쓰이는 쪽.** 하는 쪽은 `InteractorComponent`(플레이어나 AI), 쓰이는 쪽은 `InteractableComponent` 입니다.
하는 쪽이 매 틱 주변의 쓰이는 쪽을 후보로 모아 하나를 고르고, 버튼이 눌리면 진행을 시작합니다.

**정의.** `InteractionCatalog` 가 `<Interactions>` XML 을 읽습니다. 상호작용 정의(`InteractionDef`)에는 단계, 거리, 시야각, 시야, 쿨다운, 필요한 태그와 금지 태그, 맞춤 마커, 강조, 권한이 있습니다.

**진행.** `InteractionSession` 이 한 번의 진행을 셉니다. 입력 방식은 누름(`Press`), 누르고 있기(`Hold`), 연타(`Mash`) 셋이고, 단계를 여럿 둘 수 있습니다.
여럿이 함께 붙거나, 끊기면 퇴행하거나, 스킬 체크가 끼는 진행(발전기 수리, 동료 부활)은 `InteractionProgress` 입니다.

**스마트 오브젝트.** 오브젝트 위의 자리(벤치, 엄폐, 작업대, 숨는 곳)입니다. `SmartObjectComponent` 를 플레이어와 AI 가 같은 함수로 차지하고 비우며, 태그로 빈자리를 찾습니다.

**집기.** `GrabberComponent` 가 집고 던지고 붙입니다. 물리는 `IGrabPhysics` 뒤에 있습니다.

## 따라 해 보기 — 문 하나를 "쓸 수 있게" 만들기

1. 기본 정의 파일 `Resource/common/data/interactions/default.interactions.xml` 에 누름으로 여는 `Open` 정의가 있습니다. 새 종류가 필요하면 이 형식으로 더합니다.
2. 문 오브젝트에 `InteractableComponent` 를 붙이고 종류 id 를 `Open` 으로 줍니다. 같은 오브젝트에 `GimmickSensorComponent` 를 두면 완료 수가 기믹 회로의 `Interaction` 센서로 들어갑니다.
3. 플레이어 오브젝트에 `InteractorComponent` 를 붙입니다. 게임은 틱 전에 `setInput( 버튼 )` 으로 상호작용 버튼 상태를 넣습니다.
4. 플레이하면 문에 다가갈 때 안내(`InteractionPrompt`)가 생기고, 버튼을 누르면 상호작용이 끝나 회로가 문을 엽니다. `Hold` 정의라면 누르고 있는 동안 진행이 차오릅니다.

정의 XML 의 형식은 `InteractionCatalog.h` 의 헤더 주석에 있습니다. 동작 예는 테스트 `Test/EngineTest/GameFramework/Interaction/TestInteraction.cpp` 를 보세요.

## 작동 원리

하는 쪽은 `PostPhysics` 그룹에서 틱합니다. 이번 프레임의 물리 위치를 보고 고르기 위해서입니다.

1. 씬의 `InteractableComponent` 중 켜져 있고, 쿨다운이 끝났고, 태그 조건(`matchesTags`)을 지난 것을 후보로 모읍니다.
2. `InteractionSelector` 가 거리와 시야각 안에서 우선도, 그다음 가까운 순으로 고릅니다. 시야(가림)는 가까운 것부터 묻습니다. 2D 는 XY 평면에서 계산합니다.
3. 고른 대상에 강조 요청을 세우고, 이전 대상의 요청은 내립니다. 새로 누르면 진행을 시작합니다. 권한 훅이 있고 권한이 `Server` 면 먼저 허락을 받습니다.
4. 진행 중에는 대상을 바꾸지 않습니다. 대상이 사라지거나 고르는 거리의 1.25배 밖으로 나가면 취소합니다.
5. 끝나면 틱 뒤에 대상의 `completeInteraction` 이 불립니다. 쿨다운을 걸고 완료 수를 올리고, `InteractionCompletedEvent`("game" 채널)를 보내고, 권한 훅의 `notifyInteractionCompleted` 를 부릅니다.

상호작용은 기믹을 모릅니다. 같은 오브젝트의 `GimmickSensorComponent` 가 `consumeUses` 로 완료 수를 끌어 읽어 회로에 넣습니다. 기반 폴더의 층 규칙상 위층(기믹)이 아래층(상호작용)을 읽는 방향입니다.

**맞춤 지점.** 정의의 `alignment` 마커를 대상 오브젝트의 소켓 마커 테이블(`SocketSetComponent`, `*.sockets.xml`)에서 찾습니다. 마커의 +Z 가 하는 쪽이 바라볼 방향입니다.
없으면 오브젝트 원점과 앞을 씁니다. 상호작용을 시작하면 하는 쪽의 `MotionWarpingComponent` 에 그 위치와 요를 워프 목표로 넣습니다. 목표 이름은 마커 이름이고, 없으면 `Interaction` 입니다.
그래서 상호작용 클립의 `MotionWarp` 구간이 손을 문고리에 맞춥니다.

**시야**는 `WorldQuery` 로 묻습니다. 물리 서비스가 없으면 씬의 강체 물리(`ScenePhysicsWorldQuery`)와 `PhysicsWorld` AABB 중 가까운 것을 씁니다. 쏘는 쪽의 바디는 건너뛰고, 트리거는 막지 않습니다.
틱 워커에서 묻지만, 물리 스텝은 틱 그룹 사이에 돌므로 질의와 겹치지 않습니다.

**집기의 물리.** 서비스가 없으면 강체 백엔드(`RigidBodyGrabPhysics`)를 씁니다. 3D 와 2D 강체를 키네마틱으로 손에 묶고, 놓을 때 동적으로 돌리며 속도를 줍니다.
강체가 없는 대상은 트랜스폼을 직접 옮깁니다(`TransformGrabPhysics`).

**정의 핫 리로드.** `InteractionCatalog::findShared` 가 경로마다 한 번 읽어 캐시(`GameDataCache`)에 둡니다. 파일을 고치면 핫 리로드가 새 테이블을 읽고, `InteractableComponent` 는 다음 틱에 정의를 다시 찾습니다.

## 확장하는 법

- **새 상호작용 종류**는 데이터에 정의를 더합니다. 코드가 필요한 결과는 `InteractionCompletedEvent` 를 받아 처리합니다.
- **네트워크 게임**은 `IInteractionAuthority` 를 구현해 하는 쪽에 등록합니다. 권한이 `Server` 면 시작 전에 허락을 묻고, 완료를 알립니다.

## 함정과 주의

- **후보는 하는 쪽마다 매 틱 씬 전체를 훑어 모읍니다**(`forEachComponentOfType`). 오브젝트 수에 비례하므로, 하는 쪽이 많아지면 공간 격자 레지스트리로 바꿔야 합니다.
- **강조 요청은 깃발뿐입니다.** 외곽선이나 감각 모드 패스는 렌더러가 `getHighlightRequest` 를 읽어 그리게 될 때 붙습니다.
- **버튼 상태는 틱 전에 넣어야 합니다.** 하는 쪽 틱이 `setInput` 의 지난 값과 비교해 새로 눌렀는지 정하므로, 틱 중간에 넣으면 누름을 놓칩니다.

## 더 볼 곳

- [Gimmick](../Gimmick/README.md) — 상호작용 완료를 받는 회로
- [Character](../../../Engine/Character/README.md) — 모션 워핑

| 테스트 | 파일 |
|--------|------|
| `InteractionTest` | `Test/EngineTest/GameFramework/Interaction/TestInteraction.cpp` |
| `WorldSystemsTest`(진행 규칙) | `Test/EngineTest/GameFramework/World/TestWorldSystems.cpp` |
