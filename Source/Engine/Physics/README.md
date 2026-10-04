# 물리 (Physics)

이 폴더에는 두 가지가 있습니다.

- **강체 물리** — 바디 · 셰이프 · 관절 · 캐릭터 컨트롤러 · 질의 · 고정 스텝 · 접촉 이벤트. 엔진 쪽 인터페이스(`IPhysicsScene3D` · `IPhysicsScene2D`)
  뒤에 3D 는 Jolt(`Jolt/`), 2D 는 Box2D v3(`Box2D/`)가 있습니다. 아래 0 절.
- **겹침 월드**(`PhysicsWorld`) — AABB 겹침 · 연속 쓸기 · 겹침 이벤트만 재는 가벼운 월드. `BoxCollider2DComponent` · 키트의 투사체 · 근접 판정이
  씁니다. 1 절부터. 강체 씬과 따로 돌고 같은 레이어 행렬(`CollisionLayers`)을 씁니다.

---

## 0. 강체 물리

### 0.1 감싸기 — 라이브러리는 백엔드 폴더 안에만

| 무엇 | 자리 |
|---|---|
| 차원 공통 낱말 — 핸들(세대) · 바디 종류 · 관절 종류 · 모터 · 접촉 단계 | `PhysicsTypes.h` |
| 셰이프 서술자(3D: 상자 · 구 · 캡슐 · 볼록 껍질 · 삼각 메시 / 2D: 상자 · 원 · 캡슐 · 다각형 · 사슬) | `PhysicsShape.h` |
| 바디 · 관절 · 캐릭터 서술자 — 차원 묶음(`PhysicsDimension3D` · `2D`) 하나로 갈리는 템플릿 | `PhysicsDesc.h` |
| 질의 거름 · 레이/셰이프 캐스트 결과 | `PhysicsQuery.h` |
| 접촉 · 트리거 이벤트(시작 · 유지 · 끝 · 충격량)와 셰이프 쌍 → 바디 쌍 추적기 | `PhysicsContact.h` |
| 고정 스텝 누적기 · 보간 비 | `FixedStepAccumulator` |
| 바디 쌍 충돌 끄기 표 | `PhysicsPairFilter` |
| 디버그 선 출구 · 셰이프 와이어프레임 | `PhysicsDebugDraw` |
| 레이어 · 레이어 충돌 표 · 재질 · 중력 · 스텝(데이터) | `PhysicsSettings` · `Resource/engine/physics/physicssettings.xml` |
| 씬 인터페이스 | `IPhysicsScene.h` |
| 서비스 — 설정을 읽고 백엔드를 올리고 씬을 만든다 | `PhysicsSystem`(`engine::getPhysicsSystem`, 기동 단계 `Physics`) |
| 물리 에셋(뼈마다 바디 · 관절 한계 · 히트 존 · 끈 쌍) | `PhysicsAsset` · `*.physics.xml` |
| 래그돌 · 히트박스 빌더(평범한 뼈 배열) | `PhysicsRagdoll` |
| Jolt 백엔드 · 잡 시스템 | `Jolt/` |
| Box2D 백엔드 · 2D 무버 | `Box2D/` |

라이브러리 헤더(`<Jolt/...>` · `<box2d/...>`)는 그 백엔드 폴더에서만 include 하고, 라이브러리는 `Source/Engine/CMakeLists.txt` 에서만 링크합니다
(`Scripts/lint/gate/CheckThirdPartyIsolation.py`). 백엔드를 바꾸면 `IPhysicsScene` 을 구현하는 씬 하나와 `PhysicsSystem` 의 생성 한 줄이 바뀝니다.
Jolt 의 대상 기능 옵션(AVX2)과 정의는 Jolt 백엔드 소스에만 붙습니다 — 엔진 전체가 AVX2 로 컴파일되지 않게(그래서 그 소스는 PCH 를 쓰지 않는다).

