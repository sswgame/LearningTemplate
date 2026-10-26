# 페어드 애니메이션(여러 개체가 한 타임라인으로 도는 애니메이션)

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: D4-6 · 진행: 6/6

**남은 확인(통합에서 확인):** 매니페스트 id `paired-animation-tests`, `paired-animation-editor-window`, `paired-animation-two-process`, `bowshoot-projectile-checks` — 아래 "남은 것". 확인이 끝나면 통합 담당이 이 문서를 닫는다.

활과 화살, 잡기 · 처형, 말 타기, 문 열기처럼 **여러 개체가 한 타임라인에 묶여 같이 도는 애니메이션**(언리얼의 Contextual Animation)의 일반 구조다. 애니메이션 영역의 한 단위([게임플레이 후보](Candidates-gameplay.md) 의 애니메이션 절과 이웃). P0 ~ P2, 상호작용 연결 · 에셋 캐시 · 세트 고르기 · 보폭 보정 쥠은 코드와 테스트(`PairedAnimationTest` · `PairedAnimationDefTest` · `InteractionTest`)로 들어갔다.

## 1. 지금 엔진에 있는 것과 없는 것

루트 모션 · 모션 워핑 · 알림 · 소켓 부착은 모두 **개체 하나 단위**이고 일반화된 다개체 세션은 없었다. 세션이 기존 조각을 쓰는 방식이다.

| 조각 | 세션과의 관계 |
|---|---|
| `AnimationSystem`(`Engine/Object/SceneAnimation`) | 세션은 이 평가 **앞**(프레임 시스템 단계)에서 게임 스레드로 돈다. 평가 중에는 세션을 아무도 읽지 않는다 |
| `SkeletalAnimatorComponent` | 세션은 공개 함수(`play` · `setPlayRate` · `getGraphPlayer` · `findClip` · `preloadClip`)만 쓴다. 이 파일은 고치지 않는다 |
| `AnimationGraphPlayer` · `AnimationPlayer` | 세션 시계는 앵커 역할 플레이어의 시각을 읽는다(현재 슬롯이 아니면 다음 슬롯) |
| `AnimationSyncGroup` | **쓰지 않는다** — 크로스페이드 중 리더의 현재 슬롯이 이전 클립이라 위상이 틀어지고, 역할마다 시작 시각이 다르면 맞지 않는다 |
| `MotionWarpingComponent` | **그대로 쓴다**: `setWarpTarget` 으로 목표, `beginWindow` / `endWindow` 로 창. 새 워핑 노드는 만들지 않는다 |
| `AnimationNotifyComponent` + 처리기 레지스트리 | 공유 타임라인 알림은 역할 오브젝트의 이 컴포넌트로 보낸다. 훅 하나(`processExternalNotify`)를 더했다 |
| 소켓 | 활(프롭)은 궁수 손 소켓에 붙은 채로 참가한다. 부착이 평가 의존(레벨)을 만든다 |
| `AnimationCrowd` | 세션 클립은 반복하지 않아 `describeSharedPose` 가 거짓 — 참가자는 군중에서 자동으로 빠진다 |
| `InteractableComponent` | 단계의 `pairedAnimation`(세트 목록) · `interactorRole` · `interactableRole` 로 `InteractorComponent` 가 단계 시작에 세션을 시작한다 |
| `LocomotionWarpingComponent` | 세션이 역할에 들어올 때 `setPlayRateHeld` 로 쥐고 나갈 때 놓는다(Object 티어라 세션을 묻지 않는다) |

## 2. 상용 선례

