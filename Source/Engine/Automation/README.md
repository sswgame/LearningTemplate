# Automation — 자동화 시나리오

## 이것은 무엇이고 왜 있나

자동화 시나리오는 실제 `App.exe` 를 띄운 채 프레임마다 입력을 넣고 게임 상태를 단언하는 XML 파일입니다(`Resource/<영역>/automation/*.scenario.xml`).
사람이 화면을 보고 확인하던 일, 예를 들어 "점프하고 앞으로 달린다", "10 시에 상점 주인이 가게에 있다", "자동 농부가 갈고 심고 물을 준다" 를 파일로 남겨 네 백엔드에서 반복합니다.
종료 코드가 결과입니다(0 통과, 10 실패, 11 읽기 오류, 12 시간 초과, 13 건너뜀). `AppScenarioTest` 가 시나리오 파일을 찾아 백엔드마다 돌립니다.

언리얼의 Automation Test 와 Functional Test, 프로세스를 띄워 결과를 모으는 Gauntlet 에 해당하고, 유니티의 Input Test Fixture(장치 입력 흉내)와 Test Framework 의 PlayMode 테스트에 해당합니다.
그들과 다른 점은 시나리오가 코드가 아니라 데이터라는 것입니다. 단계와 탐침은 레지스트리에 한 줄로 등록하고, 게임은 기반(판 상태, 일정 시스템, AI 조종자, 폰)을 쓰기만 하면 시나리오에서 그 상태를 물을 수 있습니다.

## 머릿속 그림

```
scenario.xml ──► AutomationScenario(<At> 시점 → 프레임) ──► AutomationRunner ──► 보고 JSON(failures · metrics · records)
                                                             │
                     시작 프레임: 단계마다 검사 · 준비 ───────┤  입력 단계 → VirtualInputScript → InputManager(장치 층)
                     프레임마다: 씬 틱 뒤 단계 실행 ─────────┤  단언 단계 → AutomationProbes(씬 탐침 · 질의 탐침)
                                                             └  ExpectVisited 이력(탐침 값이 바뀔 때만 적는다)
```

**단계.** `<At>` 아래의 요소 하나가 단계입니다. 단계 종류는 `AutomationStepRegistry` 에 등록되고, 엔진 단계도 같은 레지스트리에 있습니다. 실행기는 단계 이름으로 분기하지 않습니다.

**탐침.** 시나리오가 읽는 이름 붙은 값입니다. 씬 탐침은 인자 없이 수를 내고(`Farm.FarmerX`), 질의 탐침은 `target` 과 `key` 를 받아 수나 글을 냅니다(`AI.State target="FarmAutoFarmer"` → `Till`).
글 값은 `is` 로 비교하고, 수 값은 `equals`, `near`, `atLeast`, `atMost` 로 비교합니다.

**시나리오 프레임.** 프레임 0 은 시작 조건이 처음 참인 프레임입니다(`startAfter="ScenePlaying"` 이면 씬이 플레이를 시작하고 로딩 화면이 사라진 첫 프레임).
`WaitUntil` 이 기다리는 동안은 시나리오 프레임을 세지 않으므로 뒤 단계가 그만큼 밀립니다. 시간은 늘 고정 프레임 시간(`fixedDelta`)으로 흐릅니다.

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
3. 한 프레임 뒤 일정상 활동과 위치를 확인하고, 그다음 프레임에 몸(`Villager.pierre` 오브젝트)이 가게 위치로 옮겨졌는지 봅니다. 몸은 다음 틱에 따라오므로 한 프레임을 띄웁니다.
4. 실행합니다. `App -game=HarvestValley -scenario=game/harvestvalley/automation/schedule.scenario.xml` 의 종료 코드가 0 이면 통과입니다.

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

**단언과 이력.** 단언 단계는 그 프레임의 씬 틱 뒤에 돕니다. `ExpectVisited` 가 있으면 시작할 때 그 탐침(탐침, 대상, 키 조합)마다 이력을 만들고, 프레임마다 값을 읽어 바뀔 때만 프레임 번호와 함께 적습니다.
`sequence="A, B, C"` 는 그 값들을 이 순서로 거쳤는지(사이에 다른 값이 끼어도 된다), `since` 는 그 프레임에 이미 들어 있던 값부터 셀지를 정합니다.

**경계 전후 견주기.** `<Remember probe="Editor.StateHash" as="edited"/>` 가 그 프레임의 탐침 값을 이름으로 기억하고, 뒤의 `Expect` · `WaitUntil` 이
`sameAs="edited"` · `differsFrom="edited"` 로 견줍니다. 값은 글 그대로 견주므로 64 비트 해시도 뭉개지지 않습니다. 실행기는 엔진 쪽이라 플레이 인/아웃,
모듈 리로드, 백엔드 교체를 넘어 기억합니다. `SwapBackend backend="other"` 는 지금과 다른 백엔드(DirectX 12 가 아니면 DirectX 12, DirectX 12 면 DirectX 11)로,
`backend="start"` 는 시나리오가 시작한 백엔드로 건너므로, 어느 백엔드로 돌리든 "갔다 돌아오는" 시나리오를 한 파일로 씁니다.
에디터 편집 상태는 `Editor.SceneHash`(오브젝트별 이름 · 부모 · 상태 XML, 씬 뷰 카메라 제외) · `Editor.SelectedNames` · `Editor.UndoState`(`위치/수:레이블|…`) ·
`Editor.StateHash`(넷을 접은 것)로 읽고, 오브젝트 하나의 컴포넌트 값은 `Object.Property target="이름" key="<컴포넌트>.<프로퍼티>"` 로 읽습니다.
에디터 기능을 잇는 흐름 시나리오는 `engine/automation/editor/flow*.scenario.xml` 이고, 기능별 표는 [에디터 시나리오 표](../../../docs/plans/EditorScenarios.md) 에 있습니다.

