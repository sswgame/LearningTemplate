# Jolt 백엔드 — 3D 물리

`IPhysicsScene3D` 를 Jolt 로 구현한 폴더입니다. `<Jolt/...>` 헤더를 include 할 수 있는 곳은 이 폴더뿐입니다(`CheckThirdPartyIsolation.py`).
물리를 쓰는 방법과 규칙은 [Physics](../README.md)에 있습니다. 이 문서는 Jolt 백엔드만의 사정을 다룹니다.

| 파일 | 내용 |
|---|---|
| `JoltPhysicsBackend` | 전역 초기화와 종료, 씬 생성. 헤더는 Jolt 를 include 하지 않습니다 |
| `JoltJobSystem` | Jolt 잡을 엔진 태스크(High 레인)로 실행 |
| `JoltPhysicsScene` | 바디, 셰이프, 접촉, 디버그 그리기 |
| `JoltPhysicsSceneQuery.cpp` | 관절, 캐릭터(`CharacterVirtual`), 레이 캐스트, 셰이프 캐스트, 겹침 질의 |
| `JoltUtil.h` | 수학 타입 변환, Jolt 할당자로 참조 객체 만들기 |

`JoltPhysicsBackend` 는 초기화할 때 Jolt 의 할당자를 엔진 할당자로, 로그와 assert 를 엔진 로그로 연결하고, 타입을 등록하고, 잡 시스템을 만듭니다.
`JoltPhysicsScene` 은 재질을 섞고(마찰은 기하 평균, 반발은 큰 쪽), 충격량을 계산하고, 접촉 이벤트를 결정적인 순서로 냅니다.

**레이어.** 엔진 레이어 n 은 Jolt 오브젝트 레이어 2n(움직이지 않는 바디)과 2n+1(움직이는 바디)이 되고, 넓은 단계 레이어는 둘입니다.

**잡 시스템.** 장벽에서 기다리는 스레드가 남은 잡을 직접 실행하므로, 엔진의 공유 작업 풀에 넘겨도 교착이 생기지 않습니다.

## 함정과 주의

**이 폴더의 `.cpp` 는 Engine 이 아니라 OBJECT 라이브러리 `EngineJolt_objects` 가 짓습니다**(`Source/Engine/CMakeLists.txt` 7절). 이 소스들은 Jolt 임포트 타깃의 정의와 대상 기능(AVX2)으로 컴파일되어 엔진 PCH 를 쓸 수 없으므로, 엔진 `pch.h` 와 이 폴더의 래퍼 헤더로 자기 PCH 를 따로 만듭니다.
컴파일 설정은 Engine 의 것을 파일 끝에서 옮겨 오므로, Engine 에 정의 · include · 옵션을 더할 때는 그 블록보다 앞에 둡니다. 래퍼 헤더를 더하거나 지우면 PCH 목록도 같이 고칩니다.

**vcpkg 설치본의 헤더와 라이브러리는 부동소수 예외 설정이 다릅니다.** 처리는 `JoltPhysicsBackend.cpp` 의 `findLibraryVersionId` 와 `assertFailed` 에 있고, 이유는 [Physics](../README.md)의 "함정과 주의"에 있습니다.

**리눅스에서는 Jolt 가 `libEngine.so` 에 정적으로 들어갑니다.** x64-linux 트리플릿의 Jolt 는 정적 라이브러리라 PIC 로 빌드되어 있어야 합니다. vcpkg 기본값이 PIC 입니다.

**TSan 억제 목록으로 Jolt 를 덮지 마세요.** TSan 구성(`CI-Debug-TSAN`)은 `-fsanitize=thread` 로 빌드한 Jolt(트리플릿 `x64-linux-tsan`)와 링크합니다.
접촉 콜백(`OnContactValidate`, `OnContact*`)은 Jolt 잡 스레드에서 불리고, TSan 이 그 경쟁을 잡아야 하기 때문입니다.
