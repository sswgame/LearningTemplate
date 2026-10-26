# Automation — 자동화 시나리오

## 이것은 무엇이고 왜 있나

자동화 시나리오는 실제 `App.exe` 를 띄운 채 프레임마다 입력을 넣고 게임 상태를 단언하는 XML 파일입니다(`Resource/<영역>/automation/*.scenario.xml`). 사람이 화면을 보고 확인하던 일을 파일로 남겨 네 백엔드에서 반복합니다.
시나리오는 코드가 아니라 데이터이고 단계와 탐침은 레지스트리에 한 줄로 등록합니다. 게임은 기반(판 상태, 일정 시스템, AI 조종자, 폰)을 쓰기만 하면 시나리오에서 그 상태를 물을 수 있습니다.

종료 코드가 결과입니다: 0 통과, 10 실패, 11 읽기 오류, 12 시간 초과, 13 건너뜀, 14 미확정(단언은 맞았지만 어림값 문턱이 있다), 15 처리하지 않은 로그(단언은 맞았지만 처리하지 않은 `[Warning]` · `[Error]` 가 남았다. 15 는 14 보다 앞선다).
0 은 로그까지 깨끗하다는 뜻입니다("로그 게이트"). `AppScenarioTest` 가 시나리오 파일을 찾아 백엔드마다 돌립니다.

## 머릿속 그림

```
scenario.xml ──► AutomationScenario(<At> 시점 → 프레임) ──► AutomationRunner ──► 보고 JSON(failures · metrics · records · 실행 환경 · reproduce)
                                                             │
                     시작 프레임: 단계마다 검사 · 준비 ───────┤  입력 단계 → VirtualInputScript → InputManager(장치 층)
                     프레임마다: 씬 틱 뒤 단계 실행 ─────────┤  단언 단계 → AutomationProbes(씬 탐침 · 질의 탐침)
                                                             └  ExpectVisited 이력(탐침 값이 바뀔 때만 적는다)
```

**단계.** `<At>` 아래의 요소 하나가 단계이고 `AutomationStepRegistry` 에 등록됩니다(엔진 단계도 같다). 실행기는 단계 이름으로 분기하지 않습니다. 단계 함수는 실행기가 아니라 좁은 문맥 `AutomationStepContext`(`AutomationStepContext.h`)를 받고,
시나리오를 끝낼 때도 종료 코드가 아니라 `AutomationStepEnd`(Pass · Fail · Skip)로 말합니다. 실행기는 끝난 까닭을 `AutomationOutcome` 으로 돌려주고 종료 코드 표는 `AutomationExitCode.h` 의 `getAutomationExitCode` 한 곳에만 있습니다(정수로 캐스팅하지 않는다).
Scratch · Launch · Load · `isolated` · `editor` · `requireForeground` 선언은 `AutomationScenario::getTestOptions()` 한 묶음으로만 테스트 쪽에 나갑니다.

**탐침.** 시나리오가 읽는 이름 붙은 값입니다. 씬 탐침은 인자 없이 수를 내고(`Farm.FarmerX`), 질의 탐침은 `target` 과 `key` 를 받아 수나 글을 냅니다(`AI.State target="FarmAutoFarmer"` → `Till`). 글 값은 `is`, 수 값은 `equals`, `near`, `atLeast`, `atMost` 로 비교합니다.

