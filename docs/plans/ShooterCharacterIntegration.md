# Shooter3D 캐릭터 통합의 남은 것

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-10 · 진행: 6/7

KayKit 기사 플레이어 · 스켈레톤 적 · 외형 프리셋 · 입력 맵 · 탄도선 풀은 들어갔다(`Source/Games/Shooter3D/`).
알림 휘두름 · 래그돌 히트박스 · 물리 질의 히트스캔 · 시야 · 1인칭 머리 숨김 · 보폭 워핑 · 왼손 IK · 외형 부품 풀은 코드와 데이터를 썼다(화면 미확인, 아래 확인 목록).
스프링 망토는 엔진(외형의 자기 리그 스킨드 부품)과 에셋 생성기까지 했고, 임포트와 데이터 전환이 남았다.

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
| 왼손 IK 가 무기를 찾는 길 | 외형이 조립마다 소켓 에셋을 가진 소켓 부품을 칸 이름의 **강체 유닛**으로 같은 오브젝트의 리그에 잇는다(`PoseModifierComponent::bindRigidUnit`). 리그는 `{ "unit": "MainHand", "socket": "SupportHand", "space": "handslot.r" }` | 정적 메시 무기는 `SkeletalMeshComponent` 가 아니라 기존 유닛 연결에 들 수 없었다. 유닛 대상은 데이터로 바깥 대상이라 무기를 들기 전(맨손)에도 리그 묶기가 실패하지 않고 값만 없다. `space` 가 손 본 기준으로 받아 한 프레임 늦지 않는다 |
| 부품 풀을 언제 쓰나 | 바꾼 소켓 부품(조립의 `syncParts`)과 `destroyOwnerStowingParts`(감독이 적을 걷을 때)가 씬 서비스 `SocketPartPool` 에 넣는다. `despawnParts`(상태 저장 앞)와 끝날 때는 지운다 | 붙은 부품은 캐릭터의 자식이라 캐릭터를 먼저 지우면 함께 지울 예정이 된다 — 거두기와 지우기를 한 함수(틱 뒤)로 묶어 순서를 형식으로 지킨다. 물리로 떨어진 무기는 풀이 받지 않는다 |
| 스프링 망토의 모양 | 외형 부품 `kind="Skinned"` 에 자기 `skeleton` · `rig` 를 적으면 몸 리더 포즈 대신 그 리그(리그 유닛 `Body` = 몸)가 망토 뼈를 움직인다. `socket` 을 적으면 바인드 포즈의 그 소켓 자리에 둔다(플레이 없는 미리보기) | 몸 스켈레톤을 빌리는 리그 부품은 리더 포즈와 다툰다 — 로드 오류로 막는다 |

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| S1 | 쓰러질 때 무기 떨어뜨리기 — 적 무기 프리팹 셋에 강체, `requestDropSocketParts` | 코드 · 데이터 작성(빌드 · 화면 미확인) |
| S2 | 알림으로 휘두름 피해 — 곁 데이터 `skeleton_minion.clips.json`, 알림 표 `characters/skeleton_minion/skeleton_minion.notifies.xml`, 적 프리팹 `AnimationNotifyComponent`, `onAnimationNotify` · `_attackHitTime` 삭제 | 코드 · 데이터 작성(임포트 · 빌드 미확인) |
| S3 | 1인칭 머리 숨기기 — 몸 리그 `rigs/player_body.rig.xml`(`HideHead` — 보폭 워핑 `Stride` 와 같은 리그), 몸 프리팹 `PoseModifierComponent` 하나, `ShooterAvatarComponent::setFirstPerson` | 코드 · 데이터 작성(빌드 · 화면 미확인) |
| S4 | 래그돌 쓰러짐 · 부위 히트박스 · 물리 질의 히트스캔 · 시야 · 보폭 워핑(`Stride`, `ShooterAvatarComponent::updateStrideWarping`) | 코드 · 데이터 작성(빌드 · 화면 미확인) |
| S5 | 왼손 IK — 강체 유닛 연결(`bindRigidUnit`) · 외형의 리그 유닛 잇기(`bindRigUnits`) · 몸 리그 `Grip` + `TwoBoneIK LeftHandGrip` | 코드 · 데이터 작성, `PoseModifierTest.RigidUnitSocketFollowsObjectAndUnbinds`(화면 미확인) |
| S6 | 외형 부품 풀 — 씬 서비스 `SocketPartPool`, `destroyOwnerStowingParts`, 감독이 적을 걷을 때 씀 | 코드 작성, `SocketPartPoolTest`(비용 측정 · 화면 미확인) |
| S7 | 스프링 망토 | 엔진(자기 리그 스킨드 부품 `rig`) · 생성기 `Scripts/dev/MakeSkinnedCape.py`(`TestMakeSkinnedCape`) 완료. 아래 "남은 것" |

