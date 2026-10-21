# Shooter3D 캐릭터 통합의 남은 것

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-10 · 진행: 4/6

KayKit 기사 플레이어 · 스켈레톤 적 · 외형 프리셋 · 입력 맵 · 탄도선 풀은 들어갔다(`Source/Games/Shooter3D/`).
알림 휘두름 · 래그돌 히트박스 · 물리 질의 히트스캔 · 시야 · 1인칭 머리 숨김 · 보폭 워핑은 코드와 데이터를 썼다(빌드 · 화면 미확인, 아래 확인 목록). 손 IK · 스프링 망토 · 외형 부품 풀이 남았다.

## 1. 결정

| 물음 | 결정 | 이유 |
|---|---|---|
| 쓰러질 때 무엇을 떨어뜨리나 | 외형의 소켓 부품 가운데 **강체가 있는 것**(무기 · 방패 프리팹)을 `SocketBindingComponent::release( Physics )` 로 뗀다(`ShooterEnemyComponent::requestDropSocketParts`, 틱 뒤 게임 스레드) | 어느 부품이 떨어지는지는 데이터(프리팹에 강체가 있는가)가 정한다 — 코드에 부품 이름 목록을 두지 않는다. 강체가 있는데 떼지 못하면 오류다 |
| 떨어진 무기의 층 | `Debris`(캐릭터와 부딪히지 않음), 붙은 동안 키네마틱(소켓 부착이 정한다) | 파괴 파편과 같은 층 규칙. 시체와 함께 외형이 걷는다(`despawnParts`) |
| 휘두름 피해 시각 | 클립 알림 `Impact` → 알림 표 `GameplayEvent`(이벤트 `AttackHit`, 적 프리팹의 `_attackHitEvent` 와 같은 이름) → `ShooterEnemyComponent::onAnimationNotify` | `_attackHitTime` 초는 클립이 바뀌면 어긋난다. 시각의 정본은 클립 데이터다 |
| 적 히트박스 | 같은 오브젝트의 `RagdollComponent`(물리 에셋 `skeleton_warrior.physics.xml`, 미니언 · 로그와 공용 리그) — 키네마틱 바디가 포즈를 따른다 | 부위 배율(머리 ×2)이 데이터에 있고, 쓰러지면 같은 바디가 래그돌이 된다. 캡슐 하나는 머리 · 팔 · 다리를 가르지 못한다 |
| 히트스캔이 어디서 묻나 | 틱 안에서는 막는 상자 · 바닥까지의 거리만 재고(`traceCover`), 적은 틱 뒤 게임 스레드에서 그 거리 안에서 물리 질의로(`resolveShots`) | 물리 씬은 게임 스레드만 다룬다(`CharacterHitUtil`). 엄폐물 뒤의 적은 질의 거리 밖이라 맞지 않는다 |
| 막는 상자와 물리 | `ShooterBlockerComponent` 가 플레이 시작에 같은 상자의 정적 강체를 만든다. 바닥은 씬의 정적 강체 | 상자의 정본은 `_halfSize` 하나다 — 데이터에 강체를 따로 적으면 두 크기가 어긋난다. 래그돌 · 떨어진 무기가 벽 · 바닥에 막힌다 |
| 자동 조준의 시야 | 디렉터의 막는 상자 목록을 기하로 본다(`isVisible`) | 생각은 틱(워커)에서 돌아 물리를 묻지 못한다. 같은 상자로 정적 강체를 만들므로 물리 질의와 같은 벽이다 |
| 1인칭의 무기 | 시점 무기(카메라 자식) 하나 — 몸은 보이되 머리(리그 `HideHead`, 슬롯 `FirstPersonHead`)와 머리 장비 · 몸의 무기(외형의 숨기는 슬롯 `Head` · `MainHand`, `setHiddenSlots`)를 숨긴다 | 조준 · 총구의 정본이 시점 무기다. 둘 다 보이면 총이 두 자루다 |

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| S1 | 쓰러질 때 무기 떨어뜨리기 — 적 무기 프리팹 셋에 강체, `requestDropSocketParts` | 코드 · 데이터 작성(빌드 · 화면 미확인) |
| S2 | 알림으로 휘두름 피해 — 곁 데이터 `skeleton_minion.clips.json`, 알림 표 `characters/skeleton_minion/skeleton_minion.notifies.xml`, 적 프리팹 `AnimationNotifyComponent`, `onAnimationNotify` · `_attackHitTime` 삭제 | 코드 · 데이터 작성(임포트 · 빌드 미확인) |
| S3 | 1인칭 머리 숨기기 — 몸 리그 `rigs/player_body.rig.json`(`HideHead` — 보폭 워핑 `Stride` 와 같은 리그), 몸 프리팹 `PoseModifierComponent` 하나, `ShooterAvatarComponent::setFirstPerson` | 코드 · 데이터 작성(빌드 · 화면 미확인) |
| S4 | 래그돌 쓰러짐 · 부위 히트박스 · 물리 질의 히트스캔 · 시야 · 보폭 워핑(`Stride`, `ShooterAvatarComponent::updateStrideWarping`) | 코드 · 데이터 작성(빌드 · 화면 미확인) |
| S5 | 왼손 IK · 스프링 망토 | 아래 "남은 것" — 엔진 · 에셋 선행 |
| S6 | 외형 부품 풀 | 아래 "남은 것" |

