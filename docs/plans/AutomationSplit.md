# Automation 분리

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 표

`Engine/Automation` 은 시나리오 형식 · 등록 · 타임라인 · 입력 재생 같은 일반 기능과 테스트 판정 · 로그 게이트 · 보고 같은 테스트 전용 기능을 한 폴더에 가지고 있다. 2026-10-23 사용자와 정한 결론은 **형식 경계를 먼저 고치고 폴더를 둘로 나눈다**이다.

## 1. 결론 (2026-10-23)

- 폴더 둘로 나눈다. 일반 `Engine/Scenario`: 형식 · 레지스트리 · 타임라인 · 입력 단계 · 녹화(탐침 레지스트리는 ① 에서 따로 `Engine/Probe`(티어 10)로 갔다 — 타임라인 · 입력 단계는 Input · Scene 위라 같은 폴더에 두면 기능이 탐침을 등록하지 못한다). 테스트 `Engine/Automation`: 판정 단계 · 로그 게이트 · 보고 · 이미지 지표 · Scratch · 종료 코드 · Session · Chain.
- **이름만 바꾸기는 먼저 하지 않는다**(호출부가 없는 개명이기 때문). 형식 경계를 고친 결과로 폴더가 갈라진다.

## 2. 사실

- 인게임에서 시나리오 · 탐침 · `VirtualInputScript` 를 읽는 곳은 0 이다(탐침을 읽는 곳은 실행기와 테스트뿐, 자동화가 아닌 사용은 에디터 `InputMapPanel` 의 `InputReplay` 하나).
- Shipping 에 폴더 전체가 들어가고 `-scenario` 가 Shipping App 에서 열려 있다 — 결정 기록 5-3 "자동화는 Shipping 에서도 쓴다"와 맞는 의도된 상태다. 가드 기준은 `SW_DEV_COMMANDS_ENABLED` 로 통일했다(`SwapBackend` 의 `SW_SHIPPING` 을 맞춤, 2026-10-25). 나머지 단계는 가드가 없는 것이 의도다.
- `-cook-scenes`(`HeadlessSubsystem.cpp` `runCookTask`)에 `SW_SHIPPING` 가드가 없는 것도 의도다 — 쿠킹은 `-cooked-dir` 로 준 폴더에만 쓰는 오프라인 도구이고 `CookAssets` 빌드 타깃과 배포 패키징이 배포 구성 App 으로 부른다(`-cook-shaders` · `-bake-retarget` · `-make-patch` 도 같다).

## 3. 형식 결함 셋

1. **단계 함수 형식 `AutomationStepFunction` 이 `AutomationRunner&` 를 받는다.** 단계를 등록하는 쪽(UI · GameFramework · Editor · App)이 로그 게이트 · 보고 · 종료 코드 헤더를 끌어온다 → 좁은 문맥 인터페이스(단계 ②).
2. **시나리오 형식에 테스트 전용 필드가 있다**(Scratch · Launch · Load · isolated · editor · requireForeground) → 테스트 쪽 구조로(단계 ③).
3. **실행 결과 열거형이 곧 App 종료 코드다** → (③ 일부에서 고침) `AutomationOutcome` 에서 숫자를 빼고 종료 코드 표는 `AutomationExitCode.h` 한 곳.

## 4. 계층 방향 결함

- (① 에서 고침) 실행기가 `Terrain.WrittenEditCount` 와 캐릭터 탐침을 대신 정의했다. 탐침 레지스트리를 최상위 `Engine/Probe`(티어 10, Object 만 읽는다)로 옮기고 기능이 자기 폴더에 등록한다(`Environment/Terrain/TerrainProbes.cpp`, `Character/PairedAnimation/PairedAnimationProbes.cpp`, `Scene/SceneProbes.cpp`). 티어는 정수라 Probe 를 10 에 두고 그 위 기능(Character · Environment 11, Vfx · Sequencer · Destruction 12)을 한 칸 올렸다. 실행기에 남은 탐침은 `Object.*`(질의) · `Input.*` · `Assets.Pending` · `Profiler.TraceRecording` 이다(Profiling(4)이 Probe(10) 아래이고 Input(10)이 같은 티어라 기능 쪽에 둘 수 없다).
- `ModuleHost` 가 `AutomationSession::kSubsystemName` 을 안다(역방향 지식) → Session 이 이름을 등록하는 쪽으로 뒤집는다.

