# Automation 분리

> 상태: 구현중 · 선행: 없음 · 영역: engine · 진행: 표

`Engine/Automation` 은 시나리오 형식 · 등록 · 타임라인 · 입력 재생 같은 일반 기능과 판정 · 로그 게이트 · 보고 같은 테스트 전용 기능을 한 폴더에 섞어 들고 있다. 결론은 **형식 경계를 먼저 고치고 폴더를 둘로 나눈다**이다. ①②③④ 는 코드가 들어갔고 Debug 빌드가 통과했다. 기능별 테스트 · 시나리오는 새 프레임워크를 만들려고 전부 지웠으므로 이 문서의 검증 · 회귀 언급은 새 틀이 정해진 뒤 다시 쓴다.

| 단계 | 내용 | 상태 |
|---|---|---|
| ① 탐침 | 탐침 레지스트리(상태 값을 이름으로 읽는 표)를 티어 10 `Engine/Automation/Probe` 로 내리고 각 기능이 자기 폴더에서 탐침을 등록 | 완료 |
| ② 단계 | 단계 함수가 `AutomationRunner&` 대신 좁은 문맥 `AutomationStepContext` 를 받는다 | 완료(코드, 컴파일 확인 전) |
| ③ 시나리오 | 테스트 전용 필드를 테스트 쪽으로 빼고, 실행기를 번역 단위와 클래스(`AutomationJudge` · `AutomationStepTimeline`)로 나누고, 결과 형식 · 종료 코드를 갈라 폴더를 둘로 가른다 | 완료(Debug 빌드 통과) |
| ④ 구성 분리 | `SW_ENABLE_AUTOMATION` 옵션: 끄면 Engine 이 실행기 · 세션 · 판정 · 보고를 짓지 않는다 | 완료(옵션 · 가드 · 구성 확인, 끈 구성의 Engine 빌드는 아래 참고) |

## 설계 결정

- **폴더 둘.** 일반 `Engine/Automation/Scenario`(티어 11): 형식 · 레지스트리 · 타임라인 · 입력 단계 · 녹화 · Scratch · 시간 상수. 테스트 `Engine/Automation/Runner`: 판정 단계 · 로그 게이트 · 보고 · 이미지 지표 · 종료 코드 · Session · Chain. 탐침 레지스트리는 `Engine/Automation/Probe`(티어 10)로 따로 갔다. 타임라인 · 입력 단계가 Input · Scene 위에 있어 같은 폴더에 두면 기능이 탐침을 등록하지 못하기 때문이다.
- **이름만 바꾸기는 먼저 하지 않는다**(호출부 없는 개명이므로). 형식 경계를 고친 결과로 폴더가 갈라진다. 파일 이름의 `Automation` 접두는 그대로 둔다.
- Shipping 에 폴더 전체가 들어가고 `-scenario` 가 Shipping App 에서 열려 있는 것은 의도다. 가드 기준은 `SW_DEV_COMMANDS_ENABLED` 로 통일했다. `-cook-scenes` 등 오프라인 쿠킹 도구에 `SW_SHIPPING` 가드가 없는 것도 의도다(`-cooked-dir` 로 준 폴더에만 쓰고 배포 패키징이 배포 구성 App 으로 부른다).
- 인게임에서 시나리오 · 탐침 · `VirtualInputScript` 를 읽는 곳은 없다(실행기와 테스트뿐).

## 고친 형식 · 계층 결함

