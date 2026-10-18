# 페어드 애니메이션(여러 개체가 한 타임라인으로 도는 애니메이션)

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-6 · 진행: 3/5

활과 화살, 잡기 · 처형, 말 타기, 문 열기처럼 **여러 개체가 한 타임라인에 묶여 같이 도는 애니메이션**(언리얼의 Contextual Animation, 흔히 페어드 · 싱크드 · 인터랙션 애니메이션)의 일반 구조다.
애니메이션 영역의 한 단위다([게임플레이 후보](Candidates-gameplay.md) 의 애니메이션 절과 이웃). 첫 단위(P0 ~ P2)는 `feat22` 브랜치에 코드와 테스트로 들어갔고, **빌드 없이** 썼다(통합 담당이 한 워크트리에 모아 빌드 한 번으로 검증한다).

## 1. 지금 엔진에 있는 것과 없는 것

일반화된 **다개체 세션이 없다**. 루트 모션 · 모션 워핑 · 알림 · 소켓 부착은 모두 **개체 하나 단위**로 있다. 코드로 읽은 지금 구조는 이렇다.

| 조각 | 자리 | 하는 일 | 세션과의 관계 |
|---|---|---|---|
| `AnimationSystem` | `Engine/Object/SceneAnimation` | 유닛(`SkeletalMeshComponent`)을 의존 레벨로 나눠 단계(`Time` → `BasePose` → `Attachment` → `PostProcess` → `SkinPalette`)마다 병렬 평가하고, 끝에 게임 스레드에서 `finishAnimationFrame`(알림 · 루트 모션) | 세션은 이 평가 **앞**(프레임 시스템 단계)에서 게임 스레드로 돈다. 평가 중에는 세션을 아무도 읽지 않는다 |
| `SkeletalAnimatorComponent` | `Engine/Object/Component/3D` | 그래프 플레이어 · 레이어 · 동기 그룹 · 루트 모션 · 시퀀서 덮어쓰기 슬롯 | 세션은 공개 함수(`play` · `setPlayRate` · `getGraphPlayer` · `findClip` · `preloadClip`)만 쓴다. **이 파일은 고치지 않는다**(feat20 · feat21 이 만지는 중) |
| `AnimationGraphPlayer` · `AnimationPlayer` | `Engine/Animation` | 두 슬롯 크로스페이드, 커서, 알림 수집. 그래프 밖 클립도 이름으로 재생 | 세션 시계는 앵커 역할의 플레이어 시각을 읽는다(현재 슬롯이 아니면 다음 슬롯) |
| `AnimationSyncGroup` | `Engine/Animation/AnimationPlayer.h` | 마커 없는 위상 동기(가중치가 큰 리더의 정규화 위치로) | **쓰지 않는다** — 크로스페이드 중에는 리더의 현재 슬롯이 이전 클립이라 위상이 틀어지고, 역할마다 시작 시각이 다르면 위상이 맞지 않는다 |
| `MotionWarpingComponent` | `Engine/Object/SceneAnimation` | `IRootMotionModifier` 로 루트 모션을 휘어 창 끝에 이름 붙은 목표(자리 · 요)에 닿게 한다. 창은 보통 `MotionWarp` 구간 알림이 연다 | 세션이 **그대로 쓴다**: 목표를 넣고(`setWarpTarget`) 창을 직접 연다(`beginWindow` / `endWindow`). 새 워핑 노드는 만들지 않는다 |
| `AnimationNotifyComponent` + 처리기 레지스트리 | `Engine/Character/AnimationNotify` | 클립 알림 → 표(`*.notifies.xml`)의 처리기(`SpawnPrefab` · `GameplayEvent` · …) | 공유 타임라인 알림은 역할 오브젝트의 이 컴포넌트로 보낸다. 작은 훅 하나(`processExternalNotify`)를 더했다 |
| 소켓(`SocketSetComponent` · `SocketBindingComponent`) | `Engine/Character/Socket` | 본에 붙은 이름 자리, 오브젝트 부착 | 활(프롭)은 궁수 손 소켓에 붙은 채로 역할에 참가한다. 부착이 평가 의존(레벨)을 이미 만든다 |
| `AnimationCrowd` | `Engine/Object/SceneAnimation` | 반복 클립 하나를 재생하는 유닛의 포즈를 묶음과 나눈다 | 세션 클립은 반복하지 않으므로 `describeSharedPose` 가 거짓 — 세션 참가자는 자동으로 군중에서 빠진다 |
| `InteractableComponent` | `GameFramework/.../Interaction` | 상호작용 정의와 맞춤 마커(소켓)로 맞춤 지점 계산 | 스마트 오브젝트 자리. 후속에서 상호작용이 세션을 시작하게 잇는다 |

