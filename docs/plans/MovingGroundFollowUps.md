# 움직이는 바닥 위 캐릭터의 남은 일

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: 없음 · 진행: 2/3

`moveCharacter` 가 딛은 바닥의 속도(선속도 + 회전)를 실어 주게 된 뒤(2026-10-20, 결정은 `IPhysicsCharacterScene.h` 머리말과 `JoltPhysicsSceneQuery.cpp`) 남은 세 가지입니다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| M1 | 바닥이 돌면 캐릭터 방향(yaw)도 돈다 — 상태에 바닥의 위 축 각속도(`PhysicsCharacterState::_groundYawRate`, Jolt · Box2D 가 채움)를 내보내고, `CharacterControllerComponent` 가 스텝마다 돈 각을 모아 트랜스폼을 쓸 때 얹는다 | 코드 · 테스트 기대 추가(`PhysicsScene3DTest.CharacterIsCarriedByRotatingKinematicPlatform`), 빌드 미확인 |
| M2 | 점프 · 걸어 내려섬으로 바닥을 떠난 뒤의 관성 — 상태의 `_carriedVelocity`(이번 이동에서 바닥이 실어 나른 몫)를 떠나는 스텝에 수평은 `_airVelocity` 로, 오르는 바닥이면 수직 속도에도 잇는다(3D · 2D 컴포넌트 같은 규칙, 천장 판정 뒤) | 코드, 빌드 미확인 · 컴포넌트 단위 테스트 없음 |
| M3 | 네트워크 틱 동기 — 서버와 클라이언트가 바닥 위치를 같은 틱에 맞춰야 캐릭터 예측이 어긋나지 않는다 | 남음 — 움직이는 바닥을 쓰는 넷 게임이 생기면 |

## 남은 것

- M1 · M2 확인: 빌드 뒤 `PhysicsScene3DTest` 필터, 그리고 컴포넌트 테스트(도는 판 위 캐릭터의 yaw · 움직이는 판에서 뛴 뒤 수평 이동)를 하나 더한다.
- 함정: 큰 Kinematic 메시(공중 요새)는 캐릭터 쓸기 충돌 비용이 크다 — 측정 전에 단순 상자로 바닥을 대신할 수 있는지 먼저 본다.
