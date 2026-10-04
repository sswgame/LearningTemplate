# Gimmick — 데이터로 배선하는 레벨 장치

센서 → 신호 → 연산자 → 액추에이터를 데이터로 잇습니다(소스 엔진 엔티티 입출력 · 포탈 2 퍼즐 메이커 · 마리오 메이커의 배선). 장르를 가리지 않아
키트가 아니라 기반(`GameFramework`)에 있습니다. 2D(XY 평면 · 2D 콜라이더)와 3D 가 같은 코드입니다.
시험: `EngineTest` 의 `GimmickTest`(회로 — `Test/EngineTest/TestGimmick.cpp`) · `GimmickSceneTest`(씬 — `TestGimmickScene.cpp`).
쇼케이스 씬: `Resource/game/empty/maps/gimmickshowcase.scene.xml`(움직이는 발판 · 회전 발판 · 횃불 · 드럼통 · 아이템 상자 · 스프링, 레벨 회로가 시계 → 토글로
문과 엘리베이터를 움직인다). 보려면 `Config/Game/Empty.json` 의 `_startupScene` 을 그 경로로 바꾸거나 에디터에서 `-gv_editorStartupScene=` 로 연다.

## 구성

| 타입 | 하는 일 |
|------|--------|
| `GimmickCircuitDef` | 정의(데이터): 노드(id · 종류 이름 · 매개변수 글) + 배선(`node.Output` → `node.Input`, `invert`). XML `<GimmickCircuit>` |
| `GimmickNodeRegistry` | 종류 이름 → 입력 · 출력 포트, 매개변수 표, 상태 칸 수, 걸음 함수. `getDefault()` 가 내장 종류를 든다. 게임은 복사해 종류를 더한다 |
| `GimmickCircuit` | 검증 · 짓기(`build` — 문제를 모두 모아 한 줄씩), 고정 걸음 평가(`step`), 상태 바이트 저장 · 읽기 · 해시 |
| `GimmickCircuitComponent` | 씬 · 프리팹에 저장되는 회로. 노드마다 대상 오브젝트(핸들, 비면 소유자)에서 센서를 읽고 액추에이터를 건다 |
| `GimmickSensorComponent` · `GimmickWeightComponent` | 대상 쪽 상태 — 겹친 것(태그 거르기) · 무게 · 피해 · 사용 · 신호 |
| `GimmickDamageUtil` · `GimmickDamageEvent` | 기믹이 주는 피해의 한 길("game" 채널 + 대상 센서의 Damage — 폭발 사슬) |
| `ElementRuleTable` · `ElementGrid` | 원소 상호작용 표(재질 깃발 · 상태 · 자극 · 걸음 규칙)와 그 표를 따르는 결정적 셀 자동자. 오브젝트 하나는 1 × 1 격자 |
| `WorldQuery`(`World/`) | 광선 · 시야. Jolt 서비스가 걸리면 그것, 없으면 `PhysicsWorld` AABB 폴백 |

## 내장 노드 종류

포트 값은 모두 참/거짓 수준이고 `On…` 은 한 걸음 펄스입니다. 시간 매개변수(초)는 **걸음 수로 바꿔 셉니다**(반올림) — 실수 누적이 없어 같은 입력이면 같은 걸음에 바뀝니다.

