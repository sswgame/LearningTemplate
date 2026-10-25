# Automation — 자동화 시나리오

## 이것은 무엇이고 왜 있나

자동화 시나리오는 실제 `App.exe` 를 띄운 채 프레임마다 입력을 넣고 게임 상태를 단언하는 XML 파일입니다(`Resource/<영역>/automation/*.scenario.xml`).
사람이 화면을 보고 확인하던 일, 예를 들어 "점프하고 앞으로 달린다", "10 시에 상점 주인이 가게에 있다", "자동 농부가 갈고 심고 물을 준다" 를 파일로 남겨 네 백엔드에서 반복합니다.
종료 코드가 결과입니다(0 통과, 10 실패, 11 읽기 오류, 12 시간 초과, 13 건너뜀, 14 미확정 — 단언은 맞았지만 어림값 문턱이 들어 있다, 15 처리하지 않은 로그 — 단언은 맞았지만 처리하지 않은 `[Warning]` · `[Error]` 줄이 남았다. 15 는 14 보다 앞선다).
0 은 로그까지 깨끗하다는 뜻입니다("로그 게이트"). `AppScenarioTest` 가 시나리오 파일을 찾아 백엔드마다 돌립니다.

언리얼의 Automation Test 와 Functional Test, 프로세스를 띄워 결과를 모으는 Gauntlet 에 해당하고, 유니티의 Input Test Fixture(장치 입력 흉내)와 Test Framework 의 PlayMode 테스트에 해당합니다.
그들과 다른 점은 시나리오가 코드가 아니라 데이터라는 것입니다. 단계와 탐침은 레지스트리에 한 줄로 등록하고, 게임은 기반(판 상태, 일정 시스템, AI 조종자, 폰)을 쓰기만 하면 시나리오에서 그 상태를 물을 수 있습니다.

## 머릿속 그림

```
scenario.xml ──► AutomationScenario(<At> 시점 → 프레임) ──► AutomationRunner ──► 보고 JSON(failures · metrics · records · 실행 환경 · reproduce)
                                                             │
                     시작 프레임: 단계마다 검사 · 준비 ───────┤  입력 단계 → VirtualInputScript → InputManager(장치 층)
                     프레임마다: 씬 틱 뒤 단계 실행 ─────────┤  단언 단계 → AutomationProbes(씬 탐침 · 질의 탐침)
                                                             └  ExpectVisited 이력(탐침 값이 바뀔 때만 적는다)
```

**단계.** `<At>` 아래의 요소 하나가 단계입니다. 단계 종류는 `AutomationStepRegistry` 에 등록되고, 엔진 단계도 같은 레지스트리에 있습니다. 실행기는 단계 이름으로 분기하지 않습니다.
단계 함수는 실행기(`AutomationRunner`)가 아니라 좁은 문맥 인터페이스 `AutomationStepContext`(`AutomationStepContext.h`)를 받습니다 — 실패 적기 · 시계 잡기 · 산출물 경로 · 가상 입력 · 시나리오 끝내기만 담았습니다. 단계를 등록하는 쪽(UI · GameFramework · Editor · App)은 로그 게이트 · 보고 · 종료 코드 헤더(`AutomationRunner.h`)를 include 하지 않고, 시나리오를 끝낼 때도 종료 코드가 아니라 `AutomationStepEnd`(Pass · Fail · Skip)로 말합니다. 실행기 안 상태를 만지는 엔진 단계(`AutomationRunner.cpp`)만 문맥을 실행기로 되돌려 씁니다.

**결과와 종료 코드.** 실행기는 끝난 까닭을 숫자 없는 열거형 `AutomationOutcome`(`AutomationOutcome.h`, 이름은 `getAutomationOutcomeName`)으로 돌려주고, 프로세스 종료 코드 표는 `AutomationExitCode.h` 의 `getAutomationExitCode` 한 곳에만 있습니다. 체인 · 세션 · 테스트는 결과 이름으로 말하고 정수로 캐스팅하지 않습니다. 시나리오의 Scratch · Launch · Load · `isolated` · `editor` · `requireForeground` 선언은 `AutomationScenario::getTestOptions()` 의 `AutomationTestOptions` 한 묶음으로만 나옵니다 — 폴더를 가를 때 이 묶음이 테스트 쪽으로 가는 경계입니다.

**탐침.** 시나리오가 읽는 이름 붙은 값입니다. 씬 탐침은 인자 없이 수를 내고(`Farm.FarmerX`), 질의 탐침은 `target` 과 `key` 를 받아 수나 글을 냅니다(`AI.State target="FarmAutoFarmer"` → `Till`).
글 값은 `is` 로 비교하고, 수 값은 `equals`, `near`, `atLeast`, `atMost` 로 비교합니다.

