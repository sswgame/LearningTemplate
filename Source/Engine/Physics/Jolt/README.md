# Jolt 백엔드 (3D 물리)

`IPhysicsScene3D` 의 Jolt 구현입니다. `<Jolt/...>` 헤더를 include 해도 되는 유일한 폴더입니다(`CheckThirdPartyIsolation.py`).
규칙 · 쓰는 법은 [상위 README](../README.md) 0 절.

| 파일 | 내용 |
|---|---|
| `JoltPhysicsBackend` | 전역 올리기 · 내리기(할당자를 엔진 할당자로, 로그 · 단언을 엔진 로그로, 타입 등록, 잡 시스템) · 씬 만들기. 헤더는 Jolt 를 include 하지 않는다 |
| `JoltJobSystem` | Jolt 잡을 엔진 태스크(High 레인)로 돌린다. 장벽에서 기다리는 스레드가 남은 잡을 직접 돌려 교착이 없다 |
| `JoltPhysicsScene` | 바디 · 셰이프 · 접촉(재질 섞기 · 충격량 어림 · 결정적 순서) · 디버그 그리기 |
| `JoltPhysicsSceneQuery.cpp` | 관절 · 캐릭터(`CharacterVirtual`) · 레이 / 셰이프 캐스트 · 겹침 |
| `JoltUtil.h` | 수학 타입 변환 · Jolt 참조 객체를 Jolt 할당자로 만들기 |

- 레이어: 엔진 레이어 n → 오브젝트 레이어 2n(움직이지 않음) · 2n+1(움직임), 넓은 단계 레이어 둘.
- 이 폴더의 `.cpp` 는 Jolt 임포트 타깃의 정의 · 대상 기능(AVX2)으로 컴파일되고 PCH 를 쓰지 않는다(`Source/Engine/CMakeLists.txt`).
- vcpkg 설치본의 헤더 · 라이브러리 설정 어긋남(부동소수 예외 비트)과 그 처리는 `JoltPhysicsBackend.cpp` 의 `findLibraryVersionId` · `assertFailed`.
- 리눅스(x64-linux 트리플릿)는 Jolt 가 정적 라이브러리라 `libEngine.so` 에 들어간다 — PIC 로 지어져 있어야 한다(vcpkg 기본).