**잡 시스템.** Jolt 는 스텝을 의존 관계 있는 잡으로 나누고 부른 스레드가 장벽에서 남은 잡을 직접 돌리며 기다립니다 — 잡끼리 바쁘게 기다리지
않으므로 엔진 태스크(`TaskManager`, High 레인)로 돌립니다(`JoltJobSystem`). Box2D 는 한 스레드로 돕니다 — 솔버 워커들이 서로를 바쁘게 기다리는
구조라 공유 작업 풀에 넘기면 교착할 수 있습니다.

**vcpkg Jolt 의 함정.** 설치본의 `Core.h` 는 `JPH_FLOATING_POINT_EXCEPTIONS_ENABLED` 를 박아 넣지만 라이브러리는 그것 없이 지어졌습니다(clang-cl).
`JPH::RegisterTypes()` 는 버전 ID 가 다르면 abort 하므로 백엔드는 라이브러리가 받아들이는 ID 를 찾아 `RegisterTypesInternal` 을 직접 부르고,
그 비트 하나만 다를 때만 받아들입니다. 같은 사정으로 헤더 안의 `Vec3::CheckW` 단언은 거짓 경보라 단언 처리기가 넘깁니다.

### 0.2 규칙

- **단위** 미터 · 킬로그램 · 초 · 라디안. 3D 는 +Y 위, 2D 는 XY 평면 · Z 축 둘레 각.
- **핸들**은 슬롯 + 세대입니다. 지운 뒤 같은 슬롯을 다시 써도 옛 핸들은 무효이고, 무효 핸들을 받은 함수는 아무것도 하지 않습니다.
- **씬 하나는 한 스레드**(게임 스레드)가 다룹니다. 컴포넌트의 힘 · 충격량 · 속도 설정은 어느 틱에서든 부를 수 있게 명령으로 쌓였다가 물리 프레임에
  듭니다(`PhysicsBodyCommand`).
- **고정 스텝.** `ScenePhysics` 가 누적기로 프레임 시간을 고정 스텝 수로 바꿉니다(설정 `_fixedTimeStep` · `_maxStepsPerFrame` — 넘는 시간은
  버려 느려진다). 같은 입력 · 같은 순서면 같은 결과입니다(`StepIsDeterministic`).
- **재질**은 셰이프마다입니다. 마찰은 기하 평균, 반발은 큰 쪽(두 백엔드 같다). 밀도로 질량을 정하거나 `_mass` 로 덮습니다.
- **레이어**는 설정 표의 이름 순서가 번호입니다(최대 32). 충돌 목록은 대칭으로 읽습니다. 질의는 레이어 마스크로 거릅니다.
  두 바디만 예외로 끄려면 `setPairCollision`(관절로 이은 바디는 `_bDisableCollision`).
- **이벤트**는 바디 쌍마다 시작 · 유지 · 끝입니다(셰이프가 여럿이어도 한 번). 한 스텝 안에 닿았다 떨어진 쌍은 시작과 끝을 둘 다 냅니다. 바디를
  지우거나 끄면 그 접촉은 다음 이벤트 묶음에서 끝납니다. 충격량 — 시작은 부딪힌 충격(다가온 속도 × 유효 질량 × (1 + 반발)), 유지는 그 스텝의
  접촉 충격량(쉬는 상자는 무게 × 스텝).
- **대량 생성 · 파괴**는 `createBodies` · `destroyBodies` 한 번으로(Jolt 는 넓은 단계를 한 번 고친다), 같은 모양은 `createShape` 로 미리 지어 나눠
  씁니다(파편 · 탄피). 풀에 넣어 다시 쓸 바디는 `setBodyEnabled( false )`.

### 0.3 씬의 물리와 컴포넌트

`GameObjectManager` 가 `ScenePhysics` 를 소유하고 `stepPhysics` 에서 겹침 월드 다음에 한 번 진행합니다(`Engine/Object/GameObject/ScenePhysics.h`).
3D · 2D 씬은 처음 쓸 때 만듭니다. 컴포넌트(`Engine/Object/Component/Physics/`)는 틱하지 않고 단계(바디 → 관절 → 캐릭터)마다 불립니다:

