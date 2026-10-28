# 품질 후보

> 상태: 후보 · 선행: 없음 · 영역: quality · 진행: 해당없음

테스트 틀 · 린트 · 부하 검증 · 쿠킹 · 간헐 실패 관찰에서 아직 안 한 일의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다. 이미 계획 문서가 있는 일은 [GpuVerification](GpuVerification.md) 에 있다.

## 한눈에 보기

| 절 | 한 줄 설명 | 착수 조건 유형 |
|---|---|---|
| LinuxManualChecks | X11 입력 자동 테스트(주입기 코드 끝) | Linux 컴파일 확인 후 시나리오 작성 |
| LogGateRemainders | 종료 때 `Task fencing timeout` 원인 | 다시 보일 때 |

## 테스트 · 검증

### LinuxManualChecks
제목: 리눅스 X11 입력의 자동 테스트
상태: 코드 끝, Linux 컴파일 확인 전 — XTest 주입기(`Engine/Window/Linux/XTestInputInjector`, 개발 명령 `input.xtest move|button|key`, CMake `SW_ENABLE_XTEST` 선택 의존, libxtst 없으면 기능만 끈다)를 넣었다. 이 PC 는 Windows 라 컴파일하지 못했다.
상용 엔진과 같은 선: 언리얼 Automation Driver · 유니티 Input Test Fixture 처럼 앱 안 이벤트 합성이 아니라 장치 층에서 입력을 넣는다(자동 반복은 `XSendEvent` 로 흉내 낼 수 없다).
남은 일: 리눅스(Xvfb)에서 `-DSW_ENABLE_XTEST=ON` 으로 지어 컴파일 확인 → `input.xtest` 로 좌표 · `XkbSetDetectableAutoRepeat` 를 확인하는 시나리오 · 호스트 테스트를 새 시나리오 틀 위에 만든다.

### LogGateRemainders
제목: 로그 게이트의 남은 것
왜 필요한가: 시나리오 · 모든 테스트 케이스는 선언하지 않은 Warning · Error 로 진다. 표식 없는 줄은 코드로 손봤고 상시 `ctest-nogpu` 전체가 확인한다. 돌려 보기 전이라 Release · Shipping 이나 시간에 기대는 배경 스레드 줄이 남았을 수 있고, 지는 케이스는 원인을 고치거나 그 호출에 범위(`SW_TEST_DEFENSIVE_SCOPE`)를 씌운다(판정기를 좁히지 않는다).
착수 조건: 에디터를 켠 짧은 실행(`-gv_profileFrames=90`)이 다른 GPU 작업과 겹쳐 종료 때 `Task fencing timeout (5000 ms) before module reload` Error 를 다시 낼 때(36 회 중 3 회, 단독 0 회).
남은 첫 일: 5 초 넘게 도는 에디터 배경 작업이 무엇인지 가린다. 로그의 첫 미완료 태스크 이름(`ModuleHost::drainRenderWorkers` 가 찍는다)과 `py -3 -m Scripts stacks <pid>` 의 워커 스택을 본다.
