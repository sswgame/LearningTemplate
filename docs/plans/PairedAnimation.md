# 페어드 애니메이션(여러 개체가 한 타임라인으로 도는 애니메이션)

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-6 · 진행: 6/6

활과 화살, 잡기 · 처형, 말 타기, 문 열기처럼 **여러 개체가 한 타임라인에 묶여 같이 도는 애니메이션**(언리얼의 Contextual Animation, 흔히 페어드 · 싱크드 · 인터랙션 애니메이션)의 일반 구조다.
애니메이션 영역의 한 단위다([게임플레이 후보](Candidates-gameplay.md) 의 애니메이션 절과 이웃). 첫 단위(P0 ~ P2)와 상호작용 연결 · 에셋 캐시 · 세트 고르기 · 보폭 보정 쥠은 코드와 테스트(`PairedAnimationTest` · `PairedAnimationDefTest` · `InteractionTest`)로 들어갔다.

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
| `InteractableComponent` | `GameFramework/.../Interaction` | 상호작용 정의와 맞춤 마커(소켓)로 맞춤 지점 계산 | 단계의 `pairedAnimation`(세트 목록) · `interactorRole` · `interactableRole` 로 하는 쪽(`InteractorComponent`)이 단계 시작에 세션을 시작한다 |
| `LocomotionWarpingComponent` | `Engine/Object/SceneAnimation` | 보폭 보정이 매 프레임 재생 속도를 쓴다 | 세션이 역할에 들어올 때 쥐고(`setPlayRateHeld`) 나갈 때 놓는다. 이쪽은 아래 티어(Object)라 세션을 묻지 않는다 |

## 2. 상용 선례

근거 수준: [코드] 소스를 읽음 · [문서] 공식 문서 · [기억] 발표 · 문서를 기억으로 · [추정] 공개 자료 없이 추론.

| 선례 | 무엇 | 이 설계에 가져온 것 | 근거 |
|---|---|---|---|
| 언리얼 Animation Montage | 한 메시의 슬롯에 재생하는 일회성 클립(섹션 · 알림 · 루트 모션) | 역할마다 "자기 클립 하나를 일회성으로" 재생하고, 끝나면 종료 상태로 블렌드아웃 | [문서] |
| 언리얼 Sync Group / Sync Marker | 리더-팔로워 위상 동기(마커가 있으면 발 위상) | 리더 = 앵커 역할이라는 생각. 단 위상 동기는 쓰지 않고 같은 프레임 · 같은 속도 시작 + 시각 검사로 맞춘다(1절) | [문서] |
| 언리얼 Motion Warping | 구간 알림 창과 이름 붙은 워프 목표로 루트 모션을 휨 | 정렬 방식 `warp` 가 엔진의 `MotionWarpingComponent` 를 그대로 쓴다 | [문서] · 엔진 쪽 [코드] |
| 언리얼 Contextual Animation (`ContextualAnimSceneAsset`) | 역할 목록, 역할마다 몽타주, 정렬 섹션 · 워프 포인트, 장면 인스턴스가 몽타주를 함께 재생(`MontageSync_Follow` 로 리더를 따름) | 데이터 에셋 = 역할 목록 + 클립 + 앵커 기준 오프셋, 런타임 = 장면 인스턴스(여기서는 세션) | [기억] |
| 언리얼 Gameplay Interactions · Smart Objects | 월드 오브젝트가 상호작용 슬롯을 광고하고, 예약한 쪽이 상호작용(컨텍스추얼 애니 포함)을 실행 | 진입 조건(거리)을 데이터로, 세션 시작은 게임플레이(상호작용 단계의 `pairedAnimation`)가 부른다 | [기억] |
| 유니티 Timeline | `PlayableDirector` 한 시계가 트랙마다 바인딩된 오브젝트를 구동 | "한 시계 + 역할 → 오브젝트 바인딩" 모양. 단 포즈는 각 애니메이터가 내고(루트 모션 · 알림 · 워핑을 그대로 쓰려고) 시계는 앵커가 갖는다 | [문서] |
| Naughty Dog 컨텍스추얼 애니메이션(GDC 발표들) | 상호작용을 한 장면에서 함께 저작하고, 공통 기준점(애니메이션 기준점, apReference 류)에 모든 참가자의 루트를 상대로 둔다 | 오프셋은 "앵커 루트 기준 역할 루트" 하나 — 기준점을 별도 오브젝트로 두지 않고 앵커 역할이 기준점이다 | [기억] |
| 'Assassin's Creed' 처형 · 'The Last of Us' 잡기 | 상대 위치 · 각도로 처형 세트를 고르고, 짧은 창에서 공격자를 피해자 기준 자리로 미끄러뜨려 맞춤 | `snap`(즉시) · `warp`(창 동안 휨) 두 정렬, 진입 조건 `maxAlignDistance`, 세트 고르기 `<Select>`(앵커 앞에서 본 방위각 · 거리) | [기억] · [추정] |

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
| 에셋 캐시 · 핫 리로드 | `SharedAssetCache<PairedAnimationDef>`(`CharacterDataCache.h`). 다시 읽기는 제자리에서 바꾸지 않고 표에서 떼어 낸다(`detachShared`). `PairedAnimationComponent` 는 다시 읽은 횟수가 바뀌면 다음 시작 전에 다시 받는다 | 돌고 있는 세션은 역할 인덱스로 에셋을 읽는다 — 제자리에서 역할 수가 바뀌면 범위 밖을 읽는다 |
| 세트 고르기 | 에셋마다 `<Select>`, `PairedAnimationSystem::startMatchingSession` 이 목록에서 자리에 맞는 첫 세트로 시작(없으면 `NoMatchingSet`). 고른 세트의 시작이 거절되면 다음 세트로 넘어가지 않는다 | 조건을 통과한 세트가 시작하지 못하는 것은 데이터 · 배치 결함이다 — 다른 세트로 덮지 않는다 |
| 재생 속도를 쓰는 다른 컴포넌트 | 세션이 역할에 들어올 때 `LocomotionWarpingComponent::setPlayRateHeld( true )`, 나갈 때 `false` | 보폭 보정은 Object 티어라 Character 의 세션을 include 할 수 없다(`CheckEngineLayers`). 세션이 알려 주는 쪽이 방향이 맞다 |
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
| `Select@role` · `minAngle` · `maxAngle` · `maxDistance` | `<Select>` 는 없거나 하나, 적으면 넷 다 예 | 세트 고르기 조건. `role`(앵커가 아닌 역할) 루트가 앵커 앞(+Z)에서 본 수평 방위각(도, [-180, 180])과 수평 거리(m) 안에 있어야 이 세트를 고른다. `minAngle > maxAngle` 이면 ±180° 를 지나는 뒤쪽 구간. 없으면 어느 자리에서나 고른다 |

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