**시나리오 프레임.** 프레임 0 은 시작 조건이 처음 참인 프레임입니다(`startAfter="ScenePlaying"` 이면 씬이 플레이를 시작하고 로딩 화면이 사라진 첫 프레임, `startAfter="Immediately"` 이면 씬이 플레이 중일 필요는 없지만 시작 차단 조건이 풀린 첫 프레임). `WaitUntil` 이 기다리는 동안은 시나리오 프레임을 세지 않고 `timeoutFrames` 에도 세지 않으므로
붙잡힌 채 끝나지 않는 시나리오는 **벽시계 상한**(루트 `wallTimeoutSeconds`, 기본 `AutomationScenario::kDefaultWallTimeoutSeconds` 150 초)이 12 로 끝내고 보고를 씁니다(부하 시나리오는 자기 그물이 있어 보지 않는다).
시간은 늘 고정 프레임 시간(`fixedDelta`)으로 흐릅니다. 이것은 엔진 프레임만 묶으므로 자식 프로세스의 네트워크 재전송 같은 벽시계 일은 시한을 받는 대기 명령(`netplay.waitResent`)으로 기다리고 프레임 비용(`GT.Frame`)에 기대지 않습니다(스파이크는 `profile.stall <ms>`).
에디터 코드의 경과는 모두 엔진 경과(`GameTimeScale::getUnscaledDeltaTime`)입니다(`ImGuiEditor::beginFrame` 이 `io.DeltaTime` 을 덮어쓰므로 미리보기 패널과 시나리오 녹화도 같은 시간을 읽는다).

**에디터 실행 · 기동 값.** 루트에 `editor="true"` 를 적은 시나리오는 `AppScenarioTest` 가 `-EnableEditor` 와 빈 에디터 상태 폴더로 돌립니다(`engine/automation/editor/` 는 폴더가 이미 에디터 실행). 시작한 뒤에는 바꿀 수 없는 값(첫 씬, 렌더 파이프라인)은 루트 아래 `<Launch variable="gv_firstScene" value="game/empty/maps/rendertest3d.scene.xml"/>` 로 선언하면
`AppScenarioTest` 가 `-gv_firstScene=…` 를 명령줄에 붙이고 실행기는 시작할 때 값을 견주어 다르면 읽기 오류로 멈춥니다(손으로 돌릴 때도 같은 인자를 준다).

**사진과 시계.** `<Screenshot>` 은 화면이 자리 잡을 때까지 기다렸다 찍고 기다리는 동안은 `WaitUntil` 처럼 시나리오 시계를 붙잡습니다. 사진은 다음 렌더 패킷이 찍으므로(프레임 N 의 요청은 N+1 의 그림) 요청한 프레임은 거기서 닫히고 같은 프레임의 뒤 단계(순간이동 · 변수 바꾸기)는 다음 프레임에 돌아 사진에 들어가지 않습니다.
찍는 프레임이 정해져야 하면 `waitForSettle="false"` 로 바로 찍고 자리 잡기는 앞의 `<Screenshot>` 한 장으로 기다려 둡니다.

**전경.** 실제 커서 클립(`ExpectCursorClip`)이나 화면 복사(`CaptureWindow`)를 보는 시나리오는 루트에 `requireForeground="true"` 를 적습니다. 실행기는 첫 프레임부터 창을 전경으로 가져오고 얻을 때까지 시작하지 않으며 시작 시한(`startTimeoutFrames`)까지 못 얻으면 13 입니다(전경이 아닐 때 켠 1인칭 커서 잠금은 클라이언트를 눌러야 잡히기 때문).

**기록.** `<Record>` 가 남긴 값은 보고의 `records` 에 적힙니다. 게임 상태는 CPU 에서만 정해지므로 같은 시나리오는 백엔드마다 같은 기록을 내야 하고 `AppScenarioTest` 가 비교합니다.

## 따라 해 보기

HarvestValley 의 상점 주인이 10 시에 가게에 있는지 확인하는 시나리오입니다(전체는 `game/harvestvalley/automation/schedule.scenario.xml`).