1. 단계 함수 형식이 `AutomationRunner&` 를 받아 단계 등록자(UI · GameFramework · Editor · App)가 로그 게이트 · 보고 · 종료 코드 헤더를 끌어왔다 → ② 의 좁은 문맥 인터페이스. 단계는 종료 코드 대신 `AutomationStepEnd` 로 끝낸다.
2. 시나리오 형식에 테스트 전용 필드(Scratch · Launch · Load · isolated · editor · requireForeground)가 있었다 → `AutomationTestOptions`(`getTestOptions()`)로 모았다.
3. 결과 열거형이 곧 App 종료 코드였다 → `AutomationOutcome` 에서 숫자를 빼고 종료 코드 표는 `AutomationExitCode.h` 한 곳(`kAutomationExitCodeRunning` 은 `IEngineFrameObserver::kRunning` 그 자체). "RHI 를 못 씀" 코드(77)의 정본은 `RHIInitResult.h`.
4. 실행기가 Terrain · 캐릭터 탐침을 대신 정의하던 역방향 → 탐침 레지스트리를 티어 10 으로 내리고 기능이 자기 `*Probes.cpp` 에 등록한다. 티어는 정수라 Probe 를 10 에 두고 그 위 기능(Character · Environment 11, Vfx · Sequencer · Destruction 12)을 한 칸 올렸다. GameFramework · 에디터 허브의 탐침도 영역 폴더로 옮겨 탐침을 모아 쥔 허브 파일은 없다. 실행기에 남은 `Object.*` · `Input.*` · `Assets.Pending` · `Profiler.TraceRecording` 은 가까운 층의 별도 `*Probes.cpp` 에 둔다.
5. `ModuleHost` 가 `AutomationSession::kSubsystemName` 을 알던 역방향 → 이름을 낮은 층 헤더 `Engine/Common/HostSubsystemNames.h` 로 내리고 `AutomationSession::getSubsystemDependencies` 가 의존 칸에 적는다. `ModuleImageHostSubsystem` 은 `Engine/Automation` 을 include 하지 않는다.

## ③ 의 분할 · 이동

- 스물한 파일(형식 · 등록표 · 문맥 · 타임라인 · 입력 · 창 · 백엔드 · 환경 단계 · 녹화 · Scratch · 시간 상수)을 `Engine/Automation/Scenario` 로 옮겼다. `Scratch` 와 시간 상수는 형식 헤더가 들고 있어, Scenario 가 Automation 을 include 하지 않으려면 같이 가야 했다.
- 실행기는 `AutomationRunner` 한 클래스를 두고 번역 단위만 갈랐다: `AutomationRunner.cpp`(시작 · 프레임 · 시계 잡기 · 타임라인 · 끝맺음), `AutomationRunnerJudge.cpp`(탐침 비교 · 기록 · 로그 · 이미지 판정), `AutomationRunnerSteps.cpp`(엔진 단계 등록), `AutomationStepArgs.h/.cpp`(검사와 판정이 함께 읽는 인자 · 수 비교). 시계 잡기 · 사진 대기 순서는 줄 단위로 옮겨 바뀌지 않았다.
- 클래스 분할(전에 기각했던 것을 했다): 판정 상태(실패 · 미확정 · 처리하지 않은 로그 · 로그 수집 · 허용 목록 · 방문 이력 · 기억 값 · 지표 · 기록 줄)는 `AutomationJudge` 로, 진행 상태(프레임 · 단계 순번 · 대기 · 시계 잡기 · 시한 · 스크린샷 대기열)는 `AutomationStepTimeline` 으로, 입력 원천 · 보고 · 행 감시 · 끝맺음은 얇아진 `AutomationRunner` 에 남았다. 얽힘은 상태를 소유자별로 가르자 풀렸다: 판정이 읽는 바깥 값(프레임 · 산출물 경로 · 오브젝트 매니저)은 호출마다 단계 문맥(`AutomationStepContext`)에서 받아 판정이 실행기를 알지 못하고, 결과를 `Failed` · `Unconfirmed` · `UnhandledLog` 로 정하는 일은 실행기 끝맺음이 한다(`applyLogGate` 는 결과를 인자로 받아 고친다). 이름: 보고의 벽시계 합계 클래스가 이미 `AutomationTimeline` 이라 진행 상태 쪽은 `AutomationStepTimeline` 이다. 보고 쓰기(`AutomationReportWriter`)는 지표 · 기록 줄을 더 들지 않고 판정이 소유한 목록을 인자 포인터로 받는다. 비교: 언리얼 `FAutomationTestBase`(오류 목록)와 `LatentCommands`(대기열)가 갈라진 모양이다.
- 구성 분리 `SW_ENABLE_AUTOMATION`(기본 ON — 배포 구성도 GPU 검증에 쓴다): 끄면 `Engine/Automation/Runner` 의 실행기 · 세션 · 체인 · 판정 · 로그 수집 · 허용 목록 · 보고 · 행 감시 · 이미지 지표 · 문턱 근거 · 단계 인자 · 시간 줄 소스를 Engine 이 짓지 않고(미빌드 목록에 오른다) `SW_AUTOMATION_ENABLED=0` 이다. App 은 세션을 들지 않고 `-scenario` · `-scenario-record` 가 오면 이유를 오류로 남기고 시나리오 없이 돈다(조용히 무시하지 않는다). 창 제목 추적 · 사진 준비 판정 · 탐침 등록(`*Probes`)과 `Engine/Automation/Scenario` · `Engine/Automation/Probe` 는 엔진 프레임이 읽거나 기능이 자기 폴더에서 등록하는 것이라 남는다. 상용 엔진 비교: 언리얼의 `WITH_AUTOMATION_TESTS` 가 이 모양이다.
- 데스크톱 전역 상태 단계: 단계 등록의 `_bUsesDesktop` 표시(`SW_AUTOMATION_STEP_DESKTOP`)가 있는 단계(`ExpectCursorClip` · `CaptureWindow`)와 루트 `requireForeground="true"` 는 `input="mixed"` 시나리오에서만 허용한다(전경 창은 OS 에 하나뿐). 에디터 시나리오 11 개는 화면 복사 대신 프로세스 안 UI 캡처 `CaptureEditorUI` 로 바꿔 겹쳐 돌 수 있게 했다.