**시나리오 프레임.** 프레임 0 은 시작 조건이 처음 참인 프레임입니다(`startAfter="ScenePlaying"` 이면 씬이 플레이를 시작하고 로딩 화면이 사라진 첫 프레임, `Immediately` 이면 씬이 플레이 중일 필요는 없지만 시작 차단 조건이 풀린 첫 프레임).
`WaitUntil` 이 기다리는 동안은 시나리오 프레임을 세지 않으므로 뒤 단계가 그만큼 밀립니다. 붙잡힌 프레임은 `timeoutFrames` 에도 세지 않으므로, 붙잡힌 채 끝나지 않는 시나리오는 **벽시계 상한**(루트 `wallTimeoutSeconds`, 기본 `AutomationScenario::kDefaultWallTimeoutSeconds` 150 초)이 12(시간 초과)로 끝내고 보고를 씁니다. 부하 시나리오는 자기 그물이 있어 보지 않습니다. 시간은 늘 고정 프레임 시간(`fixedDelta`)으로 흐릅니다.
고정 프레임 시간은 엔진 프레임만 묶습니다. 자식 프로세스의 네트워크 재전송 같은 벽시계 일은 프레임 수로 기다리지 않고, 시한을 받는 대기 명령(`netplay.waitResent`)으로 기다립니다.
프레임 비용(`GT.Frame` 몇 ms)에도 기대지 않습니다. 구성마다 다르므로 스파이크가 필요하면 `profile.stall <ms>` 로 만듭니다. 에디터 코드의 경과는 모두 엔진 경과(`GameTimeScale::getUnscaledDeltaTime`)입니다. `ImGuiEditor::beginFrame` 이 `ImGui::NewFrame` 앞에서 `io.DeltaTime` 을 그 값으로 덮어쓰므로 미리보기 패널(`SequencerPanel` · `AnimationGraphPanel` · `DialogueGraphPanel` · `InputMapPanel`)과 시나리오 녹화도 같은 시간을 읽습니다. 벽시계는 프로파일러 성능 탭만 자기 시계로 읽습니다.

**에디터 실행.** 루트에 `editor="true"` 를 적은 시나리오는 `AppScenarioTest` 가 `-EnableEditor` 와 빈 에디터 상태 폴더로 돌립니다. 게임 팩 시나리오가 그 게임의 에디터 창(HarvestValley 의 NPC 일정 창)을 볼 때 씁니다.
`engine/automation/editor/` 의 시나리오는 폴더가 이미 에디터 실행이라 적지 않습니다. 실행기는 이 값을 보지 않습니다(`isolated` 와 같다).

**기동 값.** 첫 씬과 렌더 파이프라인처럼 시작한 뒤에는 바꿀 수 없는 값은 루트 아래 `<Launch variable="gv_firstScene" value="game/empty/maps/rendertest3d.scene.xml"/>` 로 선언합니다.
`AppScenarioTest` 가 선언을 읽어 `-gv_firstScene=…` 를 명령줄에 붙이고(언리얼 Gauntlet 이 테스트마다 명령줄을 정하는 자리), 손으로 돌릴 때도 같은 인자를 줍니다.
실행기는 시작할 때 값을 견주어 다르면 읽기 오류로 멈춥니다 — 인자를 빠뜨린 실행이 다른 씬 · 다른 파이프라인의 그림을 단언하지 않게 합니다. 렌더 기능 시나리오(`game/empty/automation/motionblur` · `ditherappear` · `light2dcookie`)가 씁니다.

**사진과 시계.** `<Screenshot>` 은 화면이 자리 잡을 때까지(에셋 일이 끝나고 몇 프레임 더) 기다렸다 찍고, 기다리는 동안은 `WaitUntil` 처럼 시나리오 시계를 붙잡습니다.
사진은 다음 렌더 패킷이 찍으므로(프레임 N 에 요청한 사진은 프레임 N+1 의 그림이다 — 패킷은 단계보다 먼저 지어진다) 사진을 요청한 프레임은 거기서 닫히고, 같은 프레임의 뒤 단계는 다음 프레임에 돕니다.
그래서 사진 뒤에 적은 순간이동 · 변수 바꾸기는 사진에 들어가지 않습니다. 찍는 프레임이 정해져야 하는 시나리오(디더 등장의 중간 단계, 프레임마다 옮기는 카메라)는
`waitForSettle="false"` 로 그 프레임을 바로 찍고, 자리 잡기는 앞에서 보통 `<Screenshot>` 한 장으로 기다려 둡니다.

**전경.** 실제 커서 클립(`ExpectCursorClip`)이나 화면 복사(`CaptureWindow`)를 보는 시나리오는 루트에 `requireForeground="true"` 를 적습니다.
실행기는 첫 프레임부터 창을 전경으로 가져오고 얻을 때까지 시작하지 않으며, 시작 시한(`startTimeoutFrames`)까지 못 얻으면 건너뜀(13)입니다.
단계가 아닌 이유: 시작 뒤의 단계는 게임의 첫 틱보다 늦습니다. ctest 처럼 뒤에서 띄운 App 은 전경 없이 시작하고, 전경이 아닐 때 켠 1인칭 커서 잠금은
클라이언트를 눌러야 잡으므로(`InputManager::setMouseLockMode`) 시작 뒤에 전경을 가져오면 잠금이 잡히지 않습니다.

