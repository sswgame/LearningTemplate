# Automation — 자동화 시나리오

실기동 확인(무기 교체 한 칸 · 창 닫기 · 그림자)을 사람 손이나 바깥 스크립트(OS 입력 · 창 메시지) 대신 **데이터 파일 하나**로 적고, App 명령줄로 돌려
**종료 코드**로 결과를 냅니다. 참고: 언리얼 Functional Test(레벨 안 단계 · 단언) + Gauntlet(프로세스를 띄워 결과 코드 · 로그를 모음),
유니티 Test Framework(`InputTestFixture.Press/Release` · 프레임 단위 `yield`). 차이는 코드가 아니라 데이터라는 것(빌드 없이 쓴다).

| 파일 | 하는 일 |
|------|---------|
| `AutomationScenario` | 시나리오 파일을 읽은 값 — 루트 속성 · `<At frame>` · 단계(엘리먼트 이름 + 속성). 형식만 본다 |
| `AutomationRunner` | 실행기 — 시작 조건 · 단계 검사 · 입력 단계를 가상 입력(`VirtualInputScript`)으로 · 단언 · 끝 · 요약 줄 · JSON 보고 |
| `AutomationProbe` | 탐침 등록표 — `<Expect probe="…">` 가 읽는 이름 붙은 값. 게임 · 키트가 `SW_AUTOMATION_PROBE` 한 줄로 등록 |
| `AutomationStepRegistry` | 엔진 밖 단계 종류의 등록표 — GameFramework(행동 층) · 에디터(ImGui) · 플랫폼(창 메시지)이 `SW_AUTOMATION_STEP` 로 더한다 |

## 실행

```powershell
cd build/Ninja-Debug-Shooter3D/Bin
./App.exe -dx12 -scenario=game/shooter3d/automation/weaponswitch.scenario.xml -scenario-report=Saved/Automation/ws.json
echo $LASTEXITCODE   # 0 통과 · 10 실패 · 11 읽기 오류 · 12 시간 초과 · 13 건너뜀
```

- 시나리오는 **늘 고정 프레임 시간**입니다 — `EngineLoop` 가 App 의 `gv_fixedFrameDelta` 를 시나리오의 `fixedDelta` 로 둡니다(벽시계와 무관, 같은 입력 → 같은 결과).
- 끝나면 `EngineLoop::requestQuit( 결과 )` — App 이 그 코드로 끝납니다. 로그에 `[Scenario] PASS <이름> (N frame(s))` 또는 `[Scenario] FAIL …` 한 줄(실패 줄들 포함).
- 산출물(스크린샷)은 `Saved/Automation/<시나리오 이름>/`. 보고 JSON 은 `-scenario-report` 경로(비면 쓰지 않는다).

## 파일 형식 — `Resource/<영역>/automation/<이름>.scenario.xml`

```xml
<Scenario name="shooter3d.weaponswitch" fixedDelta="0.0166667" timeoutFrames="900" startAfter="ScenePlaying" startTimeoutFrames="600" input="exclusive">
  <!-- 프레임 번호는 시작 조건이 처음 참인 프레임이 0. 같은 프레임의 단계는 적은 순서. -->
  <At frame="0"><Variable name="gv_shooterAutoPlay" value="0"/></At>
  <At frame="30"><Tap slot="Key.E" hold="2"/></At>
  <At frame="40"><Expect probe="Shooter3D.WeaponIndex" equals="1"/></At>
  <At frame="300"><Pass/></At>
</Scenario>
```

| 루트 속성 | 기본 | 뜻 |
|---|---|---|
| `name` | (필수) | 보고 · 산출물 폴더 이름 |
| `fixedDelta` | `1/60` | 프레임마다 흘릴 시간(초) |
| `timeoutFrames` | 3600 | 이 프레임을 넘도록 `<Pass/>` 가 없으면 12 |
| `startAfter` | `ScenePlaying` | `ScenePlaying`(활성 씬이 플레이를 시작했고 로딩 화면이 걷힌 첫 프레임 — 로딩 화면은 게임 입력을 막는다) · `Immediately` |
| `startTimeoutFrames` | 1200 | 시작 조건을 이만큼 기다려도 안 오면 12 |
| `input` | `exclusive` | `exclusive`(OS 입력 무시 — 기본) · `mixed`(OS 입력도 받는다, 진짜 창 상태를 볼 때) |