## 7. 네트워크(엔진 쪽 복사 · 복원과 키트 `GF_NetPairedAnimation`)

- 시작은 **서버가 결정**한다. 서버는 (에셋 경로, 역할 → 오브젝트 번호 목록, 세션 스냅숏)을 복제한다. 클라이언트는 같은 에셋 · 같은 바인딩으로 `restoreSession` 을 부른다.
- 세션 상태는 결정적이다: (에셋, 바인딩, 진입 시 앵커 시각 목록, 알림 커서, 상태). 스냅샷 · 롤백(feat17 의 `setOwnerState`)에서는 이 묶음을 소유 상태로 저장하고, 되감기 뒤 다시 적용할 때 공유 알림은 "이미 낸 시각" 커서로 두 번 내지 않는다.
- 정렬(`snap` · `warp`)은 서버 권위 트랜스폼을 따른다 — 클라이언트는 정렬 결과를 예측하되 서버 보정을 받는다.
- 세션 id 는 씬 시스템의 일련번호다. 서버가 정한 id 는 `PairedAnimationSystem::restoreSession` 이 받는다(아래 "복제 상태"). 복원한 id 보다 시스템의 다음 일련번호가 커진다.
- 2026-10-20 확인: `setOwnerState` 는 main 에 있다. 애니메이터 시각 지정 훅(`SkeletalAnimatorComponent::seekCurrentTime`)도 들어왔다.

**복제 상태(엔진 쪽, 구현함)**: 세션 상태는 `PairedAnimationSessionSnapshot`(`PairedAnimationSnapshot.h`)이다. 세션 id, 마지막 갱신이 읽은 앵커 시각, 알림 커서, 역할마다 단계와 진입 시각을 담고 `writeBytes` · `readBytes` 로 `BitStream` 바이트가 된다.
에셋 경로와 역할 → 오브젝트 id 는 담지 않는다. 오브젝트 id 를 로컬 핸들로 바꾸는 일은 네트워크 계층(GameFramework)의 몫이다.