언리얼 Animation Montage(역할마다 일회성 클립) · Sync Group(리더-팔로워 — 위상 동기는 쓰지 않고 같은 프레임 · 같은 속도 시작 + 시각 검사로 맞춘다) · Motion Warping(정렬 `warp` 가 엔진의 `MotionWarpingComponent` 를 쓴다) · Contextual Animation(역할 목록 + 클립 + 앵커 기준 오프셋 = 데이터 에셋, 장면 인스턴스 = 세션) · Gameplay Interactions(진입 조건을 데이터로, 시작은 게임플레이가 부름) · 유니티 Timeline(한 시계 + 역할 바인딩, 단 포즈는 각 애니메이터가 내고 시계는 앵커가 가짐) · Naughty Dog 컨텍스추얼 애니메이션(기준점이 따로 없고 앵커 역할이 기준점) · Assassin's Creed 처형 · The Last of Us 잡기(`snap` · `warp` 정렬, `maxAlignDistance`, `<Select>` 방위각 · 거리). 근거는 문서 · 기억 수준이다.

## 3. 결정

| 물음 | 결정 | 이유 |
|---|---|---|
| 시계 | **앵커 역할의 플레이어 시각 하나**가 정본. 다음 프레임 시각을 `시각 + dt × 속도` 로 예측해 공유 알림을 그 구간에서 낸다. 다음 프레임에 앵커 시각이 예측과 다르면 오류 로그 + 중단 | 사본 시계가 없어 어긋날 수 없고, 외부가 속도를 바꾼 원인은 조용히 고치지 않고 드러낸다(폴백 금지) |
| 다른 역할 맞추기 | 모든 시작 역할이 같은 프레임에 `play`, 세션 동안 재생 속도 1. 매 프레임 역할 시각 = 앵커 시각 − 진입 시 앵커 시각인지 검사(허용 1 ms), 어긋나면 오류 + 중단 | 같은 dt 의 같은 덧셈이라 구성으로 맞는다. 동기 그룹은 크로스페이드 · 늦은 진입에서 틀린다 |
| 포즈 | 각 역할의 애니메이터가 그래프 밖 클립으로(`play( clip, false, enterBlend )`). 시퀀서 덮어쓰기 슬롯은 쓰지 않는다 | 슬롯은 루트 모션 · 클립 알림 · 워핑이 돌지 않는다 |
| 정렬 | 역할마다 `align` = `none` · `snap`(진입 때 한 번) · `warp`(창이 `warpEnd` 까지 휨, 목표는 매 프레임 앵커에서 다시 계산). 기준은 앵커 루트의 자리 · 요 + `offset` · `yaw` | 루트 보정은 오브젝트 트랜스폼의 일이라 리그 노드가 아니라 루트 모션 수정자 자리다 |
| 계속 붙어 있는 것(말 타기) | 정렬이 아니라 소켓 부착 + `align="none"` | 워핑으로 하면 한 프레임씩 늦는다 |
| 세션이 도는 곳 | `PairedAnimationSystem`(`ISceneFrameSystem`, 키 `PairedAnimation`) — `FrameSystems` 단계, 게임 스레드, 컴포넌트 틱 · 평가 앞 | 다른 오브젝트의 애니메이터를 바꾸므로 틱 워커에서 돌면 경합한다 |
| 평가 순서 | 세션이 모든 결정을 먼저 끝내고 평가 중에는 읽지 않는다. 앵커 시각은 지난 평가 결과. 프롭 부착이 레벨(앵커 먼저)을 만든다 | |
| 공유 알림 | 에셋의 `<Notify name time role/>` 를 그 역할 오브젝트의 `AnimationNotifyComponent` 표로(순간 알림만). 표에 항목이 없으면 시작 거절 | 화살은 기존 `SpawnPrefab` 처리기 경로 |
| 중단 | 필수 역할 · 앵커를 잃으면 세션 중단 — 남은 역할은 각자 `exitState` 로 `exitBlend` 동안 블렌드. 선택 역할을 잃으면 그 역할만 빠진다. 잃은 역할의 애니메이터는 건드리지 않는다 | |
| 데이터 형식 | `*.pairedanim.xml`(`CharacterDataReader` — 모르는 속성 · 원소는 오류) | 알림 표 · 소켓과 같은 관례 |
| 에셋 캐시 · 핫 리로드 | `SharedAssetCache<PairedAnimationDef>`. 다시 읽기는 제자리 교체 없이 표에서 떼어 낸다(`detachShared`), `PairedAnimationComponent` 는 다시 읽은 횟수가 바뀌면 다음 시작 전에 다시 받는다 | 돌고 있는 세션이 역할 인덱스로 에셋을 읽는다 |
| 세트 고르기 | 에셋마다 `<Select>`, `startMatchingSession` 이 자리에 맞는 첫 세트로 시작(없으면 `NoMatchingSet`). 고른 세트의 시작이 거절되면 다음 세트로 넘어가지 않는다 | 통과한 세트가 시작하지 못하는 것은 데이터 · 배치 결함 — 덮지 않는다 |
| 재생 속도 쓰는 컴포넌트 | 세션이 `LocomotionWarpingComponent::setPlayRateHeld` 를 건다 | Object 티어가 Character 의 세션을 include 할 수 없다(`CheckEngineLayers`) |
| 에셋 이름 | `PairedAnimationDef`(+ `RoleDef` · `NotifyDef`) | 저작 데이터 정의는 `Def` |

