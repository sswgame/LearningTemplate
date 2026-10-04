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
| `AnimClip` | 클립 에셋(`.animclip`) — 트랙(본 이름) 코덱 블롭 · 실수 커브 · 알림 · 루트 모션 트랙. `IAnimPlayable` |
| `Codec/AnimCodec` | 코덱 인터페이스(압축 → 코덱 id + 불투명 블롭, 런타임 샘플) · 등록부(이름 → 코덱) · 오차 측정(모델 공간 가상 정점) |
| `Codec/Raw` · `Codec/Acl` | 기준 코덱(float 그대로) · ACL 2.1 백엔드. **ACL · RTM 헤더는 `Codec/Acl/` 의 .cpp 에서만 include 한다**(게이트가 막는다) |
| `AnimPlayback` | `IAnimPlayable`(길이 · 기본 반복 · 알림) · `AnimClipCursor`(시간 · 반복 · 끝) · `AnimNotifyTrack`(지나간 알림을 정확히 한 번) |
| `AnimPlayer` | 두 칸 크로스페이드 플레이어 · `AnimSyncGroup`(리더 위상 맞추기) · `AnimParameterSet`(상태 기계 파라미터) |
| `AnimGraphAsset` · `AnimGraphPlayer` | 그래프 JSON(노드 = 상태, 링크 = 전이: 조건 `>` `<` `>=` `<=` `==` `!=` `trigger` · 블렌드 · 노드 반복)과 그것을 돌리는 상태 기계 |
| `SpriteClipAsset` · `SpriteClipPlayable` | 스프라이트 클립(`.sprite.json`)과 그 이름 붙은 구간을 `IAnimPlayable` 로 보이는 다리(2D 가 같은 재생 코드를 탄다) |
| `BlendSpace` · `DualQuaternion` | 1D/2D 파라메트릭 블렌딩(행렬 하나), DQ 스키닝 수학(아래 2 · 3 절) |
| `AnimJsonUtil` | 모르는 키를 오류로 보는 JSON 검사 · 본 변환 읽기 · 쓰기(스켈레톤 · 임포트 규칙 · 클립 곁 데이터가 함께 쓴다) |

## 0.1 파일 형식

- **`.skeleton.json`**: `{ "bones": [ { "name", "parent", "translation", "rotation"(x,y,z,w), "scale", "inverse_bind"(16, 행 우선) } ], "attachments": [ { "name", "mesh", "bone", "translation", "rotation", "scale" } ] }`.
  키는 모두 필수, 모르는 키 · 앞에 없는 부모 · 없는 본을 가리키는 부착은 로드 오류입니다.
- **`.animclip`**(리틀 엔디언): `SWAC` · 버전 1 · 플래그(반복) · 길이 · 이름 · 트랙 이름들 · 루트 모션 트랙(-1 없음) · 코덱 번호 · 블롭 · 알림(이름 · 시각 · 구간) ·
  커브(이름 · [시각, 값]). 지금 판만 읽습니다(옛 판은 원본에서 다시 임포트).
- **`.mesh` 스킨 스트림**: `Graphics/Mesh/MeshAssetFormat`(판 2) — 정점마다 본 번호 넷(uint16) · 가중치 넷(float32), 머리의 스킨 본 수.
- 만드는 쪽은 에디터의 `ModelImporter`(`Source/Editor/README.md` "모델도 들일 때 임포트한다") — 규칙(`Config/Editor/ModelImportConfig.json`)이 코덱 ·
  표본율 · 정밀도 · 가져올 클립을 고르고, 원본 옆 `<이름>.clips.json` 이 클립의 반복 · 알림 · 커브를 정합니다.

## 0.2 한 프레임 — 누가 무엇을 하나