## 남은 일

- 컴파일 확인은 Debug 로 끝났다. Release · 리눅스는 아직 안 돌렸다.
- 새 프레임워크가 서면 단계별 검증(층 게이트, 네 백엔드 시나리오 회귀)과 이 문서의 시나리오 · 테스트 언급을 새 구조에 맞춰 다시 쓴다.
- 시나리오 실행 시간 줄이기(코드 · 도구는 들어갔고 실측만 남음):
  - 보고 `timeline`(단계 · 고정 대기 · 조건 대기 · 시계 잡기별 프레임 수와 시간)을 `scenario-waits --measured-dir` 로 어림과 견준다.
  - 어림값 문턱 승격(`scenario-thresholds`)과 샤드 비용표 보정(`test-costs`)은 Release 한 바퀴를 돈 뒤 제안 표를 읽고 `--apply` 한다.
  - 긴 고정 대기 두 시나리오(`harvestvalley/control` · `abilityarena/control`)를 조건 대기로 바꿔 3150 프레임(7 %)을 줄였다. 나머지 긴 고정 대기는 프레임 수 자체가 목적이다.
  - "비에디터 게임 시나리오 묶음 실행"(한 프로세스로 케이스당 기동 비용 줄이기)은 아직 없다. App 이 `-scenario` 하나만 받으므로 같은 프로세스에서 여러 시나리오를 잇는 씬 · 모듈 상태 초기화 경계 설계가 첫 단계다.
- 시나리오로 덮이지 않은 기능은 탐침이 없어 못 덮는 경우가 많다(내비메시 · 에셋 임포트 · `AI.BehaviorTask`). 시나리오는 탐침을 먼저 더해야 한다. 기존 시나리오에 단언을 더할 후보: 캐릭터 에디터 · UI 프리뷰 · 네트 조건 · 게임 뷰 카메라 · 로딩 화면 · 군중 · 외형 합치기(기대 값은 한 번 재야 정한다). 사람 눈 확인이 필요한 것은 시나리오가 사진을 남기고 사람이 판정한다.
- ④ 의 켠 구성과 끈 구성의 Shipping App 크기 · 기동 시간 비교는 아직 재지 않았다(옵션은 기본 ON 이다).

- 2026-10-29: 세 폴더를 `Engine/Automation/{Probe,Scenario,Runner}` 한 부모로 모았다. 구성 분리는 파일 이름 목록 대신 `Runner/` 폴더째 뺀다(언리얼 AutomationWorker · AutomationController 분리와 같은 축).
