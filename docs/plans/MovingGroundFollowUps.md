# 움직이는 바닥 위 캐릭터의 남은 일

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: 없음 · 진행: 3/4

`moveCharacter` 가 딛은 바닥의 속도(선속도 + 회전)를 실어 주게 된 뒤(2026-10-20, 결정은 `IPhysicsCharacterScene.h` 머리말과 `JoltPhysicsSceneQuery.cpp`) 남은 일입니다.

## 1. 결정

- 바닥 속도를 싣는 곳은 물리 백엔드(`moveCharacter`) 하나이고, 상태에 바닥이 실어 나른 속도(`_carriedVelocity`)와 위 축 각속도(`_groundYawRate`)를 내보낸다. 방향을 돌리고 관성을 잇는 일은 트랜스폼을 쓰고 떠나는 순간을 아는 컴포넌트의 몫이다.
- 네트워크 틱 동기는 바닥을 "쌓은 시간" 이 아니라 **틱의 순수 함수**로 움직여 푼다(`TickedPathMoverComponent::computeOffset`). 같은 틱이면 서버 · 클라이언트 · 되감아 다시 시뮬레이션한 쪽이 모두 같은 자리다.

## 2. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| M1 | 바닥이 돌면 캐릭터 방향(yaw)도 돈다 — `CharacterControllerComponent` 가 스텝마다 돈 각을 모아 트랜스폼을 쓸 때 얹는다 | 코드 · 테스트(`PhysicsScene3DTest.CharacterIsCarriedByRotatingKinematicPlatform`, `PhysicsWiringTest.ControllerFollowsMovingGroundYawAndKeepsItsMomentum`), 빌드 미확인 |
| M2 | 바닥을 떠난 뒤의 관성 — 떠나는 스텝에 바닥 속도를 수평은 `_airVelocity` 로, 오르는 바닥이면 수직 속도에도(3D · 2D 같은 규칙) | 코드 · 테스트(위 PhysicsWiringTest), 빌드 미확인 |
| M3 | 틱 함수 바닥 — `TickedPathMoverComponent`(경유점 · 구간 시간 · 틱 길이 · 왕복, 틱 = 조종 시스템 틱 + `_tickOffset`). 조종 시스템이 없는 씬에서는 움직이지 않고 알린다 | 코드 · 테스트(`GimmickGenreTest.TickedPathMoverIsAPureFunctionOfTheTick`), 빌드 미확인 |
| M4 | 클라이언트의 네트워크 키트가 서버 틱과의 차이를 `setTickOffset` 으로 넣는 연결 | 남음 — 움직이는 바닥을 쓰는 넷 게임이 생기면(클라이언트 틱 ↔ 서버 틱 대응은 `ReplicationClient` 의 렌더 틱이 안다) |

## 남은 것

- M1 ~ M3 확인: 빌드 뒤 위 테스트 필터.
- 함정: 큰 Kinematic 메시(공중 요새)는 캐릭터 쓸기 충돌 비용이 크다 — 측정 전에 단순 상자로 바닥을 대신할 수 있는지 먼저 본다.
