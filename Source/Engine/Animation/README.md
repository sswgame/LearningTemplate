# 애니메이션 (Animation)

스켈레탈 애니메이션의 데이터(스켈레톤 · 클립 · 코덱)와 재생 핵심(시간 커서 · 크로스페이드 · 상태 기계 · 알림 · 동기 그룹), 그리고
2D 스프라이트 클립입니다. **2D 와 3D 는 같은 재생 코드를 씁니다** — 다른 것은 "샘플이 무엇을 내는가"(프레임 번호 ↔ 본 포즈)뿐입니다.

평가(누가 언제 포즈를 만드나)는 이 폴더가 아니라 `Object/Animation/AnimationSystem` 이고, 컴포넌트는 `Object/Component/3D/`
(`SkeletalMeshComponent` · `SkeletalAnimatorComponent`) · `Object/Component/2D/SpriteAnimatorComponent` 입니다. 경로로 나눠 주는 캐시
(`SkeletonCache` · `AnimClipCache`)는 `IAssetCache` 라 `Resource/AnimationAssetCache` 에 있습니다(티어 규칙).

## 0. 폴더

| 파일 | 무엇 |
|------|------|
| `Pose` | 본 로컬 변환(이동 · 회전 · 스케일)을 SoA 로 든 포즈 — 섞기(`blend`) · 본 마스크(`blendMasked`) · 가산(`makeAdditive` · `applyAdditive`) · 모델 공간 · 스킨 팔레트 |
| `Skeleton` | 스켈레톤 에셋(`.skeleton.json`) — 본 이름 · 부모(앞에 있음) · 레퍼런스 포즈 · 역 바인드, 임포트가 적은 본 부착 메시 표(읽기 전용) |
| `SkeletonBoneLod` | 스켈레톤 곁 본 LOD 표(`.bonelod.json`) — 화면 크기 단계마다 풀지 않을 본(자손 포함). 임포트가 지우지 않게 스켈레톤과 따로 둔다 |
| `AnimClip` | 클립 에셋(`.animclip`) — 트랙(본 이름) 코덱 블롭 · 실수 커브 · 알림 · 루트 모션 트랙. `IAnimPlayable` |
| `Codec/AnimCodec` | 코덱 인터페이스(압축 → 코덱 id + 불투명 블롭, 런타임 샘플) · 등록부(이름 → 코덱) · 오차 측정(모델 공간 가상 정점) |
| `Codec/Raw` · `Codec/Acl` | 기준 코덱(float 그대로) · ACL 2.1 백엔드. **ACL · RTM 헤더는 `Codec/Acl/` 의 .cpp 에서만 include 한다**(게이트가 막는다) |
| `AnimPlayback` | `IAnimPlayable`(길이 · 기본 반복 · 알림) · `AnimClipCursor`(시간 · 반복 · 끝) · `AnimNotifyTrack`(지나간 알림을 정확히 한 번) |
| `AnimNotifyPhase` | 울린 알림의 종류(`Instant` · `Begin` · `End`) 하나. `Component.h`(PCH 안)가 이것만 include 한다 — `AnimPlayback.h` 를 고쳐도 PCH 가 다시 지어지지 않게 |
| `AnimPlayer` | 두 칸 크로스페이드 플레이어 · `AnimSyncGroup`(리더 위상 맞추기) · `AnimParameterSet`(상태 기계 파라미터) |
| `AnimGraphAsset` · `AnimGraphPlayer` | 그래프 JSON(노드 = 상태, 링크 = 전이: 조건 `>` `<` `>=` `<=` `==` `!=` `trigger` · 블렌드 · 노드 반복)과 그것을 돌리는 상태 기계 |
| `SpriteClipAsset` · `SpriteClipPlayable` | 스프라이트 클립(`.sprite.json`)과 그 이름 붙은 구간을 `IAnimPlayable` 로 보이는 다리(2D 가 같은 재생 코드를 탄다) |
| `BlendSpace` · `DualQuaternion` | 1D/2D 파라메트릭 블렌딩(행렬 하나), DQ 스키닝 수학(아래 2 · 3 절) |
| `Graphics/Mesh/MeshVertexAnimation` | VAT 표(프레임 × 정점, 위치 + 팔면체 노멀)와 굽기(`AnimClip::samplePose` → 팔레트 → CPU 스키닝 — meshskin.hlsl 과 같은 식) |
| `Facial/FacialRig` | 얼굴 리그(`.facial.json`) — 표정 · 비즘 = 모프 타깃 가중치 묶음, 깜빡임(타깃 · 간격 · 길이) · 시선(눈 본 · 앞 축 · 최대 각 · 사카드) |
| `Facial/LipSync` | 립싱크 분석 표(`engine/animation/lipsync.json`) · 비즘 트랙(`<음성>.visemes.json`) · 분석기(RMS + 세 대역 바이쿼드 → 비즘 가중치) |
| `AnimJsonUtil` | 모르는 키를 오류로 보는 JSON 검사 · 본 변환 읽기 · 쓰기(스켈레톤 · 임포트 규칙 · 클립 곁 데이터가 함께 쓴다) |
| `Retarget/` | 리타깃 — 프로필(`*.retarget.json`) · 런타임 리타기터(`PoseRetargeter`) · 오프라인 굽기(`RetargetBakeUtil`). 아래 6 절 |
| `Rig/` | 후처리 리그 — 작업 포즈(`RigPoseBuffer`) · IK 풀이(`RigIkSolver`) · 스프링 사슬 · 리그 에셋(`*.rig.json`, 노드 등록부) · 실행기(`RigInstance`). 아래 5 절 |