| 단계 | 속성 | 하는 일 | 층 |
|---|---|---|---|
| `Press` · `Release` | `slot`(`InputSlotUtil` 글 — `Key.E` · `Mouse.Left` · `Gamepad.A` · `Gamepad1.A`) | 가상 사건 | 입력 |
| `Tap` | `slot`, `hold`(프레임, 기본 1, 0 = 같은 프레임에 뗌) | 누름 + hold 뒤 뗌 | 입력 |
| `MouseDelta` | `x` · `y`(픽셀) | `MouseRawDelta` | 입력 |
| `MousePosition` | `x` · `y`(창 클라이언트 영역의 비율 0..1 — 시작할 때 창 크기로 픽셀) | 커서를 옮김(`MouseMove`). 뒤의 마우스 버튼은 이 자리에서 눌린다(없으면 (0, 0)) — 커서 아래를 고르는 조작(RTS 선택 · 배치) | 입력 |
| `GamepadAxis` | `axis` · `value` · `pad` | 축 | 입력 |
| `Text` | `value` | 글자 입력 | 입력 |
| `Variable` | `name` · `value` | 전역 변수(gv) 값 설정 — 모르는 변수는 읽기 오류 | 환경 |
| `Expect` | `probe` + `equals` · `near`(+`tolerance`) · `atLeast` · `atMost` 중 하나 | 탐침 값을 단언, 틀리면 실패를 적고 계속 | 결과 |
| `ExpectLog` | `contains` + `count` · `atLeast` 중 하나, `since`(프레임) | 시나리오 동안의 로그 줄 수(실행기의 `[Scenario]` 줄은 세지 않는다) | 결과 |
| `Screenshot` | `file`(상대면 `Saved/Automation/<이름>/`) | 다음에 그리는 렌더 패킷에 실어 그 프레임의 화면(Present 결과)을 PPM 으로 | 결과 |
| `ExpectImage` | `file` · `metric` · `region`(`x0,y0,x1,y1` 0..1) · `ratio`(darkFraction, 기본 0.7) · `reference`(differentFrom) + 비교 하나 | 영역 지표 단언 — 스크린샷이 써질 때까지 기다린다(최대 30 프레임) | 결과 |
| `CloseWindow` | `withinSeconds`(기본 10) | 창 닫기 요청 — 그 시간 안에 루프가 끝나야 통과 | 환경 |
| `ExpectExitWithin` | `seconds`(기본 10) | 앞 단계가 창을 닫게 했다 — 그 시간 안에 끝나야 통과(창 메시지 플랫폼 단계와 함께) | 결과 |
| `Pass` · `Fail` · `Skip` | `reason`(`Fail` · `Skip`) | 끝 — `Pass` 는 실패가 적혀 있으면 10 | 끝 |
| `Intent` · `Possess` | `pawn` · `move` · `up` · `yaw` · `pitch` · `buttons` · `frames` / `controller` · `pawn` | GameFramework 등록 — 폰에 의도를 직접 넣기 · 빙의 옮기기(`Source/GameFramework/README.md` Control) | 행동 |
| `ExpectUi` | `focus`(위젯 이름 · `none`) · `screen`(활성 화면 문서 · `none`) · `screens`(화면 수) 중 하나 이상 | 런타임 UI 단언 — 엔진 UI 가 등록(`Engine/UI/Automation/UiAutomationSteps`), 같은 판정을 nogpu `UiNavigationScriptTest` 가 쓴다 | 결과 |
| `UiLayoutDump` | `file`(상대면 `Saved/Automation/<이름>/`) | UI 스택의 화면마다 레이아웃 덤프(위젯 이름 · 물리 픽셀 사각형)를 쓴다 — `AppUiTest` 가 스크린샷 안의 위젯을 이름으로 찾는다 | 결과 |
| 그 밖 | — | 등록표(`AutomationStepRegistry`)에서 이름으로 찾는다 | |

- **모르는 엘리먼트 · 모르는 속성 · 형식이 틀린 값 · 모르는 탐침 · 모르는 gv 는 읽기 오류(11)** — 조용히 버리지 않습니다. 단계 종류 · 탐침 검사는
  시작 조건이 참이 되는 프레임에 합니다(게임 · 에디터 모듈이 등록하는 이름이 그때 차 있다).
- 입력 단계는 시작할 때 가상 입력 원천으로 옮겨져 그 프레임의 `InputManager::beginFrame` 에 들어갑니다(OS 사건과 같은 자리 — `Engine/Input/README.md`).
  단언 · 환경 단계는 그 프레임의 씬 틱 뒤(`EngineLoop::endFrame` 의 입력 프레임 닫기 전)에 돕니다. 등록 단계의 `_bBeforeInput` 은 입력 재생 전에 돕니다.

## 스크린샷 · 픽셀 지표

`-gv_screenshot` 은 렌더 스레드의 자기 프레임 번호로 찍어 시나리오 프레임과 맞지 않습니다. `<Screenshot>` 은 경로를 **렌더 패킷에 실어** 그 패킷을 그린 뒤
화면에 나간 그림을 쓰고(`RenderThread` — 시나리오 동안 Present 캡처를 켜 둔다), `<ExpectImage>` 는 완료 수가 오를 때까지 기다린 뒤 PPM 을 읽어 지표를 잽니다.
지표 값은 늘 로그(`[Scenario] metric darkFraction(0,0,1,0.12) park.ppm = 0.034`)와 보고 JSON 에 적힙니다 — 문턱은 그 숫자로 정합니다.