## 남은 것

- S7 스프링 망토(임포트가 병합 뒤라 이 단계만 남았다): ① `py -3 Scripts/dev/MakeSkinnedCape.py` 로 `models_raw/kaykit/knight_cape_chain.glb` 를 만들고 ② `App --import-models`(휘두름 `SwingLands` 재임포트와 같은 실행)로 `models/kaykit/knight_cape_chain.mesh` · 스켈레톤을 쓴 뒤 ③ `itemvisuals.xml` 의 `cape_knight` 를 `<Part name="Cape" kind="Skinned" mesh="game/shooter3d/models/kaykit/knight_cape_chain.mesh" skeleton="game/shooter3d/models/kaykit/knight_cape_chain/knight_cape_chain.skeleton.xml" rig="game/shooter3d/rigs/knight_cape.rig.xml" material="game/shooter3d/materials/kaykit_knight.material" socket="Back"/>` 로 바꾼다. 원본 glb 를 임포트 없이 넣으면 `--check-models`(`AppSmokeTest.ModelCheckRunsHeadlessThroughTheEditorModule`)가 지므로 셋을 한 커밋으로 한다. 그다음 플레이어가 돌 때 망토가 뒤처져 흔들리는지 화면으로 본다.
- S5 왼손 IK 화면 확인: 2026-10-22 Release 에서 쇼케이스 씬을 베낀 임시 씬(플레이어 프리셋 · 몸 리그, `1H_Ranged_Aiming`)으로 리그를 켜고 끈 두 캡처를 견줘, 켜면 늘어져 있던 왼팔(방패를 든 팔)이
  소총 쪽으로 끌려 오는 것을 봤다. 남은 것: 실제 게임 3인칭 시점에서 왼손이 총열 아래(`SupportHand`)에 자연스럽게 붙는지(기사 세트의 방패가 왼손에 함께 있어 가린다 — 총을 든 프리셋에서
  방패 칸을 비울지 정한다), 팔꿈치 방향(어색하면 `pole`), 무기를 바꾸면 새 무기의 손잡이를 따르는지. 몸 리그의 `HideHead` 는 슬롯 가중치를 정하지 않으면 1(숨김)이라 아바타 없이 쓰면 머리가 사라진다.
- S6 부품 풀 측정: 풀 효과를 재는 도구를 만들었다 — 프로파일 구간 `GT.Appearance.syncParts`(부품 스폰이 든다), 풀 끄기 `-gv_socketPartPool=0`, 탐침 `SocketPartPool.TakeHits` · `TakeMisses` · `Stored`.
  적이 같은 부품 셋으로 스폰 · 퇴장을 되풀이하는 장면에서 풀 켬 · 끔의 구간 시간을 견준다(매니페스트 id `socket-part-pool-measure`). 쉬는 부품은 꺼진 오브젝트로 씬에 남으므로 상태 저장 경로가 생기면 그 앞에서 풀을 비우는 길이 필요하다.
- 화면 확인 시나리오(만들어 둠, 통합에서 확인): 매니페스트 id `shooter-body-views-scenario`(S3 1인칭 몸 · 3인칭 · S5 무기마다 왼손 · 걷기 — 근평면에 목 단면이 걸리면 `HideHead` 를 `neck` 까지 넓힌다, 방패 칸을 비울지 정한다, 팔꿈치는 `pole`),
  매니페스트 id `shooter-enemy-death-scenario`(S1 · S2 · S4 쓰러짐 · 떨어진 무기 · 시체 걷힘 · `EnemyHitCount`), `shooter-stride-warping-scenario`([ShooterStrideWarping](ShooterStrideWarping.md)),
  매니페스트 id `shooter-cape-scenario`(S7 뒤). 머리 ×2 · 엄폐물 뒤의 적 · 자동 조준 시야는 autoplay 로그([Shooter] hit …)와 기존 `autoaim` 시나리오로 본다.
- S7 의 ① ~ ③(임포트와 데이터 전환)은 `App --import-models` 실행이 필요해 통합이 한다 — 매니페스트 id `shooter-cape-import`(위 순서 그대로, 한 커밋). 휘두름 `Impact` 알림이 들어간 재임포트(`skeleton_minion/clips/1h_melee_attack_chop.animclip`)도 같은 실행이다.