```xml
<Scenario name="harvestvalley.schedule" fixedDelta="0.0166667" timeoutFrames="900" startAfter="ScenePlaying" input="exclusive">
	<At frame="0"><Variable name="gv_farmAutoPlay" value="0"/></At>
	<At frame="10"><Expect probe="Schedule.Activity" target="pierre" is="Sleep"/></At>
	<At frame="11"><AdvanceGameTime toHour="10"/></At>
	<At after="1">
		<Expect probe="Schedule.Activity" target="pierre" is="WorkAt"/>
		<Expect probe="Schedule.DistanceToPlace" target="pierre" key="general_store" atMost="1"/>
	</At>
	<At after="1"><Expect probe="Object.X" target="Villager.pierre" near="24" tolerance="1"/><Pass/></At>
</Scenario>
```

1. 프레임 10 에서 일정 탐침이 `pierre` 가 자고 있음을 확인합니다.
2. 프레임 11 에서 판 상태의 시계를 다음 10 시로 감습니다(지나간 시와 날의 경계는 알림으로 남아 다음 틱의 디렉터와 일정이 받는다).
3. 한 프레임 뒤 일정상 활동과 위치를 확인하고 그다음 프레임에 몸(`Villager.pierre`)이 가게 위치에 있는지 봅니다(몸은 다음 틱에 따라오므로 한 프레임을 띄운다).
4. `py -3 -m Scripts scenario schedule --backend dx12` 의 종료 코드 0 이 통과입니다(`App.exe -scenario=…` 를 손으로 조립하지 않는다 — 이 명령이 `<Launch>` 의 기동 변수 · 게임 · 보고 인자를 파일에서 그대로 붙이고 GPU 칸을 잡아 백엔드마다 한 판씩 돌린다. [검증과 측정](../../../docs/08_Verification.md) 1절)(15 면 처리하지 않은 로그가 남은 것이라 "로그 게이트" 를 따라 고칩니다).

단언이 실패하면 로그와 보고에 어느 단계에서 무엇이 달랐는지 한 줄이 남습니다.

```
frame 12 <Expect> #9: Expect Schedule.Activity(pierre) is 'WorkAt', got StayHome
frame 900 <ExpectVisited> #3: AI.State(FarmAutoFarmer) never went through 'Till > Sow > Water' since frame 0 (missing 'Water'), saw: Till@12 > Sow@40
```

## 작동 원리

**시점.** `<At>` 은 `frame`(절대 프레임), `time`(시작부터 초), `after`(앞 `<At>` 에서 프레임), `afterSeconds` 중 정확히 하나를 받습니다. 초는 `fixedDelta` 로 반올림해 프레임으로 바꿉니다.

**입력 단계.** 키 단계(`Press`, `Release`, `Tap`, `MouseDelta`, `MousePosition`, `GamepadAxis`, `Text`, `Wheel`)와 액션 단계(`Action`, `ActionPress`, `ActionRelease`, `ActionAxis`)는 시작 프레임에 모두 가상 입력 스크립트로 옮겨집니다.
액션 단계는 통합 입력 맵(`InputManager::getInputMap`)에서 바인딩을 찾아 그 키, 단추, 스틱을 내므로 트리거(Hold, DoubleTap)와 레이어가 사람의 입력과 같은 경로를 지나고 키 배치를 바꿔도 시나리오는 그대로입니다. `device="gamepad"` 처럼 장치를 고를 수 있습니다.

**대상 고르기.** `target`(그리고 `Object.DistanceTo` 의 `key`, `Mount` 의 `rider` · `vehicle`)은 오브젝트 이름 글이고 규칙은 `AutomationTarget` 하나가 정합니다. 같은 이름으로 또 만들면 저장소가 `_2` · `_3` 을 붙이므로 스폰하는 적 · 손님 · 총알은 밑 이름이 같은 오브젝트가 여럿입니다(끝의 `_` + 숫자로 나눈다: `Tree_3` 은 밑 `Tree`, `Tree_03` 은 번호가 아니다).