**기록.** `<Record>` 가 남긴 값은 보고의 `records` 에 적힙니다. 게임 상태는 CPU 에서만 정해지므로 같은 시나리오는 백엔드마다 같은 기록을 내야 하고, `AppScenarioTest` 가 이를 비교합니다.

## 따라 해 보기

HarvestValley 의 상점 주인이 10 시에 가게에 있는지 확인하는 시나리오입니다. 전체는 `game/harvestvalley/automation/schedule.scenario.xml` 에 있습니다.

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

1. 프레임 10 에서 일정 탐침이 `pierre` 가 자고 있다는 것을 확인합니다. NPC id 는 일정 데이터(`villagers.schedules.xml`)의 것입니다.
2. 프레임 11 에서 판 상태의 시계를 다음 10 시로 감습니다. 지나간 시와 날의 경계는 알림으로 남아 다음 틱의 디렉터와 일정이 받습니다.
3. 한 프레임 뒤 일정상 활동과 위치를 확인하고, 그다음 프레임에 몸(`Villager.pierre` 오브젝트)이 가게 위치에 있는지 봅니다. 몸은 다음 틱에 따라오므로 한 프레임을 띄웁니다.
   몸은 평소 내비 격자 위를 걸어 일정상 위치를 쫓고, 시계를 감아 일정상 위치가 멀리 뛰면 그 자리에 놓입니다(`FarmVillageDirectorComponent`).
4. 실행합니다. `App -game=HarvestValley -scenario=game/harvestvalley/automation/schedule.scenario.xml` 의 종료 코드가 0 이면 통과입니다(15 면 처리하지 않은 로그가 남은 것이라 "로그 게이트" 를 따라 고칩니다).

단언이 실패하면 로그와 보고에 어느 단계에서 무엇이 달랐는지 한 줄이 남습니다.

```
frame 12 <Expect> #9: Expect Schedule.Activity(pierre) is 'WorkAt', got StayHome
frame 900 <ExpectVisited> #3: AI.State(FarmAutoFarmer) never went through 'Till > Sow > Water' since frame 0 (missing 'Water'), saw: Till@12 > Sow@40
```

## 작동 원리

**시점.** `<At>` 은 `frame`(절대 프레임), `time`(시작부터 초), `after`(앞 `<At>` 에서 프레임), `afterSeconds` 중 정확히 하나를 받습니다. 초는 `fixedDelta` 로 반올림해 프레임으로 바꿉니다.
상대 시점이 있어서 "점프하고 0.1 초 뒤 이동" 같은 연속 동작을 프레임을 세지 않고 적습니다.

**입력 단계.** 키 단계(`Press`, `Release`, `Tap`, `MouseDelta`, `MousePosition`, `GamepadAxis`, `Text`, `Wheel`)와 액션 단계(`Action`, `ActionPress`, `ActionRelease`, `ActionAxis`)는 시작 프레임에 모두 가상 입력 스크립트로 옮겨집니다.
액션 단계는 그때 통합 입력 맵(`InputManager::getInputMap`)에서 액션의 바인딩을 찾아 그 키, 단추, 스틱을 냅니다. 그래서 트리거(Hold, DoubleTap)와 레이어가 사람의 입력과 같은 경로를 지나고, 키 배치를 바꿔도 시나리오는 그대로입니다.
`device="gamepad"` 처럼 장치를 고를 수 있고, 패드 바인딩을 쓰면 가상 패드를 먼저 연결합니다.

**대상 고르기.** `target`(그리고 `Object.DistanceTo` 의 `key`, `Mount` 의 `rider` · `vehicle`)은 오브젝트 이름 글이고, 규칙은 `AutomationTarget` 하나가 정합니다.
같은 이름으로 오브젝트를 또 만들면 오브젝트 저장소가 `_2` · `_3` 을 붙이므로(언리얼의 `MakeUniqueObjectName`), 스폰하는 적 · 손님 · 총알은 밑 이름이 같은 오브젝트가 여럿입니다.
이름의 밑과 번호는 언리얼 `FName` 처럼 끝의 `_` + 숫자로 나눕니다(`Tree_3` 은 밑 `Tree`, `Tree_03` 은 번호가 아니다).

| 글 | 고르는 것 |
|----|-----------|
| `ArenaGruntAI[0]` | 밑이 `ArenaGruntAI` 인 살아 있는 오브젝트를 만든 순서(오브젝트 id 오름차순)로 늘어놓은 0 번째. 없으면 그 오브젝트는 없다(`Object.Exists` 0) |
| `ArenaGruntAI_2` | 그 이름 그대로. 번호까지 적었으면 그 오브젝트를 뜻한 것으로 본다 |
| `ArenaGruntAI` | 그 이름 그대로. 단 밑이 같은 다른 오브젝트가 살아 있으면 **고를 수 없다는 실패**다 |