## 5. 단계

티어 테이블 수정이 든 ① 은 층 게이트([결정 기록](../09_Decisions.md) 5-2)와 규칙 파일(`CheckEngineLayers.toml`)을 같이 만지므로 순서를 정한다(Roadmap).

| 단계 | 내용 | 검증 | 상태 |
|---|---|---|---|
| ① 탐침 | 탐침 레지스트리를 티어 10 으로 내리고 Terrain · Character · Scene 탐침을 기능 폴더로(기능이 자기 탐침을 등록) | 층 게이트 4종, 시나리오 네 백엔드 회귀(작업자가 안 돌림 — 정의 위치만 옮겨 동작은 같다) | 완료 — 2026-10-27 GameFramework 허브(`GameplayAutomationSteps.cpp`)의 탐침 16개도 영역 폴더로 옮겼다(`SessionStateProbes`(퀘스트 포함) · `ScheduleProbes` · `AIControllerProbes` · `PawnProbes` · `VehicleProbes`). 허브에는 단계 셋(`AdvanceGameTime` · `Mount` · `Dismount`)만 남음. 같은 날 에디터 허브 `EditorScenarioSteps.cpp` 의 탐침 91개도 패널 · 기능 폴더의 `*Probes.cpp` 로 옮겼다(허브에는 단계 일곱만, 허브의 패널 헤더 include 40여 개가 사라짐). 시나리오용 전역 변수 `gv_editorProbe*` 는 `SelfTest/EditorProbeSupport.h` 에 선언한다. `AutomationRunner.cpp` 의 탐침 12개도 뺐다: `Object.*` 는 `Scene/ObjectProbes.cpp`, `Assets.Pending` 은 `AutomationCaptureProbes.cpp`, `Input.*` 와 `Profiler.TraceRecording` 은 입력(T10) · 프로파일링(T4)이 레지스트리(T10)를 못 봐서 가장 가까운 층인 `AutomationInputProbes.cpp` · `AutomationProfilerProbes.cpp` 에 둔다. 이 뒤로 탐침을 모아 쥔 허브 파일은 없다(`NetSimAutomation.cpp` · `RHIBackendSwitcherAutomation.cpp` 는 자기 기능의 탐침) |
| ② 단계 | 단계 함수 형식이 `AutomationRunner&` 대신 좁은 문맥 인터페이스(`AutomationStepContext`)를 받게 한다 | 등록자가 로그 게이트 · 보고 · 종료 코드 헤더를 더는 include 하지 않는다 | 완료(코드) — 단계 함수 형식 셋이 문맥을 받고 `AutomationRunner` 가 구현, 단계는 종료 코드 대신 `AutomationStepEnd` 로 끝낸다. 실행기 상태를 만지는 엔진 단계만 문맥을 실행기로 되돌려 쓴다. 확인 대기: 매니페스트 id `automation-step-context-tests` |
| ③ 시나리오 | 형식에서 테스트 전용 필드를 테스트 쪽 구조로 빼고, 실행기를 타임라인과 판정으로 나누고, `AutomationResult` 와 종료 코드를 Session 으로 옮긴다. 폴더가 둘로 갈라진다 | 네 백엔드 시나리오 회귀(실행기 분할이 시계 잡기 · 사진 대기 순서를 건드릴 수 있다) | 부분 완료(코드) — `AutomationOutcome`(숫자 없음) · `AutomationExitCode.h`로 결과 형식과 종료 코드를 갈랐고 테스트 전용 선언을 `AutomationTestOptions`(`getTestOptions()`)로 모았다. 확인 대기: 매니페스트 id `automation-step-context-tests` |
| ④ 구성 분리(조건부) | 실제 배포 때 Shipping 에서 테스트 쪽을 빼는 구성 분리를 다시 본다. 결정 기록 5-3 과 부딪히고 GPU 검증이 Shipping 으로 돌아가는 제약이 있다. 측정: Shipping App 크기 · 기동 시간을 Automation 소스 유무로 비교 | 크기 · 기동 시간 측정값 | 남음(조건부 — 실제 배포 때) |