| 글 | 고르는 것 |
|----|-----------|
| `ArenaGruntAI[0]` | 밑이 `ArenaGruntAI` 인 살아 있는 오브젝트를 만든 순서(오브젝트 id 오름차순)로 늘어놓은 0 번째. 없으면 그 오브젝트는 없다(`Object.Exists` 0) |
| `ArenaGruntAI_2` | 그 이름 그대로 |
| `ArenaGruntAI` | 그 이름 그대로. 단 밑이 같은 다른 오브젝트가 살아 있으면 **고를 수 없다는 실패**다(저장소가 번호를 재사용하므로 조용히 첫째를 고르지 않는다) |

만든 순서는 같은 시나리오 · 같은 고정 틱이면 실행과 백엔드에 상관없이 같습니다. "그 이름의 아무나 · 모두" 질의는 없습니다(무리 전체는 게임이 수를 세는 씬 탐침 `Arena.KillCount` 으로 봅니다).

**단언과 이력.** 단언 단계는 그 프레임의 씬 틱 뒤에 돕니다. `ExpectVisited` 는 시작할 때 그 탐침마다 이력을 만들고 값이 바뀔 때만 프레임 번호와 함께 적습니다. `sequence="A, B, C"` 는 그 값들을 이 순서로 거쳤는지(사이에 다른 값이 끼어도 된다), `since` 는 그 프레임에 이미 들어 있던 값부터 셀지를 정합니다.
`<ExpectVariable name="gv_cameraFieldOfView" value="80" tolerance="0.001"/>` 는 전역 변수의 지금 값을 타입대로 견주고(`tolerance` 는 실수 변수에만, 없으면 정확히 같아야 한다) 없는 변수 · 모르는 속성 · 그 타입으로 읽히지 않는 `value` 는 읽기 오류입니다.

**경계 전후 견주기.** `<Remember probe="Editor.StateHash" as="edited"/>` 가 그 프레임의 탐침 값을 기억하고 뒤의 `Expect` · `WaitUntil` 이 `sameAs="edited"` · `differsFrom="edited"` 로 견줍니다(글 그대로 견주므로 64 비트 해시도 뭉개지지 않고 플레이 인/아웃 · 모듈 리로드 · 백엔드 교체를 넘어 기억한다).
`SwapBackend backend="other"` 는 지금과 다른 백엔드(DirectX 12 가 아니면 DirectX 12, 12 면 DirectX 11)로, `backend="start"` 는 시작한 백엔드로 건너므로 "갔다 돌아오는" 시나리오를 한 파일로 씁니다.
에디터 편집 상태는 `Editor.SceneHash` · `Editor.SelectedNames` · `Editor.UndoState` · `Editor.StateHash`(넷을 접은 것)로, 오브젝트 하나의 컴포넌트 값은 `Object.Property target="이름" key="<컴포넌트>.<프로퍼티>"` 로 읽습니다. 에디터 흐름 시나리오 `engine/automation/editor/flow*.scenario.xml` 의 기능별 표는 [에디터 시나리오 표](../../../docs/plans/EditorScenarios.md) 입니다.

**결정적 재생과 녹화.** `-scenario-record=<Saved/Automation/...scenario.xml>` 는 씬 플레이 첫 프레임부터 입력(가상 입력 포함)을 적고 끝날 때 `-scenario-record-probes` 의 탐침 값을 끝 프레임의 `<Expect>` 로 씁니다. 같은 입력과 고정 틱이므로 다시 돌리면 같은 값이 나와야 하고 `AppScenarioTest.RecordedPlayReplaysOnEveryBackend` 가 네 백엔드에서 확인합니다.
다시 돌릴 때는 디스크 경로가 아니라 id 를 줍니다(`Saved/Automation/Recorded/a.scenario.xml` → `-scenario=saved/Automation/Recorded/a.scenario.xml`, `ScenarioRecorder::makeReplayID`).