마지막 줄이 조용히 첫째를 고르지 않는 까닭은 저장소가 지운 오브젝트의 번호를 재사용하기 때문입니다. 적이 죽고 다시 나면 `ArenaGruntAI` 가 다른 적일 수 있습니다.
만든 순서는 같은 시나리오 · 같은 고정 틱이면 실행과 백엔드에 상관없이 같습니다. "그 이름의 아무나 · 모두" 질의는 두지 않았습니다. 탐침은 값 하나를 내므로
"모두" 는 비교 연산마다 뜻이 갈리고(모두 `atLeast` 인가, 하나라도인가), "아무나" 는 실패 줄이 어느 오브젝트인지 감춥니다. 무리 전체는 게임이 수를 세는 씬 탐침(`Arena.KillCount`)으로 봅니다.

**단언과 이력.** 단언 단계는 그 프레임의 씬 틱 뒤에 돕니다. `ExpectVisited` 가 있으면 시작할 때 그 탐침(탐침, 대상, 키 조합)마다 이력을 만들고, 프레임마다 값을 읽어 바뀔 때만 프레임 번호와 함께 적습니다.
`sequence="A, B, C"` 는 그 값들을 이 순서로 거쳤는지(사이에 다른 값이 끼어도 된다), `since` 는 그 프레임에 이미 들어 있던 값부터 셀지를 정합니다.

**전역 변수 단언.** `<ExpectVariable name="gv_cameraFieldOfView" value="80" tolerance="0.001"/>` 가 전역 변수의 지금 값을 타입대로 견줍니다(bool · int · 글 · enum 은 숫자). `tolerance` 는 실수 변수에만 붙고 없으면 정확히 같아야 합니다.
없는 변수 · 모르는 속성 · 그 타입으로 읽히지 않는 `value` 는 읽기 오류, 값이 다르면 실패입니다. 팩 설정이나 사용자 설정이 변수에 적용됐는지를 화면 캡처 없이 봅니다(`shooter3d/automation/fieldofview.scenario.xml`).

**경계 전후 견주기.** `<Remember probe="Editor.StateHash" as="edited"/>` 가 그 프레임의 탐침 값을 이름으로 기억하고, 뒤의 `Expect` · `WaitUntil` 이
`sameAs="edited"` · `differsFrom="edited"` 로 견줍니다. 값은 글 그대로 견주므로 64 비트 해시도 뭉개지지 않습니다. 실행기는 엔진 쪽이라 플레이 인/아웃,
모듈 리로드, 백엔드 교체를 넘어 기억합니다. `SwapBackend backend="other"` 는 지금과 다른 백엔드(DirectX 12 가 아니면 DirectX 12, DirectX 12 면 DirectX 11)로,
`backend="start"` 는 시나리오가 시작한 백엔드로 건너므로, 어느 백엔드로 돌리든 "갔다 돌아오는" 시나리오를 한 파일로 씁니다.
에디터 편집 상태는 `Editor.SceneHash`(오브젝트별 이름 · 부모 · 상태 XML, 씬 뷰 카메라 제외) · `Editor.SelectedNames` · `Editor.UndoState`(`위치/수:레이블|…`) ·
`Editor.StateHash`(넷을 접은 것)로 읽고, 오브젝트 하나의 컴포넌트 값은 `Object.Property target="이름" key="<컴포넌트>.<프로퍼티>"` 로 읽습니다.
에디터 기능을 잇는 흐름 시나리오는 `engine/automation/editor/flow*.scenario.xml` 이고, 기능별 표는 [에디터 시나리오 표](../../../docs/plans/EditorScenarios.md) 에 있습니다.

**결정적 재생과 녹화.** `-scenario-record=<Saved/Automation/...scenario.xml>` 를 주면 씬 플레이 첫 프레임부터 입력(시나리오가 낸 가상 입력 포함)을 적고, 끝날 때 `-scenario-record-probes` 의 탐침 값을 끝 프레임의 `<Expect>` 로 씁니다.
녹화를 다시 돌리면 같은 입력과 같은 고정 틱이므로 같은 값이 나와야 합니다. 다시 돌릴 때는 디스크 경로가 아니라 id 를 줍니다 — 시나리오 실행은 작업 폴더의 `Saved` 를 디스크 도메인 `saved` 로 붙이므로
`Saved/Automation/Recorded/a.scenario.xml` 은 `-scenario=saved/Automation/Recorded/a.scenario.xml` 입니다(`ScenarioRecorder::makeReplayID`, 녹화를 마칠 때 로그에도 찍힌다). `AppScenarioTest.RecordedPlayReplaysOnEveryBackend` 가 이 왕복을 네 백엔드에서 확인합니다.