## 2. 상용 선례

근거 수준: [코드] 소스를 읽음 · [문서] 공식 문서 · [기억] 발표 · 문서를 기억으로 · [추정] 공개 자료 없이 추론.

| 선례 | 무엇 | 이 설계에 가져온 것 | 근거 |
|---|---|---|---|
| 언리얼 Animation Montage | 한 메시의 슬롯에 재생하는 일회성 클립(섹션 · 알림 · 루트 모션) | 역할마다 "자기 클립 하나를 일회성으로" 재생하고, 끝나면 종료 상태로 블렌드아웃 | [문서] |
| 언리얼 Sync Group / Sync Marker | 리더-팔로워 위상 동기(마커가 있으면 발 위상) | 리더 = 앵커 역할이라는 생각. 단 위상 동기는 쓰지 않고 같은 프레임 · 같은 속도 시작 + 시각 검사로 맞춘다(1절) | [문서] |
| 언리얼 Motion Warping | 구간 알림 창과 이름 붙은 워프 목표로 루트 모션을 휨 | 정렬 방식 `warp` 가 엔진의 `MotionWarpingComponent` 를 그대로 쓴다 | [문서] · 엔진 쪽 [코드] |
| 언리얼 Contextual Animation (`ContextualAnimSceneAsset`) | 역할 목록, 역할마다 몽타주, 정렬 섹션 · 워프 포인트, 장면 인스턴스가 몽타주를 함께 재생(`MontageSync_Follow` 로 리더를 따름) | 데이터 에셋 = 역할 목록 + 클립 + 앵커 기준 오프셋, 런타임 = 장면 인스턴스(여기서는 세션) | [기억] |
| 언리얼 Gameplay Interactions · Smart Objects | 월드 오브젝트가 상호작용 슬롯을 광고하고, 예약한 쪽이 상호작용(컨텍스추얼 애니 포함)을 실행 | 진입 조건(거리)을 데이터로, 세션 시작은 게임플레이(상호작용)가 부른다. 연결은 후속 | [기억] |
| 유니티 Timeline | `PlayableDirector` 한 시계가 트랙마다 바인딩된 오브젝트를 구동 | "한 시계 + 역할 → 오브젝트 바인딩" 모양. 단 포즈는 각 애니메이터가 내고(루트 모션 · 알림 · 워핑을 그대로 쓰려고) 시계는 앵커가 갖는다 | [문서] |
| Naughty Dog 컨텍스추얼 애니메이션(GDC 발표들) | 상호작용을 한 장면에서 함께 저작하고, 공통 기준점(애니메이션 기준점, apReference 류)에 모든 참가자의 루트를 상대로 둔다 | 오프셋은 "앵커 루트 기준 역할 루트" 하나 — 기준점을 별도 오브젝트로 두지 않고 앵커 역할이 기준점이다 | [기억] |
| 'Assassin's Creed' 처형 · 'The Last of Us' 잡기 | 상대 위치 · 각도로 처형 세트를 고르고, 짧은 창에서 공격자를 피해자 기준 자리로 미끄러뜨려 맞춤 | `snap`(즉시) · `warp`(창 동안 휨) 두 정렬, 진입 조건 `maxAlignDistance`. 세트 고르기는 후속 | [기억] · [추정] |

## 3. 결정