## 6. 남은 것과 위험

③ 의 남은 것: 실행기 분할(`AutomationRunner.cpp` 2000 줄 — 타임라인 · 입력 단계 쪽과 판정 · 보고 쪽)과 폴더 이동(`Engine/Scenario`, 파일 서른 개 안팎의 include). 둘 다 컴파일 확인이 가능한 단독 작업으로 하고 시계 잡기 · 사진 대기 순서는 네 백엔드 시나리오 회귀로 본다.
종료 코드: 숫자는 `AutomationExitCode.h` 한 곳이며(`kAutomationExitCodeRunning` 은 `IEngineFrameObserver::kRunning` 그 자체) `AppTestUtil` 은 `getAutomationExitCode` 를 부르고 `RunScenarioLogs.py` 는 그 헤더를 읽는다(`TestScenarioLogs.py` 가 지킨다). "이 기계에서 RHI 를 못 씀" 코드(77)도 C++ `RHIInitResult.h` 가 정본이고 `AppRun.py` 와 `Test/PythonTest/CMakeLists.txt`(`SW_RHI_UNUSABLE_EXIT_CODE`, 못 찾으면 `FATAL_ERROR`)가 읽으며 `TestQaRunners` 가 `ExitCode.Skipped` 와 같은지, `Test/` 아래 `SKIP_RETURN_CODE` 에 숫자 리터럴이 없는지 본다.

데스크톱 전역 상태 단계(2026-10-28): 단계 등록에 `_bUsesDesktop` 표시(`SW_AUTOMATION_STEP_DESKTOP`)를 두고 표시된 단계가 `input="mixed"` 아닌 시나리오에 있으면 실행기 준비와 시나리오 데이터 테스트가 거절한다. 지금 표시는 `ExpectCursorClip` 하나다. 남은 것: `CaptureWindow`(화면 복사 — 겹친 창이 그림에 들어간다)와 루트 `requireForeground="true"` 가 `editor/` 시나리오 11 개(assetmanage · contentbrowser · contentthumbnails · dialoguegraph · lightprobebakecancel · mapcheck · packaging · toastreveal · uiscale · componenticons · toolbaricons)에서 `input="exclusive"` 로 호스트 조각 안에서 겹쳐 돈다. 같은 표시를 달면 이 11 개가 `_Desktop` 으로 옮겨 직렬이 되므로(시간이 늘어난다) 에디터 창 복사를 오프스크린 캡처(`Screenshot`)로 바꿀 수 있는지 먼저 본다. 클립보드: UI 클립보드는 `App::installRuntimeUIHooks` 가 OS 훅을 걸 때만 OS 를 쓰고 테스트 · 시나리오는 엔진 안 클립보드를 쓰며(`UITextInputTest`), 에디터 복사 · 붙여넣기(`hierarchyedit` 의 Ctrl+C/V)는 `ObjectCopyBuffer` 라 OS 클립보드를 건드리는 시나리오는 지금 없다. OS 클립보드를 직접 쓰는 `WindowTest` 는 이미 `_Desktop` 이다.

## 7. 실행 시간 줄이기 (2026-10-27, 코드 · 도구만 — 측정은 통합)

시나리오 전체 시간(Release `app-scenarios-release` 1223 s)이 프레임 대기에서 오는지 기동에서 오는지 파일만으로는 알 수 없다. 파일의 마지막 `<At>` 프레임 합계는 187 개 시나리오에 4.6 만 프레임이고(`py -3 -m Scripts scenario-waits`), 백엔드 넷을 곱하면 프레임 비용이 큰 몫일 수 있지만 가장 긴 하나가 2100 프레임(12 ms 어림 25 초)이라 한 판의 기동 비용도 같은 자릿수다. 그래서 먼저 재게 했다.