**로그 게이트.** `<Pass/>` 에 닿은 시나리오도 처리하지 않은 `[Warning]` · `[Error]` 를 남겼으면 종료 코드 15(`UnhandledLog`)입니다(`AutomationLogPolicy` — 어림값 문턱 14 보다 앞선다). 실행기는 시나리오 구간의 줄과 프로세스 전체의 빠진 현지화 키(`LocalizationManager::getMissingKeys`)를 보고, `AppScenarioTest` 는 같은 판정을 프로세스 출력 전체(기동 · 종료 포함)에 다시 겁니다. 줄 하나가 처리됐다는 것은 둘 중 하나입니다.

- 시나리오가 일부러 내는 줄은 그 시나리오에 `<ExpectLog id="…">`(로그 id 가 있는 줄 — 문구가 바뀌어도 따라온다)나 `<ExpectLog contains="…">`(id 없는 줄만, 본문 전체와 같을 때만 가린다)로 선언합니다(줄 수 단언과 짝, `id` 와 `contains` 는 정확히 하나, `count="0"` 은 선언이 아니다). 린트 `CheckExpectLogPhrases` 가 id · 문구가 소스에 있는지 봅니다. 이미 진 실행(10 · 12 …)도 같은 게이트를 지나 처리하지 않은 줄이 `unhandledLogs[]` 에 남습니다.
- 고칠 수 없는 줄은 `Resource/engine/automation/allowedlogs.json` 의 항목으로 허용합니다. 항목은 **로그 id**(`SW_LOG_WARNING_ID` · `SW_LOG_ERROR_ID`), **범위**(`scenario` 나 `suite` 중 정확히 하나 — 전역 허용은 없다), **사유**(`reason`), **처리 계획 문서 이름**(`plan`)이 모두 있어야 읽히고 하나라도 비었거나 `TODO` 이면, id 에 와일드카드가 있거나 같은 항목이 두 번이면 읽기 오류(11)입니다.

로그 id 가 없는 줄은 허용 목록으로 가릴 수 없으니 소스에 id 를 붙입니다(텍스트 로그에는 `[id:영역.사건]` 꼬리표로 찍힌다). 실행기 줄(`[Scenario]`)과 테스트의 방어 구간 줄(`[Expected Defensive Test]`)은 보지 않고, 호스트 테스트 스위트(`HostLogJudge`)도 이 목록의 `suite` 항목을 씁니다.
보고 JSON 의 `unhandledLogs[]` · `allowedLogs[]` 를 `py -3 -m Scripts scenario-logs` 가 읽습니다(`--check` 는 더 안 나오는 항목과 15 로 끝난 보고를 실패로 세고 `--regen` 은 처리하지 않은 줄을 사유 · 계획이 `TODO` 인 초안으로 더한다). 처음 채우는 절차는 [GPU 검증](../../../docs/plans/GpuVerification.md) 4절입니다.

**시작을 미루는 조건.** `startAfter` 가 무엇이든 아래 조건이 풀려야 시작합니다(`AutomationStepRegistry::isStartBlocked`): `UILoadingScreen`, `ContentWatchFence`(사본 폴더를 새로 만든 뒤 파일 감시 표지가 오거나 시한이 지날 때), 시계를 붙잡는 `QuickLauncherIndexing` · `EditorAssetValidation` · `ContentBrowserReferenceIndex`(끝나지 않으면 시계 보류 시한 170 초와 벽시계 상한이 12 로 끝낸다), 화면 캡처만 미루는 `ScreenFade`.