- 서버: `PairedAnimationSession::captureSnapshot` 으로 복사한다. 마지막 갱신 때의 상태이고 재생 중일 때만 된다.
- 시작 틱만 복제하는 경로: `PairedAnimationSessionSnapshot::makeAtElapsedTime( def, 서버 id, 지난 시간 )` 이 이상적인 상태를 만든다. 진입 시각은 `start` 로 치므로 서버의 실제 진입(프레임 단위)과 최대 한 프레임 다르다.
- 클라이언트: `restoreSession( manager, def, 바인딩, 스냅샷 )` 이 `start` 와 같은 검사를 하고, 스냅샷이 에셋과 맞는지 보고(역할 수, 앵커 단계, 시각 범위, 진입 시각, 바인딩 있는 역할의 `Unbound`), 그다음 바꾼다. 틀리면 `InvalidSnapshot`, 같은 id 가 재생 중이면 `SessionIDInUse` 다.
- 복원은 서버 상태에 맞추는 일이라 진입 블렌드 없이(0 초) 재생하고 클립 시각을 옮긴다. 들어와 있던 역할은 `앵커 시각 − 진입 시각`, 클립이 이미 끝난(또는 서버가 내보낸) 역할은 `exitState` 를 재생한다. 아직 들어오지 않은 역할은 다음 갱신이 `start` 를 보고 들여보낸다.
- 알림은 커서 앞의 것을 내지 않는다(건너뛴 알림은 울리지 않는다). 트랜스폼은 서버 권위라 복원은 `snap` · `warp` 정렬과 진입 거리 검사를 하지 않는다. 롤백은 `abortSession` 으로 옛 세션을 중단한 뒤 복원한다.

**네트워크 계층(키트 `GF_NetPairedAnimation`, 메시지 영역 `0x60`)**: 계약은 `PairedAnimationReplication.h` 머리말이 정본이다. 정한 것은 이렇다.

- 역할 → 오브젝트 대응은 씬의 번호표 `NetObjectTable`(Engine/Net, 파괴 복제와 같은 표)이다 — 씬 오브젝트는 이름 해시(이름순 몇 번째가 아니라 한쪽에만 있는 오브젝트가 번호를 밀지 않는다), 플레이 중에 생긴 오브젝트(투사체)는 서버의 `registerSpawned` 번호를 게임의 스폰 복제가 클라이언트에서 `bindSpawned` 로 묶은 것이다. 번호가 없는 오브젝트가 참가한 세션, 리소스 경로 없이 만든 에셋(`PairedAnimationDef::getSourcePath` 가 빔)의 세션은 서버가 오류를 남기고 보내지 않는다.
- 늦게 들어온 클라이언트에도 서버는 `captureSnapshot` 의 실제 진입 시각을 보낸다(`makeAtElapsedTime` 의 추정은 쓰지 않는다). 상태 메시지는 세션을 처음 본 갱신에 모두에게, 연결이 열리면 그 연결에 한 번 간다. 끝(`Finished` · `Aborted` · 목록에서 빠짐)은 끝 메시지로 가고 클라이언트는 아직 재생 중이면 중단한다.
- 복원이 클립이 끝난 역할(서버가 게임플레이로 내보낸 선택 역할 포함)에 `exitState` 를 재생하는 규칙은 그대로 둔다. 같은 id 의 상태를 다시 받으면(보정 · 롤백) 클라이언트는 옛 세션을 중단하고 다시 복원한다. 롤백 소유 상태는 게임이 `makeSessionState` → `PairedAnimationNetState::write` 의 바이트를 `setOwnerState` 에 싣고, 되감은 뒤 `PairedAnimationReplicationClient::applyState` 를 부른다.
- 씬에 `PairedAnimationNetSessionComponent` 하나를 두면 게임 네트워크 세션(`GameNetSession`)의 역할대로 서버(틱 `PostUpdate` 에서 보냄) · 클라이언트(게임 프레임 앞에서 받음)가 된다. 클라이언트의 게임플레이가 세션을 따로 시작하지 않게 막는 일은 게임 권한의 몫이다.

## 8. 에디터(M3)

캐릭터 편집 확장의 `PairedAnimationPanel`(미리보기 창) + `PairedAnimationEditModel`(화면 없는 모델, `PairedAnimationEditModelTest`). 계약은 두 파일의 머리말이 정본이다.