| 분류 | 종류(입력 → 출력) | 매개변수 |
|------|-------------------|----------|
| 센서 | `Volume`(→ Occupied · Empty · OnEnter · OnExit) | minCount |
| | `PressurePlate`(→ Pressed · OnPress · OnRelease) | threshold · latch |
| | `Laser`(→ Blocked · Clear · OnBlock · OnClear) | range · direction |
| | `Signal`(→ Active · Inactive · OnActivate · OnDeactivate) | — |
| | `Proximity`(→ Near · Far · OnApproach · OnLeave) | radius · tag · planar |
| | `Timer`(Enable · Reset → OnTick · Running) | interval · startEnabled · repeat |
| | `Damage`(Reset → OnDamaged · Broken) | threshold · health |
| | `Interaction`(→ OnUsed · Toggled) | — |
| | `Constant`(→ Out) | value |
| 연산자 | `And` · `Or` · `Xor`(A..D → Out) · `Not`(In → Out) | — (연결 안 된 입력은 보지 않는다) |
| | `Toggle`(In · Reset → Out) · `Latch`(Set · Reset → Out, Reset 우선) | start |
| | `Counter`(Count · Down · Reset → Reached · OnReached) | target |
| | `Delay`(In → Out — 오름 · 내림을 N 걸음 뒤로, **고리를 끊는 유일한 노드**) | seconds |
| | `Pulse`(In → Out — 오르면 N 걸음 켜짐, 시간제 스위치) | seconds |
| | `Sequence`(In0..In7 · Reset → Done · OnFail · OnStep) | count |
| 액추에이터 | `Door`(Open · Toggle · Lock → Opened · Closed · Moving · OnOpened · OnClosed) | openTime · closeTime · startOpen · openOffset · openRotation(도) |
| | `Mover`(Enable · Reverse · Restart → AtStart · AtEnd · Moving · OnArrive) | speed(m/s, 호 길이) · mode(Once · Loop · PingPong) · curve(`BlendCurve` 이름 — `Engine/Animation/BlendCurve.h`) · pause · autoStart · length · faceForward |
| | `Elevator`(Up · Down · Call0..3 → Moving · OnArrive · AtBottom · AtTop) | floors("0 4 8") · speed · startFloor · axis |
| | `Rotator`(Enable · Reverse → Rotating · AtTarget · AtRest) | speed(도/초) · targetAngle(0 = 계속 돈다) · autoStart · axis |
| | `Spawner`(Spawn · Reset → OnSpawn · Exhausted) | maxCount · prefab · offset |
| | `Hazard`(Enable → Active · OnActivate · OnDamageTick) | onTime · offTime · phase · startEnabled · damage · damageInterval |
| | `Light`(On · Toggle → Lit) · `Sound`(Play → OnPlay) · `Enable`(Enable · Toggle → Enabled) | startOn · intensity / sound / startEnabled |

## 데이터 모양

XML(시험 · 도구):

```xml
<GimmickCircuit stepTime="0.0166667">
  <Node id="plate" kind="PressurePlate" threshold="50"/>
  <Node id="hold" kind="Pulse" seconds="3"/>
  <Node id="door" kind="Door" openTime="0.5" openOffset="0 3 0"/>
  <Wire from="plate.OnPress" to="hold.In"/>
  <Wire from="hold.Out" to="door.Open"/>
</GimmickCircuit>
```

씬 · 프리팹(`GimmickCircuitComponent` PROPERTY — 엔진 직렬화기가 쓴다): `_listNode` 의 `GimmickNodeDesc{ _id, _kind, _params = "openTime=0.5; openOffset=0 3 0" }`,
`_listNodeTarget`(노드 번호 자리의 `GameObjectHandle`, 비면 소유자), `_listWire` 의 `GimmickWireDesc{ _from = "plate.OnPress", _to = "hold.In", _bInvert }`.

## 규칙 · 함정

- **검증은 로드 때 한 번에 모두** — 모르는 종류 · 겹친 id · 모르는 매개변수 · 숫자가 아닌 숫자 · 모르는 노드 · 출력 · 입력 · 틀린 모드/곡선 이름 · 지연 없는 고리.
  씬의 회로는 `onPostLoad` 에서 지어 오류를 `[Error]` 로 내고 돌지 않습니다(`getErrors`). 센서 · Hazard 노드의 대상에 `GimmickSensorComponent` 가 없으면 시작 때 오류.
- **평가 순서는 지을 때 하나로 정합니다** — `Delay` 가 먼저(지난 입력으로 출력), 나머지는 위상 순서(같으면 정의 순서), 걸음 끝에 `Delay` 가 입력을 받는다.
  그래서 신호 고리는 `Delay` 를 지나야 하고(진동기 · 되먹임), 없으면 짓기 오류입니다.
- **상태는 바이트 하나**(`saveState` — 걸음 번호 · 누적 시간 · 노드 출력 · 지난 입력 · 센서 값 · 상태 칸). 회로 모양 해시가 머리에 있어 다른 회로의 바이트는 거절합니다.
  컴포넌트는 걸음을 낸 틱마다 `_stateBytes`(PROPERTY)에 써 두어 레벨 세이브 · 핫 리로드 · 플레이 복원이 이어 가고, 체크포인트는 `captureCheckpoint` · `restoreCheckpoint`.
  네트워크 스냅샷 · 롤백도 같은 바이트입니다(무버의 길 길이는 배치라 저장하지 않는다).
