# Shooter3D 캐릭터 통합의 남은 것

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-10 · 진행: 1/4

KayKit 기사 플레이어 · 스켈레톤 적 · 외형 프리셋 · 입력 맵 · 탄도선 풀은 들어갔다(`Source/Games/Shooter3D/`). 리그와 게임플레이 쪽이 남았다.

## 1. 결정

| 물음 | 결정 | 이유 |
|---|---|---|
| 쓰러질 때 무엇을 떨어뜨리나 | 외형의 소켓 부품 가운데 **강체가 있는 것**(무기 · 방패 프리팹)을 `SocketBindingComponent::release( Physics )` 로 뗀다(`ShooterEnemyComponent::requestDropSocketParts`, 틱 뒤 게임 스레드) | 어느 부품이 떨어지는지는 데이터(프리팹에 강체가 있는가)가 정한다 — 코드에 부품 이름 목록을 두지 않는다. 강체가 있는데 떼지 못하면 오류다 |
| 떨어진 무기의 층 | `Debris`(캐릭터와 부딪히지 않음), 붙은 동안 키네마틱(소켓 부착이 정한다) | 파괴 파편과 같은 층 규칙. 시체와 함께 외형이 걷는다(`despawnParts`) |
| 휘두름 피해 시각 | 클립 알림 `Impact` → 알림 표 `GameplayEvent` → `ShooterEnemyComponent::onAnimationNotify` 로 바꾼다(아래 S2) | `_attackHitTime` 초는 클립이 바뀌면 어긋난다. 시각의 정본은 클립 데이터다 |

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| S1 | 쓰러질 때 무기 떨어뜨리기 — 적 무기 프리팹 셋(`skeleton_blade` · `skeleton_axe` · `skeleton_shield`)에 강체(키네마틱, `Debris`, 상자는 원본 glb 범위), `requestDropSocketParts` | 코드 · 데이터 작성(빌드 · 화면 미확인) |
| S2 | 알림으로 휘두름 피해 · 발소리 | 아래 "남은 것" |
| S3 | 리그: 1인칭 머리만 숨기기 · 스프링 본(망토) | 아래 "남은 것" |
| S4 | 게임플레이: 래그돌 쓰러짐 · 부위 히트박스 · 물리 질의 히트스캔 · 시야, 외형 부품 풀 | 아래 "남은 것" |

## 남은 것

- S1 확인: 적을 쓰러뜨려 무기 · 방패가 떨어져 바닥에 눕는지, 시체가 걷힐 때 같이 사라지는지(Shooter3D autoplay 시나리오 스크린샷). 상자 크기는 원본 glb 의 POSITION 범위로 정했다(엔진의 X 뒤집기는 대칭이라 같다).
- S2: 적 공격 클립(`1H_Melee_Attack_Chop`, `skeleton_minion`)에 `Impact` 알림을 다는 사이드카(`models_raw/kaykit/skeleton_minion.clips.json` — `skeleton_warrior.clips.json` 과 같은 형식)와 다시 임포트, 적 프리팹의 알림 표(`Impact` → `GameplayEvent`), `ShooterEnemyComponent::onAnimationNotify` 가 공격 단계에서 맞힘을 표시하고 `_attackHitTime` 속성을 지운다. 사이드카를 바꾸면 임포트 스탬프가 바뀌므로 임포트와 한 커밋이어야 한다. 임포트(`ModelImportAnimation`)는 에디터 빌드가 있어야 돌아 빌드 없는 작업에서는 하지 않는다 — 코드만 먼저 바꾸면 클립에 알림이 없어 휘두름이 맞지 않는다.
- S3: 1인칭 머리 숨기기는 리그 노드 `ScaleBone`(`weight_slot` 으로 켜고 끔)이 있다. 플레이어 몸 프리팹에 `PoseModifierComponent`(기사 리그)를 달고, 1인칭에서 몸 전체 대신 머리만 숨기면 몸의 무기와 시점 무기가 둘 다 보이므로 시점 무기 · 몸 무기 중 하나를 정하는 일이 같이 필요하다. 스프링 본(망토)은 `knight_cape.rig.json`, 왼손을 총에 붙이는 손 IK 는 `knight.rig.json` 의 `LeftHandGrip`(대상 소켓 `Grip`)으로 데이터가 있다 — 플레이어 몸에 리그를 연결할 때 같이 확인한다(지금 `player_body.prefab.xml` 에 `PoseModifierComponent` 가 없다).
- S4: 래그돌 쓰러짐(지금은 클립), 부위 히트박스(지금은 발에서 키까지 캡슐 하나, 중심 소켓 `Chest`), 물리 질의 히트스캔 · 시야(지금 상자 목록 · 캡슐), 외형 부품 풀(스켈레톤마다 프리팹을 세우고 지운다), 뒷걸음 · 옆걸음 발 미끄러짐([ShooterStrideWarping](Candidates-gameplay.md#shooterstridewarping)).