## 0.1 파일 형식

- **`.skeleton.json`**: `{ "bones": [ { "name", "parent", "translation", "rotation"(x,y,z,w), "scale", "inverse_bind"(16, 행 우선) } ], "attachments": [ { "name", "mesh", "bone", "translation", "rotation", "scale" } ] }`.
  키는 모두 필수, 모르는 키 · 앞에 없는 부모 · 없는 본을 가리키는 부착은 로드 오류입니다.
- **`.animclip`**(리틀 엔디언): `SWAC` · 버전 1 · 플래그(반복) · 길이 · 이름 · 트랙 이름들 · 루트 모션 트랙(-1 없음) · 코덱 번호 · 블롭 · 알림(이름 · 시각 · 구간) ·
  커브(이름 · [시각, 값]). 지금 판만 읽습니다(옛 판은 원본에서 다시 임포트).
- **`.bonelod.json`**(스켈레톤 곁, 선택): `{ "levels": [ { "max_screen_size": 0.15, "remove": [ 본 이름 ... ] }, ... ] }`. 단계는 화면 크기가 줄어드는 순이고 뒤 단계는
  앞 단계가 뺀 본을 이어받는다. 없는 본 이름은 오류(`ResourceDataSchemaTest` 가 곁 스켈레톤으로 확인한다). KayKit 다섯 스켈레톤에 있다(IK 조종 본 → 발가락 · 손).
- **`engine/animation/animationlod.json`**: 애니메이션 LOD 표 — 화면 크기 단계(`min_screen_size` · `update_rate_divisor` · `interpolate`), 화면 밖 주기(0 = 포즈를
  만들지 않음), 예산(ms), 주기 상한, VAT 로 넘길 화면 크기(`Object/Animation/AnimationLod.h`).
- **`.mesh` 스킨 스트림**: `Graphics/Mesh/MeshAssetFormat`(판 2) — 정점마다 본 번호 넷(uint16) · 가중치 넷(float32), 머리의 스킨 본 수.
- **`.mesh` 모프 덩어리**(선택, 끝에 붙는 `MRPH` · 길이): 타깃 수 · [이름 · 정점 수 · (정점 번호 · 위치 차이 · 노멀 차이)]. 움직이는 정점만 싣는다. 모프가 없는
  메시는 덩어리가 없어 이전과 같은 바이트다(판을 올리지 않는다). 모르는 덩어리 · 범위 밖 정점 · 길이 어긋남은 로드 오류.
- **`.facial.json`**(메시 곁 — `models/<이름>.facial.json`, 임포트 곁 폴더 밖): `FacialRig.h` 의 형식. 표정 이름은 타깃 이름과 겹치면 안 된다(이름은 대소문자를
  무시하고, 같은 이름 커브가 타깃과 표정을 둘 다 움직인다 — 묶을 때 오류).
- **`engine/animation/lipsync.json`**: 분석 프레임율 · 무음/최대 RMS · 날카로움 · 대체 비즘 · 세 대역(Hz) · 비즘마다 대역 모양. 첫 비즘이 무음(sil).
  **`<음성>.visemes.json`**: `{ "frame_rate", "visemes": [ 이름 ], "frames": [ [ 가중치 ] ] }` — `App --import-lipsync` 가 `voice/` 폴더의 .wav · .ogg 곁에 쓴다.
- 만드는 쪽은 에디터의 `ModelImporter`(`Source/Editor/README.md` "모델도 들일 때 임포트한다") — 규칙(`Config/Editor/ModelImportConfig.json`)이 코덱 ·
  표본율 · 정밀도 · 가져올 클립을 고르고, 원본 옆 `<이름>.clips.json` 이 클립의 반복 · 알림 · 커브를 정합니다. glTF 모프 타깃은 `.mesh` 의 모프 덩어리로
  (이름은 `extras.targetNames`), `weights` 채널은 타깃 이름의 커브로 클립에 실립니다.

## 0.2 한 프레임 — 누가 무엇을 하나

```
GameObjectManager::tick
  ├─ 컴포넌트 틱(게임 코드가 애니메이터 파라미터를 쓴다)
  ├─ AnimationSystem::evaluate( dt )            ← 틱 뒤, 트랜스폼 플러시 앞
  │    LOD) 뷰(엔진 루프가 넣은 주 시점 + 그리는 추가 뷰)로 클라이언트마다 가시성 · 화면 크기 → 주기 · 보간 · 본 LOD, 예산 배분
  │    0) 의존이 바뀌었으면 레벨을 다시 짓는다(위상 정렬, 고리는 오류)
  │    1) 유닛마다 할 일 · LOD — 쉬는 유닛은 여기서 빠진다(비용 0). 일하는 유닛은 일들의 prepareAnimationFrame(게임 스레드 — 월드 행렬 · 물리 질의)
  │    2) 단계마다 레벨 순서로 engine::runParallel:
  │         Time(상태 기계 · 알림 · 루트 모션 · 커브) → [동기 그룹] → BasePose(샘플 · 크로스페이드 · 레이어 · 리더 포즈) →
  │         Attachment(부착 자리) → PostProcess(PoseModifier 자리) → SkinPalette
  │    3) 게임 스레드: finishAnimationFrame(루트 모션을 오브젝트 트랜스폼에)
  └─ 트랜스폼 플러시
GpuSceneBuilder::buildFromScene → 유닛의 팔레트를 스냅샷으로(`collectSkinPalettes`)
FrameRenderer → 모프 풀의 스킨 구간에 팔레트를 올리고 meshskin.hlsl 디스패치 한 번 → 정점 셰이더는 모프와 같은 길로 읽는다
```