- **계측:** 보고 JSON 의 `timeline`(`AutomationTimeline`, 실행기가 `onFrameEnd` 마다 한 번 더한다) — 시작 전 · 단계 · 고정 프레임 대기 · 조건 대기 · 백그라운드 시계 잡기별 프레임 수와 밀리초, 단계 앞 가장 긴 대기 다섯. 통합이 Release 한 바퀴를 돌린 뒤 `scenario-waits --measured-dir <보고 폴더>` 로 어림과 잰 값을 견준다.
- **조건 대기로 바꾼 것:** `harvestvalley/control`(1810 → 약 80 프레임 + 작물이 생길 때까지) · `abilityarena/control`(1510 → 약 90 프레임 + 첫 처치와 셋째 발사까지). 고정 1800 · 1500 프레임을 기다려 누적 수(`Farm.CropCount` · `Arena.KillCount` · `Arena.PlayerShotCount`)를 단언하던 것이다. 파일 기준 3150 프레임(전체의 7 %)이 줄고, 실제로 줄어든 시간은 통합이 두 시나리오의 `wallMilliseconds` 전후로 잰다.
  나머지 긴 고정 대기는 프레임 수 자체가 목적이다(`displayrevert` 카운트다운 · `nilecity/determinism` 의 `<Record>` · `shooter3d/enemydeath` 의 사진 시점 · 입력을 누르고 있는 길이).
- **어림값 문턱 승격:** `py -3 -m Scripts scenario-thresholds`(`--list` 가 38 개 목록, `--reports` 가 제안 표, `--apply` 가 쓴다). 숫자는 보고의 `metrics` 에서만 가져온다.
- **행 감시 교착:** 개발 명령 `automation.hang` + 시나리오 `engine/automationwatch/hang.scenario.xml`(`wallTimeoutSeconds="3"`)을 `AppHangWatchTest` 가 12 · `"hang": true` 로 확인한다. 기동이 3 초를 넘기는 구성(Debug)에서는 시작 전에 일반 시간 초과로 끝날 수 있어 한 번 돌려 본다.
- **남은 것:** 샤드 비용표(`Test/TestPolicy.json`)를 정적 어림으로 보정하는 방법(케이스 비용 ≈ 기동 + 마지막 프레임 × 프레임 비용, 에디터 · 게임별 두 상수를 `timeline` 에서 맞춘다)은 실측 뒤에 정한다.

## 8. 시나리오가 없는 기능과 추가한 시나리오 (2026-10-27, 실행 전)

덮임은 세 줄로 대조했다: 에디터 패널 id(`kPanelID` 46 개)를 시나리오의 `panel.focus` · `gv_editorProbePanel` · `mark` 와, 등록된 탐침(299 개)을 시나리오의 `probe=` 와(273 개 사용, 26 개 미사용), 게임마다 자동 플레이 스위치(`gv_*AutoPlay`)를 켠 시나리오가 있는지.
엔진 모듈은 대부분 자기 폴더에 탐침이 없어(탐침은 Scene · Editor · GameFramework 에 있다) 폴더 단위로는 대조할 수 없었다 — 탐침이 없는 기능은 시나리오로 못 덮으므로 탐침을 먼저 더해야 한다.