**보고의 실행 환경.** 보고 JSON 은 결과 칸 외에 `backend` · `game` · `startedAt` · `startedAtUnixMs` · `wallMilliseconds` · `wallLimitMilliseconds` · `lastStep` · `reproduce` 를 담아, 보고 한 장으로 어디서 어떻게 돌았고 어떻게 다시 돌리는지 알게 합니다.
`reproduce` 는 App 의 원본 인자(`CommandLineManager::getOriginalArguments`)를 그대로 옮긴 명령줄입니다(`AutomationRunTitleUtil::makeReproduceCommand`). 보고 · 녹화 경로(`scenario-report` · `scenario-record*` · `crash-reporter`)는 빼고, 이름에 token · secret · password · apikey · credential · auth · license 가 든 인자의 값은 `REDACTED` 로 가립니다(규칙은 `AutomationRunTitle.cpp` 의 `AutomationReproduceRules` 한 곳).
원본에 없는 게임 · 실제 백엔드 · 시나리오 · `<Launch>` 변수만 세션과 실행기가 채웁니다.

**행 감시.** 벽시계 상한은 프레임이 도는 동안만 보므로, 프레임 자체가 멈춘 행(교착)은 감시 스레드(`AutomationHangWatchdog`)가 잡습니다. 세션이 켠 실행(`armHangWatchdog`)만 시나리오를 읽은 뒤 스레드를 하나 띄우고(부하 시나리오 제외), `endRun` 이 내립니다.
시나리오 진입점은 `App::startAutomationSession` → `AutomationSession::begin` → `startStage` 하나뿐이고 `startStage` 가 항상 켭니다. 전용 서버 · 헤드리스 호스트(`HeadlessSubsystem`)는 세션을 세우지 않아 시나리오를 돌리지 않으므로 켤 곳이 없습니다.
세션 없이 실행기를 손으로 돌리는 테스트(`startFromText`)는 켜지 않습니다 — 감시는 프로세스를 `_Exit(12)` 로 끝내므로 테스트 프로세스가 죽는다. 체인의 프로세스 시한은 `AutomationTimeLimits::getChainKillSeconds`(단일 시한 + 단계 수 × 5 초)이고, 시나리오의 `wallTimeoutSeconds` 는 시계 보류 시한(170 초) 미만이어야 읽힙니다.
실행기는 단계 전환 때와 30 프레임마다 고정 크기 스냅샷(`AutomationHangPayload` — 이름 · 백엔드 · 마지막 자리 · 프레임 · 시작 시각 · 재현 명령 · 보고 경로)을 버전 번호 seqlock(`AutomationHangSnapshot`)으로 올립니다.
상한 + 유예(`AutomationTimeLimits::kHangGraceSeconds` 5 초)가 지나도 `endRun` 이 안 불리면 감시가 할당 · 잠금 없이 스냅샷으로 보고(`hang: true`, 종료 코드 12)를 OS 원시 호출로 쓰고, 크래시 컨텍스트 · 빵부스러기 파일과 stderr 한 줄을 남긴 뒤 프로세스를 12 로 끝냅니다.
시한들의 대소 관계(감시 + 유예 < 시계 보류 170 초 < 프로세스 시한 180 초)는 `AutomationTimeLimits.h` 한 곳에서 `static_assert` 로 못 박습니다.

**시작을 미루는 조건.** `startAfter` 가 무엇이든 아래 조건이 풀려야 시작합니다(`AutomationStepRegistry::isStartBlocked` — 시작 · 시계 범위).

| 조건 | 등록 위치 | 풀리는 조건 | 에디터에서 영원히 안 풀릴 수 있나 |
|---|---|---|---|
| `UILoadingScreen`(시작) | `UIAutomationSteps.cpp` | 로딩 화면이 닫힐 때. 화면 UI 를 막은 동안(멈춘 에디터, `isOnScreenSuppressed`)은 막지 않는다 | 아니오. 억제 깃발은 엔진 `beginFrame` 뒤 `ModuleHost::beginFrame` 이 정하므로 첫 프레임만 이전 값(꺼짐)을 보고, 멈춘 에디터는 다음 프레임에 풀린다 — `editor/loadingscreen` 은 시작이 한두 프레임 늦을 뿐이다 |
| `ContentWatchFence`(시작) | `AssetHotReload.cpp` | 사본 폴더를 새로 만든 뒤 파일 감시 표지가 오거나 시한이 지날 때(표지 시한 + 임포트 시한, 시한이면 오류 로그 후 시작) | 아니오. 스스로 시한이 있다 |
| `QuickLauncherIndexing`(시계) | `QuickLauncherPopup.cpp` | 열린 빠른 실행 팝업의 파일 색인 잡이 끝날 때 | 잡이 끝나지 않으면 시계 보류 시한(170 초)과 벽시계 상한이 12 로 끝낸다 |
| `EditorAssetValidation`(시계) | `EditorFrameSteps.cpp` | 에셋 검증이 끝날 때 | 위와 같다 |
| `ContentBrowserReferenceIndex`(시계) | `ContentBrowserPanel.cpp` | 참조 색인을 기다리는 동안 | 위와 같다 |
| `ScreenFade`(캡처) | `LoadingScreenController.cpp` | 시작은 미루지 않고 화면 캡처만 미룬다 | 해당 없음 |
필드는 읽는 쪽(`Scripts` 의 보고 읽기, `AppTestUtil::readReportRecords`)이 있어 더하기만 하고, `records` 는 끝에 둡니다. 실행기는 단계가 바뀔 때와 30 프레임마다 시나리오 · 백엔드 · 단계 · 프레임을 크래시 컨텍스트의 `Scenario` 키에 적고(`CrashHandler::setContextValue`), 단계 전환을 빵부스러기로 남깁니다.