- **유닛**(`SkeletalMeshComponent`) = 장비 부품 하나 = 오브젝트 하나. 스켈레톤이 없으면 본 하나("root")짜리 암묵 스켈레톤입니다(런타임에 `setSkeleton` 으로
  정한 스켈레톤은 경로가 비어도 덮지 않는다). 스킨드 메시는 컴포넌트마다 사본(`Mesh::createSkinInstance` — 원본의 스킨 데이터를 나눠 GPU 레스트 · 가중치는
  한 벌)을 둡니다. 군중 공유를 켜면(`setShareCrowdPose`) 사본 대신 묶음의 메시를 그립니다(아래 "군중 공유").
- **일**(`IAnimationPhaseTask`)은 유닛에 걸립니다: 애니메이터(`SkeletalAnimatorBinding`)가 Time · BasePose 를, 후처리 리그(`PoseModifierBinding`,
  `Engine/Character`)가 PostProcess 를 맡고, 소켓 부착이 Attachment 에 끼어듭니다. 단계 앞의 게임 스레드 준비는 `prepareAnimationFrame` 입니다. 단계 함수는 워커에서 돌므로 자기 유닛과 의존으로 선언한 위 유닛만 읽습니다(`addAnimationDependency`).
- **거리 LOD 기준**: `AnimationSystem::setLodViewPosition`(엔진 루프가 넣는 LOD 뷰의 첫 시점 — `setLodViews` 가 함께 정한다, 벤치는 직접 넣는다) — 스프링 본의 `lod_distance` 가 읽는다.
- **LOD**(`Object/Animation/AnimationLod` — 언리얼 URO · Significance Manager · Animation Budget Allocator 의 자리): 엔진 루프가 프레임마다 뷰를 넣고
  (`AnimationSystem::setLodViews` — 주 시점 + 이번 프레임에 그리는 화면 사각형 · 렌더 텍스처 뷰), 평가 앞에서 클라이언트(`IAnimationLodClient` — 유닛 ·
  스프라이트 애니메이터)마다 경계 구로 판정합니다:
  - **가시성**: 어느 뷰의 절두체에도 없으면 `setVisibleHint(false)` — 포즈를 건너뜁니다(`_bAnimateWhenOffscreen` 아니면, 시간 · 알림은 흐른다).
    쉬는 추가 뷰는 넣지 않으므로 CCTV 에만 보이는 캐릭터는 그 뷰가 그리는 프레임에만 포즈를 만듭니다.
  - **화면 크기 → 주기(URO)**: 화면 크기 = 경계 구 지름 / 화면 높이(뷰-투영 행렬 하나에서 — `AnimationLodUtil::computeScreenSize`). 표의 단계가 주기와
    보간을 고릅니다. 같은 주기의 유닛은 위상(핸들 해시)이 달라 한 프레임에 몰리지 않습니다. `_updateRateDivisor`(PROPERTY)는 하한입니다.
  - **보간**: 건너뛴 프레임은 "평가 때 보이던 포즈 → 새 포즈" 를 1/주기씩 갑니다 — 한 주기 늦습니다(언리얼 URO 보간과 같다). 비용은 섞기 · 모델 공간 ·
    팔레트(샘플이 빠진다).
  - **본 LOD**: 스켈레톤 곁 `.bonelod.json` 의 단계가 고른 본은 코덱이 풀지 않고(ACL `skip_track_*` · Raw 는 건너뜀) 레퍼런스 포즈로 부모를 따릅니다.
  - **예산**: 포즈 단계의 벽시계 시간 / 평가한 유닛 수(지수 이동 평균)를 유닛 하나의 비용으로 보고, `Σ 비용 / 주기` 가 예산을 넘으면 화면이 작은 것부터
    주기를 두 배씩(상한까지) 늘리고 보간을 켭니다(`AnimationLodUtil::allocateBudget`).
  - 뷰를 한 번도 받지 않은 시스템(시험 · 서버 · 헤드리스)은 판정하지 않습니다 — 가시성 훅 · 주기를 직접 부르는 쪽을 덮지 않습니다. `-gv_animationLod=0` 이 끕니다.
- **군중 공유**(`Object/Animation/AnimationCrowd` — 언리얼 Animation Sharing 의 자리, 표 `engine/animation/animationcrowd.json`): `_bShareCrowdPose` 를 켠 유닛은
  시간 단계 뒤(게임 스레드) 묶음 · 혼자 · VAT 중 하나가 됩니다(`updateCrowdMembership`).
  - **묶음**: 열쇠 = (스켈레톤, 스킨 원본, 클립, 재생 속도, 루트 묶기, 변형 칸). 칸은 클립 한 바퀴를 `variations_per_clip` 으로 나눈 위상이고 묶음 시각 =
    군중 시계 × 속도 + 칸 × 폭이라 유닛 시각과 반 칸 안이며 어긋나지 않습니다(같은 상태면 칸을 바꾸지 않는다). 묶음 하나 = 포즈 하나 · 메시(사본) 하나 =
    결과 구간 하나 · 팔레트 하나 · 배치 하나(멤버 수와 무관). 보이는 멤버가 있는 묶음만 평가합니다(병렬). 멤버는 포즈 단계를 돌지 않고, 포즈 읽기
    (`getLocalPose` · `getModelSpaceTransforms` · `getSkinPalette`)는 묶음의 것을 돌려줍니다. 시간 · 상태 기계 · 알림 · 루트 모션 · 커브는 유닛마다 그대로입니다.
  - **혼자**: 섞는 중 · 레이어 · 시퀀서 덮어쓰기 · 반복하지 않는 클립 · 후처리 일 · 리더 따르기면 나눌 수 없어 사본 풀(`acquireSoloMesh`)의 메시로 평가합니다
    (묶음의 마지막 포즈에서 이어 섞기가 튀지 않는다). 끝나면 사본을 풀에 돌려줍니다.
  - **VAT**: LOD 가 `vertex_animation_screen_size` 아래로 본 유닛(따르는 유닛 없음 · 속도 1)은 (원본, 클립)의 VAT 메시 하나로 넘어갑니다 — 처음 쓸 때
    `MeshVertexAnimationBaker` 가 굽고(15 fps, 반복 클립은 한 바퀴가 클립 길이와 같게), 넘어갈 때 한 번 인스턴스 시각 오프셋(유닛 시각 - 군중 시계)을 적습니다.
  - 멤버 · 참조가 없는 묶음은 `bucket_keep_seconds` 뒤 지웁니다(상태가 오가며 메시를 다시 만들지 않게). 진단: `GT.Animation.crowd*` 카운터 · `RT.Skin.*` 카운터,
    `-gv_animationForceVertexAnimation=1`(모든 공유 유닛을 VAT 로).
