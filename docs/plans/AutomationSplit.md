# Automation 분리

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 표

`Engine/Automation` 은 시나리오 형식 · 등록 · 타임라인 · 입력 재생 같은 일반 기능과, 테스트 판정 · 로그 게이트 · 보고 같은 테스트 전용 기능을 한 폴더에 같이 가지고 있다. 2026-10-23 사용자와 정한 결론은 **형식 경계를 먼저 고치고 폴더를 둘로 나눈다**이다.
결정은 1절, 사실은 2절, 추정 · 확인하지 못한 것은 6절에 구분해 적는다.

## 1. 결론 (2026-10-23)

- 폴더 둘로 나눈다.
  - 일반 `Engine/Scenario`: 형식 · 레지스트리 · 타임라인 · 입력 단계 · 녹화. 탐침 레지스트리는 ① 에서 따로 `Engine/Probe`(티어 10)로 갔다 — 타임라인 · 입력 단계는 Input · Scene 위라 같은 폴더에 두면 기능이 탐침을 등록하지 못한다.
  - 테스트 `Engine/Automation`: 판정 단계 · 로그 게이트 · 보고 · 이미지 지표 · Scratch · 종료 코드 · Session · Chain.
- **이름만 바꾸기는 먼저 하지 않는다.** 호출부가 없는 개명이기 때문이다(2절 첫 항목). 형식 경계를 고친 결과로 폴더가 갈라진다.
- 진짜 구조 결함은 셋이다(3절). 계층 방향 결함은 4절이다.

## 2. 사실

- 인게임에서 시나리오 · 탐침 · `VirtualInputScript` 를 읽는 곳은 현재 0 이다. 탐침을 읽는 곳은 실행기와 테스트뿐이다. 자동화가 아닌 사용은 에디터 `InputMapPanel` 의 `InputReplay` 하나다.
- Shipping 에 폴더 전체가 들어가고 `-scenario` 가 Shipping App 에서 열려 있다(`App.cpp:310-331`). 결정 기록 5-3 "자동화는 Shipping 에서도 쓴다"와 맞는 의도된 상태다.
- Shipping 가드 기준이 파일마다 다르다 — `SwapBackend` 는 `SW_SHIPPING`, `DevCommand` 는 `SW_DEV_COMMANDS_ENABLED`, 나머지는 가드가 없다.
- `-cook-scenes`(`HeadlessSubsystem.cpp:254`)에는 `SW_SHIPPING` 가드가 없다. 다른 곳에서 막는지는 **확인하지 못했다**.

## 3. 형식 결함 셋

1. **단계 함수 형식 `AutomationStepFunction` 이 `AutomationRunner&` 를 받는다.** 그래서 단계를 등록하는 쪽(UI · GameFramework · Editor · App)이 로그 게이트 · 보고 · 종료 코드 헤더를 끌어온다. → 좁은 문맥 인터페이스를 받게 한다(단계 ②).
2. **시나리오 형식(`AutomationScenario.h:135-148`)에 테스트 전용 필드가 있다** — Scratch · Launch · Load · isolated · editor · requireForeground. → 테스트 쪽 구조로 옮긴다(단계 ③).
3. **실행 결과 열거형이 곧 App 종료 코드다.** → `AutomationResult` 와 종료 코드는 Session 으로 옮긴다(단계 ③).

## 4. 계층 방향 결함

- (① 에서 고침) 실행기가 `Terrain.WrittenEditCount` 와 캐릭터 탐침을 대신 정의했다. 탐침 레지스트리를 최상위 폴더 `Engine/Probe`(티어 10, Object 만 읽는다)로 옮기고, 기능이 자기 폴더에 등록한다(`Environment/Terrain/TerrainProbes.cpp`, `Character/PairedAnimation/PairedAnimationProbes.cpp`). 티어는 정수라 "9.5" 대신 Probe 를 10 에 두고 그 위 기능(Character · Environment 11, Vfx · Sequencer · Destruction 12)을 계산값대로 한 칸 올렸다.
- `ModuleHost` 가 `AutomationSession::kSubsystemName` 을 안다(역방향 지식). → Session 이 이름을 등록하는 쪽으로 뒤집는다.
- `Profiler.TraceRecording` 은 같은 구조가 **아니다** — Profiling(4)이 Probe(10) 아래라 기능 쪽에 둘 수 없고, 실행기가 읽는 방향이 맞다. `Input.*` 도 Input(10)이 Probe 와 같은 티어라 실행기에 남는다. `Scene.*`(Scene 11)은 Scene 폴더로 옮길 수 있으나 씬 공통 값이라 ① 에서 옮기지 않았다.

## 5. 단계

티어 테이블 수정이 든 ① 은 [LayerTierGate](LayerTierGate.md) 와 규칙 파일(`CheckEngineLayers.toml`)을 같이 만지므로 순서를 정한다(Roadmap).

| 단계 | 내용 | 읽을 문서 | 고칠 문서 | 검증 | 상태 |
|---|---|---|---|---|---|
| ① 탐침 | 탐침 레지스트리를 티어 9.5 로 내리고, Terrain · Character 탐침을 기능 폴더로 옮긴다(기능이 자기 탐침을 등록) | Automation README, `CheckEngineLayers.toml` | 이 문서, Automation README, 티어 테이블 | 층 게이트 4종, 시나리오 네 백엔드 회귀 | 완료 |
| ② 단계 | 단계 함수 형식이 `AutomationRunner&` 대신 좁은 문맥 인터페이스를 받게 한다 | 이 문서 3절 | 이 문서, Automation README | 등록자(UI · GameFramework · Editor · App)가 로그 게이트 · 보고 · 종료 코드 헤더를 더는 include 하지 않는다 | 남음 |
| ③ 시나리오 | 형식에서 테스트 전용 필드를 테스트 쪽 구조로 빼고, 실행기를 타임라인과 판정으로 나누고, `AutomationResult` 와 종료 코드를 Session 으로 옮긴다. 폴더가 둘로 갈라진다 | 이 문서 1절 · 3절 | 이 문서, Automation README, 새 `Engine/Scenario` README | 네 백엔드 시나리오 회귀(실행기 분할이 시계 잡기 · 사진 대기 순서를 건드릴 수 있다) | 남음 |
| ④ 구성 분리(조건부) | 실제 배포 때 Shipping 에서 테스트 쪽을 빼는 구성 분리를 다시 본다. 결정 기록 5-3 과 부딪히고, GPU 검증이 Shipping 으로 돌아가는 제약이 있다. 측정: Shipping App 크기 · 기동 시간을 Automation 소스 유무로 비교 | 결정 기록 5-3, [GpuVerification](GpuVerification.md) | 결정 기록 | 크기 · 기동 시간 측정값 | 남음(조건부 — 실제 배포 때) |

## 6. 위험과 확인하지 못한 것

- 실행기 분할(③)이 시계 잡기 · 사진 대기 순서를 건드린다 — 네 백엔드 시나리오 회귀로 확인한다.
- ① 의 시나리오 네 백엔드 회귀는 작업자가 돌리지 않았다(탐침 정의 위치만 옮겨 동작은 같다 — 통합 검증이 본다).
- `-cook-scenes` 의 Shipping 차단 여부, Shipping 가드 기준 통일 여부, `Profiler.TraceRecording` 의 같은 구조 여부.