## 남은 것

- S5 왼손 IK: 무기 부품은 정적 메시라 리그의 소켓 대상이 고르는 유닛 목록(`PoseModifierComponent::setSocketTable` — `SkeletalMeshComponent` 만)에 들 수 없고, 외형도 그 표를 리그에 잇지 않는다. 먼저 (1) 외형이 조립 뒤 같은 오브젝트의 리그에 소켓 표를 잇고, (2) 정적 부품 소켓은 "부품 오브젝트 월드 × 소켓 로컬" 바깥 대상으로 해석하게 `resolveSocketTarget` 을 넓힌다. 그다음 몸 리그에 `{ "name": "Grip", "socket": "MainHand.SupportHand" }` 와 `TwoBoneIK`(`upperarm.l` · `lowerarm.l` · `hand.l`)를 더한다.
- S5 스프링 망토: Shooter3D 의 기사 망토(`prefabs/kaykit/knight_cape.prefab.xml`)는 사슬 뼈가 없는 정적 메시다. `knight_cape.rig.json`(`cape0..3`)을 쓰려면 사슬 뼈가 있는 스킨드 망토 에셋(벤치 데모 데이터와 같은 것)을 가져와 외형의 스킨드 부품으로 바꾸는 것이 먼저다.
- S6 외형 부품 풀: 스켈레톤마다 부품 프리팹을 세우고 지운다(`CharacterAppearanceComponent::syncParts` · `despawnParts`). 짧은 설계 — 씬마다 에셋 경로별 쉬는 부품 목록을 두고, 지울 때 소켓 부착을 풀고(`SocketBindingComponent` 를 붙은 상태로 되돌리고 강체를 키네마틱으로) 숨겨 넣으며, 스폰할 때 꺼낸다. 떨어진 무기(물리로 뗀 것)는 풀로 돌리지 않고 지운다. 잰 뒤에 정한다(스켈레톤 하나의 부품 셋 스폰 비용).
- 확인(S1 · S2 · S3 · S4, 빌드 뒤):
  - 병합 뒤 `App --import-models` 로 Shooter3D 모델을 다시 가져와 `skeleton_minion/clips/1h_melee_attack_chop.animclip` 에 `Impact` 알림이 들어갔는지, 임포트 결과와 곁 데이터를 한 커밋으로 남긴다. 그 전에는 적의 휘두름이 맞지 않는다.
  - Shooter3D autoplay 시나리오(네 백엔드)가 통과하는지 — `EnemyHitCount` 가 오르는지(히트박스 레이어 `Ragdoll` 질의), 적이 쓰러질 때 래그돌로 넘어져 바닥에 눕는지, 떨어진 무기 · 방패가 바닥(정적 강체)에 눕는지, 시체가 걷힐 때 같이 사라지는지(스크린샷).
  - 머리를 맞히면 피해가 두 배인지(로그), 엄폐물 뒤의 적은 맞지 않는지, 자동 조준이 엄폐물 뒤 적에게 쏘지 않는지.
  - 1인칭(`-gv_cameraPreset=firstperson`)에서 내려다보면 몸 · 다리가 보이고 머리 · 투구 · 몸의 총이 없는지, 3인칭에서 다시 보이는지. 근평면 0.05 에 목 단면이 걸리면 `HideHead` 를 `neck` 까지 넓힌다.
  - 뒷걸음 · 옆걸음에서 발 미끄러짐이 줄었는지(`-gv_shooterMotionTrace` · 연속 스크린샷).