| 컴포넌트 | 하는 일 |
|---|---|
| `RigidBodyComponent` · `RigidBody2DComponent` | 바디 하나. 종류 · 셰이프(둘 이상이면 컴파운드) · 레이어 · 재질 · 질량 · 트리거 · 연속을 속성으로 든다. Dynamic 은 보간한 자세를 트랜스폼에 쓰고, Kinematic 은 트랜스폼을 따라 스텝마다 나눠 움직이고, 코드가 옮기면(`teleportTo` 포함) 순간이동 |
| `JointComponent` · `Joint2DComponent` | 이 오브젝트의 강체를 부모 사슬에서 가장 가까운 강체(또는 월드)에 잇는다. 자리 · 축은 컴포넌트의 월드 자세 |
| `CharacterControllerComponent` · `CharacterController2DComponent` | 캡슐 무버. `setMoveVelocity` · `jump`, 중력은 컴포넌트가 쌓는다. 자리는 발 |

이벤트는 두 오브젝트의 켜진 컴포넌트에 갑니다 — 막는 접촉은 `onCollisionBegin/Stay/End( CollisionInfo )`, 트리거는 `onOverlapBegin/Stay/End( OverlapInfo )`
(`_selfBody` · `_otherBody` 로 어느 바디였는지 — 래그돌 뼈 · 히트 존). 바디의 사용자 값이 오브젝트 id 라서, 컴포넌트가 아닌 코드(래그돌 빌더)가
만든 바디도 사용자 값에 오브젝트 id 를 실으면 그 오브젝트가 받습니다. `gv_physicsDebugDraw` 가 켜지면 활성 씬의 바디 · 캐릭터를 디버그 선으로
그립니다.

### 0.4 래그돌 · 히트박스(물리 에셋)

```cpp
PhysicsAsset asset;
(void)asset.loadFromResource( "game/<게임>/characters/hero.physics.xml" );
PhysicsSkeletonView skeleton{ boneNames, parentIndices, modelSpaceBones };   // 애니메이션 타입이 아니라 배열 셋
PhysicsRagdollOptions options;
options._pSettings = &engine::getPhysicsSystem().getSettings();           // 레이어 이름을 푼다
options._userData  = owner->getObjectId();                                 // 이벤트가 이 오브젝트로 간다
options._bodyType  = PhysicsBodyType::Kinematic;                           // 히트박스로 시작
PhysicsRagdoll ragdoll;
(void)PhysicsRagdollBuilder::create( scene, asset, skeleton, worldFromModel, options, ragdoll );
PhysicsRagdollBuilder::driveToPose( scene, ragdoll, skeleton, worldFromModel, fixedStep );   // 매 스텝: 애니메이션을 따른다
// 맞음: raycast → hit._body → ragdoll.findBodyIndex → asset._listBody[i]._hitZone (이름 · 피해 배율)
PhysicsRagdollBuilder::setBodyType( scene, ragdoll, PhysicsBodyType::Dynamic );               // 죽음: 래그돌로
PhysicsRagdollBuilder::readBoneTransforms( scene, ragdoll, skeleton, worldFromModel, outPose ); // 포즈를 받는다
```

셰이프 · 관절 축은 뼈 로컬입니다. 관절은 그 뼈의 바디와 부모 사슬에서 가장 가까운 바디가 있는 뼈를 Cone(스윙 원뿔 + 비틀림) · Hinge 등으로 잇고,
이은 바디끼리 · `_listDisabledPair` 의 쌍은 부딪히지 않습니다. 바디 없는 뼈(손가락 · 모자)는 되읽을 때 입력 포즈의 부모 상대 변환으로 따라갑니다.
에셋의 뼈가 스켈레톤에 없으면 오류이고 아무것도 만들지 않습니다. 견본: `Resource/engine/physics/samples/chain.physics.xml`.

---

