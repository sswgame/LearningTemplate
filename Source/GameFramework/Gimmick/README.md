# Gimmick — 데이터로 배선하는 레벨 장치

센서 → 신호 → 연산자 → 액추에이터를 데이터로 잇습니다(소스 엔진 엔티티 입출력 · 포탈 2 퍼즐 메이커 · 마리오 메이커의 배선). 장르를 가리지 않아
키트가 아니라 기반(`GameFramework`)에 있습니다. 2D(XY 평면 · 2D 콜라이더)와 3D 가 같은 코드입니다.
시험: `EngineTest` 의 `GimmickTest`(회로 — `Test/EngineTest/TestGimmick.cpp`) · `GimmickSceneTest`(씬 — `TestGimmickScene.cpp`).

## 구성

| 타입 | 하는 일 |
|------|--------|
| `GimmickCircuitDef` | 정의(데이터): 노드(id · 종류 이름 · 매개변수 글) + 배선(`node.Output` → `node.Input`, `invert`). XML `<GimmickCircuit>` |
| `GimmickNodeRegistry` | 종류 이름 → 입력 · 출력 포트, 매개변수 표, 상태 칸 수, 걸음 함수. `getDefault()` 가 내장 종류를 든다. 게임은 복사해 종류를 더한다 |
| `GimmickCircuit` | 검증 · 짓기(`build` — 문제를 모두 모아 한 줄씩), 고정 걸음 평가(`step`), 상태 바이트 저장 · 읽기 · 해시 |
| `GimmickCircuitComponent` | 씬 · 프리팹에 저장되는 회로. 노드마다 대상 오브젝트(핸들, 비면 소유자)에서 센서를 읽고 액추에이터를 건다 |
| `GimmickSensorComponent` · `GimmickWeightComponent` | 대상 쪽 상태 — 겹친 것(태그 거르기) · 무게 · 피해 · 사용 · 신호 |
| `GimmickDamageUtil` · `GimmickDamageEvent` | 기믹이 주는 피해의 한 길("game" 채널 + 대상 센서의 Damage — 폭발 사슬) |
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
| | `Mover`(Enable · Reverse · Restart → AtStart · AtEnd · Moving · OnArrive) | speed(m/s, 호 길이) · mode(Once · Loop · PingPong) · curve(`CameraBlendCurve` 이름) · pause · autoStart · length · faceForward |
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
