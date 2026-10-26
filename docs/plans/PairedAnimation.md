# 페어드 애니메이션(여러 개체가 한 타임라인으로 도는 애니메이션)

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: D4-6 · 진행: 6/6

**남은 확인(통합에서 확인):** 매니페스트 id `paired-animation-tests`, `paired-animation-editor-window`, `paired-animation-two-process`, `bowshoot-projectile-checks` — 아래 "남은 것". 확인이 끝나면 통합 담당이 이 문서를 닫는다.

활과 화살, 잡기 · 처형, 말 타기, 문 열기처럼 **여러 개체가 한 타임라인에 묶여 같이 도는 애니메이션**(언리얼 Contextual Animation)의 일반 구조입니다([게임플레이 후보](Candidates-gameplay.md) 의 애니메이션 절과 이웃). P0 ~ P2, 상호작용 연결 · 에셋 캐시 · 세트 고르기 · 보폭 보정 쥠은 코드와 테스트(`PairedAnimationTest` · `PairedAnimationDefTest` · `InteractionTest`)로 들어갔습니다.

## 1. 지금 엔진에 있는 것과 없는 것

루트 모션 · 모션 워핑 · 알림 · 소켓 부착은 **개체 하나 단위**였고 다개체 세션이 없었습니다. 세션은 기존 조각을 이렇게 씁니다: `AnimationSystem` 평가 **앞**(프레임 시스템 단계)에서 게임 스레드로 돌고 평가 중에는 읽히지 않음 · `SkeletalAnimatorComponent` 는 공개 함수(`play` · `setPlayRate` · `getGraphPlayer` · `findClip` · `preloadClip`)만 쓰고 고치지 않음 · 세션 시계는 앵커 플레이어의 시각 · `MotionWarpingComponent` 를 그대로 씀(`setWarpTarget` + `beginWindow` / `endWindow`, 새 워핑 노드 없음) · 공유 알림은 역할 오브젝트의 `AnimationNotifyComponent`(훅 `processExternalNotify`) · 프롭은 궁수 손 소켓에 붙은 채 참가(부착이 평가 의존을 만듦) · 세션 클립은 반복하지 않아 `AnimationCrowd` 에서 자동으로 빠짐 · 상호작용 단계의 `pairedAnimation` · `interactorRole` · `interactableRole` 이 세션을 시작 · `LocomotionWarpingComponent` 는 세션이 `setPlayRateHeld` 로 쥐고 놓음(Object 티어라 세션을 모름). **`AnimationSyncGroup` 은 쓰지 않습니다** — 크로스페이드 중 리더 슬롯이 이전 클립이라 위상이 틀어지고 역할마다 시작 시각이 달라 맞지 않습니다.

## 2. 상용 선례

언리얼 Montage(역할마다 일회성 클립) · Contextual Animation(역할 목록 + 클립 + 앵커 기준 오프셋 = 에셋, 장면 인스턴스 = 세션) · Motion Warping(`warp` 정렬) · 유니티 Timeline(한 시계 + 역할 바인딩, 포즈는 각 애니메이터가 냄) · Naughty Dog(앵커 역할이 기준점) · 처형 · 잡기류(`snap` · `warp`, `maxAlignDistance`, `<Select>` 방위각 · 거리). 문서 · 기억 수준 근거입니다.

## 3. 결정