- **리더 포즈**: `setLeaderPose( 몸 )` 또는 `_bFollowParentPose` — 팔로워는 리더의 로컬 포즈를 본 이름으로 옮겨 받고 리더는 의존이 됩니다.
- **되감기**(`Object/Animation/AnimationRewind` — 언리얼 Rewind Debugger 의 자리, Shipping 에는 없다): 기록을 켜면(`-gv_animationRewind=1` · 콘솔
  `anim.rewind on` · 에디터 Animation Rewind 패널) 평가 뒤 일한 유닛마다 창(`-gv_animationRewindSeconds`, 기본 10 초)만큼 고리 버퍼에 남긴다 — 포즈(본마다
  회전 int16 넷 · 이동 int16 셋(프레임 최대 크기로 나눔), 스케일이 1 이 아닐 때만 셋 더 = 14/20 바이트, 원래 40) · 월드 행렬 · 그래프 상태 · 상태 시각 ·
  이번 프레임 알림 · 커브 · 루트 모션(`IAnimationPhaseTask::collectDebugState`). 되감는 동안(`setScrubTime` · `anim.rewind.scrub <초 전>` · 패널의 시간 막대)
  평가가 멈추고 기록된 포즈가 유닛에 걸리며, 풀면 모든 유닛을 다시 평가한다. 유닛이 사라져도 기록은 창을 벗어날 때까지 남는다.
- **모프 가중치**: 유닛이 그리는 메시의 타깃 수만큼 든다(`getMorphWeights`). 기본 포즈 단계가 포즈와 함께 0 으로 비우고, 일들이 더한다 — 애니메이터는 이름이
  타깃과 같은 커브를 그대로(BasePose 끝), 얼굴은 후처리에서. 렌더 빌더가 [0, 1] 로 묶어 팔레트 뒤에 싣고 스키닝 컴퓨트가 **스키닝 앞에** 레스트에 더한다.
  가중치가 0 이 아닌 유닛은 군중 묶음과 나누지 않는다(묶음은 가중치를 나누지 않는다).
- **얼굴**(`Object/Component/3D/FacialAnimationComponent` — 언리얼 MetaHuman 커브 · 포즈 에셋 · 립싱크 플러그인의 자리): 같은 오브젝트 유닛의 일로 돈다.
  시간 단계에 깜빡임 · 사카드 타이머 · 말하기 시각(결정적 난수 — 컴포넌트 id 씨앗), 후처리 단계에 표정(`setExpressionWeight` + 애니메이터의 같은 이름 커브) ·
  비즘(트랙, 없으면 PCM 진폭이 대체 비즘을 연다) · 깜빡임을 가중치로 더하고, 눈 본을 시선 목표 쪽으로 돌린다(부모 공간 최소 회전 · 최대 각으로 자름 ·
  로컬 회전 뒤에 붙임). 시선 목표는 게임 스레드가 모델 공간으로 옮겨 둔다(`finishAnimationFrame` — 한 프레임 늦다). `speak( 음성 )` 은 곁 트랙을 먼저 찾고,
  오디오가 있으면 소리도 낸다. 시험 머리는 `game/empty/models/testhead.*`(합성 — 타깃 일곱 · 눈 본 둘, KayKit 은 모프가 없다).

## 0.3 2D · 3D 가 함께 쓰는 것

| 무엇 | 2D(`SpriteAnimatorComponent`) | 3D(`SkeletalAnimatorComponent`) |
|------|------|------|
| 재생할 것 | `SpriteClipPlayable`(구간 = 프레임 시간의 합) | `AnimClip` |
| 시간 · 반복 · 끝 | `AnimClipCursor` · `AnimPlayer` | 같음 |
| 상태 기계 · "끝나면 다음" · 조건 전이 | `AnimGraphPlayer` + `AnimGraphAsset` | 같음 |
| 알림 · 동기 그룹 | `AnimNotifyTrack`(구간마다 `animations[].notifies` — 구간 시작 기준 초) · `AnimSyncGroup` | 같음 |
| 알림 디스패치 | `SpriteAnimatorComponent::setNotifyListener` — 틱(워커)에서 넘기고 받는 쪽이 틱 뒤로, 구간이 바뀐 틱은 `_bRestarted` | `setNotifyListener` — 게임 스레드 마무리 |
| 샘플 | 재생 시각 → 구간 안 프레임 · 트랜스폼 키 시각 | 재생 시각 → 코덱 → 본 포즈 |
| LOD | `SpriteAnimatorLodClient` — 안 보이면 · 주기 밖이면 스프라이트 프레임 · 키를 넘기지 않음(시간 · 상태는 매 틱), 보이면 그 틱에 맞춤 | `SkeletalMeshLodClient` — 포즈 건너뛰기 · 보간 · 본 LOD · 예산 |
| 되감기 | 상태만(구간 이름 · 시각 · 프레임) — 되감는 동안 기록된 프레임을 걸고 흐르지 않음 | 압축 포즈 + 상태(그래프 · 알림 · 커브 · 루트 모션) |