**보고의 실행 환경과 행 감시.** 보고 JSON 은 결과 칸 외에 `backend` · `game` · `startedAt` · `wallMilliseconds` · `wallLimitMilliseconds` · `lastStep` · `reproduce` 를 담습니다(읽는 쪽이 있어 필드는 더하기만 한다). `reproduce` 는 App 원본 인자를 옮긴 명령줄이고 보고 · 녹화 경로는 빼며 이름에 token · secret · password · apikey · credential · auth · license 가 든 인자의 값은 `REDACTED` 입니다(`AutomationReproduceRules` 한 곳).
벽시계 상한은 프레임이 도는 동안만 보므로 프레임 자체가 멈춘 행은 감시 스레드(`AutomationHangWatchdog`)가 잡습니다. 세션이 켠 실행(`armHangWatchdog`, 부하 시나리오 제외)만 띄우고 `endRun` 이 내리며, 세션 없이 실행기를 손으로 돌리는 테스트(`startFromText`)는 켜지 않습니다(감시는 프로세스를 `_Exit(12)` 로 끝내므로 테스트 프로세스가 죽는다).
상한 + 유예(`AutomationTimeLimits::kHangGraceSeconds` 5 초)가 지나도 `endRun` 이 안 불리면 스냅샷(`AutomationHangPayload`)으로 보고(`hang: true`, 종료 코드 12)를 할당 · 잠금 없이 쓰고 크래시 컨텍스트와 stderr 한 줄을 남긴 뒤 끝냅니다. 체인의 프로세스 시한은 `AutomationTimeLimits::getChainKillSeconds` 이고 시나리오의 `wallTimeoutSeconds` 는 시계 보류 시한(170 초) 미만이어야 읽힙니다(시한들의 대소 관계는 `AutomationTimeLimits.h` 의 `static_assert`).
행 감시의 통합 시험은 개발 명령 `automation.hang <초>`(호출한 게임 스레드를 붙잡는다. `profile.stall` 은 5 초 상한이라 상한 3 + 유예 5 초를 못 넘긴다)로 게임 스레드를 멈추는 시나리오 `engine/automationwatch/hang.scenario.xml` 이고, 일부러 12 로 끝나므로 `AppScenarioTest` 가 훑는 `engine/automation` 밖에 두고 `AppHangWatchTest` 가 종료 코드 12 와 `"hang": true` 를 본다.

**시간이 어디에 쓰였나.** 보고의 `timeline` 이 프레임마다 걸린 벽시계를 종류별로 더해 둡니다(`AutomationTimeline`): `start`(시작 전) · `backgroundHold`(앞 실행의 백그라운드 작업이 시계를 붙잡음) · `held`(`WaitUntil` · `Screenshot` 같은 조건 대기) · `step`(단계가 돈 프레임) · `scheduledWait`(돌 단계 없이 `<At>` 시점만 기다린 프레임 = 고정 프레임 대기).
종류마다 `<키>Frames` · `<키>Milliseconds` 이고 `slowestWaits[]` 는 단계 앞 대기(고정 + 조건) 가운데 가장 긴 다섯이다(`beforeStep` · `frame` · `frames` · `milliseconds`). 시나리오 파일의 대기 합계는 `py -3 -m Scripts scenario-waits`(정적 어림, `--measured-dir` 로 보고의 잰 값과 견준다)가 센다.
조건 대기로 바꾸는 기준은 "프레임 수가 아니라 상태에 닿는 순간이 이어질 일"이다 — 입력 길이(키를 4 초 누름) · 사진 시점 · 카운트다운처럼 프레임 수 자체가 단언 대상이거나 `<Record>` 처럼 백엔드끼리 같은 프레임에 견주는 것은 고정으로 둔다.


## 확장하는 법

새 탐침을 더하는 절차입니다.

1. 게임이나 키트의 .cpp 에 읽기 함수를 둡니다. 씬 탐침은 `bool( const GameObjectManager*, float64& )`, 질의 탐침은 `bool( const AutomationProbeQuery&, AutomationProbeValue& )` 입니다.
2. 같은 .cpp 의 `namespace sw` 안에 `SW_AUTOMATION_PROBE( id, "<게임>.<값>", "설명", &함수 )` 또는 `SW_AUTOMATION_QUERY_PROBE( id, 이름, 설명, &함수, target 필요, key 필요 )` 한 줄을 둡니다.
3. 값을 낼 수 없으면 false 를 돌려주고 질의 탐침은 `outValue.setText( 이유 )` 로 이유를 적습니다(실패 줄에 실립니다).

