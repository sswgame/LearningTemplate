# 물리 확장 — 부력 · 파워드 래그돌 · 천과 캐릭터 · 결정성

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: D1-8 · 진행: 3/5

차량 물리(`WheeledVehicleComponent`, `IPhysicsVehicleScene`)는 이미 있다. 이 문서는 남은 넷(부력 · 유체, 파워드 래그돌, 천과 캐릭터의 상호작용, 플랫폼 간 결정성)의 설계와 단위다.

## 1. 설계

- **부력(P1).** 3D 만 있는 역할 인터페이스 `IPhysicsBuoyancyScene` 을 둔다 — 소프트 바디와 같은 차원 트레이트 방식(`kSupportsBuoyancy`, 2D 는 빈 `PhysicsNoBuoyancyScene` 이라 부르면 컴파일 오류). 한 번의 부력은 수면 평면(점 · 법선) · 부력(유체 밀도 ÷ 바디 밀도) · 끌림 · 흐름이고(`PhysicsBuoyancyDesc`), Jolt `BodyInterface::ApplyBuoyancyImpulse` 가 셰이프의 잠긴 부피와 부력 중심을 정확히 구한다. 씬 쪽은 `BuoyancyVolumeComponent`(물 상자, 윗면이 수면)가 고정 스텝 직전에 겹친 바디를 찾아 건다 — 물 자체는 바디가 아니다.
- **파워드 래그돌(P2).** Cone 관절(Jolt `SwingTwistConstraint`)의 스윙 · 비틀림 모터로 이웃 바디의 상대 자세를 목표로 끈다(`IPhysicsJointScene::driveJointToRotation` — 두 바디의 월드 목표 자세를 받아 백엔드가 상대 회전으로 바꾸고 `SetTargetOrientationBS` 로 건다). 래그돌 쪽은 `PhysicsRagdollBuilder::driveToPoseWithMotors` 가 뼈 포즈로 관절마다 부른다. 몸 전체의 자리는 중력 · 충돌이 정하고 꺾임만 포즈를 따른다. Hinge 관절(무릎 · 팔꿈치)은 끌지 않는다.
- **천과 캐릭터(P3).** 캐릭터(`CharacterVirtual`)는 이동 질의로만 세계와 닿아 소프트 바디가 몸을 뚫는다. 캐릭터에 같은 캡슐의 키네마틱 속 바디를 두면(Jolt `mInnerBodyShape`) 천 · 센서가 몸을 느낀다 — `PhysicsCharacterDesc::_bInnerBody` · `CharacterControllerComponent::_bInnerBody`(기본 끔 — 속 바디는 움직이는 바디를 더 세게 밀어 게임이 고른다).
- **결정성(P4, 결정).** Jolt 의 플랫폼 간 결정성(`JPH_CROSS_PLATFORM_DETERMINISTIC`)은 끈 채로 둔다. 같은 플랫폼 · 같은 빌드의 결정성은 지금도 있다(같은 스텝 · 같은 입력이면 같은 결과). 켜면 부동소수 경로가 느려지고 vcpkg 포트 기능 · 모든 Jolt 사용 TU 가 함께 바뀐다. 착수 조건은 "롤백을 물리 객체로 넓힌다" 는 결정이다 — 그때 이 단위를 연다.
- 확인은 모두 빌드 뒤다. 실행 확인 목록은 아래 표의 상태 칸과 "남은 것" 에 있다.

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| P1 | 부력 — `IPhysicsBuoyancyScene` · Jolt 구현 · `BuoyancyVolumeComponent` · 테스트 `PhysicsBuoyancyTest` 셋 | 코드 완료(빌드 안 함) |
| P2 | 파워드 래그돌 API — `driveJointToRotation`(Jolt Cone · Box2D 오류) · `PhysicsRagdollBuilder::driveToPoseWithMotors` | 코드 완료(빌드 안 함) |
| P3 | 천과 캐릭터 — 캐릭터 속 바디(`_bInnerBody`) | 코드 완료(빌드 안 함) |
| P4 | `RagdollComponent` 에 파워드 모드를 잇는다 — 모터가 쫓을 포즈는 애니메이션 평가 직후 · 물리 섞기 전의 포즈여야 한다(지금 유닛의 포즈는 `blendPhysicsPose` 가 물리 결과로 덮어써, 그것을 쫓으면 모터가 제자리다). 평가 직후 포즈를 담는 자리(유닛 작업 `IAnimationPhaseTask` 의 앞 단계)를 정하고 `_poweredMaxTorque` 를 둔다 | 남음 |
| P5 | 플랫폼 간 결정성 — 위 착수 조건이 오면 켜고 `DestructionDamageTest` 처럼 구성 · 플랫폼을 넘는 해시 테스트를 둔다 | 결정(끈 채로) |

## 남은 것

- P1 확인: 테스트 셋을 돌린다. 부력 2 의 상자가 수면 둘레에 멈추는 높이(±0.6 m)는 추정이다 — 실제 값을 보고 테스트 경계를 맞춘다.
- P2 확인: 래그돌 씬에서 모터 토크를 올리며 팔다리가 포즈로 끌리는지, 쿼터니언 곱 순서(`JoltPhysicsScene::driveJointToRotation` 의 `conj(qA) × qB`)가 맞는지 본다 — 틀리면 관절이 반대로 꺾인다.
- 옮겨 온 설계 메모(옛 후보 절): 센서 규칙(옛 `D1-2.1`, 2D 는 들어감) — 센서끼리 서로를 본다(box2d `sensor.c` 3.1.1). 막는 콜라이더도 센서다. 겹침은 고정 스텝에서만 난다. 무버 캐스트가 센서를 거르도록 비센서 셰이프 마스크에 전용 비트를 둔다. 결정은 [결정 기록](../09_Decisions.md) 의 `D1-2.1` 줄이다. 옛 `D4-6.6`(파워드 래그돌 보류)은 P2 · P4 다.
- P3 확인: 천을 단 오브젝트 옆으로 속 바디를 켠 캐릭터가 지나갈 때 천이 밀리는지 본다. 캐릭터 자신의 사격 · 질의가 제 속 바디를 맞히지 않는지(사용자 값 거르기 `_ignoreUserData`) 본다.