- 편집 대상은 파일 한 장의 내용(`PairedAnimationContentDef`)이다. 쓰기는 `PairedAnimationDef::makeXMLText` 하나, 규칙 검사는 그 글을 런타임 로더로 다시 읽는 것 하나다 — 에디터 쪽 사본 규칙이 없다. 규칙에 어긋나면 저장하지 않는다.
- 미리보기: 앵커는 원점, 정렬 역할은 정렬 목표(`PairedAnimationDef::computeAlignTarget` — 세션과 같은 식), 프롭은 원점(부착하지 않음). 역할마다 고른 프리팹(에셋에 저장하지 않음)의 애니메이터를 재생 속도 0 으로 두고 스크럽 시각의 클립 시각으로 옮긴다.
- 오프셋 기즈모: 정렬 역할 오브젝트를 창 선택에 놓고 기즈모로 옮기면 그 자리가 앵커 기준 오프셋 · 요로 들어간다(`setRoleOffsetFromWorld`, 연속 편집 한 단계).

## 9. 모션 매칭과의 관계

모션 매칭(보류 결정, `docs/plans/MotionMatching.md` 는 feat21 에서 만드는 중)과 **독립**이다. 세션은 역할의 애니메이터에 "이 클립을 재생하라" 만 하고, 끝나면 `exitState` 로 넘긴다. 이동 시스템이 그래프 상태든 모션 매칭이든 진입 · 나감은 같은 `play` 경계에서 일어난다.

## 10. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| P0 | `PairedAnimationDef` 로더와 검사(필수 역할 · 클립 없음 · NaN · 모르는 속성 · 앵커 규칙 · 알림 역할) | 완료(Debug 빌드 · `PairedAnimationDefTest` 통과) |
| P1 | `PairedAnimationSession` · `PairedAnimationSystem` · `PairedAnimationComponent`, `AnimationNotifyComponent::processExternalNotify` | 완료(Debug 빌드 · `PairedAnimationTest` 통과) |
| P2 | 정렬 `snap` · `warp`(`MotionWarpingComponent` 재사용, 새 노드 없음) | 완료(Debug 빌드 · `PairedAnimationTest` 통과) |
| P3a | 세션 상태 복사 · 복원(7절 "복제 상태": `PairedAnimationSessionSnapshot` · `captureSnapshot` · `restoreSession`) | 코드와 `PairedAnimationTest` 작성(빌드 · 테스트 미확인, 병합 뒤 한 번 확인) |
| P3b | 네트워크 계층 연결(서버가 세션 시작 · 상태를 복제, 클라이언트가 복원, 롤백 소유 상태) — 키트 `GF_NetPairedAnimation` | 코드와 `PairedAnimationReplicationTest` 작성(빌드 · 테스트 미확인, 두 프로세스 확인은 아래 "남은 것") |
| M3 | 에디터 미리보기(8절) — `PairedAnimationPanel` · `PairedAnimationEditModel` | 코드와 `PairedAnimationEditModelTest` 작성(빌드 · 테스트 미확인, 창 화면 확인은 아래 "남은 것") |
| — | 상호작용 단계에서 세션 시작 · 에셋 캐시(핫 리로드) · 세트 고르기(`<Select>`) · 보폭 보정 쥠 | 완료(`InteractionTest` · `PairedAnimationTest`) |
| — | 시나리오 테스트(Shooter3D 궁수 · 활 · 화살 실제 클립) | 에셋: 궁수는 기사 `2H_Ranged_Shoot`, 활 · 화살은 `Scripts/dev/MakeBowProps.py`(`TestMakeBowProps` 통과). 활 · 화살 임포트(`models/props/`), 프리팹(`prefabs/props/bow` · `arrow` · `arrow_projectile` — 투사체는 동적 강체 + `LaunchVelocityComponent` 30 m/s), 활 알림 표(`characters/bow/bow.notifies.xml`), 외형 아이템 `bow_long` · 프리셋 `ShooterArcher`, 씬 `maps/bowshowcase.scene.xml`, 시나리오 `automation/showcase/bowshoot.scenario.xml`(자동 수집 밖). Release 네 백엔드 통과 · 당김 자세 화면 확인(활은 왼손 `BowGrip`). 투사체 방향 등은 아래 "남은 것" |
| — | 프롭 역할을 데이터로 묶기 — 넘기지 않은 역할은 같은 에셋 · 그 역할을 적은 다른 `PairedAnimationComponent` 로(둘이면 오류), `_bStartOnPlay`, 탐침 `PairedAnimation.SessionState` | 코드와 `PairedAnimationTest.ComponentBindsRolesNamedByOtherComponents` 작성(빌드 미확인) |

## 남은 것