## 0.4 함정

- **행벡터 규약이다.** 모델 공간 = 로컬 × 부모(`local * parentModel`), 스킨 팔레트 = 역 바인드 × 모델 공간. glTF 의 열 우선 행렬 배열을 행 우선으로 읽으면
  그대로 이 규약이고, 엔진 공간(왼손)으로는 S·M·S(S = diag(-1,1,1,1))로, 회전은 (x, -y, -z, w) 로 옮깁니다.
- **가산 포즈의 회전은 `inverse(ref) * pose`**(로컬에서 먼저 적용), 얹을 때 `base * delta` 입니다. 순서를 바꾸면 부모 공간에서 돌아 팔이 엉뚱한 축으로 돈다.
- **알림은 반 열린 구간 (이전, 지금]** 이고, 재생 직후 첫 걸음만 시작 시각을 포함합니다. 반복 경계는 (이전, 끝] + [0, 지금] — 한 시각의 알림이 한 바퀴에 한 번.
  길이가 있는 알림(구간 · NotifyState)은 시작에서 `Begin`, 끝(시작 + 길이, 한 바퀴 끝을 넘지 않음)에서 `End` 가 울리고 같은 시각이면 `End` 가 먼저입니다.
  `AnimFiredNotify::_pSource` · `_eventIndex` 가 구간 하나를 가립니다. 처리(이름 → 처리기)는 `Engine/Character/AnimNotify/AnimNotifyComponent` — 애니메이터는 받는 쪽
  (`IAnimNotifyListener`, `Object/Animation/AnimNotifyListener.h`)에 프레임마다 한 번 넘깁니다.
- **크로스페이드가 다른 크로스페이드로 끊기면 플레이어는 섞이던 한 칸을 버린다**(`AnimPlayer::getInterruptCount` 가 오른다). 스켈레탈 애니메이터는 끊긴 순간의
  기본 포즈를 새 페이드 길이 동안 섞어 사라지게 해 이어 붙인다 — 포즈를 직접 섞는 다른 소비자(스프라이트는 프레임이라 해당 없음)도 같은 일을 해야 튀지 않는다.
- **끝 자세로 끝나는 클립(겨누기 · 들어 올리기)을 반복 레이어로 돌리면 끝 → 처음에서 튄다** — 레이어 `_bLoop = SW_FALSE` 로 끝 자세를 쥔다.
- **ACL 블롭은 16 바이트 정렬이어야 한다** — `AnimClip` 은 `AnimCodecBlock` 배열로 보관하고, 바이트 배열에서 재는 곳(`measureMaxError`)은 정렬된 사본을 만든다.
- **ACL 의 정밀도 · shell 거리 기본값은 센티미터 단위**다(0.01 · 3.0). 엔진은 미터라 규칙의 `animation_precision` 0.0001 · `animation_shell_distance` 0.1 이 기본이다.
| `BlendCurve` | 전환 곡선 · 길이(`BlendCurveSpec`)와 시간 → 가중치(`evaluateBlendWeight`). 카메라 디렉터 · 시퀀서 · 소켓 부착의 되돌아가기가 같은 구현을 쓴다 |

---

## 1. 본 계층과 스키닝 행렬 (`Skeleton`, `Pose`)

### 1.1 계층형 본 구조
캐릭터의 움직임은 트리 형태의 본 계층으로 이루어집니다. `Skeleton` 은 부모가 항상 자식보다 앞에 오는 배열이라(`addBone` 이 막는다) 모델 공간을 한 번 훑어 구합니다:
```
           [Hips (Root)]
          /                 [Spine]             [LeftUpLeg]
       |                     |
     [Head]             [LeftFoot]
```
- **로컬 변환**(`Pose` 의 본 하나 — 이동 · 회전 · 스케일): 부모 본 기준.
- **모델 공간**(`Pose::computeModelSpace`): 행벡터 규약이라 자식의 로컬이 먼저, 부모의 모델 공간이 나중입니다.
  $$	ext{Model}_i = 	ext{Local}_i 	imes 	ext{Model}_{	ext{parent}(i)}$$

### 1.2 스킨 팔레트
정점은 역 바인드(바인드 포즈 모델 공간의 역)를 먼저 지나고 지금 모델 공간을 지납니다(`Pose::computeSkinPalette`):
$$	ext{Palette}_i = 	ext{InverseBind}_i 	imes 	ext{Model}_i$$
레퍼런스(바인드) 포즈에서는 단위 행렬이라 정점이 움직이지 않습니다. GPU 는 팔레트를 본 하나 = float4 셋(0 · 1 · 2 열)으로 받아 정점마다 본 넷을 섞습니다(meshskin.hlsl).

---

## 2. 듀얼 쿼터니언 (`DualQuaternion`) 스키닝