- **쉬는 자세는 처음 플레이에 한 번 잡아 저장**(`_listRestPose`) — 핫 리로드 · 세이브 로드가 움직이던 문 자리를 쉬는 자세로 착각하지 않습니다.
- 회로는 `PrePhysics` 에서 틱합니다. 겹침은 물리 step 뒤에 오므라 센서 변화는 다음 틱에 보입니다(한 프레임). 다른 오브젝트의 자리는 세터(틱 쓰기 큐)로,
  켜기 · 빛 · 스폰 · 소리는 틱 뒤(`executeOrDeferPostTick`)로 겁니다.

## 원소 규칙표

"불은 풀로 번지고, 물은 불을 끄고, 바람이 불을 민다" 를 데이터로 적습니다 — 기본표 `Resource/common/data/elements/default.elements.xml`.
코드에는 규칙의 **종류**만 있습니다: 자극 줄(`<On>` — 조건 material · flag · status · without, 동작 setMaterial · addStatus · removeStatus ·
floodStatus + through), 걸음 규칙(`Expire` — 상태가 재질 수치만큼 오래되면 칸을 비움, `Convert` — 상태 칸의 네 이웃 재질을 바꿈, `Spread` — 상태를
`to` 깃발 이웃으로 옮김, 모양 `Neighbors4` · `Wind`). 상태 종류는 `Age`(오른다) · `Countdown`(내려가 사라진다). 이름은 표 안에서 선언한 것만 쓰고, 모르는 이름 ·
규칙 · 모양 · 원소는 읽기 오류입니다(`ResourceDataSchemaTest` 의 `.elements.xml`).

- 한 걸음 = 상태 시간 흘리기 → 걸음 규칙을 **같은 상태에서** 모으기(규칙 순서 → 행 우선) → 같은 순서로 적용. 적용 때 다시 확인하므로(이미 녹은 얼음 · 이미 붙은 불)
  두 규칙이 같은 칸을 겨눠도 결과가 칸 순서에 달리지 않습니다.
- 자극은 위에서부터 **처음 맞는 한 줄만** 적용합니다(불: 얼음이면 녹이고, 아니면 탈 것에 붙인다).
- 액션 어드벤처 키트의 `AdventureElementGrid` 는 이 격자 위에 키트의 재질 열거 · 설정을 얹은 것입니다 — 설정 값으로 기본표와 같은 모양의 표를 코드로 짓습니다
  (`ElementRuleTest.AdventureGridMatchesDefaultTable` 이 걸음마다 같은지 본다).

## 장르 기믹 세트(`Genre/`)와 프리팹

회로(B)와 센서를 다시 쓰는 작은 컴포넌트 + `Resource/common/prefabs/gimmicks/<장르>/*.prefab.xml`(엔진 직렬화기로 쓴 것 — 손으로 쓰지 않는다).
시험: `GimmickGenreTest`(`Test/EngineTest/TestGimmickGenre.cpp` — 동작 + 모든 프리팹이 스폰되고 회로 검증을 지난다).

