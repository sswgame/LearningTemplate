<!-- 생성 문서 — 손으로 고치지 않는다. 정본은 코드다. 다시 만들기: py -3 Scripts/generate/GenerateConfigReference.py -->

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

XML 속성(값 하나) · 자식 원소(목록 · 구조체)의 이름은 아래 칸 이름 그대로입니다. 모르는 이름은 로드 오류입니다.

## 칸

물리 설정 표 하나입니다. `PhysicsSystem` 이 기동 때 읽어 모든 씬에 나눠 줍니다. 레이어 순서가 레이어 번호입니다(0..31). 레이어 · 재질이 하나도 없으면 `Default` 하나씩을 채웁니다(`ensureDefaults`).

정본: [`Source/Engine/Physics/PhysicsSettings.h`](../../Source/Engine/Physics/PhysicsSettings.h)

| 칸 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_gravity` | `float3` | `0.0, -9.81, 0.0` |  | m/s2 | 3D gravity |
| `_gravity2D` | `float2` | `0.0, -9.81` |  | m/s2 | 2D gravity |
| `_subStepCount` | `uint32` | `1` | 1 ~ 16 |  | 엔진 고정 스텝(`EngineConfig::_fixedDeltaTime`) 하나에 도는 물리 스텝 수입니다. 물리 스텝 = 고정 스텝 / 이 값, 프레임당 상한도 이 배수다. |
| `_subStepCount2D` | `uint32` | `4` | 1 ~ - |  | Box2D sub-steps per step |
| `_listLayer` | `vector<PhysicsLayerDef>` | — |  |  | Collision layers in index order and what they collide with |
| `_listMaterial` | `vector<PhysicsMaterialDef>` | — |  |  | Physics materials |

## `PhysicsLayerDef`

충돌 레이어 하나와 그것이 부딪히는 레이어 이름들입니다. 표는 대칭으로 읽습니다 — 한쪽만 적어도 둘이 부딪힙니다.

정본: [`Source/Engine/Physics/PhysicsSettings.h`](../../Source/Engine/Physics/PhysicsSettings.h)

| 칸 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_name` | `hashed_string` | — |  |  | Layer name |
| `_listCollidesWith` | `vector<hashed_string>` | — |  |  | Layers this one collides with (symmetric) |

## `PhysicsMaterialDef`

물리 재질 하나입니다. 두 셰이프가 닿으면 마찰은 기하 평균(√(a·b)), 반발은 큰 쪽을 씁니다(Jolt · Box2D 기본과 같다).

정본: [`Source/Engine/Physics/PhysicsSettings.h`](../../Source/Engine/Physics/PhysicsSettings.h)

| 칸 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_name` | `hashed_string` | — |  |  | Name the bodies and shapes pick it by |
| `_friction` | `float32` | `0.5` | 0.0 ~ - |  | Coulomb friction coefficient |
| `_restitution` | `float32` | `0.0` | 0.0 ~ 1.0 |  | Bounciness: 0 stops, 1 keeps the speed |
| `_density` | `float32` | `1000.0` | 0.0 ~ - |  | Mass per volume (3D, kg/m^3) or per area (2D, kg/m^2) |