### 2.1 기존 선형 행렬 스키닝(LBS)의 문제점 (캔디 랩퍼 왜곡)
기존의 선형 보간(Linear Blend Skinning, LBS) 방식은 팔/다리나 손목이 180도 회전할 때 관절 부분이 비틀리며 부피가 사탕 포장지처럼 쪼그라드는 **캔디 랩퍼 아티팩트(Candy-wrapper artifact)**가 발생합니다.

### 2.2 듀얼 쿼터니언(DQ)의 원리
듀얼 쿼터니언은 **3D 회전과 3D 이동 변환을 하나의 8차원 복소수 구조체**로 완벽하게 통합합니다:
$$\hat{q} = q_r + \epsilon q_d \quad (\epsilon^2 = 0)$$
- **실수부 ($q_r$)**: 3차원 회전을 나타내는 단위 쿼터니언.
- **허수부 ($q_d$)**: 이동 벡터 $\mathbf{t}$와 회전 쿼터니언의 곱: $q_d = \frac{1}{2} \mathbf{t} q_r$.

### 2.3 DLB (Dual Linear Blend) 보간
두 개 이상의 모션 포즈를 합성할 때, 듀얼 쿼터니언을 선형 결합한 후 정규화(Normalize)하기만 하면 **부피 손실이나 관절 왜곡 없이 완벽한 최단 경로 스크류 회전(Screw Motion)**으로 부드럽게 보간됩니다:
$$\hat{q}_{\text{blend}} = \text{Normalize}\left( (1 - t)\hat{q}_A + t\hat{q}_B \right)$$

### 2.4 듀얼 쿼터니언이 담지 못하는 것 — 스케일

$\hat{q}$ 는 **회전과 이동만** 담는다. 그래서 `DualQuaternion::fromMatrix` 는 행렬의 스케일을 떼어 버리고,
`toMatrix4x4()` 는 스케일이 1 인 행렬을 돌려준다. 여기서 두 가지가 따라온다.

- **회전을 뽑기 전에 축 길이로 나눠야 한다.** 스케일이 섞인 행렬에 `createFromRotationMatrix` 를
  그대로 걸면 스케일이 회전에 새어 든다 — 스케일 $(2,1,1)$ 과 Z축 90° 가 섞인 포즈는 112.6° 로 읽힌다.
  `fromMatrix` 는 `float4x4::decompose` 를 거치므로 이 함정을 지난다.
- **포즈를 섞을 때는 스케일을 따로 보간한다.** `BlendSpace` 는 포즈를 (스케일, 강체 변환) 으로 가른
  뒤 스케일은 선형 보간, 나머지는 DLB 로 섞고 다시 곱한다. 가르지 않으면 표본 지점에서는 원본
  포즈가 그대로 나오는데 그 사이에서만 스케일이 1 로 주저앉아, 파라미터를 조금 옮기는 것만으로
  포즈가 튄다.

---

## 3. 파라메트릭 모션 블렌딩 (`BlendSpace1D`, `BlendSpace2D`)

### 3.1 1D Blend Space (`BlendSpace1D`)
이동 속도(`speed`) 같은 1차원 파라미터에 따라 **Idle $\rightarrow$ Walk $\rightarrow$ Run** 모션을 연속적으로 합성합니다.

```
Speed:  0.0 m/s           5.0 m/s           10.0 m/s
       [Idle] ──────────── [Walk] ──────────── [Run]
                ↑                  ↑
             Speed=2.5         Speed=7.5
          (Idle 50% + Walk 50%) (Walk 50% + Run 50%)
```

### 3.2 2D Blend Space (`BlendSpace2D`)
방향(`direction`, -180°~180°)과 속도(`speed`, 0~10) 2개의 파라미터를 입력받아 **역거리 가중치(Inverse Distance Weighting, IDW)** 방식으로 8방향 모션(Walk_Forward, Walk_Backward, Strafe_Left, Strafe_Right 등)을 실시간으로 자연스럽게 합성합니다.

$$\text{Weight}_i = \frac{1}{\text{dist}(\mathbf{p}, \mathbf{p}_i)^2}, \quad \text{NormalizedWeight}_i = \frac{\text{Weight}_i}{\sum \text{Weight}}$$

표본 개수에 상한은 없다 — 가중치를 고정 크기 배열에 담지 않는다.

---

## 4. C++ 사용 예제

```cpp
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"

// 유닛(스킨드 메시 · 스켈레톤)과 애니메이터를 한 오브젝트에 붙인다 — 평가는 AnimationSystem 이 한다.
sw::SkeletalMeshComponent* pMesh = pObject->addComponent<sw::SkeletalMeshComponent>();
pMesh->setMeshId( "game/shooter3d/models/kaykit/knight.mesh" );
pMesh->setSkeletonPath( "game/shooter3d/models/kaykit/knight/knight.skeleton.json" );

sw::SkeletalAnimatorComponent* pAnimator = pObject->addComponent<sw::SkeletalAnimatorComponent>();
pAnimator->setClipFolder( "game/shooter3d/models/kaykit/knight/clips" ); // <클립 소문자>.animclip
pAnimator->setInitialState( "Idle" );

// 게임 코드: 상태 기계 파라미터 · 직접 재생 · 레이어 · 알림
pAnimator->getParameters().setFloat( sw::hashed_string( "Speed" ), 3.0f );
pAnimator->play( sw::hashed_string( "Walking_A" ), true, 0.2f );
pAnimator->addLayer( sw::AnimLayerDesc{ sw::hashed_string( "1H_Ranged_Aiming" ), sw::hashed_string( "spine" ), 1.0f } );
for ( const sw::AnimFiredNotify& fired : pAnimator->getFiredNotifies() ) { /* 발소리 · 이펙트 */ }
```