**로그 게이트.** `<Pass/>` 에 닿은 시나리오도 처리하지 않은 `[Warning]` · `[Error]` 를 남겼으면 종료 코드 15(`UnhandledLog`)로 끝납니다(`AutomationLogPolicy`). 단언이 모두 맞아도, 어림값 문턱이 있어도(14) 마찬가지입니다.
이미 진 실행(10 · 12 …)은 코드를 바꾸지 않지만 같은 게이트를 지나, 처리하지 않은 줄이 보고의 `unhandledLogs[]` 에 남습니다.
실행기는 시나리오 구간의 줄과, 프로세스 전체에서 빠진 현지화 키(`LocalizationManager::getMissingKeys`)를 봅니다. `AppScenarioTest` 는 같은 판정을 프로세스 출력 전체(기동 · 종료 포함)에 다시 겁니다.
줄 하나가 처리됐다는 것은 둘 중 하나입니다.

- 시나리오가 일부러 내는 줄은 그 시나리오에 `<ExpectLog contains="…">` 로 선언합니다(줄 수 단언과 짝입니다).
- 고칠 수 없는 줄은 `Resource/engine/automation/allowedlogs.json` 의 항목으로 허용합니다. 항목은 **로그 id**(`SW_LOG_WARNING_ID` · `SW_LOG_ERROR_ID`), **범위**(`scenario` 나 `suite` 중 정확히 하나 — 전역 허용은 없다),
  **사유**(`reason`), **처리 계획 문서 이름**(`plan`)이 모두 있어야 읽힙니다. 하나라도 비었거나 `TODO` 이면, id 에 와일드카드가 있거나 같은 항목이 두 번이면 시나리오가 읽기 오류(11)로 섭니다.

로그 id 가 없는 줄은 허용 목록으로 가릴 수 없습니다 — 소스에 id 를 붙입니다(텍스트 로그에는 `[id:영역.사건]` 꼬리표로 찍혀 `AppScenarioTest` 가 기동 · 종료 구간의 줄도 id 로 가립니다).
실행기 줄(`[Scenario]`)과 테스트의 방어 구간 줄(`[Expected Defensive Test]`)은 보지 않습니다. 호스트 테스트 스위트(`HostLogJudge`)도 이 목록의 `suite` 항목을 씁니다.
보고 JSON 의 `unhandledLogs[]`(처리하지 않은 줄)와 `allowedLogs[]`(맞은 허용 항목의 id)를 `py -3 -m Scripts scenario-logs` 가 읽습니다. `--check` 는 목록에 있으나 끝까지 도는 시나리오에서 더 나오지 않는 항목(지운다)과 15 로 끝난 보고를
실패로 세고, `--regen` 은 처리하지 않은 줄을 사유 · 계획이 `TODO` 인 초안 항목으로 더합니다. 처음 채우는 절차는 [GPU 검증](../../../docs/plans/GpuVerification.md) 4절입니다.

## 확장하는 법

새 탐침을 더하는 절차입니다.

1. 게임이나 키트의 .cpp 에 읽기 함수를 둡니다. 씬 탐침은 `bool( const GameObjectManager*, float64& )`, 질의 탐침은 `bool( const AutomationProbeQuery&, AutomationProbeValue& )` 입니다.
2. 같은 .cpp 의 `namespace sw` 안에 `SW_AUTOMATION_PROBE( id, "<게임>.<값>", "설명", &함수 )` 또는 `SW_AUTOMATION_QUERY_PROBE( id, 이름, 설명, &함수, target 필요, key 필요 )` 한 줄을 둡니다.
3. 값을 낼 수 없으면 false 를 돌려주고, 질의 탐침은 `outValue.setText( 이유 )` 로 이유를 적습니다. 그 이유가 실패 줄에 실립니다.

