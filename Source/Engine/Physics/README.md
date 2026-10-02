# 물리 및 연속 충돌 감지(CCD) 가이드

이 문서는 초보 개발자와 기여자를 위해 **이산 충돌 감지(Discrete Collision)**의 한계, **터널링 현상(Tunneling Effect)**의 원인, 그리고 SW Engine에서 제공하는 **Swept AABB / Swept Sphere 연속 충돌 감지(CCD)**의 수학적 원리와 사용법을 상세히 설명합니다.

---

## 1. 터널링 현상(Tunneling Effect)이란?

### 1.1 이산 충돌 감지의 한계
일반적인 물리 엔진의 이산 충돌 감지(Discrete Collision Detection)는 매 프레임의 **특정 순간(시작점과 끝점)**에 오브젝트가 겹쳐 있는지만 검사합니다.

만약 총알이나 레이저처럼 **초고속으로 움직이는 작은 투사체**가 1프레임 동안 이동한 거리($\Delta \mathbf{s} = \mathbf{v} \cdot \Delta t$)가 장애물의 두께보다 크다면, 이전 프레임에는 벽 앞쪽에 있고 다음 프레임에는 이미 벽 뒤쪽으로 순간이동하여 **벽과의 충돌을 아예 감지하지 못하고 뚫고 지나가는 터널링 현상**이 발생합니다.

```
[이산 충돌 감지 (Discrete) - 터널링 발생!]
Frame 1 위치 (앞)                      Frame 2 위치 (뒤)
      ●                                       ●
  (충돌 검사)            [얇은 벽]         (충돌 검사)
       │                    │                  │
       └────────────────────┼──────────────────┘
                 충돌 판정: FALSE (관통!)
```

---

## 2. 연속 충돌 감지 (CCD - Continuous Collision Detection)

### 2.1 동작 원리: 스윕 테스트 (Sweep Test)
연속 충돌 감지는 점이 아니라 **오브젝트가 1프레임 동안 이동한 전체 궤적(Swept Volume)**을 검사하여 충돌 여부를 판정합니다.

```
[연속 충돌 감지 (CCD) - 스윕 궤적 검사]
Frame 1 위치                             Frame 2 위치
      ● ═════════════════[충돌!]══════════════> ●
                         [얇은 벽]
       │                    │                  │
       └────────────────────┼──────────────────┘
          충돌 판정: TRUE (정확한 충돌 시간 t = 0.47 계산!)
```

### 2.2 민코프스키 합(Minkowski Sum)과 슬랩 레이캐스트
`CCD::sweepAabb`는 이동하는 AABB의 크기(반경)만큼 정적 대상 AABB를 3차원으로 확장(Minkowski Sum)한 뒤, 확장된 박스에 대해 이동 중심점으로부터 이동 변위 벡터($\mathbf{d}$)로 3D 슬랩(Slab) 광선을 투사합니다.

1. **대상 박스 확장**:
   $$\text{Expanded}.\min = \text{Target}.\min - \text{MovingHalfExtents}$$
   $$\text{Expanded}.\max = \text{Target}.\max + \text{MovingHalfExtents}$$
2. **슬랩 진입 시각($t_{\text{enter}}$) 계산**:
   $$t_x = \frac{\text{Expanded}_x - \text{Center}_x}{\text{Displacement}_x}$$
3. $0.0 \le t_{\text{enter}} \le 1.0$ 범위에 진입 시각이 존재하면 정확한 **정규화 충돌 시각 $t$**, **접촉 지점(Hit Point)**, **접촉 법선(Hit Normal)**을 반환합니다.

---

## 3. C++ 사용 예제

### 3.1 `CCD::sweepAabb` 단독 사용
```cpp
#include "Engine/Physics/CCD.h"

// 1) 얇은 벽(두께 1m)과 초고속 총알(한 프레임에 100m 이동)
sw::AABB wall{ sw::float3{ -10.0f, 0.0f, 50.0f }, sw::float3{ 10.0f, 10.0f, 51.0f } };
sw::AABB bullet{ sw::float3{ -0.1f, 1.5f, 0.0f }, sw::float3{ 0.1f, 1.7f, 0.2f } };
sw::float3 displacement{ 0.0f, 0.0f, 100.0f }; // 1프레임 동안 100m 전진

// 2) CCD 스윕 검사 수행
sw::SweepHit hitResult{};
if ( sw::CCD::sweepAabb( bullet, displacement, wall, hitResult ) )
{
    // hitResult._time: 약 0.498 (궤적의 49.8% 지점에서 충돌)
    // hitResult._hitNormal: (0, 0, -1) (벽 앞면 법선)
    // hitResult._hitPoint: 충돌 발생 위치
}
```

### 3.2 `PhysicsWorld::sweepTest` 월드 레벨 스윕
월드에 등록된 수천 개의 물리 바디 중 이동 궤적 경계(Swept Bounds)에 걸치는 바디들을 공간 그리드로 브로드페이즈 필터링한 후, 가장 먼저 부딪히는 최단 충돌체를 찾아냅니다.

```cpp
#include "Engine/Physics/PhysicsWorld.h"

sw::PhysicsWorld physicsWorld;

// 월드 내 충돌체들과의 스윕 검사 (충돌 레이어 0번 대상)
sw::SweepHit hit{};
if ( physicsWorld.sweepTest( projectileAABB, velocity * deltaTime, 0, hit ) )
{
    // 가장 먼저 부딪힌 바디 핸들 및 오브젝트
    sw::SlotHandle hitBody = hit._hitBody;
    uint64 hitObjectId = hit._hitObjectId;
}
```

---

## 4. 겹침 이벤트 (`PhysicsWorld::step`)

강체가 없으므로 `step` 은 적분하지 않고 **겹침만 다시 잽니다.** 지난 step 과 견줘 새로 겹친 쌍은 시작, 떨어지거나 바디가 사라진 쌍은 끝으로
`getOverlapEvents()` 에 냅니다(유니티 `OnTriggerEnter2D/Exit2D` · 언리얼 `BeginOverlap/EndOverlap`). 계속 겹친 쌍은 다시 내지 않습니다.

`GameObjectManager` 가 틱 · 트랜스폼 적용이 끝난 뒤 게임 스레드에서 한 번 부릅니다(`stepPhysics`):

1. 등록된 콜라이더(`BoxCollider2DComponent`)의 바디를 그 프레임의 월드 자리로 한 번에 맞춘다 — 시작 전이거나 꺼진 콜라이더는 빠진다.
2. `step` 해 이벤트를 받는다.
3. 이벤트마다 두 오브젝트의 켜진 컴포넌트에 `onOverlapBegin( pOther )` / `onOverlapEnd( pOther )` 를 부른다(상대가 사라졌으면 nullptr).

콜라이더는 틱하지 않습니다. 예전에는 병렬 틱에서 제 바디를 맞춰, 같은 그룹에서 겹침을 묻는 쪽이 스케줄에 따라 옛 · 새 자리를 봤습니다.
틱 안의 질의(`queryAabb` · `sweepTest`)는 지난 step 의 자리를 봅니다(유니티 물리 질의와 같다).