| 물음 | 결정 | 이유 |
|---|---|---|
| 시계는 무엇인가 | **앵커 역할의 플레이어 시각 하나**가 정본이다. 세션은 매 프레임 그것을 읽고, 다음 프레임 시각을 `시각 + dt × 속도` 로 예측해 공유 알림을 그 구간에서 낸다. 다음 프레임에 앵커 시각이 예측과 다르면(속도 · 시각을 누가 바꿈) 오류 로그 + 중단 | 사본 시계를 두지 않으니 둘이 어긋날 수 없다. 어긋나는 원인(외부가 속도를 바꿈)은 조용히 고치지 않고 드러낸다(폴백 금지) |
| 다른 역할을 어떻게 맞추나 | 모든 시작 역할이 **같은 프레임에 `play`**, 세션 동안 **재생 속도 1**(끝나면 원래 값). 매 프레임 역할 시각 = 앵커 시각 − 진입 시 앵커 시각인지 검사(허용 1 ms), 어긋나면 오류 + 중단 | 같은 dt 의 같은 덧셈이라 구성으로 맞는다. 동기 그룹은 크로스페이드 · 늦은 진입에서 틀린다(1절) |
| 포즈는 누가 내나 | 각 역할의 **애니메이터가 그래프 밖 클립으로** 낸다(`play( clip, false, enterBlend )`). 시퀀서 덮어쓰기 슬롯은 쓰지 않는다 | 슬롯은 포즈만 섞고 루트 모션 · 클립 알림 · 워핑이 돌지 않는다. `play` 경로는 셋 다 이미 된다 |
| 정렬 | 역할마다 `align` = `none`(부착된 프롭 · 앵커) · `snap`(진입 때 한 번 자리 · 요를 맞춤) · `warp`(`MotionWarpingComponent` 창이 `warpEnd` 까지 휨, 목표는 매 프레임 앵커에서 다시 계산 = 계속 맞춤). 기준은 앵커 루트의 자리 · 요 + 역할의 `offset` · `yaw` | 루트 보정은 오브젝트 트랜스폼의 일이라 리그 노드(`PostProcess` 단계, 워커에서 자기 포즈만 쓴다)가 아니라 루트 모션 수정자 자리다. 엔진에 이미 있다 |
| 말 타기처럼 계속 붙어 있어야 하는 것 | 정렬이 아니라 **소켓 부착**(기존 경로)으로 붙이고 `align="none"` | 계속 맞춤을 워핑으로 하면 한 프레임씩 늦는다. 부착은 평가 의존까지 만든다 |
| 세션은 어디서 도나 | `PairedAnimationSystem`(`ISceneFrameSystem`, 키 `PairedAnimation`) — `FrameSystems` 단계, 게임 스레드, 컴포넌트 틱 · 애니메이션 평가 앞 | "병렬 틱보다 먼저 · 게임 스레드" 자리가 이것뿐이다. 다른 오브젝트의 애니메이터를 바꾸므로 틱 워커에서 돌면 경합한다 |
| 평가 순서 · 의존성 | 세션이 게임 스레드에서 모든 결정을 먼저 끝내고, 평가 중에는 아무것도 읽지 않는다. 앵커 시각은 지난 평가의 결과를 읽는다. 프롭이 앵커 손에 붙어 있으면 부착이 레벨(앵커 먼저)을 만든다 | 병렬 평가에서 세션 시계는 읽기 전용이라는 요구를 "아예 읽지 않음" 으로 푼다 |
| 공유 알림 | 에셋의 `<Notify name time role/>` 를 그 역할 오브젝트의 `AnimationNotifyComponent` 표로 보낸다(순간 알림만). 표에 항목이 없으면 **시작 거절** | 화살은 기존 경로(`SpawnPrefab` 처리기)로 생긴다. 이름 → 처리기는 캐릭터 데이터가 정한다 |
| 중단 정책 | 필수 역할 · 앵커를 잃으면(오브젝트가 없어짐, 애니메이터가 다른 것을 재생함) 세션 중단 — 남은 역할은 각자 `exitState` 로 `exitBlend` 동안 블렌드. 선택 역할을 잃으면 그 역할만 빠진다 | 데이터 없이 규칙 하나. 잃은 역할의 애니메이터는 건드리지 않는다(게임플레이가 이미 다른 것을 재생 중) |
| 데이터 형식 | `*.pairedanim.xml`(캐릭터 데이터 XML 관례, `CharacterDataReader` — 모르는 속성 · 원소는 오류) | 알림 표 · 소켓과 같은 사람이 같은 도구로 고친다 |
| 에셋 이름 | `PairedAnimationDef`(+ `RoleDef` · `NotifyDef`) | 저작 데이터 정의는 `Def`(`docs/agents/NamingCpp.md` `Naming`) |

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
| `PairedAnimation@duration` | 예 | 공유 타임라인 길이(초, 유한 · 양수). 앵커 클립 길이와 1 ms 안에서 같아야 시작한다 |
| `Role@name` | 예 | 역할 이름. 겹치면 오류 |
| `Role@clip` | 예 | 그 역할 애니메이터의 클립 이름(클립 폴더의 `<소문자>.animclip`). 비면 오류. 파일이 없으면 시작 거절 |
| `Role@required` | 예 | 빠지면 시작하지 않는다 · 잃으면 중단 |
| `Role@anchor` | 아니오(없으면 거짓) | 정확히 하나. 앵커는 `required="true"` · `start` 0 · `align` 을 적지 않는다 |
| `Role@start` | 아니오(없으면 0) | 공유 타임라인에서 그 역할이 들어오는 시각. `start + 클립 길이 ≤ duration` |
| `Role@enterBlend` · `exitBlend` | 예 | 들어올 때 · 나갈 때 블렌드(초, 0 이상) |
| `Role@exitState` | 예 | 나갈 때 재생할 상태(그래프 상태나 클립) |
| `Role@align` | 앵커 밖에서 예 | `none` · `snap` · `warp` |
| `Role@offset` · `yaw` | `snap` · `warp` 에서 `offset` 예, `yaw` 아니오(없으면 0) | 앵커 루트 기준 그 역할 루트의 자리(m, 앵커 요로 돌린다) · 요(도). 정렬 시각(`snap` 은 진입, `warp` 는 `warpEnd`)의 관계다 |
| `Role@warpEnd` | `warp` 에서 예 | 역할 클립 시각(초). `enterBlend < warpEnd ≤ 클립 길이` — 크로스페이드가 끝나야 루트 모션 프레임의 클립이 이 클립이 된다 |
| `Role@maxAlignDistance` | `snap` · `warp` 에서 예 | 진입 조건. 시작 때 역할 루트와 목표의 거리가 이보다 멀면 시작 거절(`TooFarToAlign`) |
| `Role@skeleton` | 아니오 | 적으면 참가자 메시의 스켈레톤 경로와 같아야 한다 |
| `Notify@name` · `time` · `role` | 예 | 공유 알림. `0 ≤ time ≤ duration`, `role` 은 있는 역할 |