- 에디터 미리보기(M3) 화면 확인: 창을 열어 역할 프리팹을 놓고 스크럽 · 기즈모로 오프셋을 옮기는 에디터 시나리오(`-EnableEditor`, `CaptureEditorUI`).
- 네트워크(P3b) 두 프로세스 확인: `Server` 와 `App -gv_netConnectAddress` 에서 세션 시작 · 늦은 참가 · 끝이 복제되는지. 씬 문서 밖에서 생긴 참가자는 번호표의 생성 번호(`NetObjectTable::registerSpawned` · `bindSpawned`)로 부를 수 있게 됐다 — 스폰 자체를 복제하는 것(무엇을 스폰했는지 클라이언트에 알리고 묶는 일)은 게임의 몫이고 아직 쓰는 게임이 없다.
- 쇼케이스 남은 확인: 2026-10-22 Release 네 백엔드(DX12 · Vulkan · OpenGL · DX11)에서 `bowshoot` 시나리오가 통과하고 당김 스크린샷이 같다. 활 · 메긴 화살은 오른손 `Gun` 이 아니라 앞으로 뻗은
  왼손 소켓 `BowGrip`(handslot.l, 피치 70°)으로 옮겨 활대가 서고 화살이 앞(살짝 위)을 본다. 남은 것: ① 쇼케이스 카메라(옆 고정)로는 놓은 뒤 날아가는 투사체가 한 프레임도 잡히지 않아
  `_localDirection` 이 화살 앞인지 화면으로 못 봤다 — 앞에서 보는 카메라를 씬에 더해 본다. ② 당김 내내 화살 길이만 한 가는 줄 하나가 활 쪽에 붙어 보인다(메긴 화살 부품을 빼도 남는다 — 활 메시의
  시위 띠 스킨이 의심된다). ③ 날아가는 동안 화살 머리가 속도 쪽을 보게 하는 것(지금은 처음 자세 그대로 물리가 돌린다).

## 11. 알려진 한계

- 재생 속도를 매 프레임 쓰는 컴포넌트는 지금 `LocomotionWarpingComponent` 하나이고 세션이 쥐어 둔다. 재생 속도를 쓰는 새 컴포넌트가 생기면 같은 쥠을 받아야 한다 — 받지 않으면 세션 시각 검사가 어긋남으로 보고 중단한다(조용히 틀어지지는 않는다).
- 상호작용의 누름(`Press`) 단계는 누름으로 이미 끝나므로, 세션 시작이 거절되면 경고만 남고 상호작용은 끝난 채다. 누르고 있기 · 연타 단계는 취소된다.
- 애니메이션 되감기(`AnimationRewind`) 스크럽 중에는 시각이 예측과 달라 중단된다(개발 빌드 진단 기능).
- 샘플 에셋(`Resource/game/shooter3d/data/anim/bow_shoot.pairedanim.xml`)의 쇼케이스(`maps/bowshowcase.scene.xml`) 시나리오는 자동 수집 밖이라 손으로 돌린다(2026-10-22 네 백엔드 통과). 세션이 끝나면 시스템이 거둬 탐침 `PairedAnimation.SessionState` 는 Idle(0)이다.
- 프롭 역할 묶기는 매니저 전체를 훑는다(시작할 때 한 번). 같은 에셋 · 같은 역할을 적은 프롭이 씬에 둘 이상이면(궁수 둘이 같은 활 프리팹) 고를 수 없어 거절한다 — 그때는 부르는 쪽이 바인딩을 넘긴다.
- 복원(`restoreSession`)은 `warp` 창을 다시 열지 않는다. 창 도중에 이어 받은 클라이언트는 휘어지지 않은 루트 모션으로 가고 서버의 트랜스폼 보정을 받는다(7절).

## 12. 다른 작업과 겹치는 자리

- `SkeletalAnimatorComponent.cpp/.h` 는 고치지 않았다. feat21 이 전이 · inertialization 으로 `play` 의 블렌드 의미를 바꾸면 `enterBlend` · `exitBlend` 의 뜻을 다시 본다.
- feat20 이 레이어 · 마스크를 데이터로 옮기면 세션 클립 위에 레이어(상체 조준)가 계속 섞인다 — 세션 중 레이어를 끌지는 데이터(역할 속성)로 정할 후속 자리다.
- feat14 의 `TComponentHandle<T>` 가 들어오면 바인딩의 `ComponentHandle` 을 `TComponentHandle<SkeletalAnimatorComponent>` 로 바꾼다(형식으로 "애니메이터 핸들만" 을 막는다).
- `AnimationNotifyComponent` 에 함수 하나(`processExternalNotify`)를 더했다. feat20 의 풋스텝 이벤트가 같은 파일을 만지면 병합 때 나란히 놓으면 된다.
