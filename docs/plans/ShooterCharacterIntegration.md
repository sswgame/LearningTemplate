# Shooter3D 캐릭터 통합의 남은 것

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-10 · 진행: 1/4

KayKit 기사 플레이어 · 스켈레톤 적 · 외형 프리셋 · 입력 맵 · 탄도선 풀은 들어갔다(`Source/Games/Shooter3D/`). 리그와 게임플레이 쪽이 남았다.

## 1. 결정

| 물음 | 결정 | 이유 |
|---|---|---|
| 쓰러질 때 무엇을 떨어뜨리나 | 외형의 소켓 부품 가운데 **강체가 있는 것**(무기 · 방패 프리팹)을 `SocketBindingComponent::release( Physics )` 로 뗀다(`ShooterEnemyComponent::requestDropSocketParts`, 틱 뒤 게임 스레드) | 어느 부품이 떨어지는지는 데이터(프리팹에 강체가 있는가)가 정한다 — 코드에 부품 이름 목록을 두지 않는다. 강체가 있는데 떼지 못하면 오류다 |
| 떨어진 무기의 층 | `Debris`(캐릭터와 부딪히지 않음), 붙은 동안 키네마틱(소켓 부착이 정한다) | 파괴 파편과 같은 층 규칙. 시체와 함께 외형이 걷는다(`despawnParts`) |
| 휘두름 피해 시각 | 클립 알림 `Impact` → 알림 표 `GameplayEvent`(이벤트 `AttackHit`) → `ShooterEnemyComponent::onAnimationNotify`. 휘두름이 끝날 때까지 이벤트가 없으면 오류 | 예전 `_attackHitTime` 초는 클립이 바뀌면 어긋났다. 시각의 정본은 클립 데이터다 — 시각으로 대신하지 않는다 |
| 1인칭에서 무엇을 숨기나 | 몸은 보이고 머리(리그 `ScaleBone`, 슬롯 `FirstPersonHead`)와 외형 칸 `Head` · `MainHand` 만 숨긴다. 손에는 시점 무기가 보인다 | 조준선과 총구가 카메라 기준이어야 해서 몸 무기 대신 시점 무기를 남긴다. 값은 몸 프리팹 데이터(`ShooterAvatarComponent`) |

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| S1 | 쓰러질 때 무기 떨어뜨리기 — 적 무기 프리팹 셋(`skeleton_blade` · `skeleton_axe` · `skeleton_shield`)에 강체(키네마틱, `Debris`, 상자는 원본 glb 범위), `requestDropSocketParts` | 코드 · 데이터 작성(빌드 · 화면 미확인) |
| S2 | 알림으로 휘두름 피해 — 코드 · 알림 표(`characters/skeleton_minion/skeleton_minion.notifies.xml`) · 적 프리팹(`AnimationNotifyComponent`, `_attackHitEvent`) | 코드 · 데이터 작성(빌드 미확인). 사이드카 + 다시 임포트는 아래 "남은 것" — 그 전에는 적이 맞히지 못하고 오류를 한 번 남긴다 |
| S3 | 리그: 1인칭 머리만 숨기기 | 코드 · 데이터 작성(빌드 · 화면 미확인) — `rigs/knight_firstperson.rig.json`, `ShooterAvatarComponent::setFirstPerson`, `CharacterAppearanceComponent::setHiddenSlots` |
| S4 | 게임플레이: 래그돌 쓰러짐 · 부위 히트박스 · 물리 질의 히트스캔 · 시야, 외형 부품 풀 | 아래 "남은 것" |

## 남은 것

- S1 확인: 적을 쓰러뜨려 무기 · 방패가 떨어져 바닥에 눕는지, 시체가 걷힐 때 같이 사라지는지(Shooter3D autoplay 시나리오 스크린샷). 상자 크기는 원본 glb 의 POSITION 범위로 정했다(엔진의 X 뒤집기는 대칭이라 같다).
- S2 임포트(빌드 뒤 첫 일): 사이드카 `models_raw/kaykit/skeleton_minion.clips.json` 을 아래 내용으로 만들고 `App --import-models` 로 다시 임포트해 사이드카 · `import.stamp` · 클립을 한 커밋으로 넣는다(사이드카만 넣으면 임포트 스탬프 검사가 깨진다). 시각 0.45 초는 지운 `_attackHitTime` 의 값이다 — 화면에서 칼이 닿는 프레임으로 고친다. 그 뒤 Shooter3D autoplay 시나리오로 적이 맞히는지(플레이어 체력이 준다)와 오류 줄이 없는지 본다. 발소리 알림(`FootL` · `FootR` → `Footstep`)도 같은 사이드카에 넣을 수 있다.
  `{ "clips": { "1H_Melee_Attack_Chop": { "notifies": [ { "name": "Impact", "time": 0.45 } ] } } }`
- S3 확인: 1인칭에서 아래를 보면 몸 · 다리가 보이고 머리 · 투구 · 몸 무기가 보이지 않는지, 3인칭으로 바꾸면 모두 돌아오는지(스크린샷). 기사 리그의 발 · 시선 · 손 IK(`knight.rig.json` — 대상 `RigLookTarget` 오브젝트 · `Weapon` 유닛)와 스프링 본(망토, `knight_cape.rig.json`)은 플레이어 몸에 아직 붙이지 않았다 — 대상 연결을 화면으로 확인하며 붙인다.
- S4: 래그돌 쓰러짐(지금은 클립), 부위 히트박스(지금은 발에서 키까지 캡슐 하나, 중심 소켓 `Chest`), 물리 질의 히트스캔 · 시야(지금 상자 목록 · 캡슐), 외형 부품 풀(스켈레톤마다 프리팹을 세우고 지운다), 뒷걸음 · 옆걸음 발 미끄러짐([ShooterStrideWarping](Candidates-gameplay.md#shooterstridewarping)).
