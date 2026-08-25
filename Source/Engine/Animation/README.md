# 3D 스켈레탈 애니메이션 & Blend Space 가이드

이 문서는 초보 개발자와 기여자를 위해 **스켈레탈 애니메이션(Skeletal Animation)**의 기본 원리, **듀얼 쿼터니언(Dual Quaternion)** 스키닝 수학, 그리고 **Blend Space**를 활용한 파라메트릭 모션 블렌딩의 동작 방식을 상세히 설명합니다.

---

## 1. 3D 스켈레탈 본 계층과 스키닝 행렬 (`Skeleton`, `Bone`)

### 1.1 계층형 본(Bone) 구조
캐릭터의 움직임은 트리 형태의 본(Bone) 계층 구조로 이루어집니다:
```
           [Hips (Root)]
          /             \
    [Spine]             [LeftUpLeg]
       |                     |
     [Neck]             [LeftLeg]
       |                     |
     [Head]             [LeftFoot]
```
- **본 공간 트랜스폼 (`_boneSpaceTransform`, Bone Space)**: 부모 본을 기준으로 한 상대적인 위치/회전/스케일 로컬 변환.
- **캐릭터 공간 트랜스폼 (`_characterSpaceTransform`, Character Space)**: 부모의 변환을 순차적으로 결합하여 계산되는 캐릭터 원점 기준 모델 공간(Model Space) 상의 절대 위치/회전:
  $$\text{CharacterSpace}_i = \text{CharacterSpace}_{\text{parent}(i)} \times \text{BoneSpace}_i$$

### 1.2 스키닝 행렬 (Skinning Matrices)
정점 셰이더가 캐릭터의 버텍스를 움직이기 위해 필요한 최종 스키닝 행렬은 **역 레퍼런스 포즈 행렬(`_invReferencePose`, Inverse Reference Pose)**과의 곱입니다:
$$\text{SkinningMatrix}_i = \text{CharacterSpace}_i \times \text{InvReferencePose}_i$$
캐릭터가 기본 레퍼런스 포즈(T-포즈)에 있을 때는 $\text{CharacterSpace}_i \times \text{InvReferencePose}_i = \mathbf{I}$ (단위 행렬)이 되어 정점이 움직이지 않습니다.

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

---

## 4. C++ 사용 예제

```cpp
#include "Engine/Animation/BlendSpace.h"
#include "Engine/Animation/Skeleton.h"

// 1) 스켈레톤 생성 및 본 등록 (부모 인덱스, 역레퍼런스 포즈, 본 공간 트랜스폼)
sw::Skeleton skeleton;
int32 hipsIdx = skeleton.addBone( "Hips", -1, invReferencePoseHips, boneSpaceHips );
int32 spineIdx = skeleton.addBone( "Spine", hipsIdx, invReferencePoseSpine, boneSpaceSpine );

// 2) 1D Blend Space 생성
sw::BlendSpace1D blendSpace;
blendSpace.addSample( 0.0f, "Idle", idleRootPose );
blendSpace.addSample( 5.0f, "Walk", walkRootPose );
blendSpace.addSample( 10.0f, "Run", runRootPose );

// 3) 캐릭터 이동 속도(7.5 m/s)에 맞춰 실시간 포즈 평가 및 스켈레톤 갱신
blendSpace.evaluateSkeleton( 7.5f, skeleton );

// 4) GPU 렌더링용 최종 스키닝 행렬 배열 추출
const sw::vector<sw::float4x4>& skinningMatrices = skeleton.getSkinningMatrices();
```