숫자는 모두 유한해야 한다(`nan` · `inf` 는 오류). 어느 것도 조용히 기본값이 되지 않는다 — "없으면 0" 은 형식이 정한 "적지 않음" 값이고, 필수 값이 빠지면 읽기가 실패한다.

## 5. 런타임

```
PairedAnimationComponent(시작하는 쪽, 에셋 경로 · 자기 역할)
   └─ PairedAnimationSystem(씬 프레임 시스템, 세션 목록 · 참가자 독점)
         └─ PairedAnimationSession(역할 → 애니메이터 핸들, 시계, 알림 커서)
```

- **시작**(`startSession`, 게임 스레드 · 틱 밖): 먼저 **모두 검사하고 그다음에 바꾼다**(반쯤 시작한 세션이 없다). 바인딩 검사(모르는 역할 · 겹친 역할 · 같은 애니메이터 두 번 · 빠진 필수 역할) → 참가자 해석(애니메이터 핸들, 메시 스켈레톤) → 클립 읽기 · 길이 검사 → `warp` 역할의 `MotionWarpingComponent` · 루트 모션 검사 → 공유 알림 대상의 표 항목 검사 → 진입 거리 검사. 통과하면 `start` 0 역할을 모두 `play` 하고 속도 1, `snap` 은 자리를 맞추고 `warp` 는 목표 · 창을 연다.
- **매 프레임**(`update`, `FrameSystems` 단계): 역할 해석(잃음 → 정책) → 앵커 시각 읽기 · 예측 검사 → 역할 시각 검사 → 공유 알림(예측 구간) → 늦은 역할 진입 → `warp` 목표 갱신 · 창 닫기 → 클립이 끝난 역할 나감 → 앵커가 끝에 닿으면 마침.
- **알림 시각**: 공유 알림은 "이번 프레임 평가가 지나갈 구간" 에서 프레임 시작에 낸다. 클립 알림(같은 프레임 평가 끝의 게임 스레드)과 같은 프레임에 보인다. 소켓 자리는 지난 포즈다.
- **진입은 프레임 단위**다. 늦은 역할은 앵커 시각이 `start` 를 지난 첫 프레임에 들어오므로 최대 한 프레임 늦는다(그 차이를 진입 시 앵커 시각으로 적어 두고 검사도 그 기준이다).
- **끝 · 중단**: 남은 역할마다 애니메이터가 아직 세션 클립을 재생 중이면 `exitState` 로 블렌드, 속도를 되돌리고, 열린 워프 창을 닫는다. 세션은 그 프레임 동안 상태(`Finished` · `Aborted`)를 보이고 다음 프레임에 목록에서 빠진다.