---

## 5. 후처리 리그 (`Rig/`) — IK · 제약 · 스프링 본

그래프가 만든 포즈 위에서 **순서 있는 노드 목록**(데이터)이 돕니다 — `AnimationPhase::PostProcess`, 캐릭터마다 잡 병렬, 스키닝 앞.
붙이는 컴포넌트는 `PoseModifierComponent`(`Engine/Character`)이고, 이 폴더는 오브젝트를 모르는 순수 계산이라 오브젝트 없이 시험합니다.

| 파일 | 무엇 |
|------|------|
| `RigPoseBuffer` | 로컬(원본) + 지연 갱신 모델 공간(위치 · 회전 · 스케일). 모델 공간에 쓰면 로컬을 고치고 자손을 더럽힌다 — 다음 읽기가 더러운 첫 본부터 한 번 훑는다 |
| `RigIkSolver` | 2 본(해석해 · 극점) · FABRIK · CCD · 조준(상한) · 관절 제한(원뿔 = 흔들림 + 비틀림, 경첩 = 한 축 [최소, 최대]) · 흔들림/비틀림 분해 |
| `RigSpringChain` | 베를레 입자 사슬 — 강성 · 감쇠 · 중력 · 구/캡슐 충돌체, 고정 스텝(누적기, 프레임당 상한), 순간이동 감지 |
| `RigNode` | 대상 서술(`RigTargetDef`) · 문맥(준비 · 평가 · 묶기) · 엄격한 JSON 리더(`RigJsonReader` — 읽은 키를 적고 모르는 키는 오류) · 노드 기반 |
| `RigAsset` | `*.rig.json` — 대상 · 노드(원형) + 노드 등록부(`RigNodeRegistry`, 이름 → 만들기; 모르는 종류는 로드 오류) |
| `RigInstance` | 에셋을 스켈레톤에 묶은 실행 상태 — 노드 복제 · 바깥 대상 값 · 가중치(커브 · 시퀀서 칸) · 모프 출력 · 공유 충돌체 |
| `RigIkNodes` · `RigConstraintNodes` · `RigSecondaryNodes` | 엔진 노드(아래 표) |

**한 번 평가**: 게임 스레드 `prepare`(땅 광선 · 거리 LOD · 시간 모으기) → 워커 `evaluate`: 로컬 포즈로 작업 포즈를 열고 노드를 파일 순서대로 돈다.
노드 가중치 = `weight` × (`weight_curve` 가 있으면 그 클립 커브 값, 없는 커브는 0) × (`weight_slot` 에 시퀀서가 넣은 값, 안 넣었으면 1).
가중치가 1 보다 작으면 노드가 쓴 본의 로컬을 노드 앞 값과 섞고, 0 이면 노드를 돌리지 않는다. **순서가 결과다** — "위치 복사 → 거리 제한" 과 그 반대는 다르다.

**대상**은 `bone` · `socket` · `object` 중 하나 + 선택 `unit` · `space` · `translation` · `rotation`(도). 자기 유닛의 본 · 소켓은 그 자리의 **지금 작업 포즈**를
읽고(앞 노드의 결과가 보인다), `unit` 이 있으면 다른 유닛의 이번 프레임 포즈(그 유닛이 먼저 평가되도록 의존을 건다, 고리면 로드 오류), `object` 는 프레임 시작의
월드 변환이다. `space: "<자기 본>"` 은 대상을 프레임 시작에 그 본 기준으로 찍어 두었다가 지금 그 본에 얹는다 — 손에 쥔 무기의 손잡이처럼 본에 딱 붙어 다니는 것을
늦지 않게 따르고, 무기 유닛에 의존을 걸지 않는다(손 → 무기 → 손 고리).

| 종류 | 키(공통: `type` · `name` · `weight` · `weight_curve` · `weight_slot`) |
|------|------|
| `TwoBoneIk` | `root` · `mid` · `end` · `target` · `pole` · `match_rotation`(끝 회전 = 대상 회전 — 손잡이 쥐기) · `keep_end_rotation`(기본 true) |
| `FabrikChain` · `CcdChain` | `bones`(뿌리 → 끝, 사이에 본이 끼어도 됨) · `target` · `iterations` · `tolerance` · `max_step_degrees`(CCD) · `match_rotation` · `limits`(`[{ "bone", "type": "Cone"/"Hinge", "swing_degrees", "twist_degrees", "axis", "min_degrees", "max_degrees" }]`) |
| `Aim` | `bone` · `target` · `aim_axis`(본 로컬, 기본 +Z) · `max_degrees`(애니메이션 방향 기준 상한) · `chain`(`[{ "bone", "weight" }]` — 척추 · 목이 나눠 받음) |
| `FootPlacement` | `pelvis` · `feet`(`[{ "root", "mid", "end" }]`) · `trace_up` · `trace_down` · `max_pelvis_drop` · `max_raise` · `interp_speed` · `align_to_normal` · `max_align_degrees` |
| `CopyTransform` · `Position` · `Rotation` | `bone` · `target` · `maintain_offset`(처음 평가의 상대 자리), `CopyTransform` 만 `position` · `rotation` |
| `ParentSwitch` | `bone` · `parents`(대상들) · `initial` · `settle_seconds` — 조절 `parent`. 바꾸는 프레임은 지난 출력 자리 그대로(튀지 않음), 정착 시간 동안 새 부모 자리로 |
| `Distance` | `bone` · `target` · `min` · `max` |
| `LimitRotation` | `bone` · `min_degrees` · `max_degrees`([피치, 요, 롤], 레퍼런스 기준) |
| `TwistDistribution` | `source` · `axis` · `bones`(`[{ "bone", "weight" }]`) — 조상 비틀림 본이 받은 만큼 소스에서 뺀다(손의 모델 방향 유지) |
| `PoseDriver` | `driver` · `radius_degrees` · `poses`(`[{ "name", "rotation", "morphs": [{ "morph", "weight" }], "bones": [{ "bone", "rotation", "translation" }] }]`) — 가우스 RBF, 보간 행렬을 묶을 때 푼다 |
| `SpringChain` | `bones` · `stiffness` · `damping` · `gravity` · `particle_radius` · `fixed_step` · `max_substeps` · `teleport_distance` · `lod_distance` · `colliders`(`[{ "bone", "shape": "Sphere"/"Capsule", "a", "b", "radius" }]`) · `use_shared_colliders` |

