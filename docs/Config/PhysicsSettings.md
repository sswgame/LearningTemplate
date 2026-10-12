<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# PhysicsSettings

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Resource/engine/physics/physicssettings.xml` |
| 층 | 엔진 기본값 |
| 읽는 곳 | `PhysicsSettings::loadFromResource` (`PhysicsSystem`) |
| 언제 | 기동(씬 물리를 처음 세울 때) |
| 배포본 | 엔진 팩에 실림 |
| 커밋 | 한다 |

XML 속성(값 하나)과 자식 요소(목록, 구조체)의 이름은 아래 필드 이름 그대로입니다. 모르는 이름은 로드 오류입니다.

## 필드

물리 설정 하나입니다. `PhysicsSystem` 이 기동할 때 읽어 모든 씬에 나눠 줍니다. 레이어 순서가 레이어 번호다(0..31). 레이어와 재질이 하나도 없으면 `Default` 하나씩을 채운다(`ensureDefaults`).

원본: [`Source/Engine/Physics/PhysicsSettings.h`](../../Source/Engine/Physics/PhysicsSettings.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_gravity` | `float3` | `0.0, -kDefaultGravity, 0.0` |  | m/s2 | 3D gravity |
| `_gravity2D` | `float2` | `0.0, -kDefaultGravity` |  | m/s2 | 2D gravity |
| `_subStepCount` | `uint32` | `1` | 1 ~ 16 |  | 엔진 고정 스텝(`EngineConfig::_fixedDeltaTime`) 하나에 도는 물리 스텝 수입니다. 물리 스텝 = 고정 스텝 / 이 값, 프레임당 상한도 이 배수다. |
| `_subStepCount2D` | `uint32` | `4` | 1 ~ - |  | Box2D sub-steps per step |
| `_workerCount2D` | `uint32` | `1` | 1 ~ 16 |  | 2D 씬 하나가 스텝에 쓰는 일꾼 수(스텝 스레드 포함)입니다. 1 이면 한 스레드이고, 2 이상이면 씬마다 전용 워커를 그만큼 하나 적게 띄웁니다(`Box2DWorkerPool`). 바디가 수천일 때만 이득이다 — 재는 법은 `PhysicsBenchTest.Box2DStepByWorkerCount`. 결과는 워커 수와 관계없이 같다. |
| `_listLayer` | `vector<PhysicsLayerDef>` | — |  |  | Collision layers in index order and what they collide with |
| `_listMaterial` | `vector<PhysicsMaterialDef>` | — |  |  | Physics materials |

## `PhysicsLayerDef`

충돌 레이어 하나와 그 레이어가 부딪히는 레이어 이름들입니다. 테이블은 대칭으로 읽으므로 한쪽만 적어도 둘이 부딪힙니다.

원본: [`Source/Engine/Physics/PhysicsSettings.h`](../../Source/Engine/Physics/PhysicsSettings.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_name` | `hashed_string` | — |  |  | Layer name |
| `_listCollidesWith` | `vector<hashed_string>` | — |  |  | Layers this one collides with (symmetric) |

## `PhysicsMaterialDef`

물리 재질 하나입니다. 두 셰이프가 닿으면 마찰은 기하 평균(√(a·b))을, 반발은 큰 쪽을 씁니다(Jolt 와 Box2D 의 기본과 같습니다).

원본: [`Source/Engine/Physics/PhysicsSettings.h`](../../Source/Engine/Physics/PhysicsSettings.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_name` | `hashed_string` | — |  |  | Name the bodies and shapes pick it by |
| `_friction` | `float32` | `0.5` | 0.0 ~ - |  | Coulomb friction coefficient |
| `_restitution` | `float32` | `0.0` | 0.0 ~ 1.0 |  | Bounciness: 0 stops, 1 keeps the speed |
| `_density` | `float32` | `1000.0` | 0.0 ~ - |  | Mass per volume (3D, kg/m^3) or per area (2D, kg/m^2) |