## 6. 프롭 참가자 — 활과 화살

- 활은 자기 스켈레톤(시위 · 활대 본)과 애니메이터가 있는 오브젝트다. 궁수 손 소켓에 **붙은 채로** `Bow` 역할로 참가해(`align="none"`) 같은 시각을 재생한다. 부착이 평가 의존을 만들어 활은 궁수 다음 레벨에서 평가된다.
- 화살 발사는 공유 알림 `ReleaseArrow` → 활 오브젝트 알림 표의 `SpawnPrefab`(투사체 프리팹, 시위 소켓) — 기존 경로 그대로다. 메긴 화살(`Arrow` 역할)은 선택 역할로 놓는 시각에 클립이 끝나 나간다(숨김 상태로).

## 7. 네트워크(설계만, 구현은 후속)

- 시작은 **서버가 결정**한다. 서버는 (세션 id, 에셋 경로, 시작 틱, 역할 → 오브젝트 id 목록)을 복제한다. 클라이언트는 같은 에셋 · 같은 바인딩으로 같은 `startSession` 을 부르고, 시작 틱과 지금 틱의 차이만큼 시각을 앞당겨 따라잡는다(앵커 시각을 옮기는 길이 필요하다 — 애니메이터에 시각 지정 훅, feat20 · feat21 병합 뒤).
- 세션 상태는 결정적이다: (에셋, 바인딩, 진입 시 앵커 시각 목록, 알림 커서, 상태). 스냅샷 · 롤백(feat17 의 `setOwnerState`)에서는 이 묶음을 소유 상태로 저장하고, 되감기 뒤 다시 적용할 때 공유 알림은 "이미 낸 시각" 커서로 두 번 내지 않는다.
- 정렬(`snap` · `warp`)은 서버 권위 트랜스폼을 따른다 — 클라이언트는 정렬 결과를 예측하되 서버 보정을 받는다.
- 세션 id 는 지금 씬 시스템의 일련번호다. 복제할 때는 서버가 정한 id 를 받는 시작 경로가 필요하다(후속).

## 8. 에디터(M3, 후속)

미리보기 씬에 역할마다 메시를 놓고 한 시계로 문지르기(스크럽) · 정렬 오프셋 기즈모 · 알림 시각 편집. 지금은 데이터를 손으로 쓰고 런타임 검사가 틀린 값을 멈춘다.

## 9. 모션 매칭과의 관계

모션 매칭(보류 결정, `docs/plans/MotionMatching.md` 는 feat21 에서 만드는 중)과 **독립**이다. 세션은 역할의 애니메이터에 "이 클립을 재생하라" 만 하고, 끝나면 `exitState` 로 넘긴다. 이동 시스템이 그래프 상태든 모션 매칭이든 진입 · 나감은 같은 `play` 경계에서 일어난다.