## 4. 데이터 — `*.pairedanim.xml`

```xml
<PairedAnimation duration="1.6">
  <Role name="Archer" anchor="true" required="true" clip="Bow_Shoot_Archer" enterBlend="0.15" exitState="Idle" exitBlend="0.2"/>
  <Role name="Bow" required="true" clip="Bow_Shoot_Bow" align="none" enterBlend="0" exitState="Bow_Idle" exitBlend="0.1"/>
  <Role name="Arrow" required="false" clip="Arrow_Nock" start="0.2" align="none" enterBlend="0" exitState="Arrow_Hidden" exitBlend="0"/>
  <Notify name="ReleaseArrow" time="0.9" role="Bow"/>
</PairedAnimation>
```

| 원소 · 속성 | 필수 | 뜻 · 검사 |
|---|---|---|
| `PairedAnimation@duration` | 예 | 타임라인 길이(초, 유한 · 양수). 앵커 클립 길이와 1 ms 안에서 같아야 시작한다 |
| `Role@name` | 예 | 겹치면 오류 |
| `Role@clip` | 예 | 클립 폴더의 `<소문자>.animclip`. 파일이 없으면 시작 거절 |
| `Role@required` | 예 | 빠지면 시작하지 않는다 · 잃으면 중단 |
| `Role@anchor` | 아니오(거짓) | 정확히 하나. `required="true"` · `start` 0 · `align` 없음 |
| `Role@start` | 아니오(0) | 들어오는 시각. `start + 클립 길이 ≤ duration` |
| `Role@enterBlend` · `exitBlend` | 예 | 초, 0 이상 |
| `Role@exitState` | 예 | 나갈 때 재생할 상태(그래프 상태나 클립) |
| `Role@align` | 앵커 밖에서 예 | `none` · `snap` · `warp` |
| `Role@offset` · `yaw` | `snap` · `warp` 에서 `offset` 예, `yaw` 아니오(0) | 앵커 루트 기준 자리(m, 앵커 요로 돌림) · 요(도) |
| `Role@warpEnd` | `warp` 에서 예 | 역할 클립 시각(초). `enterBlend < warpEnd ≤ 클립 길이` |
| `Role@maxAlignDistance` | `snap` · `warp` 에서 예 | 시작 때 목표와 거리가 더 멀면 거절(`TooFarToAlign`) |
| `Role@skeleton` | 아니오 | 적으면 참가자 메시의 스켈레톤 경로와 같아야 한다 |
| `Notify@name` · `time` · `role` | 예 | `0 ≤ time ≤ duration`, `role` 은 있는 역할 |
| `Select@role` · `minAngle` · `maxAngle` · `maxDistance` | `<Select>` 는 없거나 하나, 적으면 넷 다 예 | `role` 루트가 앵커 앞(+Z)에서 본 수평 방위각(도, [-180, 180])과 거리(m) 안이면 고른다. `minAngle > maxAngle` 이면 ±180° 를 지나는 뒤쪽 구간. 없으면 어디서나 |