| 물음 | 결정 | 이유 |
|---|---|---|
| 시계 | **앵커 역할의 플레이어 시각 하나**가 정본. 다음 프레임 시각을 `시각 + dt × 속도` 로 예측해 공유 알림을 그 구간에서 낸다. 예측과 다르면 오류 로그 + 중단 | 사본 시계가 없어 어긋날 수 없고 외부가 속도를 바꾼 원인을 조용히 고치지 않는다(폴백 금지) |
| 다른 역할 맞추기 | 시작 역할 모두 같은 프레임에 `play`, 세션 동안 재생 속도 1. 매 프레임 역할 시각 = 앵커 시각 − 진입 시 앵커 시각인지 검사(1 ms), 어긋나면 오류 + 중단 | 같은 dt 의 같은 덧셈이라 구성으로 맞는다 |
| 포즈 | 각 애니메이터가 그래프 밖 클립으로(`play( clip, false, enterBlend )`). 시퀀서 덮어쓰기 슬롯 안 씀 | 슬롯은 루트 모션 · 알림 · 워핑이 안 돈다 |
| 정렬 | `align` = `none` · `snap`(진입 때 한 번) · `warp`(`warpEnd` 까지 휨, 목표는 매 프레임 앵커에서 재계산). 기준은 앵커 루트 자리 · 요 + `offset` · `yaw` | 루트 보정은 오브젝트 트랜스폼의 일이라 루트 모션 수정자 자리 |
| 계속 붙어 있는 것(말 타기) | 소켓 부착 + `align="none"` | 워핑으로 하면 한 프레임씩 늦는다 |
| 세션이 도는 곳 | `PairedAnimationSystem`(`ISceneFrameSystem`, 키 `PairedAnimation`) — `FrameSystems` 단계, 게임 스레드, 컴포넌트 틱 · 평가 앞 | 다른 오브젝트의 애니메이터를 바꾸므로 틱 워커에서 돌면 경합 |
| 공유 알림 | 에셋의 `<Notify name time role/>` 를 그 역할 오브젝트의 알림 표로(순간 알림만). 표에 항목이 없으면 시작 거절 | 화살은 기존 `SpawnPrefab` 처리기 경로 |
| 중단 | 필수 역할 · 앵커를 잃으면 중단, 남은 역할은 `exitState` 로 `exitBlend` 동안 블렌드. 선택 역할을 잃으면 그 역할만 빠짐. 잃은 역할의 애니메이터는 안 건드림 | |
| 데이터 형식 | `*.pairedanim.xml`(`CharacterDataReader` — 모르는 속성 · 원소는 오류) | 알림 표 · 소켓과 같은 관례 |
| 에셋 캐시 · 핫 리로드 | `SharedAssetCache<PairedAnimationDef>`. 다시 읽기는 제자리 교체 없이 표에서 떼어 내고(`detachShared`) 컴포넌트가 다음 시작 전에 다시 받음 | 돌고 있는 세션이 역할 인덱스로 에셋을 읽는다 |
| 세트 고르기 | 에셋마다 `<Select>`, `startMatchingSession` 이 자리에 맞는 첫 세트로 시작(없으면 `NoMatchingSet`). 고른 세트가 시작을 거절해도 다음 세트로 안 넘어감 | 통과한 세트의 시작 실패는 데이터 · 배치 결함 — 덮지 않는다 |
| 재생 속도 쓰는 컴포넌트 | 세션이 `LocomotionWarpingComponent::setPlayRateHeld` 를 건다 | Object 티어가 Character 의 세션을 include 할 수 없다(`CheckEngineLayers`) |

## 4. 데이터 — `*.pairedanim.xml`

```xml
<PairedAnimation duration="1.6">
  <Role name="Archer" anchor="true" required="true" clip="Bow_Shoot_Archer" enterBlend="0.15" exitState="Idle" exitBlend="0.2"/>
  <Role name="Bow" required="true" clip="Bow_Shoot_Bow" align="none" enterBlend="0" exitState="Bow_Idle" exitBlend="0.1"/>
  <Role name="Arrow" required="false" clip="Arrow_Nock" start="0.2" align="none" enterBlend="0" exitState="Arrow_Hidden" exitBlend="0"/>
  <Notify name="ReleaseArrow" time="0.9" role="Bow"/>
</PairedAnimation>
```

필수: `PairedAnimation@duration`(유한 · 양수, 앵커 클립 길이와 1 ms 안), `Role@name`(겹치면 오류) · `clip`(`<소문자>.animclip`, 파일 없으면 시작 거절) · `required` · `enterBlend` · `exitBlend`(0 이상) · `exitState`, 앵커 밖 `align`. 정확히 하나인 `anchor="true"` 는 `required="true"` · `start` 0 · `align` 없음. `start`(기본 0, `start + 클립 길이 ≤ duration`), `snap` · `warp` 에서는 `offset`(앵커 요로 돌린 앵커 루트 기준 자리, m) · `maxAlignDistance`(시작 때 목표와 더 멀면 `TooFarToAlign` 거절)가 필수이고 `yaw`(도, 기본 0), `warp` 는 `warpEnd`(`enterBlend < warpEnd ≤ 클립 길이`)도 필수입니다. `Role@skeleton` 은 적으면 참가자 메시와 같아야 합니다. `Notify`: `name` · `time`(`0 ≤ time ≤ duration`) · `role`. `<Select role minAngle maxAngle maxDistance>` 는 없거나 하나이고 적으면 넷 다 필수이며, `role`(앵커가 아닌 역할) 루트가 앵커 앞(+Z)에서 본 수평 방위각(도)과 거리(m) 안이면 고릅니다(`minAngle > maxAngle` 은 ±180° 를 지나는 뒤쪽 구간). 숫자는 유한해야 하고 조용한 기본값은 없습니다.

## 5. 런타임

`PairedAnimationComponent`(시작하는 쪽) → `PairedAnimationSystem`(씬 프레임 시스템, 참가자 독점) → `PairedAnimationSession`(역할 → 애니메이터 핸들, 시계, 알림 커서).