탐침 레지스트리(`Engine/Probe/AutomationProbe.h`)는 티어가 컴포넌트 모델(Object) 바로 위라 그 위의 Engine 기능(Scene · Character · Environment · Vfx …)도 자기 폴더의 .cpp 에 탐침을 둡니다(`Scene/SceneProbes.cpp`, `Environment/Terrain/TerrainProbes.cpp`). 실행기에 남은 탐침은 실행기 자신의 상태, 오브젝트 질의(`Object.*`), Probe 와 티어가 같거나 낮은 것(`Input.*` · `Profiler.TraceRecording`)입니다.

새 단계는 `SW_AUTOMATION_STEP( id, "이름", 실행, 검사, 입력 전 여부 )` 로 등록합니다. 입력으로 옮겨지는 단계는 `SW_AUTOMATION_STEP_WITH_HOOKS` 의 준비 함수에서 `runner.getInputScript()` 에 이벤트를 넣고, 속성은 검사 함수에서 모두 확인합니다(모르는 속성은 읽기 오류). 검사 함수는 순수 함수라 실행기 없이 부를 수 있고 `AutomationScenarioDataTest`(nogpu)가 모든 시나리오 파일의 단계를 이 함수로 훑습니다. 등록표에 이름을 묻는 검사(탐침 · 전역 변수 · 개발 명령)는 `AutomationStepValidation::findUnresolvedDependency` 로 "틀렸다" 와 "이 프로세스가 모른다" 를 가리고,
단계를 등록하는 모듈(에디터 · App)이 안 올라온 프로세스도 검사하게 하려면 검사 함수를 ImGui · 렌더를 모르는 파일에 두고 `SW_AUTOMATION_STEP_VALIDATOR( id, 이름, 검사 )` 로 따로 올립니다(`Editor/SelfTest/EditorScenarioStepValidation.cpp`, `Engine/Automation/AutomationBackendSteps.cpp`). 입력 맵 · 창 · ImGui 가 있어야 답이 나오는 검사는 준비 함수(`_pPrepare`)에 둡니다.
AI 의 상태를 보려면 `AIControllerComponent` 파생의 `think` 에서 상태가 바뀔 때 `setStateName( "Chase" )` 를 부르고 판단 값은 `getBlackboard()` 에 둡니다(탐침 `AI.State` · `AI.Blackboard`). 게임 로직 단계와 탐침(시계, 판 상태, 일정, AI, 빙의, 탑승)은 `GameFramework/Base/Gameplay/Automation/GameplayAutomationSteps.cpp` 에 있습니다.
등록자는 단계 · 탐침을 정의한 .cpp 의 정적 객체입니다. Shipping 은 통째 링크(`sw_linkWholeArchive`)하므로 그 .cpp 를 붙드는 빈 함수를 두지 않습니다.

## 함정과 주의