**데모.** `App -gv_benchRig=1`(Empty 게임 벤치, `Source/Games/Empty/BenchSceneRig.cpp`) — KayKit 기사가 기울기(정적 강체) 위에서 쇠뇌를 겨눈다:
발 디딤(`FootPlacement`) · 움직이는 구를 보는 머리(`Aim` + 가슴 나눔) · 쇠뇌 `Grip` 소켓을 잡는 왼손(`TwoBoneIk`, `space: handslot.r`) · 팔뚝 비틀림
(`TwistDistribution`). 쇠뇌는 오른손 소켓을 따르는 유닛(`CopyTransform`), 망토는 가슴을 따르는 뿌리 + 스프링 사슬 유닛이다. 데이터는
`game/shooter3d/rigs/`(`knight.rig.json` · `knight_weapon.rig.json` · `knight_cape.rig.json` · `knight_cape.skeleton.json` · `crossbow_2h.sockets.xml`).
`-gv_benchRigView=0..3` 카메라, `-gv_benchRigEnabled=0` 리그 끔(비용 대조군), `-gv_benchRig=N` 이면 N 명.

**2D.** `"planar": true` 인 리그는 모든 풀이를 XY 평면 · Z 축 회전으로 돕니다(`RigSolveSpace`) — 위치를 평면에 투영하고, 스프링 입자도 평면에 남깁니다. 같은 노드 · 같은 데이터 형식입니다.

**함정.**
- `quaternion::fromToRotation` 은 코사인 차 1e-6(약 0.08°) 안쪽을 단위 회전으로 버린다 — 사슬 IK 의 마지막 몇 mm 가 그 안이라 CCD 가 멈춘다. 리그는
  `RigIkSolver::makeFromToRotation` 을 쓴다.
- `quaternion::inverse()` 는 const 가 아닌 값에서 **제자리 버전(void)** 이 골라진다 — 식 안에서는 `RigIkSolver::makeInverse` 를 쓴다.
- 트위스트 본이 소스의 조상이면 소스에서 그 몫을 **부모 쪽(왼쪽)** 에서 빼야 손의 모델 방향이 남는다(흔들림과 비틀림은 교환되지 않는다).

---

## 6. 리타깃 (`Retarget/`) — 비율이 다른 스켈레톤 사이

| 무엇 | 자리 |
|------|------|
| 프로필(데이터) | `RetargetProfile` · `*.retarget.json` — 원본 · 대상 스켈레톤 경로, `root` · `pelvis` 짝, `translation`(`ScaleByPelvisHeight` · `Copy` · `None`), `chains`(`name` · `source` · `target` 본 목록 · `ik_goal`). 본 수가 다른 사슬은 사슬 길이 비율로 짝짓는다 |
| 런타임 | `PoseRetargeter`(순수) · `PoseRetargetComponent`(`Object/Component/3D` — 원본 유닛을 의존으로 걸고 기본 포즈 단계에서 옮긴다) |
| 오프라인 굽기 | `RetargetBakeUtil::bakeClip` — 원본 클립을 표본율로 샘플 · 리타깃 · 코덱으로 압축, 알림 · 커브 · 반복 · 루트 모션 트랙을 옮긴다(저장은 `AnimClip::saveToFile`) |
| 비율 | 대상 레퍼런스 덮어쓰기 — `BoneProportion::applyToPose`(`Engine/Character`)로 본 비율을 건 레퍼런스를 넘긴다 |

한 번 옮기기: (1) 짝지은 본은 **모델 공간 회전 차이**(원본 × 원본 레퍼런스⁻¹ × 대상 레퍼런스)를 옮긴다 — 두 스켈레톤의 로컬 축 약속이 달라도 맞다.
(2) 뿌리 · 골반 이동은 레퍼런스에서 움직인 만큼 × 골반 높이 비. (3) IK 목표 사슬(다리)의 끝을 "대상 끝 레퍼런스 + 원본 끝 움직임 × 비" 에 두고 2 본 IK(본 셋)
또는 FABRIK — 보폭이 골반 이동과 같은 비라 발이 미끄러지지 않는다. 다 펴도 닿지 않는 목표(늘린 다리의 보폭 끝)면 골반을 그만큼 내린다(그러지 않으면 KayKit
걷기에서 발이 4 cm 뜬다). 끝 본의 모델 회전은 (1) 의 값을 지킨다. KayKit 기사 · 해골은 같은 리그라, 시험은 해골 하수인의 다리를 본 비율로 1.25 배 늘려 보인다
(`RetargetTest.KnightWalkOnProportionedMinion`, 프로필은 `game/shooter3d/rigs/knight_to_minion.retarget.json`).
