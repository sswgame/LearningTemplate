# 강체 물리 컴포넌트

씬의 물리(`Engine/Object/GameObject/ScenePhysics.h`)에 등록되는 컴포넌트들입니다. 틱하지 않고 물리 프레임의 단계(바디 → 관절 → 캐릭터)마다
불립니다 — `beginPhysicsFrame` · 스텝마다 `prePhysicsStep` / `postPhysicsStep` · `endPhysicsFrame( alpha )`. 규칙은
[Engine/Physics/README.md](../../../Physics/README.md) 0.3 절.

| 컴포넌트 | 차원 | 단계 |
|---|---|---|
| `RigidBodyComponent` · `RigidBody2DComponent` | 3D · 2D | 바디 |
| `JointComponent` · `Joint2DComponent` | 3D · 2D | 관절 |
| `CharacterControllerComponent` · `CharacterController2DComponent` | 3D · 2D | 캐릭터 |

- 바디 · 관절 · 캐릭터는 플레이 중에만 있다(시작 전 · 꺼짐 · 지워지는 중이면 놓는다). 핸들은 저장하지 않는다 — 핫 리로드 · 되돌리기로 다시 만든
  컴포넌트는 새 바디를 만든다.
- 속성을 바꾸면(에디터 · 세터) 다음 물리 프레임에 다시 짓는다(`PhysicsComponent::requestRebuild`).
- 힘 · 충격량 · 속도 · 이동 요청은 어느 틱에서든 부를 수 있다 — 잠금 아래 쌓였다가 게임 스레드의 물리 프레임에 든다.
- 트랜스폼에 쓴 자세와 다음 프레임의 트랜스폼이 다르면 코드가 옮긴 것으로 보고 순간이동한다(Kinematic 은 그 자세로 움직인다).