숫자는 모두 유한해야 한다. 조용한 기본값은 없고 "없으면 0" 은 형식이 정한 "적지 않음" 값이다.

## 5. 런타임

```
PairedAnimationComponent(시작하는 쪽, 에셋 경로 · 자기 역할)
   └─ PairedAnimationSystem(씬 프레임 시스템, 세션 목록 · 참가자 독점)
         └─ PairedAnimationSession(역할 → 애니메이터 핸들, 시계, 알림 커서)
```

- **시작**(`startSession`, 게임 스레드 · 틱 밖): **모두 검사하고 그다음에 바꾼다**. 바인딩 → 참가자 해석 → 클립 읽기 · 길이 → `warp` 역할의 모션 워핑 · 루트 모션 → 공유 알림 표 항목 → 진입 거리. 통과하면 `start` 0 역할을 `play`, 속도 1, `snap` 은 자리 맞춤, `warp` 는 목표 · 창.
- **매 프레임**(`update`): 역할 해석 → 앵커 시각 · 예측 검사 → 역할 시각 검사 → 공유 알림(예측 구간, 프레임 시작) → 늦은 역할 진입 → `warp` 갱신 · 창 닫기 → 끝난 역할 나감 → 앵커가 끝에 닿으면 마침. 소켓 자리는 지난 포즈다.
- **진입은 프레임 단위**: 늦은 역할은 앵커 시각이 `start` 를 지난 첫 프레임에 들어와 최대 한 프레임 늦고, 검사도 그 진입 시각 기준이다.
- **끝 · 중단**: 아직 세션 클립을 재생 중인 역할은 `exitState` 로 블렌드, 속도 복구, 열린 워프 창 닫기. 세션은 그 프레임에 상태(`Finished` · `Aborted`)를 보이고 다음 프레임에 목록에서 빠진다.

## 6. 프롭 참가자 — 활과 화살

활은 자기 스켈레톤과 애니메이터가 있는 오브젝트로, 궁수 손 소켓에 붙은 채 `Bow` 역할(`align="none"`)로 참가한다(궁수 다음 레벨에서 평가). 발사는 공유 알림 `ReleaseArrow` → 활 알림 표의 `SpawnPrefab`(투사체, 시위 소켓). 메긴 화살(`Arrow`)은 선택 역할로 클립이 끝나 숨김 상태로 나간다.

## 7. 네트워크(엔진 쪽 복사 · 복원과 키트 `GF_NetPairedAnimation`)

- 시작은 **서버가 결정**한다. 서버는 (에셋 경로, 역할 → 오브젝트 번호 목록, 세션 스냅숏)을 복제하고 클라이언트는 같은 에셋 · 바인딩으로 `restoreSession` 을 부른다. 세션 id 는 씬 시스템의 일련번호이고 복원한 id 보다 다음 일련번호가 커진다.
- 세션 상태는 결정적이다(에셋, 바인딩, 진입 시 앵커 시각, 알림 커서, 상태). 롤백(`setOwnerState`, `SkeletalAnimatorComponent::seekCurrentTime` 모두 main 에 있다)에서는 이 묶음을 소유 상태로 저장하고, 공유 알림은 "이미 낸 시각" 커서로 두 번 내지 않는다.
- 정렬은 서버 권위 트랜스폼을 따른다 — 클라이언트는 예측하되 서버 보정을 받는다.

**복제 상태(엔진 쪽)**: `PairedAnimationSessionSnapshot`(`PairedAnimationSnapshot.h`) — id, 마지막 갱신이 읽은 앵커 시각, 알림 커서, 역할별 단계 · 진입 시각. `writeBytes` · `readBytes`(`BitStream`). 에셋 경로와 오브젝트 id 는 담지 않는다(GameFramework 네트워크 계층의 몫).