| 장르 | 프리팹 | 컴포넌트 · 회로 |
|------|--------|----------------|
| 플랫포머 | movingplatform · crumbleplatform · onewayplatform · spring · conveyor · ladder · rope | `SplineComponent` + `Mover`, `CrumblePlatformComponent`, `OneWayPlatformComponent`(`isSolidFor`), `LaunchPadComponent`, `ConveyorComponent`, `ClimbZoneComponent`(`findClimbZone`) |
| 어드벤처 | pushblock · floorswitch · lever · lockeddoor · torch | `PushBlockComponent`(칸 단위, 막히면 서고 Push 상호작용으로 민다), 센서만(레벨 회로가 배선), `Unlock` → `Latch` → `Door`, `ElementStatusComponent`(원소표의 1 × 1 칸) + `Signal` → `Light` |
| 슈터 | explosivebarrel · jumppad · keycarddoor · turret · destructiblecover | `ExplosiveBarrelComponent`(사슬 — 반경에 닿은 파괴 오브젝트를 그 자리에서 깬다), `LaunchPadComponent`, `SwipeKeycard` → `Pulse` → `Door`, `TurretComponent`(고르기 · 시야 · 회전 · 히트스캔), `DestructibleComponent`(단계 · 파괴 훅 — 파괴 컴포넌트가 있으면 단계마다 깎고 마지막에 조각으로) + 엄폐 스마트 오브젝트 |
| 레이싱 | boostpad · itembox | `BoostPadComponent`(`GimmickBoostEvent`), `ItemBoxComponent`(씨앗 + 연 횟수 가중치 뽑기, 숨었다 되살아남) |
| 공포 | scaretrigger · flickerlight · hidingspot | `ScareTriggerComponent`(연출 신호 · 소리, 한 번), `FlickerLightComponent`(퀘이크 빛 스타일 문자열), `HidingSpotComponent`(스마트 오브젝트 자리 + `State.Hidden`) |
| 잠입 | squeakyfloor | `NoiseEmitterComponent`(올라서면 · 주기 · 직접 — `GimmickNoiseEvent`, `AiStimulus::_noiseRadius`), `LightExposure::computeExposure`(점 · 스폿 · 방향광, 가림) |
| 메트로배니아 | abilitygate | `AbilityGateComponent`(능력 태그를 가진 것이 다가오면 몸을 끄고 열린 채 남음) |
| RPG | gatheringnode | `GatheringNodeComponent`(Gather 상호작용 → 아이템 이벤트, 다 쓰면 꺼졌다 다시 자람) |
| 공용 | hazardzone · elevator · rotatingplatform · barrelspawner | `Hazard` · `Elevator`(Toggled → Call1, 반전 → Call0) · `Rotator` · `Timer` → `Spawner` |

- 장르 컴포넌트의 시간은 60 Hz 걸음 수로 셉니다(`GenreGimmickUtil::makeClock`). 몸 숨기기(`setBodyActive` — 씬 컴포넌트를 켜고 끔)와 상호작용 켜기는 틱 뒤로 미룹니다.
- **기믹이 주는 피해(`GimmickDamageUtil`)는 틱 뒤에 대상 센서에 들어갑니다** — 병렬 틱에서 바로 넣으면 대상이 이번 틱에 먹을지가 스케줄에 달려 사슬 폭발 · 롤백이
  결정적이지 않다. 늘 다음 틱에 먹습니다.
- 이벤트("game" 채널): `GimmickLaunchEvent` · `GimmickBoostEvent` · `GimmickItemEvent` · `GimmickCueEvent`(연출 이름 — Scare · Explosion · Stage · Destroyed) ·
  `GimmickNoiseEvent` · `GimmickDamageEvent`. 받는 쪽(이동 몸 · 차량 · 인벤토리 · 카메라 · 오디오)은 게임 · 키트입니다.
- 프리팹의 콜라이더는 지금 `BoxCollider2DComponent`(트리거 겹침 폴백)입니다. 3D 물리 백엔드가 들어오면 3D 트리거 · 강체 콜라이더로 바꿉니다.
- **파괴(`Engine/Destruction`)와 잇기.** 오브젝트에 파괴 컴포넌트(`FractureComponent` · 2D `Fracture2DComponent`)가 있고 파쇄 데이터가 있으면 몸을 끄는 대신 조각으로
  부서집니다. 폭발 드럼통은 반경이 경계에 닿은 파괴 오브젝트(자기 포함)에 자리 있는 폭발(`applyRadialDamageAtWorld` — 중심 변형 `_fractureStrain`, 충격량
  `_blastImpulse`)을 주므로 사슬 폭발이 근처 벽 · 상자를 그 자리에서 깹니다. 엄폐물(`DestructibleComponent`)은 단계마다 중심에 `_stageStrain` × 단계 비율로
  조각을 깎고 마지막 단계에 `_shatterStrain` 으로 부숩니다(단계 · 신호 · 연출은 그대로). 시험: `GimmickFractureTest`(`Test/EngineTest/TestGimmickFracture.cpp`).