탐침 레지스트리(`Engine/Probe/AutomationProbe.h`)는 티어가 컴포넌트 모델(Object) 바로 위라 그 위의 Engine 기능(Scene · Character · Environment · Vfx …)도 자기 폴더의 .cpp 에 탐침을 둡니다(`Scene/SceneProbes.cpp`, `Environment/Terrain/TerrainProbes.cpp`).
실행기(Automation)가 기능 헤더를 include 해 기능의 탐침을 대신 정의하지 않습니다. 실행기에 남은 탐침은 실행기 자신의 상태, 오브젝트 질의(`Object.*`), 그리고 Probe 와 티어가 같거나 낮아 기능 쪽에 둘 수 없는 것(`Input.*` · `Profiler.TraceRecording`)입니다.

새 단계는 `SW_AUTOMATION_STEP( id, "이름", 실행, 검사, 입력 전 여부 )` 로 등록합니다. 입력으로 옮겨지는 단계는 `SW_AUTOMATION_STEP_WITH_HOOKS` 의 준비 함수에서 `runner.getInputScript()` 에 이벤트를 넣습니다.
속성은 검사 함수에서 모두 확인합니다. 모르는 속성은 읽기 오류로 만들어 오타가 기본값으로 조용히 바뀌지 않게 합니다.

AI 의 상태를 시나리오에서 보려면 `AIControllerComponent` 파생의 `think` 에서 상태가 바뀔 때 `setStateName( "Chase" )` 를 부르고, 판단 값은 `getBlackboard()` 에 둡니다. 탐침 `AI.State` 와 `AI.Blackboard` 가 그것을 읽습니다.
게임 로직 단계와 탐침(시계, 판 상태의 돈 · 퀘스트, 일정, AI, 빙의, 탑승)의 정의는 `GameFramework/Base/Gameplay/Automation/GameplayAutomationSteps.cpp` 에 있습니다.
게임마다 이 기반을 쓰는 시나리오는 `abilityarena/automation/ai`(적 AI 상태 · 번호 대상), `voxelcraft/automation/ai`(자동 플레이 빙의 · 상태), `meadowvillage/automation/day`(시간 감기 · 돈 · 퀘스트),
`themepark/automation/guests`(손님 풀의 번호 대상), `harvestvalley/automation/schedule` · `autofarmer` 입니다. 탑승 단계(`Mount` · `Dismount` · `Pawn.Seat`)는 탈것을 쓰는 게임이 없어 시나리오가 없습니다.

등록자는 단계 · 탐침을 정의한 .cpp 의 정적 객체입니다. Shipping 은 Engine · GameFramework · 키트 · 게임을 통째로 링크하므로(`sw_linkWholeArchive`, 테스트 실행 파일도 같다)
그 .cpp 를 붙드는 빈 함수(`ensureLinked`)를 두지 않습니다.

## 함정과 주의

- **같은 프레임에 넣은 입력의 결과를 그 프레임에 단언하지 않습니다.** 입력은 그 프레임의 틱 전에 들어가므로, 같은 프레임의 다른 입력(도구 1 을 누른 뒤 도구 2)이 단언보다 먼저 상태를 바꿉니다. 단언은 다음 `<At>` 에 둡니다.
- **`WaitUntil` 이 붙잡은 프레임은 시나리오 프레임에 들어가지 않습니다.** `ExpectVisited` 의 실패 줄에 찍힌 프레임과 `time` 은 시나리오 프레임이라, 실제 경과 시간을 추정할 때는 `held the clock for N frame(s)` 줄의 값을 더합니다.
- **시계를 감은 결과는 탐침이 바로 보지만 그 결과를 따르는 오브젝트는 다음 틱에 움직입니다.** 일정상 위치(`Schedule.*`)와 몸의 위치(`Object.*`)를 같은 프레임에 단언하면 몸 쪽이 한 프레임 늦습니다.
- **녹화로 정확히 재생하려면 녹화도 고정 프레임 시간으로 돌려야 합니다.** 시나리오 없이 사람이 녹화할 때는 `-gv_fixedFrameDelta=0.0166667` 를 함께 줍니다. 녹화는 고정 프레임 시간을 소수 일곱 자리로 씁니다. 넷째 자리에서 자르면 재생이 프레임마다 0.2 % 더 흘러 위치 검사점이 어긋납니다.
- **배타 시나리오는 시작 조건을 기다리는 프레임부터 사람의 장치를 버리고 포인터를 (0, 0) 에 둡니다.** 기다리는 동안 게임이 실제 마우스 위치를 읽으면 그 값이 게임 상태(NileCity 건설 커서)에 남아 실행마다 다른 결과가 나옵니다.
  백엔드끼리 `records` 가 다르면 `-gv_automationHashDump=1` 로 두 번 돌려 `[Scenario] hash item` 줄을 비교하면 어긋난 오브젝트가 나옵니다.
- **경계를 넘는 단언은 탐침 셋으로 나눠 둡니다.** `Editor.StateHash` 하나만 견주면 실패 줄이 해시 둘만 보여 줍니다. 같은 프레임에 `Editor.SceneHash` · `Editor.UndoState` ·
  `Editor.SelectedNames` 를 함께 두면 어느 쪽이 달라졌는지 바로 나오고, 씬 내용이 다르면 `-gv_automationHashDump=1` 의 `[Scenario] scene hash item` 줄로 오브젝트를 찾습니다.