- **같은 프레임에 넣은 입력의 결과를 그 프레임에 단언하지 않습니다.** 입력은 그 프레임의 틱 전에 들어가 같은 프레임의 다른 입력이 단언보다 먼저 상태를 바꿉니다. 단언은 다음 `<At>` 에 둡니다. 시계를 감은 결과도 탐침은 바로 보지만 그것을 따르는 오브젝트(`Schedule.*` 대 `Object.*`)는 다음 틱에 움직입니다.
- **`WaitUntil` 이 붙잡은 프레임은 시나리오 프레임에 들어가지 않습니다.** 실제 경과는 `held the clock for N frame(s)` 줄의 값을 더해 추정합니다.
- **녹화로 정확히 재생하려면 녹화도 고정 프레임 시간으로 돌립니다.** 시나리오 없이 사람이 녹화할 때는 `-gv_fixedFrameDelta=0.0166667` 를 함께 줍니다(녹화는 소수 일곱 자리로 쓰며 넷째 자리에서 자르면 위치 검사점이 어긋난다).
- **배타 시나리오는 시작 조건을 기다리는 프레임부터 사람의 장치를 버리고 포인터를 (0, 0) 에 둡니다.** 백엔드끼리 `records` 가 다르면 `-gv_automationHashDump=1` 로 두 번 돌려 `[Scenario] hash item` 줄을 비교하면 어긋난 오브젝트가 나옵니다.
- **경계를 넘는 단언은 탐침 셋으로 나눠 둡니다.** `Editor.StateHash` 하나만 견주면 실패 줄이 해시 둘만 보여 주므로 같은 프레임에 `Editor.SceneHash` · `Editor.UndoState` · `Editor.SelectedNames` 를 함께 두고 씬 내용이 다르면 `[Scenario] scene hash item` 줄(`-gv_automationHashDump=1`)로 오브젝트를 찾습니다.
- **`backend="other"` 로 건넌 뒤에는 그림을 견주지 않습니다**(`backend="start"` 로 돌아온 뒤에). 그림이 "같다" 는 문턱은 편집 크기보다 훨씬 작게 둡니다(편집이 0.015 면 0.002).
- **`ExpectImage` 의 문턱은 근거 속성이 있어야 읽힙니다**: `measured="2026-10-22 dx12:0.095 vk:0.095"`(날짜와 백엔드별 잰 값, 모두 같으면 `all:0.095`) 또는 `provisional="이유"` 중 정확히 하나. 어림값 문턱이 맞은 시나리오는 14 로 끝나고 보고 JSON 의 `unconfirmed` 에 줄이 남으며(틀리면 실패) `metrics` 의 값을 `measured` 로 옮기면 확정입니다(`py -3 -m Scripts scenario-thresholds --reports <보고 폴더>` 가 백엔드마다 돈 보고에서 제안 표를 만들고 `--apply` 가 쓴다 — 백엔드 사이 값이 벌어진 것은 결함 의심이라 올리지 않는다).
- **글 값 탐침은 `is` 로만 비교합니다**(수 탐침은 `is` 로도 비교할 수 있고 끝의 0 을 정리한 글과 견준다).
- **리소스에 쓰는 시나리오는 원본 폴더에 쓰지 않습니다.** `<Scratch from="engine/automation/editor/assetmanage" as="scratch/assetmanage"/>` 로 선언하면 실행기가 시작할 때 `Resource/scratch/…`(저장소에 넣지 않음)를 지우고 원본을 복사하며(글 파일의 원본 id 는 사본 id 로, `.meta` 의 guid 는 새로) 단계는 사본 id 를 씁니다(`AutomationScratch`).
  사본의 에셋은 Quick Open(원본이 걸릴 수 있다)이 아니라 `asset.openScratch <사본 id>` 로 엽니다.
- **한 틱에 끝나는 작업만 있는 행동 트리는 도는 잎이 없습니다.** `AI.BehaviorTask` · `Skirmish.AITask` 는 그 틱에 마지막으로 성공한 작업을 보이므로 늘 성공하는 작업이 대부분의 프레임을 차지합니다.

## 더 볼 곳

단계 문맥 `AutomationStepContext.h`, 엔진 단계와 실행기 탐침 `AutomationRunner.cpp`, 입력 맵 액션 단계 `AutomationActionSteps.cpp`, 녹화 `ScenarioRecorder.cpp`, 탐침 레지스트리와 대상 고르기 `Engine/Probe/AutomationProbe.cpp`, 시나리오 × 백엔드 실행 `Test/AppTest/TestAppScenario.cpp`, 에디터 UI 캡처 단계 `Editor/SelfTest/EditorCaptureSteps.cpp`.
검증 절차는 [검증과 측정](../../../docs/08_Verification.md) 3절, 결정은 [결정 기록](../../../docs/09_Decisions.md) 에 있습니다.