```
GameObjectManager::tick
  ├─ 컴포넌트 틱(게임 코드가 애니메이터 파라미터를 쓴다)
  ├─ AnimationSystem::evaluate( dt )            ← 틱 뒤, 트랜스폼 플러시 앞
  │    0) 의존이 바뀌었으면 레벨을 다시 짓는다(위상 정렬, 고리는 오류)
  │    1) 유닛마다 할 일 · LOD — 쉬는 유닛은 여기서 빠진다(비용 0)
  │    2) 단계마다 레벨 순서로 engine::runParallel:
  │         Time(상태 기계 · 알림 · 루트 모션 · 커브) → [동기 그룹] → BasePose(샘플 · 크로스페이드 · 레이어 · 리더 포즈) →
  │         Attachment(부착 자리) → PostProcess(PoseModifier 자리) → SkinPalette
  │    3) 게임 스레드: finishAnimationFrame(루트 모션을 오브젝트 트랜스폼에)
  └─ 트랜스폼 플러시
GpuSceneBuilder::buildFromScene → 유닛의 팔레트를 스냅샷으로(`collectSkinPalettes`)
FrameRenderer → 모프 풀의 스킨 구간에 팔레트를 올리고 meshskin.hlsl 디스패치 한 번 → 정점 셰이더는 모프와 같은 길로 읽는다
```

- **유닛**(`SkeletalMeshComponent`) = 장비 부품 하나 = 오브젝트 하나. 스켈레톤이 없으면 본 하나("root")짜리 암묵 스켈레톤입니다. 스킨드 메시는 컴포넌트마다
  메시 객체를 따로 둡니다(모프 풀 구간이 메시마다 하나라서) — 군중 공유는 다음 일입니다.
- **일**(`IAnimationPhaseTask`)은 유닛에 걸립니다: 애니메이터(`SkeletalAnimatorBinding`)가 Time · BasePose 를 맡고, 나중의 PoseModifier · 소켓 부착이
  Attachment · PostProcess 에 끼어듭니다. 단계 함수는 워커에서 돌므로 자기 유닛과 의존으로 선언한 위 유닛만 읽습니다(`addAnimationDependency`).
- **LOD 훅**: `setUpdateRateDivisor(N)`(포즈를 N 프레임에 한 번, 시간 · 알림은 매 프레임), `setVisibleHint(false)` + `_bAnimateWhenOffscreen` 아님 →
  포즈를 건너뜀. 가시성 판정(카메라 절두체 · 중요도 예산)은 이 훅을 부르는 쪽의 일입니다(아직 없음).
- **리더 포즈**: `setLeaderPose( 몸 )` 또는 `_bFollowParentPose` — 팔로워는 리더의 로컬 포즈를 본 이름으로 옮겨 받고 리더는 의존이 됩니다.

## 0.3 2D · 3D 가 함께 쓰는 것

| 무엇 | 2D(`SpriteAnimatorComponent`) | 3D(`SkeletalAnimatorComponent`) |
|------|------|------|
| 재생할 것 | `SpriteClipPlayable`(구간 = 프레임 시간의 합) | `AnimClip` |
| 시간 · 반복 · 끝 | `AnimClipCursor` · `AnimPlayer` | 같음 |
| 상태 기계 · "끝나면 다음" · 조건 전이 | `AnimGraphPlayer` + `AnimGraphAsset` | 같음 |
| 알림 · 동기 그룹 | `AnimNotifyTrack` · `AnimSyncGroup`(스프라이트 클립에 알림 형식은 아직 없음) | 같음 |
| 샘플 | 재생 시각 → 구간 안 프레임 · 트랜스폼 키 시각 | 재생 시각 → 코덱 → 본 포즈 |

## 0.4 함정

- **행벡터 규약이다.** 모델 공간 = 로컬 × 부모(`local * parentModel`), 스킨 팔레트 = 역 바인드 × 모델 공간. glTF 의 열 우선 행렬 배열을 행 우선으로 읽으면
  그대로 이 규약이고, 엔진 공간(왼손)으로는 S·M·S(S = diag(-1,1,1,1))로, 회전은 (x, -y, -z, w) 로 옮깁니다.
- **가산 포즈의 회전은 `inverse(ref) * pose`**(로컬에서 먼저 적용), 얹을 때 `base * delta` 입니다. 순서를 바꾸면 부모 공간에서 돌아 팔이 엉뚱한 축으로 돈다.
- **알림은 반 열린 구간 (이전, 지금]** 이고, 재생 직후 첫 걸음만 시작 시각을 포함합니다. 반복 경계는 (이전, 끝] + [0, 지금] — 한 시각의 알림이 한 바퀴에 한 번.
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