- **`backend="other"` 로 건넌 뒤에는 그림을 견주지 않습니다.** 백엔드마다 그림이 조금씩 다르므로, 스크린샷 비교는 `backend="start"` 로 돌아온 뒤에 합니다.
  그림이 "같다" 는 단언은 편집 크기보다 훨씬 작은 문턱(예: 편집이 0.015 면 0.002)으로 둡니다. 문턱이 편집 크기에 가까우면 편집이 사라져도 통과합니다.
- **`ExpectImage` 의 문턱은 근거 속성이 있어야 읽힙니다.** `measured="2026-10-22 dx12:0.095 vk:0.095"`(날짜와 백엔드별 잰 값, 모두 같으면 `all:0.095`) 또는 `provisional="이유"` 중 정확히 하나입니다. 숫자를 측정 전에 적어야 하면 `provisional` 로 적습니다.
  어림값 문턱이 맞은 시나리오는 통과(0)가 아니라 미확정(14)으로 끝나고 보고 JSON 의 `unconfirmed` 에 줄이 남습니다(틀리면 근거와 무관하게 실패). 지표 값은 늘 `metrics` 에 적히니 그 값을 `measured` 로 옮기면 확정입니다. 확인은 `AutomationThresholdBasis`.
- **글 값 탐침을 수로 비교하면 실패로 적힙니다.** `AI.State` 같은 글 탐침은 `is` 로만 비교합니다. 수 탐침은 `is` 로도 비교할 수 있으며, 끝의 0 을 정리한 글(`1`, `0.5`)과 비교합니다.
- **리소스에 쓰는 시나리오는 원본 폴더에 쓰지 않습니다.** 루트 아래 `<Scratch from="engine/automation/editor/assetmanage" as="scratch/assetmanage"/>` 로 쓰는 폴더를 선언하면 실행기가 시작할 때 `Resource/scratch/…`(저장소에 넣지 않음)를 지우고 원본을 복사합니다(글 파일의 원본 id 는 사본 id 로, `.meta` 의 guid 는 새로). 단계는 사본 id 를 씁니다. 중단된 실행이 남긴 파일도 다음 시작이 지웁니다(`AutomationScratch`). 사본의 에셋을 여는 길은 Quick Open(이름으로 찾아 원본이 걸릴 수 있다)이 아니라 `asset.openScratch <사본 id>` 다. 쿠커의 도메인 목록(`engine` · `common` · `game/*`)에 `scratch` 가 없어 사본은 팩에 들지 않는다.
- **한 틱에 끝나는 작업만 있는 행동 트리는 도는 잎이 없습니다.** `AI.BehaviorTask` 와 `Skirmish.AITask` 는 그때 그 틱에 마지막으로 성공한 작업을 보이므로, 늘 성공하는 작업(노는 일꾼 보내기)이 대부분의 프레임을 차지합니다.

## 더 볼 곳

| 파일 | 내용 |
|------|------|
| `AutomationStepContext.h` | 단계 함수가 받는 좁은 문맥(`AutomationStepContext`)과 단계가 시나리오를 끝내는 말(`AutomationStepEnd`) |
| `AutomationRunner.cpp` | 엔진 단계와 실행기 탐침(`Object.*`, `Input.*`, `Assets.Pending`) |
| `Engine/Probe/AutomationProbe.cpp` | 탐침 레지스트리, 대상 고르기 규칙(`AutomationTarget`, `AutomationProbeQuery::make`) |
| `Scene/SceneProbes.cpp` · `Environment/Terrain/TerrainProbes.cpp` · `Character/PairedAnimation/PairedAnimationProbes.cpp` | 기능이 자기 폴더에 등록한 탐침(`Scene.*` · `Terrain.*` · `PairedAnimation.*`, `-gv_automationHashDump`) |
| `AutomationActionSteps.cpp` | 입력 맵 액션 단계 |
| `ScenarioRecorder.cpp` | 녹화를 시나리오로 바꾸기 |
| `GameFramework/Base/Gameplay/Automation/GameplayAutomationSteps.cpp` | 게임 시간, 일정, AI, 빙의, 탑승 |
| `Test/AppTest/TestAppScenario.cpp` | 시나리오 × 백엔드 실행, 기록 비교, 녹화 왕복 |
| `Editor/SelfTest/EditorCaptureSteps.cpp` | 에디터 UI 캡처 단계 `CaptureEditorUI`(패널 + 플로팅 창 PNG, 마젠타 비율 · 잘린 창 수 단언) |

검증 절차는 [검증과 측정](../../../docs/08_Verification.md) 3절, 결정은 [결정 기록](../../../docs/09_Decisions.md) 에 있습니다.