| 덮이지 않은 것 | 처리 |
|---|---|
| 패널 `audiograph` · `behavior_tree` · `world_cells` | 추가: `editor/audiographpanel` · `behaviortreepanel` · `worldcellspanel` — 열기(`Editor.PanelOpen` 조건 대기) → UI 캡처(`maxMagentaFraction=0`) → 닫기. 패널 안 조작은 표식(mark)이 없거나(world_cells 는 `worldCells.cell.N` 이 월드 파티션 씬을 요구) 파일을 써서 하지 않았다 |
| 반사 프로브 베이크(`Editor.ReflectionProbeBake*` · `BakedReflectionProbeCount`) | 추가: `editor/reflectionprobebake` — 프로브 없는 씬에서 끝난 횟수 1 · 구운 수 0. 프로브가 있는 굽기는 씬 옆에 `.reflprobe` 를 써 하지 않는다(Scratch 가 필요) |
| WildWest 자동 플레이(기수가 가장 가까운 말에 탐) | 추가: `wildwest/autoplaymount` — 탑승 순간까지 조건 대기 |
| FrontierTown 자동 플레이(시계 8 배) | 추가: `frontiertown/autoplayclock` — 이틀째까지(1 배속이면 42 초라 시한 30 초에서 진다), `refused` 줄 0 |
| MeadowVillage 자동 플레이(시계 8 배) | 추가: `meadowvillage/autoplayclock` — 첫 날 넘김까지(1 배속이면 90 초라 시한 40 초에서 진다) |
| `Editor.NavMeshCount` · `NavMeshSegments` | 못 덮음: 내비메시가 서려면 걸을 바닥이 있는 씬 픽스처가 필요하다(`physicsnavview` 는 커맨드가 돈 것만 본다) |
| `Editor.AssetImport*` | 못 덮음: 원본 모델 · 텍스처를 바꿔야 해서 Scratch 의 원본 에셋 복사가 있어야 한다 |
| `Editor.CharacterEditor*` · `UIPreview*` · `NetPlayMeasuredLossPercent` · `GameViewCamera*` · `LoadingScreenShown` | 다음 일: 기존 시나리오(`charactereditor` · `uipreviewdesigner` · `netconditions` · `sceneviewgameview` · `loadingscreen`)에 단언을 더하는 일이라 이번에는 기존 파일을 건드리지 않았다 |
| `Appearance.MergedSections` · `SocketPartPool.*` · `Animation.CrowdVertexAnimationUnits` · `PoseEvaluatedUnits` | 다음 일: 군중 · 외형 합치기 시나리오에 단언 — 기대 값은 한 번 재야 정한다 |
| `AI.BehaviorTask` | 못 덮음: 한 틱에 끝나는 작업만 있는 트리는 도는 잎이 없다(README 함정) |
| 사람 눈 확인(질감 · 그림자 · 색 · 애니메이션 느낌) | 시나리오가 사진을 남기고 판정은 사람이 한다(`눈 확인용` 주석 9 곳 + 이번 패널 셋). 숫자 문턱이 되는 것만 `ExpectImage` 가 맡는다 |

추가한 일곱 시나리오는 고정 프레임 대기가 없다(대기는 `WaitUntil`, 그 사이는 `after="1"` 한 프레임). 이름 줄기가 겹치지 않게 했다(`autoplay` 는 shooter3d 가 쓴다). 실행하지 못했다 — 통합이 `AutomationScenarioDataTest`(단계 속성) 와 네 백엔드 실행으로 처음 본다.
로그 id 가 없는 Warning · Error 가 나는 길은 쓰지 않았다: 패널은 열기만 하고 파일을 읽지 않으며, 자동 플레이 경고(`[WildWest] auto play: …`)는 탈 말이 없을 때만 나고 그때는 일부러 15 로 지게 둔다.

### 통합이 돌릴 확인

- 행 감시 교착 시나리오: 매니페스트 id `app-hang-watch`(`AppHangWatchTest`, Debug). 지면 보고의 reason 을 먼저 본다 — 기동이 `wallTimeoutSeconds`(3 초)를 넘겼으면 `hang.scenario.xml` 의 시한을 올린다.
- 시나리오 단계별 시간: 매니페스트 id `scenario-timeline-measure`(`scenario-waits --measured-dir`, `app-scenarios-release` 뒤).
- 어림값 문턱 승격 제안: 매니페스트 id `scenario-thresholds-promote`(보고만, 승격은 표를 보고 사람이 따로 돌린다). `{bin}/Saved/Automation` 보고는 시나리오 줄기 + 백엔드 이름으로 덮어쓰이므로 `ExpectImage` 가 든 줄기가 유일한 것들(어림값 13 개 시나리오)만 쓴다.
- 처음 도는 시나리오 일곱: 매니페스트 id `scenarios-new-q2`. 지면 보고의 failures · unhandledLogs 를 본다 — 패널은 `panel.focus` 철자, 자동 플레이는 시한(30 · 40 초)이 8 배속 가정과 맞는지부터.
