# 물리 확장 — 부력 · 파워드 래그돌 · 천과 캐릭터 · 결정성

> 상태: 측정대기 · 선행: 없음 · 영역: engine · 옛번호: D1-8 · 진행: 5/5

**남은 확인(통합에서 확인):** 매니페스트 id `physics-cloth-character-scene`(P3), `physics-powered-ragdoll-scene`(P4). 단위 테스트는 묶음의 `ctest-nogpu` 가 돈다. 확인이 끝나면 통합 담당이 이 문서를 닫는다.

차량 물리(`WheeledVehicleComponent`, `IPhysicsVehicleScene`)는 이미 있다. 이 문서는 남은 넷(부력 · 유체, 파워드 래그돌, 천과 캐릭터의 상호작용, 플랫폼 간 결정성)의 설계와 단위다.

## 1. 설계

- **부력(P1).** 3D 만 있는 역할 인터페이스 `IPhysicsBuoyancyScene` 을 둔다 — 소프트 바디와 같은 차원 트레이트 방식(`kSupportsBuoyancy`, 2D 는 빈 `PhysicsNoBuoyancyScene` 이라 부르면 컴파일 오류). 한 번의 부력은 수면 평면(점 · 법선) · 부력(유체 밀도 ÷ 바디 밀도) · 끌림 · 흐름이고(`PhysicsBuoyancyDesc`), Jolt `BodyInterface::ApplyBuoyancyImpulse` 가 셰이프의 잠긴 부피와 부력 중심을 정확히 구한다. 씬 쪽은 `BuoyancyVolumeComponent`(물 상자, 윗면이 수면)가 고정 스텝 직전에 겹친 바디를 찾아 건다 — 물 자체는 바디가 아니다.
- **파워드 래그돌(P2).** Cone 관절(Jolt `SwingTwistConstraint`)의 스윙 · 비틀림 모터로 이웃 바디의 상대 자세를 목표로 끈다(`IPhysicsJointScene::driveJointToRotation` — 두 바디의 월드 목표 자세를 받아 백엔드가 상대 회전으로 바꾸고 `SetTargetOrientationBS` 로 건다). 래그돌 쪽은 `PhysicsRagdollBuilder::driveToPoseWithMotors` 가 뼈 포즈로 관절마다 부른다. 몸 전체의 자리는 중력 · 충돌이 정하고 꺾임만 포즈를 따른다. Hinge 관절(무릎 · 팔꿈치)은 끌지 않는다.
- **천과 캐릭터(P3).** 캐릭터(`CharacterVirtual`)는 이동 질의로만 세계와 닿아 소프트 바디가 몸을 뚫는다. 캐릭터에 같은 캡슐의 키네마틱 속 바디를 두면(Jolt `mInnerBodyShape`) 천 · 센서가 몸을 느낀다 — `PhysicsCharacterDesc::_bInnerBody` · `CharacterControllerComponent::_bInnerBody`(기본 끔 — 속 바디는 움직이는 바디를 더 세게 밀어 게임이 고른다).
  속 바디는 엔진 바디 표에도 오른다(`BodyRecord::_bCharacterInner`, 사용자 값은 캐릭터의 것) — Jolt 의 모든 바디는 사용자 값이 엔진 핸들이어야 접촉 알림 · 질의 거르기(`_ignoreUserData`) · 닿음 결과가 오브젝트로 풀린다. 지우는 것은 `destroyCharacter` 뿐이다(`destroyBody` 는 오류).
- **결정성(P4, 결정).** Jolt 의 플랫폼 간 결정성(`JPH_CROSS_PLATFORM_DETERMINISTIC`)은 끈 채로 둔다. 같은 플랫폼 · 같은 빌드의 결정성은 지금도 있다(같은 스텝 · 같은 입력이면 같은 결과). 켜면 부동소수 경로가 느려지고 vcpkg 포트 기능 · 모든 Jolt 사용 TU 가 함께 바뀐다. 착수 조건은 "롤백을 물리 객체로 넓힌다" 는 결정이다 — 그때 이 단위를 연다.
- 빌드와 단위 테스트는 끝났다(2026-10-22, Debug-Server 의 EngineTest). 남은 확인은 화면(천 · 컴포넌트 단 파워드 래그돌)이고 "남은 것" 에 있다.

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| P1 | 부력 — `IPhysicsBuoyancyScene` · Jolt 구현 · `BuoyancyVolumeComponent` · 테스트 `PhysicsBuoyancyTest` 셋 | 완료(테스트 셋 통과, 부력 2 상자는 y = -0.0008 에 선다 — 경계 ±0.1) |
| P2 | 파워드 래그돌 API — `driveJointToRotation`(Jolt Cone · Box2D 오류) · `PhysicsRagdollBuilder::driveToPoseWithMotors` | 완료(`PhysicsScene3DTest.ConeMotorDrivesChildToTargetRotation` 가 돌린 부모로 곱 순서를, `PhysicsRagdollTest.PoweredMotorsBendJointsToThePose` 가 뼈 포즈 구동을 본다) |
| P3 | 천과 캐릭터 — 캐릭터 속 바디(`_bInnerBody`) | 완료(`PhysicsScene3DTest.CharacterInnerBodyIsQueriedAsTheCharacter`) — 천이 밀리는지는 화면 확인 남음 |
| P4 | `RagdollComponent` 의 파워드 모드 — `_poweredMaxTorque`(0 이면 축 늘어진 래그돌). 모터의 목표는 섞기 전 포즈다: `blendPhysicsPose` 가 섞기 직전의 로컬 포즈를 모델 공간 `_listAnimationModel` 로 담고(섞은 뒤 포즈를 쫓으면 모터가 제자리다), `beginPhysicsFrame` 끝의 `drivePoweredMotors` 가 래그돌 동안 모터를 걸고 벗어날 때 한 번 끈다 | 빌드 · 빌더 단 테스트 통과 — 컴포넌트 단(애니메이터가 도는 래그돌)은 화면 확인 남음 |
| P5 | 플랫폼 간 결정성 — 위 착수 조건이 오면 켜고 `DestructionDamageTest` 처럼 구성 · 플랫폼을 넘는 해시 테스트를 둔다 | 결정(끈 채로) |

## 남은 것

- 옮겨 온 설계 메모(옛 후보 절): 센서 규칙(옛 `D1-2.1`, 2D 는 들어감) — 센서끼리 서로를 본다(box2d `sensor.c` 3.1.1). 막는 콜라이더도 센서다. 겹침은 고정 스텝에서만 난다. 무버 캐스트가 센서를 거르도록 비센서 셰이프 마스크에 전용 비트를 둔다. 결정은 [결정 기록](../09_Decisions.md) 의 `D1-2.1` 줄이다. 옛 `D4-6.6`(파워드 래그돌 보류)은 P2 · P4 다.
- P3 확인(매니페스트 id `physics-cloth-character-scene`): 천을 단 오브젝트 옆으로 속 바디를 켠 캐릭터가 지나갈 때 천이 밀리는지 본다(화면). 제 속 바디를 거르는 질의는 테스트가 본다.
- P4 확인(매니페스트 id `physics-powered-ragdoll-scene`): `_poweredMaxTorque` 를 올린 래그돌이 쓰러지며 팔다리를 애니메이션 자세로 끄는지, 일어서기로 넘어갈 때 모터가 꺼지는지(`_bMotorsDriven`) 본다. 섞기 전 포즈는 래그돌 동안 애니메이터가 계속 평가한다는 가정이다 — 래그돌에서 애니메이터가 멈추면 목표가 마지막 포즈에 머문다.
