# 강체 물리 컴포넌트

씬의 물리(`Engine/Object/GameObject/ScenePhysics.h`)에 등록되는 컴포넌트입니다. 쓰는 방법과 규칙은 [Physics](../../../Physics/README.md)에 있습니다.

| 컴포넌트 | 차원 | 단계 |
|---|---|---|
| `RigidBodyComponent`, `RigidBody2DComponent` | 3D, 2D | 바디 |
| `JointComponent`, `Joint2DComponent` | 3D, 2D | 관절 |
| `CharacterControllerComponent`, `CharacterController2DComponent` | 3D, 2D | 캐릭터 |
| `WheeledVehicleComponent` | 3D | 관절 |

이 컴포넌트들은 틱하지 않습니다. 물리 프레임마다 바디, 관절, 캐릭터 단계 순서로(`PhysicsComponentPhase`) 다음 함수가 불립니다. 관절과 바퀴 차는 바디가 있어야 만들 수 있으므로 바디 뒤입니다.

1. `beginPhysicsFrame` — 물리 프레임 시작
2. 스텝마다 `prePhysicsStep` 과 `postPhysicsStep`
3. `endPhysicsFrame( alpha )` — 보간 비율로 자세를 트랜스폼에 씀

`SocketPhysicsBody.h` 의 `ISocketPhysicsBody` 는 소켓 부착이 무기를 물리로 떼어 낼 때 쓰는 인터페이스이고, `RigidBodyComponent` 가 구현합니다.

## 함정과 주의

**바디 핸들을 저장하지 마세요.** 바디, 관절, 캐릭터는 플레이 중에만 있습니다. 시작 전이거나, 꺼졌거나, 지워지는 중이면 놓습니다.
핫 리로드나 되돌리기로 다시 만든 컴포넌트는 새 바디를 만듭니다.

**속성을 바꾸면 다음 물리 프레임에 다시 만들어집니다.** 에디터나 세터로 바꾼 값은 `PhysicsComponent::requestRebuild` 로 표시되고 다음 물리 프레임에 적용됩니다. 같은 틱 안에서는 옛 바디가 그대로입니다.

**힘과 이동 요청은 어느 틱에서든 부를 수 있습니다.** 힘, 충격량, 속도, 이동 요청은 잠금 아래 쌓였다가 게임 스레드의 물리 프레임에 적용됩니다.

**트랜스폼을 직접 옮기면 순간이동입니다.** 컴포넌트가 쓴 자세와 다음 프레임의 트랜스폼이 다르면 코드가 옮긴 것으로 보고 순간이동합니다. `Kinematic` 바디는 그 자세까지 스텝마다 나눠 움직입니다.