# 겹침 월드 · 연속 충돌 감지(ContinuousCollision) 가이드

이 문서는 초보 개발자와 기여자를 위해 **이산 충돌 감지(Discrete Collision)**의 한계, **터널링 현상(Tunneling Effect)**의 원인, 그리고 SW Engine에서 제공하는 **Swept AABB / Swept Sphere 연속 충돌 감지(ContinuousCollision)**의 수학적 원리와 사용법을 상세히 설명합니다.

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

## 2. 연속 충돌 감지 (ContinuousCollision - Continuous Collision Detection)

### 2.1 동작 원리: 스윕 테스트 (Sweep Test)
연속 충돌 감지는 점이 아니라 **오브젝트가 1프레임 동안 이동한 전체 궤적(Swept Volume)**을 검사하여 충돌 여부를 판정합니다.

```
[연속 충돌 감지 (ContinuousCollision) - 스윕 궤적 검사]
Frame 1 위치                             Frame 2 위치
      ● ═════════════════[충돌!]══════════════> ●
                         [얇은 벽]
       │                    │                  │
       └────────────────────┼──────────────────┘
          충돌 판정: TRUE (정확한 충돌 시간 t = 0.47 계산!)
```

### 2.2 민코프스키 합(Minkowski Sum)과 슬랩 레이캐스트
`ContinuousCollision::sweepAabb`는 이동하는 AABB의 크기(반경)만큼 정적 대상 AABB를 3차원으로 확장(Minkowski Sum)한 뒤, 확장된 박스에 대해 이동 중심점으로부터 이동 변위 벡터($\mathbf{d}$)로 3D 슬랩(Slab) 광선을 투사합니다.

1. **대상 박스 확장**:
   $$\text{Expanded}.\min = \text{Target}.\min - \text{MovingHalfExtents}$$
   $$\text{Expanded}.\max = \text{Target}.\max + \text{MovingHalfExtents}$$
2. **슬랩 진입 시각($t_{\text{enter}}$) 계산**:
   $$t_x = \frac{\text{Expanded}_x - \text{Center}_x}{\text{Displacement}_x}$$
3. $0.0 \le t_{\text{enter}} \le 1.0$ 범위에 진입 시각이 존재하면 정확한 **정규화 충돌 시각 $t$**, **접촉 지점(Hit Point)**, **접촉 법선(Hit Normal)**을 반환합니다.

---

## 3. C++ 사용 예제