**결정적 재생과 녹화.** `-scenario-record=<Saved/Automation/...scenario.xml>` 를 주면 씬 플레이 첫 프레임부터 입력(시나리오가 낸 가상 입력 포함)을 적고, 끝날 때 `-scenario-record-probes` 의 탐침 값을 끝 프레임의 `<Expect>` 로 씁니다.
녹화를 다시 돌리면 같은 입력과 같은 고정 틱이므로 같은 값이 나와야 합니다. `AppScenarioTest.RecordedPlayReplaysOnEveryBackend` 가 이 왕복을 네 백엔드에서 확인합니다.

**로그 게이트.** `<Pass/>` 에 닿은 시나리오도 선언하지 않은 `[Warning]` · `[Error]` 를 남겼으면 실패입니다(`AutomationLogPolicy`). 실행기는 시나리오 구간의 줄과, 프로세스 전체에서 빠진 현지화 키(`LocalizationManager::getMissingKeys`)를 봅니다.
`AppScenarioTest` 는 같은 판정을 프로세스 출력 전체(기동 · 종료 포함)에 다시 겁니다. 시나리오가 일부러 내는 줄은 그 시나리오에 `<ExpectLog contains="…">` 로 적고,
고칠 수 없는 줄만 `Resource/engine/automation/allowedlogs.json` 에 이유와 함께 넣습니다. 실행기 줄(`[Scenario]`)과 테스트의 방어 구간 줄(`[Expected Defensive Test]`)은 보지 않습니다.

## 확장하는 법

새 탐침을 더하는 절차입니다.

1. 게임이나 키트의 .cpp 에 읽기 함수를 둡니다. 씬 탐침은 `bool( const GameObjectManager*, float64& )`, 질의 탐침은 `bool( const AutomationProbeQuery&, AutomationProbeValue& )` 입니다.
2. 같은 .cpp 의 `namespace sw` 안에 `SW_AUTOMATION_PROBE( id, "<게임>.<값>", "설명", &함수 )` 또는 `SW_AUTOMATION_QUERY_PROBE( id, 이름, 설명, &함수, target 필요, key 필요 )` 한 줄을 둡니다.
3. 값을 낼 수 없으면 false 를 돌려주고, 질의 탐침은 `outValue.setText( 이유 )` 로 이유를 적습니다. 그 이유가 실패 줄에 실립니다.

새 단계는 `SW_AUTOMATION_STEP( id, "이름", 실행, 검사, 입력 전 여부 )` 로 등록합니다. 입력으로 옮겨지는 단계는 `SW_AUTOMATION_STEP_WITH_HOOKS` 의 준비 함수에서 `runner.getInputScript()` 에 이벤트를 넣습니다.
속성은 검사 함수에서 모두 확인합니다. 모르는 속성은 읽기 오류로 만들어 오타가 기본값으로 조용히 바뀌지 않게 합니다.

AI 의 상태를 시나리오에서 보려면 `AIControllerComponent` 파생의 `think` 에서 상태가 바뀔 때 `setStateName( "Chase" )` 를 부르고, 판단 값은 `getBlackboard()` 에 둡니다. 탐침 `AI.State` 와 `AI.Blackboard` 가 그것을 읽습니다.
게임 로직 단계와 탐침(시계, 일정, AI, 빙의, 탑승)의 정의는 `GameFramework/Base/Gameplay/Automation/GameplayAutomationSteps.cpp` 에 있습니다.

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
- **글 값 탐침을 수로 비교하면 실패로 적힙니다.** `AI.State` 같은 글 탐침은 `is` 로만 비교합니다. 수 탐침은 `is` 로도 비교할 수 있으며, 끝의 0 을 정리한 글(`1`, `0.5`)과 비교합니다.
- **한 틱에 끝나는 작업만 있는 행동 트리는 도는 잎이 없습니다.** `AI.BehaviorTask` 와 `Skirmish.AITask` 는 그때 그 틱에 마지막으로 성공한 작업을 보이므로, 늘 성공하는 작업(노는 일꾼 보내기)이 대부분의 프레임을 차지합니다.

## 더 볼 곳

| 파일 | 내용 |
|------|------|
| `AutomationRunner.cpp` | 엔진 단계와 엔진 탐침(`Object.*`, `Scene.*`, `Input.*`) |
| `AutomationActionSteps.cpp` | 입력 맵 액션 단계 |
| `ScenarioRecorder.cpp` | 녹화를 시나리오로 바꾸기 |
| `GameFramework/Base/Gameplay/Automation/GameplayAutomationSteps.cpp` | 게임 시간, 일정, AI, 빙의, 탑승 |
| `Test/AppTest/TestAppScenario.cpp` | 시나리오 × 백엔드 실행, 기록 비교, 녹화 왕복 |
| `Editor/SelfTest/EditorCaptureSteps.cpp` | 에디터 UI 캡처 단계 `CaptureEditorUI`(패널 + 플로팅 창 PNG, 마젠타 비율 · 잘린 창 수 단언) |

검증 절차는 [검증과 측정](../../../docs/08_Verification.md) 3절, 결정은 [결정 기록](../../../docs/09_Decisions.md) 에 있습니다.