| 지표 | 정의 |
|---|---|
| `meanLuma` | 영역 평균 휘도(0..1, Rec.709) |
| `darkFraction` | 영역에서 휘도가 **영역 중앙값 × ratio** 보다 어두운 픽셀 비율 — 그림자 · 실루엣. 영역 전체가 한 밝기면 0 이다(섞여야 값이 난다) |
| `meanRedMinusBlue` | 평균 (R − B) — 배경 대비 색 |
| `differentFrom` | `reference` 그림과의 평균 절대 차(0..1) — 백엔드 일치 · 움직임 |

## 탐침 · 단계 등록

```cpp
namespace sw
{
    namespace
    {
        bool readWeaponIndex( const GameObjectManager* pManager, float64& outValue ) { … }
    } // namespace
    SW_AUTOMATION_PROBE( shooterWeaponIndex, "Shooter3D.WeaponIndex", "Weapon slot of the first shooter player", &readWeaponIndex );
} // namespace sw
```

- 탐침은 그 게임의 컴포넌트 .cpp(다른 기호가 쓰이는 파일)에 둡니다 — Shipping 정적 링크에서 등록자가 빠지지 않게(`SW_GAME_AUTOPLAY` 와 같은 처지).
- 모듈을 내리면(핫 리로드) 그 모듈의 탐침 · 단계도 빠집니다. 같은 이름은 두 번 등록되지 않습니다(뒤 것을 거절하고 오류).

## CTest — `AppScenarioTest`(hostgpu)

`Test/AppTest/TestAppScenario.cpp` 가 `Resource/engine/automation/*.scenario.xml` 과 활성 게임 팩(`Config/Game/<게임>.json` 의 `_packRoot`)의
`automation/*.scenario.xml` 을 찾아 **백엔드마다** `App -scenario=… -scenario-report=…` 로 띄우고 종료 코드 0 을 단언합니다(13 건너뜀 · 77 이 기계에 없는 백엔드,
시나리오마다 180 초 시한). 파일을 놓기만 하면 돈다 — CMake 에 목록을 적지 않는다. 게임은 프리셋마다 하나라 게임 시나리오는 그 프리셋에서 돈다:

```powershell
ctest --test-dir build/Ninja-Debug-Shooter3D -L hostgpu -R AppTest_HostOnly --output-on-failure
```

로그 · 보고는 `Bin/Saved/Automation/<시나리오>_<백엔드>.log` · `.json`.

## 에디터 단계(에디터 모듈이 등록 — `-EnableEditor`)

| 단계 | 속성 | 하는 일 |
|---|---|---|
| `EditorClick` | `mark`(에디터 자체 시험 이름표 — `EditorSelfTestMarks::note`) · `button`(0..4) | 그 위젯 가운데로 마우스를 옮겨 누르고 뗀다 |
| `EditorText` | `value` | ImGui 에 글자 입력 |

에디터 패널 입력은 엔진 입력 층이 아니라 ImGui 가 받으므로(Win32 메시지를 ImGui 백엔드가 직접 받는다) 가상 입력 장치가 아니라 ImGui 사건으로 넣습니다.
이 단계를 쓰는 시나리오는 시작할 때 위젯 이름표 적기를 켭니다.

## 창 단계(`AutomationWindowSteps` — Windows 만, 다른 플랫폼은 같은 이름으로 건너뜀 13)

| 단계 | 속성 | 하는 일 |
|---|---|---|
| `PostWindowMessage` | `message` = `WM_ACTIVATE_INACTIVE` · `WM_ACTIVATE_ACTIVE` · `WM_KILLFOCUS` · `WM_SETFOCUS` · `WM_LBUTTONDOWN_CLIENT` · `WM_LBUTTONUP_CLIENT`(클라이언트 가운데) · `WM_CLOSE` | 자기 창에 OS 메시지(SendMessage — 처리기가 돈 뒤 돌아온다, `WM_CLOSE` 만 PostMessage) |
| `ExpectCursorClip` | `state` = `locked`(클립이 클라이언트 영역 안) · `free`(가상 화면 전체) | `GetClipCursor` 단언 |
| `RequireForeground` | — | 전경을 얻지 못하면 시나리오를 13 으로 끝낸다(잠금은 전경에서만 건다) |

진짜 OS 창 상태를 보는 시나리오는 `input="mixed"`(OS 포커스 사건을 버리지 않는다) — 도는 동안 사람이 키보드 · 마우스를 만지면 섞인다.
첫 시나리오: `game/shooter3d/automation/weaponswitch` · `closewindow`, `game/themepark/automation/shadow`.
