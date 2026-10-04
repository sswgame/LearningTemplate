# Box2D 백엔드 (2D 물리)

`IPhysicsScene2D` 의 Box2D v3 구현입니다. `<box2d/...>` 헤더를 include 해도 되는 유일한 폴더입니다(`CheckThirdPartyIsolation.py`).
규칙 · 쓰는 법은 [상위 README](../README.md) 0 절.

| 파일 | 내용 |
|---|---|
| `Box2DPhysicsBackend.h` | 씬 만들기(전역 상태 없음). Box2D 헤더를 include 하지 않는다 |
| `Box2DPhysicsScene` | 바디 · 셰이프(사슬 포함) · 레이어 거름(범주 비트) · 쌍 예외(필터 관절) · 이벤트(접촉 · 센서 · 부딪힘) |
| `Box2DPhysicsSceneQuery.cpp` | 관절(용접 · 회전 · 거리 · 직선 — Cone 은 오류) · 캐릭터 무버 · 레이 / 셰이프 캐스트 · 겹침 |
| `Box2DUtil.h` | 수학 타입 변환 · 사용자 값 · 셰이프 id 키 |

- 월드는 한 스레드로 돈다(솔버 워커가 서로를 바쁘게 기다리는 구조라 공유 작업 풀에 넘기지 않는다).
- 월드에 붙는 관절은 씬이 드는 정적 바디(원점)에 붙인다. 위치 모터는 관절 스프링(목표 각 · 거리)이다.
- 충격량: 시작은 부딪힘 이벤트의 다가온 속도 × 유효 질량 × (1 + 반발), 유지는 마지막 서브 스텝의 `normalImpulse` × 서브 스텝 수
  (`totalNormalImpulse` 는 이완 반복까지 더해 약 두 배다).
- 캐릭터는 `b2World_CollideMover` → `b2SolvePlanes` → `b2World_CastMover` 를 다섯 번까지 되풀이하고, 캡슐을 조금 내려 디딤을 찾는다.