- **시작**(`startSession`, 게임 스레드 · 틱 밖): **모두 검사하고 그다음에 바꾼다** — 바인딩 → 참가자 해석 → 클립 · 길이 → `warp` 역할의 모션 워핑 · 루트 모션 → 공유 알림 표 항목 → 진입 거리. 통과하면 `start` 0 역할을 `play`, 속도 1, `snap` 은 자리 맞춤, `warp` 는 목표 · 창.
- **매 프레임**(`update`): 역할 해석 → 앵커 시각 · 예측 검사 → 역할 시각 검사 → 공유 알림(예측 구간, 프레임 시작) → 늦은 역할 진입 → `warp` 갱신 · 창 닫기 → 끝난 역할 나감 → 앵커가 끝에 닿으면 마침. 진입은 프레임 단위라 늦은 역할은 최대 한 프레임 늦고 검사도 그 진입 시각 기준입니다. 소켓 자리는 지난 포즈입니다.
- **끝 · 중단**: 아직 세션 클립을 재생 중인 역할은 `exitState` 로 블렌드, 속도 복구, 열린 워프 창 닫기. 그 프레임에 `Finished` · `Aborted` 를 보이고 다음 프레임에 목록에서 빠집니다.

## 6. 프롭 참가자 — 활과 화살

활은 자기 스켈레톤과 애니메이터가 있는 오브젝트로 궁수 손 소켓에 붙은 채 `Bow` 역할(`align="none"`)로 참가합니다. 발사는 공유 알림 `ReleaseArrow` → 활 알림 표의 `SpawnPrefab`, 메긴 화살(`Arrow`)은 선택 역할로 클립이 끝나 숨김 상태로 나갑니다.

## 7. 네트워크(엔진 쪽 복사 · 복원과 키트 `GF_NetPairedAnimation`)

- 시작은 **서버가 결정**하고 (에셋 경로, 역할 → 오브젝트 번호 목록, 세션 스냅숏)을 복제하며 클라이언트는 같은 에셋 · 바인딩으로 `restoreSession` 을 부릅니다. 롤백에서는 (에셋, 바인딩, 진입 시 앵커 시각, 알림 커서, 상태)를 `setOwnerState` 에 싣고 공유 알림은 "이미 낸 시각" 커서로 두 번 내지 않습니다. 정렬은 서버 권위 트랜스폼을 따르고 클라이언트는 예측 + 서버 보정입니다.
- **복제 상태**: `PairedAnimationSessionSnapshot`(`PairedAnimationSnapshot.h`, `BitStream` 바이트)에 id · 마지막 갱신의 앵커 시각 · 알림 커서 · 역할별 단계 · 진입 시각이 있고 에셋 경로와 오브젝트 id 는 담지 않습니다(GameFramework 네트워크 계층의 몫). 서버는 `captureSnapshot`(재생 중일 때만), 시작 틱만 복제하는 경로는 `makeAtElapsedTime`(진입 시각을 `start` 로 쳐 최대 한 프레임 다름), 클라이언트는 `restoreSession( manager, def, 바인딩, 스냅샷 )`(`start` 와 같은 검사 + 스냅샷이 에셋과 맞는지 보고 바꿈, 실패는 `InvalidSnapshot` · `SessionIDInUse`). 복원은 진입 블렌드 0 으로 클립 시각을 옮기고, 끝난 역할은 `exitState`, 건너뛴 알림은 울리지 않으며 `snap` · `warp` 정렬 · 진입 거리 검사는 하지 않습니다. 롤백은 `abortSession` 후 복원.
- **네트워크 계층**(메시지 영역 `0x60`, 계약은 `PairedAnimationReplication.h` 머리말): 역할 → 오브젝트 대응은 `NetObjectTable`(씬 오브젝트는 이름 해시, 플레이 중 생긴 오브젝트는 서버 `registerSpawned` 번호를 게임의 스폰 복제가 `bindSpawned` 로 묶음). 번호 없는 참가자나 리소스 경로 없는 에셋(`getSourcePath` 빔)의 세션은 서버가 오류를 남기고 보내지 않습니다. 늦게 들어온 클라이언트에도 실제 진입 시각(`captureSnapshot`)을 보내고, 상태는 처음 본 갱신에 모두에게 · 연결이 열리면 그 연결에 한 번, 끝은 끝 메시지(재생 중이면 중단). 같은 id 의 상태를 다시 받으면 옛 세션을 중단하고 다시 복원합니다. 롤백은 `makeSessionState` → `PairedAnimationNetState::write` → `setOwnerState`, 되감은 뒤 `PairedAnimationReplicationClient::applyState`. 씬에 `PairedAnimationNetSessionComponent` 하나를 두면 `GameNetSession` 역할대로 서버(`PostUpdate`)/클라이언트(게임 프레임 앞)가 되고, 클라이언트 게임플레이가 세션을 따로 시작하지 않게 막는 일은 게임 권한의 몫입니다.