## 10. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| P0 | `PairedAnimationDef` 로더와 검사(필수 역할 · 클립 없음 · NaN · 모르는 속성 · 앵커 규칙 · 알림 역할) | feat22, 빌드 전 |
| P1 | `PairedAnimationSession` · `PairedAnimationSystem` · `PairedAnimationComponent`, `AnimationNotifyComponent::processExternalNotify` | feat22, 빌드 전 |
| P2 | 정렬 `snap` · `warp`(`MotionWarpingComponent` 재사용, 새 노드 없음) | feat22, 빌드 전 |
| P3 | 네트워크 재현(7절) | 아래 "남은 것" |
| M3 | 에디터 미리보기(8절) | 아래 "남은 것" |
| — | 시나리오 테스트(Shooter3D 궁수 · 활 · 화살 실제 클립), 상호작용(`InteractableComponent`)에서 세션 시작, 에셋 캐시(`CharacterDataCache` 처럼 핫 리로드), 상대 위치로 세트 고르기 | 아래 "남은 것" |

## 남은 것

- 에디터 미리보기(M3): 역할마다 메시를 놓고 한 시계로 스크럽 · 오프셋 기즈모 · 알림 시각 편집.
- 네트워크 재현(P3): 서버가 세션 id · 시작 틱 · 역할 → 오브젝트를 복제한다. 클라이언트 따라잡기에는 애니메이터 시각 지정 훅이 필요하다(feat20 · feat21 병합 뒤). 롤백은 feat17 `setOwnerState` 에 세션 상태를 소유 상태로 싣는다.
- 시나리오 테스트: Shooter3D 궁수 · 활 · 화살 실제 클립과 활 메시(`data/anim/bow_shoot.pairedanim.xml` 은 형식 검사만 돈다).
- 상호작용(`InteractableComponent`)에서 세션 시작, 에셋 캐시(핫 리로드), 상대 위치로 처형 세트 고르기.
- 세션 중 `LocomotionWarpingComponent` 가 재생 속도를 쓰지 않게(`PairedAnimationSystem::isParticipant`).

## 11. 알려진 한계

- 재생 속도를 매 프레임 쓰는 컴포넌트(`LocomotionWarpingComponent`)가 같은 오브젝트에 있으면 세션 시각 검사가 어긋남으로 보고 중단한다. 세션 중에는 그 컴포넌트가 멈춰야 한다(후속 — 세션 참가 여부를 묻는 길은 `PairedAnimationSystem::isParticipant`).
- 애니메이션 되감기(`AnimationRewind`) 스크럽 중에는 시각이 예측과 달라 중단된다(개발 빌드 진단 기능).
- 샘플 에셋(`Resource/game/shooter3d/data/anim/bow_shoot.pairedanim.xml`)의 클립 · 활 메시는 아직 없다. 데이터 형식 검사(`ResourceDataSchemaTest`)만 돈다.

## 12. 다른 작업과 겹치는 자리

- `SkeletalAnimatorComponent.cpp/.h` 는 고치지 않았다. feat21 이 전이 · inertialization 으로 `play` 의 블렌드 의미를 바꾸면 `enterBlend` · `exitBlend` 의 뜻을 다시 본다.
- feat20 이 레이어 · 마스크를 데이터로 옮기면 세션 클립 위에 레이어(상체 조준)가 계속 섞인다 — 세션 중 레이어를 끌지는 데이터(역할 속성)로 정할 후속 자리다.
- feat14 의 `TComponentHandle<T>` 가 들어오면 바인딩의 `ComponentHandle` 을 `TComponentHandle<SkeletalAnimatorComponent>` 로 바꾼다(형식으로 "애니메이터 핸들만" 을 막는다).
- `AnimationNotifyComponent` 에 함수 하나(`processExternalNotify`)를 더했다. feat20 의 풋스텝 이벤트가 같은 파일을 만지면 병합 때 나란히 놓으면 된다.