- 서버: `captureSnapshot`(재생 중일 때만). 시작 틱만 복제하는 경로는 `makeAtElapsedTime( def, id, 지난 시간 )`(진입 시각을 `start` 로 쳐서 최대 한 프레임 다르다).
- 클라이언트: `restoreSession( manager, def, 바인딩, 스냅샷 )` 이 `start` 와 같은 검사 + 스냅샷이 에셋과 맞는지 보고 그다음 바꾼다. 틀리면 `InvalidSnapshot`, 같은 id 재생 중이면 `SessionIDInUse`.
- 복원은 진입 블렌드 0 으로 재생하고 클립 시각을 옮긴다. 끝났거나 서버가 내보낸 역할은 `exitState`, 아직 안 들어온 역할은 다음 갱신이 들인다. 건너뛴 알림은 울리지 않고, `snap` · `warp` 정렬과 진입 거리 검사는 하지 않는다. 롤백은 `abortSession` 후 복원.

**네트워크 계층(키트 `GF_NetPairedAnimation`, 메시지 영역 `0x60`)**: 계약은 `PairedAnimationReplication.h` 머리말이 정본이다.

- 역할 → 오브젝트 대응은 `NetObjectTable`(Engine/Net)이다 — 씬 오브젝트는 이름 해시, 플레이 중 생긴 오브젝트는 서버 `registerSpawned` 번호를 게임의 스폰 복제가 `bindSpawned` 로 묶은 것. 번호 없는 참가자 · 리소스 경로 없는 에셋(`getSourcePath` 빔)의 세션은 서버가 오류를 남기고 보내지 않는다.
- 늦게 들어온 클라이언트에도 `captureSnapshot` 의 실제 진입 시각을 보낸다. 상태 메시지는 처음 본 갱신에 모두에게, 연결이 열리면 그 연결에 한 번. 끝은 끝 메시지, 클라이언트는 재생 중이면 중단한다. 같은 id 의 상태를 다시 받으면(보정 · 롤백) 옛 세션을 중단하고 다시 복원한다. 롤백은 `makeSessionState` → `PairedAnimationNetState::write` 를 `setOwnerState` 에 싣고 되감은 뒤 `PairedAnimationReplicationClient::applyState`.
- 씬에 `PairedAnimationNetSessionComponent` 하나를 두면 `GameNetSession` 역할대로 서버(`PostUpdate` 에서 보냄) · 클라이언트(게임 프레임 앞에서 받음)가 된다. 클라이언트 게임플레이가 세션을 따로 시작하지 않게 막는 일은 게임 권한의 몫이다.

## 8. 에디터(M3)

`PairedAnimationPanel`(미리보기 창) + `PairedAnimationEditModel`(화면 없는 모델). 계약은 두 파일 머리말이 정본이다. 편집 대상은 `PairedAnimationContentDef`, 쓰기는 `makeXMLText` 하나, 규칙 검사는 그 글을 런타임 로더로 다시 읽는 것 하나(사본 규칙 없음, 어긋나면 저장 안 함). 미리보기는 앵커 원점, 정렬 역할은 `computeAlignTarget`(세션과 같은 식), 프롭은 원점이고 역할마다 고른 프리팹의 애니메이터를 속도 0 으로 스크럽 시각에 둔다. 정렬 역할을 기즈모로 옮기면 `setRoleOffsetFromWorld` 로 오프셋 · 요가 들어간다.

## 9. 모션 매칭과의 관계

모션 매칭(보류 결정, `docs/plans/MotionMatching.md`)과 **독립**이다. 세션은 "이 클립을 재생하라" 와 끝날 때 `exitState` 만 다룬다.