### 3.1 `ContinuousCollision::sweepAabb` 단독 사용
```cpp
#include "Engine/Physics/ContinuousCollision.h"

// 1) 얇은 벽(두께 1m)과 초고속 총알(한 프레임에 100m 이동)
sw::AABB wall{ sw::float3{ -10.0f, 0.0f, 50.0f }, sw::float3{ 10.0f, 10.0f, 51.0f } };
sw::AABB bullet{ sw::float3{ -0.1f, 1.5f, 0.0f }, sw::float3{ 0.1f, 1.7f, 0.2f } };
sw::float3 displacement{ 0.0f, 0.0f, 100.0f }; // 1프레임 동안 100m 전진

// 2) ContinuousCollision 스윕 검사 수행
sw::SweepHit hitResult{};
if ( sw::ContinuousCollision::sweepAabb( bullet, displacement, wall, hitResult ) )
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

1. 등록된 콜라이더(`BoxCollider2DComponent`)의 바디를 그 프레임의 월드 자리 · 레이어 · 판정 방식(연속 · 트리거)으로 한 번에 맞춘다(`updateBody`) —
   시작 전이거나 꺼진 콜라이더는 빠진다. 그 사이 순간이동했으면(`SceneComponent::teleportTo`) 순간이동으로 맞춘다(4.1).
2. `step` 해 이벤트를 받는다.
3. 이벤트마다 두 오브젝트의 켜진 컴포넌트에 `onOverlapBegin( const OverlapInfo& )` / `onOverlapEnd( const OverlapInfo& )` 를 부른다 — 상대 오브젝트(끝에서
   사라졌으면 nullptr), 양쪽 콜라이더가 트리거인지, 닿은 때를 싣는다.

**트리거.** 콜라이더를 트리거로 두면(`setTrigger` — 유니티 `isTrigger` · 언리얼 Overlap 반응) 겹침은 똑같이 내지만, 받는 쪽은 `OverlapInfo::_bOtherTrigger`
로 "몸이 아니라 감지 범위였다" 를 압니다. 투사체 · 공격 판정은 상대의 트리거에 막히지도 피해를 주지도 않습니다. 겹침은 바디 쌍마다 나므로 한
오브젝트에 몸과 감지 범위가 함께 있으면 같은 상대에게서 둘이 옵니다 — 훅이 `OverlapInfo` 를 받는 이유가 그 둘을 가르기 위해서입니다.

콜라이더는 틱하지 않습니다 — 병렬 틱에서 제 바디를 맞추면 같은 그룹에서 겹침을 묻는 쪽이 스케줄에 따라 옛 · 새 자리를 봅니다.
틱 안의 질의(`queryAabb` · `sweepTest`)는 지난 step 의 자리를 봅니다(유니티 물리 질의와 같다).

### 4.1 연속 바디 — 겹침 이벤트에서 터널링 막기

이산 겹침은 step 마다 **끝 자리**만 봅니다. 한 프레임에 얇은 적보다 멀리 가는 총알은 1 절의 그림처럼 겹친 적 없이 지나갑니다. 콜라이더를
연속으로 두면(`BoxCollider2DComponent::setContinuous` — 투사체는 시작할 때 스스로 켠다) `step` 이 그 바디를 **지난 step 의 자리**(`PhysicsBody::_stepAabb`)에서
지금 자리까지 `ContinuousCollision::sweepAabb` 로 쓸어, 그 사이에 처음 닿은 바디도 이번 step 의 겹침으로 칩니다(유니티 `CollisionDetectionMode2D.Continuous`).

- 지나간 쌍은 이번 step 에 시작하고, 다음 step 에(이미 떨어졌으면) 끝납니다.
- 이벤트 목록은 **닿은 때(`PhysicsOverlapEvent::_time`) 순서**입니다. 총알이 한 step 에 적 둘을 지나가면 앞의 적이 먼저 오고, 총알은 거기서 사라지므로
  뒤의 적은 맞지 않습니다. 끝 이벤트와 제자리 겹침은 `_time = 1` 입니다.
- 출발점에서 이미 겹쳐 있던 쌍(닿은 때 0)은 쓸림으로 더하지 않습니다 — 그 겹침은 지난 step 이 쟀습니다. 넣으면 떠난 쌍의 끝이 한 step 늦습니다.
- **상대도 움직였으면 상대 운동으로 잽니다**(Box2D 총알 TOI · 유니티 Continuous Dynamic). 모든 바디가 지난 step 의 자리를 들고 있고, 두 바디 모두 거기서
  출발해 연속 바디의 이동에서 상대의 이동을 뺀 만큼 쓸립니다 — 프레임 사이에 총알 길을 가로질러 건너편으로 간 적도 맞습니다. 그 적의 **지금** 자리는
  총알이 쓸린 범위 밖일 수 있어, 후보는 쓸린 범위를 이번 step 에 가장 많이 움직인 바디의 거리만큼 넓혀 모읍니다.
- 출발점은 바디를 더한 자리부터 잡힙니다. **순간이동은 쓸지 않습니다** — `SceneComponent::teleportTo`(언리얼 `TeleportPhysics` · 유니티 `Rigidbody.position`
  대입)는 그 컴포넌트와 그 아래 붙은 모두를 표시하고, 콜라이더는 바디를 `BodyMoveType::Teleport` 로 맞춰 새 자리를 다음 쓸림의 출발점으로 둡니다. 그냥
  옮기면(`setWorldPosition`) 그 길이 쓸립니다.