## 8. 에디터(M3)

`PairedAnimationPanel`(미리보기 창) + `PairedAnimationEditModel`(화면 없는 모델). 계약은 두 파일 머리말이 정본입니다. 쓰기는 `PairedAnimationDef::makeXMLText` 하나, 규칙 검사는 그 글을 런타임 로더로 다시 읽는 것 하나(사본 규칙 없음, 어긋나면 저장 안 함). 미리보기는 앵커 원점 · 정렬 역할은 `computeAlignTarget`(세션과 같은 식) · 프롭은 원점이고 역할마다 고른 프리팹의 애니메이터를 속도 0 으로 스크럽 시각에 둡니다. 정렬 역할을 기즈모로 옮기면 `setRoleOffsetFromWorld` 로 오프셋 · 요가 들어갑니다.

## 9. 모션 매칭과의 관계

모션 매칭(`docs/plans/MotionMatching.md`)과 **독립**입니다. 세션은 "이 클립을 재생하라" 와 끝날 때 `exitState` 만 다룹니다.

## 10. 단계

P0 ~ P2(로더 · 세션 · 정렬)와 상호작용 연결 · 에셋 캐시 · 세트 고르기 · 보폭 보정 쥠은 **완료**(`PairedAnimationDefTest` · `PairedAnimationTest` · `InteractionTest`). P3a(세션 복사 · 복원, 7절), P3b(키트 `GF_NetPairedAnimation`, `PairedAnimationReplicationTest`), M3(에디터, 8절, `PairedAnimationEditModelTest`), 프롭 역할을 데이터로 묶기(`_bStartOnPlay`, 탐침 `PairedAnimation.SessionState`, `PairedAnimationTest.ComponentBindsRolesNamedByOtherComponents`)는 코드와 테스트가 있고 빌드 · 테스트 미확인입니다. Shooter3D 궁수 · 활 · 화살 시나리오 `automation/showcase/bowshoot.scenario.xml`(자동 수집 밖, 에셋은 `Scripts/dev/MakeBowProps.py`)은 Release 네 백엔드 통과 · 당김 자세 화면 확인(활은 왼손 소켓 `BowGrip`)입니다.

## 남은 것

- 에디터 미리보기(M3) 화면 확인: 창을 열어 역할 프리팹을 놓고 스크럽 · 기즈모로 오프셋을 옮기는 에디터 시나리오(`-EnableEditor`, `CaptureEditorUI`).
- 네트워크(P3b) 두 프로세스 확인: `Server` 와 `App -gv_netConnectAddress` 에서 세션 시작 · 늦은 참가 · 끝이 복제되는지. 스폰 자체를 복제하는 것은 게임의 몫이고 아직 쓰는 게임이 없습니다.
- 쇼케이스(`bowshoot`): ① 옆 고정 카메라로는 놓은 뒤 날아가는 투사체가 안 잡혀 `_localDirection` 이 화살 앞인지 못 봤다 — 앞에서 보는 카메라를 씬에 더해 본다. ② 당김 내내 화살 길이만 한 가는 줄 하나가 활 쪽에 붙어 보인다(활 메시의 시위 띠 스킨 의심). ③ 날아가는 동안 화살 머리가 속도 쪽을 보게 하기(지금은 처음 자세 그대로).

## 11. 알려진 한계

- 재생 속도를 매 프레임 쓰는 컴포넌트가 새로 생기면 세션 쥠을 받아야 합니다 — 안 받으면 시각 검사가 어긋남으로 보고 중단합니다.
- 상호작용 `Press` 단계는 세션 시작이 거절되면 경고만 남습니다(누르고 있기 · 연타 단계는 취소). `AnimationRewind` 스크럽 중에는 시각이 예측과 달라 중단됩니다.
- 프롭 역할 묶기는 시작 때 매니저 전체를 훑고, 같은 에셋 · 같은 역할의 프롭이 씬에 둘 이상이면 거절합니다(부르는 쪽이 바인딩을 넘김).
- 복원은 `warp` 창을 다시 열지 않아 창 도중에 이어 받은 클라이언트는 휘지 않은 루트 모션으로 가고 서버 보정을 받습니다.

## 12. 다른 작업과 겹치는 자리

feat21 이 `play` 의 블렌드 의미를 바꾸면 `enterBlend` · `exitBlend` 를 다시 보고, feat20 이 레이어를 데이터로 옮기면 세션 중 레이어(상체 조준) 끄기를 역할 속성으로 정할 후속 자리이며, feat14 의 `TComponentHandle<T>` 가 들어오면 바인딩의 `ComponentHandle` 을 `TComponentHandle<SkeletalAnimatorComponent>` 로 바꿉니다.