## 10. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| P0 ~ P2 | 로더 · 검사 / 세션 · 시스템 · 컴포넌트 / 정렬 `snap` · `warp` | 완료(Debug 빌드 · `PairedAnimationDefTest` · `PairedAnimationTest` 통과) |
| P3a | 세션 상태 복사 · 복원(7절) | 코드와 `PairedAnimationTest` 작성(빌드 · 테스트 미확인, 병합 뒤 한 번 확인) |
| P3b | 네트워크 계층 — 키트 `GF_NetPairedAnimation` | 코드와 `PairedAnimationReplicationTest` 작성(미확인, 두 프로세스 확인은 "남은 것") |
| M3 | 에디터 미리보기(8절) | 코드와 `PairedAnimationEditModelTest` 작성(미확인, 창 확인은 "남은 것") |
| — | 상호작용 단계 세션 시작 · 에셋 캐시 · 세트 고르기 · 보폭 보정 쥠 | 완료(`InteractionTest` · `PairedAnimationTest`) |
| — | Shooter3D 궁수 · 활 · 화살 시나리오 | `automation/showcase/bowshoot.scenario.xml`(자동 수집 밖), 에셋은 `Scripts/dev/MakeBowProps.py`. Release 네 백엔드 통과 · 당김 자세 화면 확인(활은 왼손 소켓 `BowGrip`) |
| — | 프롭 역할을 데이터로 묶기(`_bStartOnPlay`, 탐침 `PairedAnimation.SessionState`) | 코드와 `PairedAnimationTest.ComponentBindsRolesNamedByOtherComponents` 작성(빌드 미확인) |

## 남은 것

- 에디터 미리보기(M3) 화면 확인: 창을 열어 역할 프리팹을 놓고 스크럽 · 기즈모로 오프셋을 옮기는 에디터 시나리오(`-EnableEditor`, `CaptureEditorUI`).
- 네트워크(P3b) 두 프로세스 확인: `Server` 와 `App -gv_netConnectAddress` 에서 세션 시작 · 늦은 참가 · 끝이 복제되는지. 스폰 자체를 복제하는 것(무엇을 스폰했는지 클라이언트에 알리고 묶기)은 게임의 몫이고 아직 쓰는 게임이 없다.
- 쇼케이스(`bowshoot`): ① 쇼케이스 카메라(옆 고정)로는 놓은 뒤 날아가는 투사체가 잡히지 않아 `_localDirection` 이 화살 앞인지 못 봤다 — 앞에서 보는 카메라를 씬에 더해 본다. ② 당김 내내 화살 길이만 한 가는 줄 하나가 활 쪽에 붙어 보인다(활 메시의 시위 띠 스킨 의심). ③ 날아가는 동안 화살 머리가 속도 쪽을 보게 하기(지금은 처음 자세 그대로 물리가 돌린다).

## 11. 알려진 한계

- 재생 속도를 매 프레임 쓰는 컴포넌트는 `LocomotionWarpingComponent` 하나이고 세션이 쥐어 둔다. 새 컴포넌트가 생기면 같은 쥠을 받아야 한다 — 안 받으면 시각 검사가 어긋남으로 보고 중단한다.
- 상호작용 `Press` 단계는 누름으로 이미 끝나므로 세션 시작이 거절되면 경고만 남는다. 누르고 있기 · 연타 단계는 취소된다.
- `AnimationRewind` 스크럽 중에는 시각이 예측과 달라 중단된다(개발 빌드 진단 기능).
- 샘플 에셋 `Resource/game/shooter3d/data/anim/bow_shoot.pairedanim.xml` 의 쇼케이스는 자동 수집 밖이라 손으로 돌린다.
- 프롭 역할 묶기는 시작 때 매니저 전체를 훑는다. 같은 에셋 · 같은 역할을 적은 프롭이 씬에 둘 이상이면 거절한다 — 부르는 쪽이 바인딩을 넘긴다.
- 복원은 `warp` 창을 다시 열지 않는다. 창 도중에 이어 받은 클라이언트는 휘지 않은 루트 모션으로 가고 서버 보정을 받는다.

## 12. 다른 작업과 겹치는 자리

- `SkeletalAnimatorComponent` 는 고치지 않았다. feat21 이 `play` 의 블렌드 의미를 바꾸면 `enterBlend` · `exitBlend` 를 다시 본다.
- feat20 이 레이어 · 마스크를 데이터로 옮기면 세션 중 레이어(상체 조준) 끄기는 역할 속성으로 정할 후속 자리다.
- feat14 의 `TComponentHandle<T>` 가 들어오면 바인딩의 `ComponentHandle` 을 `TComponentHandle<SkeletalAnimatorComponent>` 로 바꾼다.
