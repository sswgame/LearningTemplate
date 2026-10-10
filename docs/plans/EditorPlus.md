# 에디터 보강 계획

다음 세션 작업 계획 — 끝나면 지운다.

이 문서는 에디터를 상용 엔진 수준의 작업 도구로 보강하는 계획입니다. 사용자가 에디터 보강을 다음 세션에서 하기로 정했고,
세션의 스크래치 폴더는 세션이 끝나면 사라지므로 계획을 저장소에 둡니다. 다음 세션은 이 문서만 읽고 이어서 작업할 수 있어야 합니다.

## 이 문서를 쓰는 법

- **단위 하나가 커밋 하나입니다.** 단위를 끝내면 그 절을 이 문서에서 지우고, 2절 표의 줄도 지웁니다. 남길 교훈은 그 영역 README 의 함정 절에 한두 줄로 옮깁니다.
  모든 단위가 끝나면 이 문서를 지우고, [백로그](../06_Backlog.md) 1-4 의 "에디터 보강" 항목도 지웁니다.
- **에디터 보강이 끝나면 에디터 문서를 새 문체로 다시 씁니다.** `Source/Editor/README.md` 를 비롯한 에디터 문서는 5차 문서 다시 쓰기에서 일부러 뺐습니다.
  보강하면서 패널과 확장 지점이 크게 바뀌기 때문입니다. 다시 쓸 때는 [문서 쓰기 지침](../10_WritingDocs.md)의 모듈 README 틀을 따릅니다.
- **본문 대부분은 2026-10-06 의 editor-plus 제안서를 옮긴 것입니다.** 그때의 코드를 읽고 쓴 설계 초안이라, 줄 번호와 함수 위치는 어긋날 수 있습니다. 이름으로 찾습니다.
  코드 조각 안의 "적용 때 맞춘다" 는 실제 API 이름을 그때 확인하라는 뜻입니다. 결정 D1 ~ D15 의 요약은 [결정 기록](../09_Decisions.md) 5-11 에 있습니다.
- **12절과 13절은 추가 단위입니다.** editor-res 제안서에서 남긴 아이콘 단위(R3, R4, R5, R9)와, 패널 점검(panel-audit)이 찾은 부족한 점(N1 ~ N12, Animation Graph)입니다.

### 이미 끝난 것과 이 계획의 전제

- **단계 0 의 E1 ~ E5 는 끝났습니다.** E1(Win32 창 제목이 첫 글자에서 잘리던 결함)은 2차에서 고쳤고, `CheckWin32WideCalls` 게이트가 다시 생기지 않게 막습니다.
  E2(에디터 실행의 스크린샷이 까맣던 결함), E3(Unlit 보기 모드가 기본 셰이더에서 무효), E4(Hierarchy 토글 잘림과 타일 이름 끊김)는 5b 에서 고칩니다.
- 5b 는 패널 점검의 결함 단위(D1 ~ D26)와 에디터 시나리오 기반(B0), editor-res 의 R1, R2, R6, R7, R8 도 넣기로 했습니다. 시작하기 전에 `git log` 로 실제로 들어갔는지 확인합니다.
  들어가지 않은 것이 있으면 이 문서의 해당 단위보다 먼저 넣습니다.
- 2차, 3차, 4차 제안의 이름(gfx-editor-rest 단위 8, 3차 B2, config-docs, server-target, runtime-ui 8-5)은 선행 조건으로 적혀 있습니다. 모두 main 에 들어갔으므로 충족된 것으로 읽습니다.
  예외는 T4 의 패키징 진입점입니다. 아직 없으므로 T4 가 먼저 만듭니다.

### 원문과 달라진 이름

| 원문 | 지금 |
|---|---|
| `ICON_FA_EYE` 같은 Font Awesome 아이콘 | 5b 의 R2 뒤에는 `editoricon::k*`(`Source/Editor/Common/GUI/EditorIconGlyphs.h`). `ICON_FA_GEAR` → `kSettings`, `ICON_FA_ROTATE` → `kRefresh`, `ICON_FA_TRIANGLE_EXCLAMATION` → `kWarning`, 나머지는 같은 낱말(`kBug`, `kMap`, `kCamera`, `kEye`) |
| "백로그 3절(3-8 에디터 등)에 한 줄" | 그 영역 README 의 함정 절. 에디터는 `Source/Editor/README.md` 의 "함정 · 계약", 모듈은 `Source/Engine/Module/README.md`, 코어는 `Source/Core/README.md`, 렌더러는 `Source/Engine/Renderer/README.md`, 프로파일링은 `Source/Engine/Profiling/README.md` |
| "백로그 1-4 의 C, G, H 줄", "대기열 S, M 의 항목" | 이 문서. 백로그 1-4 에는 이 문서를 가리키는 항목 하나만 있습니다 |
| `cmake/Engine/TargetRules.cmake` | `cmake/Engine/ModuleTargets.cmake`(`sw_addGameFrameworkKit`, `sw_addGameModule` 이 있는 파일) |
| `EditorViewportToolbarSettings` | `ViewportToolbarSettings`(`Source/Editor/Viewport/EditorViewportToolbar.h`) |
| `SerializerUtil` 의 `writePropertyText` | `SerializerUtil::formatPropertyText`(`applyPropertyText` 의 짝) |
| `Scripts/dev/Package.py`, `py -3 -m Scripts package` | `Scripts/dev/MakePackage.py`. `Scripts/dev/` 의 이름은 동사로 시작합니다(`CheckScriptLayout`) |

### 확인 = 에디터 시나리오

자동화 시나리오가 생겼으므로 에디터 기능도 사람이 손으로 확인하지 않고 시나리오로 확인합니다. 단위마다 "확인 = 에디터 시나리오" 줄에 시나리오 하나를 적었습니다.

- **시나리오 파일**은 `Resource/engine/automation/` 아래에 둡니다(`git grep -l scenario.xml Resource` 로 지금 위치를 확인합니다). 5b 의 B0 가 에디터 전용 하위 폴더를 두면 그 폴더에 둡니다.
- **실행**은 `App.exe -EnableEditor -dx12 -scenario=<경로> -scenario-report=<경로>` 입니다. 에디터 단계는 `-EnableEditor` 일 때만 등록되고, 에디터 시나리오는 백엔드 하나로 충분합니다.
  CTest 에서는 `AppScenarioTest`(`Test/AppTest/TestAppScenario.cpp`, `hostgpu`)가 돌립니다.
- **쓰는 단계**는 시나리오 실행기의 에디터 단계(`EditorClick`, `EditorText`, B0 의 `EditorKey`)와 엔진 단계(`Expect`, `ExpectLog`, `Screenshot`, `ExpectImage`)입니다. 단계 목록은 `Source/Engine/Automation/README.md` 에 있습니다.
- **이름표와 탐침은 그 단위가 더합니다.** 시나리오가 누를 위젯에는 `EditorSelfTestMarks::note` 로 이름표를 남기고, 확인할 값은 `SW_AUTOMATION_PROBE` 로 탐침을 등록합니다.
  이름표는 시험을 켰을 때만 기록되므로 평소 비용이 없습니다.
- 단위 테스트(`EditorTest`)와 에디터 자체 시험(`SW_EDITOR_SELF_TEST`)은 원문대로 둡니다. 시나리오는 그 위에서 사용자가 실제로 하는 조작을 처음부터 끝까지 확인합니다.

## 0. 결정 — 사용자 지시("특별한 의도가 없으면 상용 엔진과 견줘 가장 나은 쪽")에 따라 추천으로 정했다

| # | 물음 | 정한 것 | 근거(상용 비교) |
|---|------|---------|------------------|
| D1 | 확장 지점의 모양 | **EditorModule 을 SHARED DLL 로 바꾸고 `SW_EDITOR_API` 로 내보낸다.** 확장 모듈은 EditorModule 을 링크한다. 옛 백로그의 "등록부를 EditorFramework SHARED 로 떼어" 안은 이것으로 바꿨다 | 언리얼은 에디터 모듈 · 플러그인이 `UnrealEd` · `Slate` DLL 을 **그대로 링크**한다(별도 얇은 틀이 아니다). 유니티 `UnityEditor` 어셈블리, Godot `EditorPlugin` 도 에디터 API 전체를 준다. 등록부만 떼면 확장이 쓸 위젯(`EditorWidgets` · `EditorDocumentPanel` · 노드 그래프 틀 · 선택 · Undo)이 EditorModule 안에 남아 결국 40 개 파일을 옮겨야 한다. 리로드 의존 그래프(키트 → SWGame)가 이미 "의존이 바뀌면 의존하는 쪽을 다시 올린다" 를 하므로 EditorModule → 확장도 같은 길로 간다 |
| D2 | 확장 모듈 이름 · 자리 · 종류 | 키트의 에디터 확장은 **`GF_Editor_<키트>`**, 폴더 `Kits/<묶음>/<키트>/Editor/`. 게임의 에디터 확장은 **`SWGameEditor`**, 폴더 `Source/Games/<게임>/Editor/`. 매니페스트 종류 **`EditorExtension`**(Dev 만, `-EnableEditor` 일 때만 올린다) | 언리얼 플러그인은 런타임 모듈 옆에 `Source/<X>Editor` 를 둔다(같은 플러그인 폴더). 2 차의 모듈 이름 규칙(`GF_Server_<X>` · `GF_Client_<X>`)과 같은 접두 꼴이라 게이트(`CheckModuleTargets`)가 접두로 구성(Dev 만)을 본다 |
| D3 | ImGui 가 DLL 마다 정적 링크(vcpkg imgui 는 정적 전용)인 문제 | 확장 DLL 마다 **컨텍스트 결속기**(현재 ImGui · ImPlot 컨텍스트와 할당자를 그 DLL 의 ImGui 사본에 건다)를 둔다. 결속 소스는 **CMake 가 확장마다 자동으로 만든다**(손으로 빠뜨릴 수 없게) | Dear ImGui FAQ "DLL 경계" 지침(`SetCurrentContext` · `SetAllocatorFunctions` 를 DLL 마다)이 정답이다. imgui 를 DLL 로 다시 짓는 것은 vcpkg 포트 변경이라 금지 |
| D4 | 확장이 메뉴 · 단축키 · 팔레트에 들어오는 길 | 커맨드 표(`_s_arrCommandRow`)는 그대로 두고 **등록 줄 `SW_EDITOR_COMMAND`** 를 더한다 — 등록부가 표 + 등록 줄을 합친다 | 언리얼 `UToolMenus` 확장 · `FUICommandList`, 유니티 `[MenuItem]` 속성, Godot `add_tool_menu_item` — 모두 "자기 파일에서 한 줄" 이다 |
| D5 | 에디터 환경설정 저장 | **`Saved/Editor/EditorPreferences.json` — 기본값과 다른 값만**(config-docs D1 원칙). 섹션은 리플렉션 구조체 하나 = `SW_EDITOR_SETTINGS` 한 줄. 테마(`EditorConfig`)는 첫 섹션으로 흡수 | 언리얼 Editor Preferences(`UDeveloperSettings` 파생을 등록하면 섹션이 생긴다, 사용자별 ini), 유니티 Preferences(`SettingsProvider` 등록) |
| D6 | 단축키 편집 | 커맨드 등록부 위의 **사용자 덮어쓰기** `Saved/Editor/Shortcuts.json`(바꾼 커맨드만), 충돌은 저장 전에 빨갛게 + 덮어쓰기/취소 | 유니티 Shortcuts Manager(프로필 = 기본에서 바뀐 것만), 언리얼 Keyboard Shortcuts(커맨드 목록에서 키 받기) |
| D7 | 모듈 켜고 끄기 | **프로젝트 매니페스트(`SWGame.module.json` 의 `_listModuleOverride`)를 고치고** "구성 · 빌드 · 다시 시작" 을 묻는다(CMake 가 같은 매니페스트로 짓기 때문에 실행 중 켜기는 없다) | 언리얼 Plugins 창 — 켜면 "Restart Now". 의존 때문에 함께 꺼지는 모듈을 미리 보인다(`ModuleCatalog::resolve` 를 그대로 돌린다) |
| D8 | assert 무시 대화상자 | **대화형 에디터 실행에서만** Windows `MessageBoxW`(계속 · 다시 시도 · 취소 = 이번만 무시 · 디버거로 멈춤 · 이 자리 늘 무시). 디버거가 붙었거나, 자동 실행(`-gv_profileFrames` · `-scenario` · 자체 시험 · 새 `-unattended`)이거나, 시험이 가로채면 지금처럼 | 언리얼 `ensure`/`check` 대화상자(Windows 메시지 상자, 무인 실행에선 안 뜸), 유니티는 대화상자가 없다(로그만) — 학습용으로 "그 자리에서 계속" 이 쓸모 있다 |
| D9 | GPU 캡처 도구 | **RenderDoc in-app API**(헤더 `ThirdParty/renderdoc/renderdoc_app.h` — MIT), `-renderdoc` 로 시작 때 올림, 에디터 버튼 · 개발 명령 `renderdoc.capture`. PIX 는 하지 않는다 | 네 백엔드(DX11 · DX12 · Vulkan · GL)를 모두 잡는 것은 RenderDoc 뿐. 언리얼 RenderDoc 플러그인 · 유니티 Frame Capture 버튼과 같은 자리 |
| D10 | 스크린샷 버튼 형식 | **PNG**(`Saved/Screenshots/<시각>.png`, 게임 뷰 그림 — 씬 뷰 툴바의 단추는 씬 뷰 그림). 시험용 `-gv_screenshot` 은 PPM 계약 그대로 | 언리얼 HighResShot · 유니티 Game view 스크린샷 모두 PNG. PPM 은 시험 도구용 |
| D11 | 보기 모드 추가 | **Normals · Depth(선형) · Overdraw** 셋. Shader Complexity 는 하지 않는다(머티리얼 그래프가 없어 의미가 약하다) | 유니티 Scene view Draw Mode(Shaded · Wireframe · Overdraw · Normals …), 언리얼 View Mode(Lit · Unlit · Wireframe · World Normal · Scene Depth · Quad Overdraw) |
| D12 | 스레드 미니 타임라인 | **패널 안에 넣는다**(켤 때만 사건을 링 버퍼에 기록). 긴 분석은 계속 Tracy | 유니티 Profiler Timeline 이 에디터 안이다. 언리얼은 Insights 따로 — 우리는 둘 다(빠른 확인은 패널, 깊은 분석은 Tracy) |
| D13 | 버그 리포트 한 방 | 언리얼 **`BugIt` 모양**: 엔진 개발 명령 `bugit [메모]`(게임 창에서도 된다) + 에디터 버튼 → `Saved/BugIt/<시각>/`(스크린샷 · 로그 사본 · 씬 경로 · 카메라 자리 · 재현 명령 · 입력 녹화 · 시스템 정보), 재현은 `bugitgo <폴더>` | 언리얼 `BugIt` / `BugItGo` 그대로. zip 은 만들지 않는다(폴더가 첨부하기 쉽고 의존이 늘지 않는다) |
| D14 | 공용 커브 | 엔진 **`FloatCurve`**(리플렉션 값 타입 — 키 · 보간(Constant · Linear · Cubic) · 접선) + 인스펙터 그리기 확장(작은 미리보기 + 팝업 편집기). `GameCurve`(키트 데이터)는 옮기지 않는다(백로그 1-6) | 언리얼 `FRichCurve` + Curve Editor, 유니티 `AnimationCurve` + CurveField — 값 타입이 먼저 있어야 편집기가 공용이 된다 |
| D15 | 콘텐츠 브라우저 범위 | 기본은 **활성 게임 팩 + `engine/` + `common/`**, "모든 팩 보기" 토글. "어디서 쓰이나"(참조 찾기)는 텍스트 에셋을 훑는 역색인(백그라운드) | 언리얼 Content Browser 는 프로젝트 + (선택) 엔진 콘텐츠, Reference Viewer. 유니티 "Find References In Scene" 도 텍스트(YAML) 검색이다 |

**추천을 바꾸고 싶으면** 단위 표(2 절)에서 그 결정이 걸린 단위만 빼면 된다 — D1 을 바꾸면 C1 · C2 의 코드가 달라지고(EditorFramework 로 옮길 파일 표가 필요),
나머지 단위는 C 단계가 만든 등록 줄만 쓰므로 그대로다.

---

## 1. 띄워 보며 본 것 (2026-10-06, 이 PC — 화면 배율 150 %)

O1 ~ O6(창 제목 잘림, 에디터 스크린샷이 까맣던 것, Unlit 무효, 토글 잘림, 타일 이름 끊김, 편집 중인 씬이 안 보이던 것)는 E1 ~ E5 가 고쳐서 표에서 뺐습니다. 증거 파일 이름(`ed3.png` 등)은 그때 스크래치에 있던 스크린샷입니다.

| # | 본 것 | 증거 | 단위 |
|---|-------|------|------|
| O7 | 콘텐츠 브라우저가 **모든 게임 팩 폴더**(abilityarena … voxelcraft)를 보인다 — 활성 게임은 하나 | `ed3.png` | A1 |
| O8 | 인스펙터의 다중 선택은 **"Multi-Selection (N objects)" 한 줄 + 첫 오브젝트만** 편집한다 | `InspectorPanel.cpp:133` | I1 |
| O9 | `-gv_editorOpenPanel=all` 이면 모든 패널이 900×620 으로 같은 자리에 떠서 겹친다(도킹 공간 정점 0) | `ed5.log` 덤프 | 의도(덤프용 스위치 — `EditorDockLayout::beginDockspace` 주석). 고치지 않는다 |

---

## 2. 단계 · 단위 · 규모 (우선순위 순 — 위가 아래의 바닥)

★ = **가장 체감이 큰 것**(매 실행 · 매 편집에서 보이거나, 지금 손으로 하는 일을 없앤다).

| 단계 | 단위 | 무엇 | 규모 | 선행 | 체감 |
|------|------|------|------|------|------|
| **3 인스펙터 · 콘텐츠** | A1 | 콘텐츠 브라우저 — 활성 팩만 + "어디서 쓰이나" 역색인 | M | | ★ |
| **5 공용 편집 틀** | T2 | 맵 검사 패널(Map Check — 씬 규칙 · 저장 때 · 클릭하면 선택) | M | | |
| | T3 | 공용 노드 그래프 틀을 확장에 공개(`EditorGraphDocumentPanel` 내보내기 + 노드 찾아 넣기 · 핀 타입 색 · 검증 표시) | S | C1 | |
| | T4 | 패키징 창(타깃 · 프리셋 · 쿠킹 · 산출 폴더, 진행 로그) | M | 2 차 server-target 패키징 진입점 | |
| **6 로드맵 — 미룬 영역 패널을 확장 모듈로** | — | GM · 오디오 믹서 · 내비메시 · 애니메이션/리그 · 기믹 회로 그래프 · 지형 칠하기 · 설정 브라우저 · 카탈로그 편집기(F) · 다중 월드 툴 창 | (표) | C · T3 | |
| **추가 — 아이콘(12절)** | R3 | 컴포넌트와 오브젝트 아이콘(Hierarchy, 인스펙터) | M | 5b R2 | ★ |
| | R4 | 뷰포트 빌보드와 클릭 선택 | M | R3 · C2 | ★ |
| | R5 | 재생, 기즈모, 뷰포트 툴바, 커맨드 아이콘 | S | 5b R2 | |
| | R9 | 콘텐츠 브라우저 종류 아이콘과 텍스처 썸네일 | S~M | 5b R2 · A1 | |
| **추가 — 패널 부족한 점(13절)** | N1 ~ N12 | 뷰포트, Hierarchy, 콘텐츠 브라우저, 인스펙터, Output Log, 플레이, 도구 문서, Animation Graph 와 그 밖 | S ~ L | 단위마다 | ★ |

원문의 합계는 단위 29 였습니다. E1 ~ E5 를 빼고 남은 editor-plus 단위는 21 개(1 단계 5, 2 단계 4, 3 단계 4, 4 단계 4, 5 단계 4)입니다.
여기에 아이콘 단위 넷과 패널 점검의 단위 열둘(N5 처럼 작은 것은 다른 단위와 합쳐도 됩니다)이 더해집니다.

---

## 4. 단계 1 — 확장 지점(C): 키트 · 게임이 Dev 전용 확장 모듈로 패널 · 인스펙터 · 시각화 · 커맨드를 단다 ★

**상용 비교.** 언리얼은 플러그인마다 런타임 모듈 옆에 `<X>Editor` 모듈(`Type: "Editor"`)을 두고, 그 모듈의 `StartupModule` 이 `FPropertyEditorModule::RegisterCustomClassLayout`
(Details 커스터마이즈) · `FComponentVisualizer` · `UToolMenus::ExtendMenu` · 탭 스포너(`FGlobalTabmanager::RegisterNomadTabSpawner`) · Asset Editor Toolkit 을 등록한다 —
에디터 모듈들은 `UnrealEd` · `Slate` DLL 을 **그대로 링크**한다. 유니티는 `Editor` 폴더(또는 Editor 전용 asmdef)에 `EditorWindow` · `[CustomEditor]` · `PropertyDrawer` ·
`[MenuItem]` 을 두면 속성으로 등록된다. Godot 은 `EditorPlugin`(`add_inspector_plugin` · `add_control_to_dock` · `add_tool_menu_item`)이다.
세 엔진 모두 **(1) 확장은 런타임 코드와 다른 단위(Dev 전용)이고 (2) 자기 파일에서 한 줄로 등록하며 (3) 에디터 API 전체를 쓴다.** 우리 등록부(`SW_EDITOR_*`)는 (2)는 이미 되어 있고,
EditorModule 이 MODULE DLL 이라 (1) · (3) 이 안 된다 — 등록 목록이 템플릿 함수 정적이라 다른 DLL 은 자기 사본에 등록하고, 내보낸 심볼도 없다. D1 의 결정대로
EditorModule 을 언리얼 `UnrealEd` 처럼 내보내고, 키트 확장이 리로드 그래프(키트 → SWGame 과 같은 길)에 붙게 한다.

**단계 공통 규칙(README "확장 모듈" 절로 남긴다).**
- 확장 모듈은 `SW_EDITOR_PANEL` · `SW_EDITOR_INSPECTOR` · `SW_EDITOR_VISUALIZER` · `SW_EDITOR_POPUP` · `SW_EDITOR_SELF_TEST` · `SW_EDITOR_COMMAND` · (P2) `SW_EDITOR_SETTINGS` ·
  (I3) `SW_EDITOR_PROPERTY_DRAWER` 를 EditorModule 과 **똑같이** 쓴다. 등록은 EditorModule 의 목록 하나에 오른다.
- 확장 모듈이 내려가면(핫 리로드 · 종료) 그 모듈의 등록 줄을 가리키는 인스턴스(패널 · 인스펙터 · 팝업 · 커맨드)가 **이미지를 내리기 전에** 떨어진다.
- 확장 모듈의 ImGui 호출은 결속기(CMake 가 만든다)가 건 컨텍스트로 간다. ImDrawList 콜백(`AddCallback`)은 확장에서 쓰지 않는다(렌더 스레드가 늦게 부른다).

---

## 5. 단계 2 — 에디터 설정: 프로퍼티 그리드 · 환경설정 · 단축키 · 모듈 창 ★

**상용 비교.** 언리얼 Editor Preferences · Project Settings 는 `UDeveloperSettings` 파생 클래스를 등록하면 섹션이 생기고, 같은 **Details 뷰**(`IDetailsView` — 인스펙터와 같은 위젯)로 그린다.
저장은 사용자별 ini(기본에서 바뀐 것만). Keyboard Shortcuts 는 같은 창의 한 섹션(커맨드마다 키 받기 · 충돌 경고), Plugins 창은 켜고 끄면 "Restart Now".
유니티는 `SettingsProvider` 등록(Preferences · Project Settings 두 창, 같은 IMGUI/UIElements 그리기), Shortcuts Manager(키보드 그림 + 프로필, 바뀐 것만 저장, 충돌 표시),
Package Manager 가 모듈 켜기/끄기. Godot 은 Editor Settings(검색 · 섹션 트리 · "바뀐 것만 보기") · Shortcuts 탭 · Project Settings > Plugins.
공통점: **(1) 설정 = 리플렉션 객체, 그리기는 인스펙터와 같은 위젯 (2) 섹션은 등록으로 늘어난다(확장 모듈도) (3) 사용자 파일에는 바뀐 것만.**

---

## 6. 단계 3 — 인스펙터 · 콘텐츠 브라우저 ★

**상용 비교.** 언리얼 Details 는 여러 액터를 고르면 **공통 프로퍼티를 함께 고치고** 값이 다르면 "Multiple Values" 를 보이며, 기본값과 다른 프로퍼티 옆에 **노란 되돌리기 화살표**,
오른쪽 클릭 Copy/Paste(프로퍼티 글), 타입별 그리기는 `IPropertyTypeCustomization`. 유니티 Inspector 도 다중 편집(`—` 표시, `[CanEditMultipleObjects]`), 프리팹 오버라이드는 굵은 글씨 ·
"Revert", `PropertyDrawer`(`[CustomPropertyDrawer(typeof(T))]`). Godot 인스펙터도 다중 편집 · 되돌리기 아이콘 · `EditorInspectorPlugin`.
콘텐츠 쪽: 언리얼 Content Browser 는 프로젝트 콘텐츠가 기본(엔진 · 플러그인 콘텐츠는 보기 옵션), **Reference Viewer**(참조하는 것 · 참조되는 것), 이름 바꾸면 리디렉터 + "Fix Up".
유니티 Project 창 "Find References In Scene" · 의존 검색(`AssetDatabase.GetDependencies`). 우리 인스펙터는 다중 선택에서 첫 오브젝트만 고치고(O8), 되돌리기는 오른쪽 클릭 메뉴에만 숨어 있다.

### A1 콘텐츠 브라우저 — 활성 팩만 + "어디서 쓰이나" 역색인 ★

**목적.** (1) O7 — `game` 뿌리가 `Resource/game/`(게임 여덟 팩 전부)라 다른 게임 폴더가 늘 보이고 검색에도 섞인다. (2) 에셋을 지우거나 고치기 전에 **누가 이것을 쓰는가**를 볼 길이 없다
(검증 규칙 `references-exist` 는 "없는 참조" 만 본다 — 역방향이 없다).

**바꿀 것.**
1) 뿌리 — `ContentBrowserPanel::refreshRoots` 의 `addRoot( "game", … )` 를:
```cpp
        // 기본은 활성 게임 팩 하나다(언리얼 Content Browser 가 프로젝트 콘텐츠만 보이는 것과 같다). "All packs" 를 켜면 game/ 전체.
        const string& packRoot = GameConfig::getActive()._packRoot;            // "game/themepark"
        if ( _bShowAllPacks == SW_TRUE || packRoot.empty() )
            addRoot( "game", ResourceUtil::getDomainFolderPath( "game" ) );
        else
            addRoot( packRoot.c_str(), ResourceUtil::resolveResourcePath( packRoot ) );   // 경로 풀이 함수 이름은 ResourceUtil 에 맞춘다
```
툴바 끝에 체크박스 "All packs"(`_bShowAllPacks` — uint8, 바뀌면 `_bRootsDirty`). 환경설정 Viewport 처럼 "Content Browser" 섹션(P2)에 `_bShowAllPacksByDefault` 하나.
2) 역색인 — 새 `Common/Commands/EditorReferenceIndex.h` · `.cpp`(ImGui 없음):
```cpp
namespace sw::editor
{
    /** @brief 참조 하나 — @p _referrerPath 가 글 안에서 @p _targetPath 를 적었다. */
    struct EditorAssetReference
    {
        string _referrerPath; ///< 리소스 id(`game/themepark/maps/park.scene.xml`)
        string _targetPath;   ///< 리소스 id(정규화 · 소문자)
        uint32 _line{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorReferenceIndex
     * @brief 리소스 트리의 텍스트 에셋(xml · json · material · scene · prefab · uidoc …)을 훑어 "누가 무엇을 적었나" 를 모읍니다(유니티 Find References 와 같은 텍스트 검색).
     * @details 글 안의 따옴표 문자열 가운데 **실제로 있는 리소스 id**(`engine/` · `common/` · `game/` 로 시작하고 파일이 있는 것)만 참조로 셉니다 — 이름 붙은 글이 우연히 경로와 같아도
     *          파일이 없으면 세지 않는다. 바이너리(dds · mesh · bin)는 훑지 않는다. 짓기는 백그라운드(`EditorBackgroundTask`), 질의는 짓기가 끝난 뒤.
     *          파일 감시가 텍스트 에셋을 알리면 그 파일 줄만 다시 훑는다.
     */
    class SW_EDITOR_API EditorReferenceIndex
    {
    public:
        /** @brief @p resourceRoot 아래를 모두 훑어 새로 짓습니다(백그라운드 스레드에서 부른다). */
        void build( string_view resourceRoot );
        /** @brief 파일 하나의 줄을 다시 훑습니다(지워졌으면 그 파일이 적은 참조를 뺀다). */
        void refreshFile( string_view resourceRoot, string_view resourcePath );
        /** @brief @p targetPath 를 적은 참조를 모읍니다(먼저 비운다). */
        void findReferrers( string_view targetPath, vector<EditorAssetReference>& outListReference ) const;
        /** @brief @p referrerPath 가 적은 참조를 모읍니다(먼저 비운다) — "이 에셋이 쓰는 것". */
        void findDependencies( string_view referrerPath, vector<EditorAssetReference>& outListReference ) const;
        /** @brief 글 하나에서 리소스 id 후보를 뽑습니다(시험이 쓰는 반쪽). @p pfnExists 가 true 인 것만. */
        static void extractReferences( string_view referrerPath, string_view text, bool ( *pfnExists )( string_view resourcePath ), vector<EditorAssetReference>& outListReference );

    private:
        mutable mutex                _mutex;
        vector<EditorAssetReference> _listReference; ///< 대상 경로 순 정렬(질의는 이분 탐색)
    };
} // namespace sw::editor
```
콘텐츠 브라우저 오른쪽 클릭에 "Find References"(참조하는 것) · "Show Dependencies"(이것이 쓰는 것) — 결과는 작은 팝업 표(경로 · 줄), 더블클릭 = 그 에셋 열기(기존 `openAsset`).
색인은 EditorContext 가 하나 들고 에디터가 뜬 뒤 백그라운드로 짓는다(진행 중이면 "Indexing… (n files)"). 지우기(Delete) 확인 대화상자에 "이 에셋을 쓰는 곳 N 개" 를 함께 보인다.
**이름 바꾸기 + 참조 고침은 하지 않는다** — 카탈로그 편집기(F, 6 단계)가 같은 색인 위에 한다(9절 로드맵 7).

**시험 — `Test/EditorTest/Common/Commands/TestEditorReferenceIndex.cpp`(`EditorReferenceIndexTest`):**
- `ExtractKeepsOnlyExistingResourceIds` — 글 `<Mesh path="engine/models/cube.mesh"/> <Name value="engine/not/a/file"/>` 에서 있는 것만(시험 `pfnExists` 가 첫 경로만 true).
- `ReferrersAndDependenciesAreInverse` — 시험 리소스 폴더(임시 폴더에 파일 셋)로 `build` → A 가 B 를 적었으면 `findReferrers( B )` 에 A, `findDependencies( A )` 에 B.
- `RefreshFileDropsRemovedReferences` — A 를 고쳐 B 를 지우고 `refreshFile` → B 의 참조자 0.
자체 시험 `contentBrowser.showsActivePackOnly` — `game` 뿌리 줄 이름이 `_packRoot` 인지(Empty 게임이면 `game/empty`).

**확인 = 에디터 시나리오.** `contentbrowser.scenario.xml`: 탐침 `Editor.ContentRootCount.game` 이 1(활성 팩 하나)인지 보고, "All packs" 체크박스(이름표 `contentBrowser.allPacks`)를 누른 뒤 게임 팩 수만큼 늘었는지 봅니다.
이어서 색인이 끝나기를 기다린 뒤(탐침 `Editor.ReferenceIndexReady`) 쓰이는 메시 하나의 타일을 오른쪽 클릭하고 "Find References" 를 눌러 결과 수 탐침이 1 이상인지 봅니다.

**남길 교훈.** 9절 로드맵 7 카탈로그 편집기 줄에 "역색인은 `EditorReferenceIndex` 를 쓴다" 를 덧붙인다. `Source/Editor/README.md` 함정 · 계약 절에 한 줄: `- **콘텐츠 브라우저는 활성 팩이 기본**(All packs 토글). 참조 찾기는 EditorReferenceIndex — 텍스트 에셋의 따옴표 글 가운데 실제로 있는 리소스 id 만 센다.`
**커밋 메시지:**
```
에디터 - 콘텐츠 브라우저가 활성 게임 팩만 보이고 "어디서 쓰이나" 를 찾는다

문제점:
- game 뿌리가 Resource/game 전체라 다른 게임 여덟 팩이 늘 보이고 검색에 섞였다.
- 에셋을 지우거나 고치기 전에 누가 그것을 쓰는지 볼 길이 없었다(검증은 없는 참조만 본다).

해결방안:
- 뿌리를 GameConfig 의 _packRoot 로(All packs 토글, 환경설정 기본값).
- EditorReferenceIndex(ImGui 없음): 텍스트 에셋의 따옴표 글 가운데 실제로 있는 리소스 id 만 참조로, 백그라운드로 짓고 파일 감시로
  파일 줄만 갱신. 메뉴 Find References · Show Dependencies, 지우기 확인에 참조 수.

결과:
- EditorReferenceIndexTest 셋, 자체 시험 contentBrowser.showsActivePackOnly.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```
**적용 뒤 확인:** `EditorTest --test_filter=EditorReferenceIndexTest.*`, 에디터에서 쓰이는 메시 하나로 Find References(엔진 팩에는 메시가 없으니 게임 팩의 것). 색인 짓기 시간을 로그로 한 번 잰다(저장소 전체 — 수 초면 둔다, 길면 시작 지연을 백로그 1-4 에).
---

## 8. 단계 5 — 공용 편집 틀(커브 · 맵 검사 · 노드 그래프 · 패키징)

**상용 비교.** 언리얼은 값 타입(`FRichCurve` · `FRuntimeFloatCurve`)이 있고 Details 안 작은 미리보기 + Curve Editor 탭, Map Check(로드 · 빌드 때 경고 목록 — 줄을 누르면 액터 선택),
그래프 편집기는 `SGraphEditor`(블루프린트 · 머티리얼 · 애님 그래프 · 사운드 큐가 같은 틀 — 노드 찾아 넣기 · 핀 타입 색 · 컴파일 오류 표시), Project Launcher/Package Project(플랫폼 · 구성 · 쿠킹 · 진행 로그).
유니티는 `AnimationCurve` + CurveField · Curve Editor 창, Console 의 클릭 → 오브젝트 핑, GraphView(셰이더 그래프 · VFX 그래프의 공용 틀), Build Settings 창. 우리는 노드 그래프 틀이 EditorModule 안에만 있고(애님 · 대화 둘이 쓴다),
검증 결과 저장소(`ValidationIssueLog` — "맵 검사 패널이 읽는다" 고 주석에 적힌)는 있는데 패널이 없다.

### T2 맵 검사 패널 — `ValidationIssueLog` 를 보이고, 누르면 그 오브젝트로

**목적.** 검증 결과는 이미 모인다(`ObjectValidation` — 로드 · 저장 · 인스펙터 편집 때 `ValidationIssueLog` 에). 보는 곳이 경고 로그뿐이다.

**바꿀 것.**
1) `ValidationIssueLog` 에 바뀜 번호 — `uint32 getRevision() const`(바꿀 때마다 +1, 원자) — 패널이 프레임마다 목록을 다시 모으지 않게. (Engine 헤더 → 엔진 ABI 도장.)
2) `Panels/MapCheckPanel.h` · `.cpp` — `SW_EDITOR_PANEL( MapCheckPanel, "map_check", EditorPanelCategory::Tool, 2040 );` 제목 `"Map Check"`. 표: 무게(아이콘) · 오브젝트(`_sourceLabel`) · 타입 · 프로퍼티 · 메시지,
   무게 필터(Error · Warning) · 검색, 줄 클릭 = 그 오브젝트 선택(`_sourceID` = 오브젝트 id → `GameObjectManager::findGameObjectByID` → `EditorSelection::selectObject`) + 더블클릭 = 뷰포트 초점,
   프로퍼티가 있으면 인스펙터가 그 프로퍼티 줄로 스크롤(그리드에 `requestFocusProperty( name )` 한 칸 — 다음 프레임 `SetScrollHereY`).
   위 단추 "Check Map" = 활성 씬의 모든 오브젝트에 `ObjectValidation::reportGameObject( obj, false )`(오브젝트 1 만 개 기준 시간을 로그 — 길면 프레임 예산으로 나눈다).
3) 상태줄(메뉴바 오른쪽 "Ready · RHI" 옆)에 `ICON_FA_TRIANGLE_EXCLAMATION N` — 오류가 있으면 빨강, 누르면 패널. 씬을 열었을 때 오류가 있으면 토스트 "Map Check: N errors"(언리얼과 같다).
**시험.** 판단(필터 · 정렬 · 무게별 수)은 ImGui 없는 `MapCheckRows::build( listIssue, filter, outListRow )` → `MapCheckRowsTest`(EditorTest). 자체 시험 `mapCheck.selectsIssueObject` — 검증 함수가 있는 시험 컴포넌트
(EngineTest 의 검증 시험이 쓰는 타입이 EditorModule 에 없으면 `ValidationIssueLog::replaceIssues` 로 가짜 결과를 넣는다)를 놓고 줄 클릭 → 선택이 그 오브젝트.
**확인 = 에디터 시나리오.** `mapcheck.scenario.xml`: 검증 오류가 있는 시험 씬을 열고 상태줄의 경고 수(이름표 `statusBar.mapCheck`)를 눌러 패널을 연 뒤, 첫 줄(이름표 `mapCheck.row.0`)을 누르면
탐침 `Editor.SelectionCount` 가 1 이고 선택된 오브젝트 이름이 오류 오브젝트인지 봅니다. 시험 씬은 `Resource/engine/` 아래의 에디터 시험 씬에 오류 하나를 일부러 둔 사본입니다.

**남길 교훈.** 없음. **커밋 메시지:**
```
에디터 - Map Check 패널(ValidationIssueLog 표 · 클릭하면 오브젝트 선택 · Check Map · 상태줄 수)

문제점:
- 로드 · 저장 · 편집 때 검증 결과가 ValidationIssueLog 에 모이는데 보는 곳이 경고 로그뿐이었다. 어느 오브젝트의 어느 프로퍼티인지 찾아가는 길이 없었다.

해결방안:
- ValidationIssueLog::getRevision(바뀔 때만 다시 모은다).
- Map Check 패널: 무게 · 오브젝트 · 타입 · 프로퍼티 · 메시지, 필터 · 검색, 클릭 = 선택, 더블클릭 = 초점 + 인스펙터 그 줄로, Check Map(활성 씬 전부).
  상태줄의 경고 수(누르면 패널), 씬을 열 때 오류가 있으면 토스트.

결과:
- MapCheckRowsTest, 자체 시험 mapCheck.selectsIssueObject.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```

### T3 공용 노드 그래프 틀을 확장에 공개 — 노드 찾아 넣기 · 핀 타입 색 · 검증 표시

**목적.** `EditorGraphDocumentPanel<AssetType>` · `EditorNodeGraph` 는 애님 · 대화 두 패널이 쓰는 틀이다. 6 단계의 기믹 회로 그래프(키트 · 기반 확장 모듈)가 쓰려면 내보내야 하고,
상용 그래프 편집기의 기본 셋(빈 곳 오른쪽 클릭 → **검색해 노드 넣기**, 핀 타입마다 색 · 맞지 않는 핀 연결 거절, **오류 노드 빨간 테두리 + 툴팁**)이 틀에 있어야 패널마다 다시 짜지 않는다.

**바꿀 것.**
- `EditorNodeGraph`(클래스) · `EditorNodeGraphId` 도우미에 `SW_EDITOR_API`. `EditorGraphDocumentPanel` 은 템플릿이라 헤더 그대로(확장이 인스턴스화).
- `EditorNodeGraph` 에 셋:
```cpp
        /** @brief 노드 종류 하나 — 찾아 넣기 목록의 줄입니다. */
        struct NodeKindEntry
        {
            const utf8* _pName;     ///< 보이는 이름
            const utf8* _pCategory; ///< 묶음
            uint32      _kindId;    ///< 패널이 정한 값 — 고르면 돌려준다
        };
        /** @brief 빈 곳 오른쪽 클릭이면 검색 팝업을 그리고, 고르면 true 와 그 종류 · 캔버스 좌표를 돌려줍니다. */
        bool drawAddNodePopup( const vector<NodeKindEntry>& listKind, uint32& outKindId, float2& outCanvasPosition );
        /** @brief 핀 타입 색 표를 둡니다(타입 id → 색). 연결 시도 때 두 핀의 타입이 다르면 거절하고 이유를 그 자리에 보입니다. */
        void setPinTypeColors( const vector<Color4>& listColor );
        /** @brief 이번 프레임의 오류 노드입니다(빨간 테두리 + 툴팁). 패널이 검증한 결과를 넘긴다. */
        void setNodeIssues( const vector<EditorGraphNodeIssue>& listIssue );
```
  애님 · 대화 패널이 찾아 넣기를 쓰게 바꾼다(지금 노드 넣기 메뉴를 이것으로 — 동작은 같고 검색이 생긴다).
- 판단(검색 일치 · 묶음 정렬 · 핀 타입 호환)은 ImGui 없는 `EditorNodeGraphSearch` · `EditorPinTypeUtil` → EditorTest.
**시험.** `EditorNodeGraphSearchTest`(부분 일치 · 묶음 순서 · 빈 검색은 전부) · `EditorPinTypeUtilTest`(같은 타입 · 와일드카드 · 거절 이유). 자체 시험 `dialogueGraph.addNodeBySearch`(2 차 단위 8 입력 창구 — 빈 곳 오른쪽 클릭 → 글자 → Enter → 노드 수 +1).
**확인 = 에디터 시나리오.** `graphaddnode.scenario.xml`: Dialogue Graph 를 시험 문서로 열고 캔버스 빈 곳(이름표 `graph.canvas`)을 `EditorClick button="1"` 로 오른쪽 클릭한 뒤,
`EditorText` 로 노드 이름 일부를 치고 `EditorKey key="Enter"` 를 보내 탐침 `Editor.GraphNodeCount` 가 하나 늘었는지 봅니다. 같은 시나리오를 Animation Graph 로 하나 더 둡니다(13절 N8 이 이어 씁니다).

**남길 교훈.** 없음(남는 기믹 회로 그래프는 9절 로드맵 6). **커밋 메시지:**
```
에디터 - 노드 그래프 틀을 확장에 내보내고 찾아 넣기 · 핀 타입 색 · 오류 노드 표시를 틀에 둔다

문제점:
- 노드 그래프 틀(EditorNodeGraph · EditorGraphDocumentPanel)이 EditorModule 안에만 있어 확장 모듈이 쓸 수 없었고, 노드 넣기는 패널마다
  메뉴였으며 핀 타입 · 오류 노드 표시가 없었다.

해결방안:
- EditorNodeGraph 내보내기. drawAddNodePopup(검색 · 묶음), setPinTypeColors(색 · 맞지 않는 연결 거절 + 이유), setNodeIssues(빨간 테두리 + 툴팁).
  판단은 ImGui 없는 EditorNodeGraphSearch · EditorPinTypeUtil.
- 애님 · 대화 그래프가 찾아 넣기를 쓴다.

결과:
- EditorNodeGraphSearchTest · EditorPinTypeUtilTest, 자체 시험 dialogueGraph.addNodeBySearch.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```

### T4 패키징 창 — 타깃 · 프리셋 · 쿠킹 · 산출 폴더 · 진행 로그

**목적.** 배포본 만들기가 손 절차(Shipping 프리셋 구성 · 빌드 → 쿠킹 → 폴더에 모으기)다. 언리얼 Package Project · 유니티 Build Settings 처럼 창 하나에서 고르고 누른다.
**선행:** 패키징 **진입점 하나**(명령줄 — CI 와 창이 같이 쓴다). 2 차 server-target 정정 결정("패키징 때 타깃별로 에셋 종류를 뺀다")을 적용할 때 그 진입점이 생겼으면 그것을 부른다.
지금은 없으므로 이 단위가 먼저 `Scripts/dev/MakePackage.py` 를 만든다:
```
py -3 Scripts/dev/MakePackage.py --target Client|Server --game <프리셋 이름> [--rhi <백엔드>] [--output Saved/Packages] [--skip-build] [--skip-cook]
  1) cmake --preset Ninja-Shipping(게임 · RHI 캐시 값) → cmake --build   (진행 줄: "[package] step 1/4 build")
  2) Scripts/generate/CookAssets.py --all --output <스테이징>/Cooked        ("[package] step 2/4 cook")
  3) 스테이징: Bin 의 exe · DLL(Shipping 은 한 exe) + 팩 + 설정(Config/Game/<게임>.json · 서버면 서버 설정) + THIRD_PARTY_NOTICES,
     타깃별 제외 표(server-target 결정 — Server: 텍스처 · 셰이더 바이너리 · 오디오)    ("[package] step 3/4 stage")
  4) 검사: 스테이징의 exe 를 --version · --check-pack 으로 한 번 띄운다             ("[package] step 4/4 verify")
  끝: "[package] done <폴더> <크기>" · 실패면 "[package] FAILED <단계> <이유>" 와 종료 코드
```
창 — `Panels/PackagingPanel.h` · `.cpp` `SW_EDITOR_PANEL( PackagingPanel, "packaging", EditorPanelCategory::Tool, 2050 );` 제목 `"Packaging"`: 타깃(Client · Server) · 게임(활성 기본) · RHI(쿠킹 표의 이름) · 산출 폴더 ·
"Skip build/cook" 체크 · "Package" 단추 → 진입점을 새 프로세스로(`EditorExternalToolJob`), `[package] step k/n` 줄로 진행 막대, 로그 창(오류 줄 빨강 · 더블클릭 IDE), 끝나면 "Open Folder".
줄 읽기는 ImGui 없는 `PackagingProgressParser` → EditorTest.
**시험.** `PackagingProgressParserTest`(단계 · done · FAILED 줄), 진입점은 `PythonTest_TestPackage`(파이썬 시험 — `--skip-build --skip-cook` 로 스테이징 · 제외 표만, 임시 폴더). 실제 Shipping 패키징은 손 확인(시간이 길다).
**확인 = 에디터 시나리오.** `packaging.scenario.xml`: Packaging 창을 열고 "Skip build" 와 "Skip cook" 을 켠 뒤 "Package" 를 눌러 진행 탐침 `Editor.PackagingState` 가 끝(성공)이 될 때까지 기다립니다.
빌드와 쿠킹을 건너뛰므로 스테이징만 돌아 수십 초 안에 끝납니다. 실제 Shipping 패키징은 `PythonTest_TestPackage` 와 손 확인으로 둡니다.

**남길 교훈.** 없음. **커밋 메시지:**
```
에디터 · 스크립트 - 패키징 진입점(Scripts/dev/MakePackage.py)과 Packaging 창

문제점:
- 배포본 만들기가 Shipping 구성 · 빌드 → 쿠킹 → 폴더 모으기의 손 절차였고, 창도 진입점도 없었다.

해결방안:
- Scripts/dev/MakePackage.py: 빌드 · 쿠킹 · 스테이징(타깃별 제외 표 · 설정 · 서드파티 고지) · 검사, 단계마다 "[package] step k/n" 줄과 종료 코드.
- Packaging 창: 타깃 · 게임 · RHI · 산출 폴더 · 건너뛰기, 새 프로세스로 실행하고 단계 줄로 진행 · 로그 · Open Folder. 줄 읽기는 ImGui 없는
  PackagingProgressParser.

결과:
- PackagingProgressParserTest, PythonTest_TestPackage(스테이징 · 제외 표).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```

---

## 9. 단계 6 — 로드맵: 미룬 영역 패널을 확장 모듈로 (단위 없음)

C 단계 뒤에는 "에디터 패널이 그 영역 코드 옆에 산다" 가 된다 — 엔진 영역(Engine)의 패널은 EditorModule 안, 기반 GameFramework 영역은 **`GF_Editor_Base`**(GameFramework 기반의 에디터 확장 —
`Source/GameFramework/Base/Editor/`), 키트 영역은 `GF_Editor_<키트>`. 순서는 체감 · 선행 순.

| 순 | 패널 | 어디에 | 규모 | 선행 | 상용 비교 |
|----|------|--------|------|------|-----------|
| 1 | **프로젝트 설정 창**(설정 브라우저 — 2 차 config-docs 의 생성 문서 대신 편집) | EditorModule — P2 의 그리드로 `Config/Engine` · `Config/Game` 리플렉션 설정을 읽고 쓰기(기본과 다른 값만 — config-docs D1) | M | P1 · P2 · config-docs | 언리얼 Project Settings · 유니티 Project Settings |
| 2 | **물리 셰이프 시각화 · 기즈모**(백로그 1-2 강체 물리 (3)) | EditorModule 시각화(`SW_EDITOR_VISUALIZER`) + 콜라이더 크기 손잡이 | S | C2 | 언리얼 컴포넌트 비주얼라이저 · 유니티 Edit Collider |
| 3 | **내비메시 보기 · 베이크 단추**(백로그 1-2 내비메시 (4)) | `GF_Editor_Base` — 시각화(타일 · 폴리 · 링크) + 커맨드 `navigation.bake` | S | C5 모양 | 언리얼 `P` 키 내비 표시 · Build Paths, 유니티 Navigation 창 |
| 4 | **오디오 믹서 패널**(백로그 1-6 오디오) | EditorModule — 버스 미터 · 음소거/솔로 · 볼륨, 이벤트 라이브러리 다시 읽기 | S~M | — | 언리얼 Audio Mixer · 유니티 Audio Mixer 창 |
| 5 | **GM 도구 패널**(online-ops D4) | `GF_Editor_Admin`(온라인 GF_Admin 의 확장) — `AdminClient` 로 조회 · 지급 · 제재 · 감사 열람 | M | C · online-ops | (상용 엔진 밖 — 운영 도구) |
| 6 | **기믹 회로 그래프 편집 창**(백로그 1-6 기믹) | `GF_Editor_Base` — T3 의 틀, 노드 · 배선 · 검증 오류 · 대상 오브젝트 고르기 | M~L | T3 | 언리얼 블루프린트 그래프 · 유니티 Visual Scripting |
| 7 | **카탈로그 편집기(F)** | EditorModule — 카탈로그 계약 하나 · enum 이름 표 · DataTablePanel 확장 · 저장 시 검증 · A1 역색인으로 이름 바꾸기 참조 고침 | L | A1 · P1 | 언리얼 DataTable/DataAsset 편집 + Fix Up Redirectors |
| 8 | **지형 칠하기**(백로그 1-3 환경) | EditorModule — 뷰포트 도구 모드(올리기 · 깎기 · 다듬기 · 레이어 칠하기), 높이장 · 스플랫 저장 | L | 뷰포트 도구 모드 틀 | 언리얼 Landscape 모드 · 유니티 Terrain 도구 |
| 9 | **다중 월드 툴 창** 과 그 위의 애니메이션 · 리그 · 프리팹 · 머티리얼 미리보기 창 | Engine(다중 월드) + EditorModule(툴 창 틀 — 창마다 선택 · Undo 범위) | L | 카메라 4 단계 | 언리얼 FPreviewScene · Asset Editor Toolkit, 유니티 Prefab Stage |
| 10 | **UI 문서 디자이너**(4 차 8-5 다음) | EditorModule — 팔레트 → 끌어 놓기 · 앵커 손잡이 | L | 4 차 · 9 | 언리얼 UMG 디자이너 · 유니티 UI Builder |

백로그에서 옮긴 세부 사항입니다.

- **1 프로젝트 설정 창.** 생성 메타데이터 `docs/Config/ConfigReference.json`(파일, 필드, 타입, 기본값, 범위, 설명)을 읽어 `Config/` 와 팩 설정 파일을 찾아 엽니다.
  고친 파일은 `ConfigManager::reloadConfigFile` 로 다시 읽고, 기본값과 같은 값은 파일에서 지웁니다(설정 파일에는 다른 값만).
- **5 GM 도구 패널.** GF_Admin 의 `AdminClient` 로 조회, 지급, 제재, 감사 열람을 합니다.
- **7 카탈로그 편집기.** 카탈로그 계약 하나가 `ResourceDataSchemaTest` 의 종류 테이블을 대체합니다. enum 이름 테이블은 `CityCatalog.cpp` 의 하드코딩 개수를 포함해 25 곳에 흩어져 있습니다.
- 에디터에 이미 있는 것(전역 변수 테이블, 커맨드 팔레트, 핫 리로드, Undo, Play 와 한 프레임 진행, InputReplay, 기즈모, 미니덤프, RenderTargetPanel, 프로파일러 패널과 Tracy 연결)은 다시 만들지 않습니다.

이 표는 옛 백로그 1-4 의 "에디터 · 개발 편의 기능" 묶음과 대기열의 에디터 패널 이름들을 대신합니다. 단계 1 ~ 5 가 끝나도 이 표에 남은 줄은 [백로그](../06_Backlog.md) 1-4 로 옮기고 이 문서를 지웁니다.

---

## 10. 적용 순서 · 겹치는 파일 · 확신 수준

**순서(editor-plus 20 커밋):** C1 → C2 → C3 → C4 → C5 → P1 → P2 → P3 → P4 → I1 → I2 → I3 → A1 → G1 → G4 → H2 → H3 → T1 → T2 → T3 → T4.
12절과 13절의 단위는 선행 칸을 지키며 사이에 끼웁니다. 아이콘 R3 은 C 단계보다 먼저 넣어도 되고, R4 는 C2(시각화 켬/끔을 id 로) 뒤가 깔끔합니다.

- **기계적 · 빌드 한 번 묶음:** C1(MODULE → SHARED · 내보내기 표) · P1(이동 표)은 커밋만 나누고 빌드는 각 단계 끝.
- **전체 빌드가 필요한 단위(Core · Engine 헤더 — 엔진 ABI 스탬프):** C2(`ModuleUnloadListener.h`) · C4(`ModuleCatalog.h`) · G1(`FrameRendererUtil.h`) · G4(`FrameProfiler`) · T1 · T2(`ReflectionValidation.h`).
- **셰이더 쿠킹:** G1 뒤(`App.exe --cook-shaders` — 매니페스트 · 바이너리 커밋, 충돌 나면 고르지 말고 다시 쿠킹한다).
- **re-configure:** C1 · C4 · C5 · P2(새 `REFLECT` 헤더) · T1.
- **자체 시험 기대 목록**(`AppSmokeTest.EditorSelfTestsPassInsideTheEditor`)에 더하는 id: `themepark.extensionPanelDraws` · `themepark.layoutPreviewLoads` · `preferences.searchFiltersSections` ·
  `shortcuts.captureAssignsCombo` · `modules.panelListsKits` · `inspector.multiEditAppliesToAll` · `contentBrowser.showsActivePackOnly` · `testRunner.runsSelfTestInPlace` · `curve.dragKeyRecordsOneUndo` ·
  `mapCheck.selectsIssueObject` · `dialogueGraph.addNodeBySearch`(11). 입력 흉내(`EditorSelfTestInput`)는 이미 있다.
- **에디터 시나리오**: 단위마다 하나씩 더한다(머리말의 "확인 = 에디터 시나리오"). `AppScenarioTest` 가 모두 돌리므로 시나리오가 늘면 `AppTest` 의 `HOST_SHARDS` 를 늘린다.
- **검증(묶음 끝 한 번):** Debug 빌드 경고 0, `ctest -L nogpu`, `ctest -L lint`, Shipping `-L hostgpu`(G1 의 RenderPassGPUTest 네 백엔드, 에디터 시나리오), 에디터 실행 넷(`-dx12 · -dx11 · -vk · -gl -EnableEditor -gv_profileFrames=40`) `[Error]` 0,
  자체 시험 전부, **핫 리로드 둘(C5 의 확인)**, 리눅스는 CI 로 확인(C1 SHARED · C4 CMake). 게임별 빌드(ThemeParkTycoon 프리셋)에서 `GF_Editor_ThemePark` 가 빌드되는지.

**겹치는 파일.** 원문이 적은 다른 제안서(2차 ~ 4차)는 모두 main 에 들어갔으므로, 겹침은 이 문서 안의 단위끼리만 봅니다.

| 파일 | 단위 | 처리 |
|---|---|---|
| `InspectorPanel.cpp` | P1(이동) · I1 · I2 · R3 · N4 | P1 이동을 먼저 넣는다 |
| `HierarchyPanel.cpp` | R3 · N2 · N12 | R3(라벨 줄) 뒤에 N2 · N12 |
| `EditorViewportClient.cpp` · `EditorViewportToolbar.*` | C2 · G1 · R4 · R5 · N1 | C2(마스크 → id) 먼저. R4 의 `getMaskBitById` 는 C2 의 `EditorVisualizerToggles` 로 바꿔 쓴다 |
| `ContentBrowserPanel.*` | A1 · R9 · N3 | A1 의 역색인 뒤에 N3(이름 바꾸기 · 옮기기) |
| `ModuleHost.cpp` · `ModuleCatalog.*` · `ModuleManifest.cmake` · `ModuleTargets.cmake` | C4 · P4 | 게이트(`CheckModuleTargets`)는 한 커밋에서 |
| `EditorCommandGUI.cpp` · `EditorCommandRegistry.*` | C3 · P3 · R5 · 여러 단위의 표 한 줄 | 표 줄은 메뉴 순서 값이 겹치지 않게(`validate` 가 잡는다) |
| `EditorSelfTestCases.cpp` · `AppSmokeTest` 기대 목록 | 11 줄 | 줄 더하기 — 순서 키 겹침만 본다 |
| `AnimGraphPanel.cpp` | T3 · R5 · N8 | T3 → N8 |

**확신 수준.**
- **실행으로 확인할 것:**
  - C 단계 — (a) 확장이 EditorModule 보다 먼저 언로드되는지(종료 · 리로드): 순서가 틀리면 등록자 소멸이 지운 목록을 만진다 → `ModuleHost::shutdown` 에서 확장 이름을 먼저 언로드한다.
    (b) 결속기: `themepark.extensionPanelDraws` 가 PASS(정점 > 0)면 맞다. 실패하면 확장 DLL 의 `GImGui` 가 null 이거나 다른 컨텍스트 — 결속 소스가 생성 · 링크됐는지(`<모듈>UIBinder.cpp`).
    (c) EditorModule SHARED 의 리눅스 링크(CI).
  - R4 — 오브젝트가 수천 개인 씬(`-gv_benchMeshes=8000 -EnableEditor`)에서 빌보드 수집이 1 ms 를 넘는지.

---

## 11. 남길 교훈 요약 (단위 커밋마다 그 줄만)

| 단위 | 남길 곳 | 남길 것 |
|---|---|---|
| C1 ~ C5 | `Source/Editor/README.md` 함정 · 계약, `Source/Engine/Module/README.md` 함정 · 계약 | 확장 모듈 위치, 등록 세대, 언로드 리스너, 결속기, `SW_EDITOR_COMMAND`, EditorExtension 종류 |
| P2 · P3 · P4 | 에디터 README, 모듈 README | 에디터 설정은 환경설정 섹션, 사용자 단축키 덮어쓰기, 매니페스트 내용도 configure 의존 |
| I1 ~ I3 | 에디터 README | 리플렉션 그리기는 `EditorPropertyGrid`, 타입 그리기 확장은 `SW_EDITOR_PROPERTY_DRAWER` |
| A1 | 에디터 README | 활성 팩 기본, 참조 찾기는 `EditorReferenceIndex` |
| T1 | [백로그](../06_Backlog.md) 1-6 | `GameCurve` 는 `FloatCurve` 로 옮기지 않음(남은 일) |
| R3 · R4 | 에디터 README | 컴포넌트 아이콘 테이블, 빌보드 클릭이 레이 피킹보다 먼저 |
| 9절 로드맵 | [백로그](../06_Backlog.md) 1-4 | 이 문서를 지울 때 남은 로드맵 줄을 옮긴다 |

---

## 12. 추가 단위 — 아이콘(editor-res 의 R3, R4, R5, R9)

editor-res 제안서(2026-10-07)는 에디터 리소스를 아홉 단위로 나눴습니다. 5b 는 아이콘 폰트와 Font Awesome 교체(R1, R2), 누락 텍스처(R6), 프로토타입 격자(R7), 앱 아이콘(R8)을 넣고,
아래 넷을 이 계획으로 넘겼습니다. 아이콘은 모두 5b 의 R1 이 만든 아이콘 폰트(`Resource/editor/fonts/sweditoricons.ttf`, 글리프 이름은 `editoricon::k*`)를 씁니다.
새 아이콘이 필요하면 `Scripts/common/EditorIconFont.py` 에 그리기 함수를 더하고 `Scripts/generate/GenerateEditorIcons.py` 를 실행합니다(5b R1 이 둔 스크립트입니다).

### R3 컴포넌트와 오브젝트 아이콘(Hierarchy, 인스펙터) ★

**무엇.** 컴포넌트 타입에서 아이콘과 색을 찾는 테이블 `EditorComponentIcon`(`Source/Editor/Common/GUI/EditorComponentIcon.h`, 새 파일)을 두고, Hierarchy 의 오브젝트 줄과 컴포넌트 줄, 인스펙터의 컴포넌트 카드 머리에 붙입니다.
찾는 순서는 셋입니다. 먼저 타입 이름 테이블을 타입 자신부터 부모 타입으로 올라가며 찾고, 없으면 리플렉션 `Category` 테이블, 그래도 없으면 `editoricon::kComponent` 입니다.
에디터는 GameFramework 를 링크하지 않으므로 타입 포인터가 아니라 짧은 타입 이름(`TypeInfo::_name`)으로 맞춥니다. 행마다 빌보드 여부(`_bBillboard`, 메시가 없는 종류)를 둬서 R4 가 같은 테이블을 씁니다.
오브젝트 줄은 빌보드 종류 컴포넌트가 있으면 그 아이콘(전구, 카메라)을, 없으면 `kGameObject` 를 보입니다. `[Category]` 배지는 그대로 둡니다.

**왜.** 지금 Hierarchy 는 이름과 `[Category]` 글자만 보여서 빛, 카메라, 트리거를 한눈에 구분할 수 없습니다. 인스펙터 카드 머리도 이름뿐입니다.

**상용 비교.** 언리얼 Outliner 는 액터 클래스 아이콘을, Details 는 컴포넌트 아이콘을 보입니다. 유니티 Inspector 의 컴포넌트 머리와 Godot 씬 트리의 노드 아이콘도 같습니다.
키트와 게임이 자기 컴포넌트 아이콘을 등록하는 길은 C 단계(확장 모듈) 뒤에 등록 매크로로 더합니다.

**테스트.** `EditorComponentIconTest`(EditorTest, ImGui 없음): 파생 타입이 기반 행보다 먼저 맞는지(`PointLightComponent` → `kLightPoint`), 모르는 타입은 `kComponent` 인지, 테이블의 Engine 타입 이름이 레지스트리에 있는지(타입 이름을 바꾸면 행이 조용히 죽습니다).

**확인 = 에디터 시나리오.** `componenticons.scenario.xml`: 빛과 카메라가 있는 시험 씬을 열고, 탐침 `Editor.HierarchyRowIcon.<이름>`(그 줄에 그린 글리프 코드포인트, 이 단위가 등록)이 빛 줄은 `kLightPoint`, 카메라 줄은 `kCamera` 인지 봅니다.
`Screenshot` 으로 Hierarchy 영역을 찍어 둡니다(눈으로 볼 기록).

### R4 뷰포트 빌보드와 클릭 선택 ★

**무엇.** `EditorViewportBillboard`(`Source/Editor/Viewport/EditorViewportBillboard.h`, 새 파일)가 빌보드 종류 컴포넌트가 붙은 오브젝트마다 하나씩, 그 월드 위치에 둥근 배지와 아이콘 글리프를 고정 화면 크기(지름 26 px × DPI 배율)로 그립니다.
그리기와 클릭이 같은 수집 함수(`collect`)를 씁니다. 시각화 등록 id 는 `billboard`(툴바 "Icons", 기본 켬)입니다.
`EditorViewportClient::processPicking` 은 레이 피킹보다 먼저 빌보드를 화면 거리로 찾아, 맞으면 그 오브젝트와 컴포넌트를 고릅니다. 활성 카메라의 오브젝트는 화면 가운데를 가리므로 건너뜁니다.

**왜.** 빛, 카메라, 오디오, 트리거, 내비 오브젝트는 메시가 없어서 뷰포트에 보이지 않습니다. 피킹 테이블이 메시, 스프라이트, 콜라이더만 집어서 클릭으로 고를 수도 없고, 지금은 Hierarchy 에서만 고를 수 있습니다.

**상용 비교.** 언리얼의 `UBillboardComponent`(에디터 전용 스프라이트), 유니티의 Gizmo 아이콘, Godot 의 3D 기즈모 아이콘(단색 + 노드 색)에 해당합니다. 세 엔진 모두 아이콘을 눌러 고릅니다.

**주의.** 원문은 시각화 마스크 비트(`getMaskBitById`)로 켬 여부를 봤습니다. C2 가 마스크를 `EditorVisualizerToggles`(id)로 바꾸므로 그것을 씁니다.
오브젝트가 수천 개인 씬에서 매 프레임 모든 컴포넌트를 훑으면 비쌀 수 있습니다. `-gv_benchMeshes=8000 -EnableEditor` 로 에디터 UI 프레임 시간을 전후로 측정하고, 1 ms 를 넘으면 빛, 카메라, 오디오처럼 레지스트리가 있는 종류만 훑습니다.

**테스트.** 자체 시험 `viewport.billboardPick`(빈 씬에 `PointLightComponent` 하나, 그 화면 위치를 클릭하면 선택이 그 오브젝트). 변이 검사: `processPicking` 의 빌보드 블록을 빼면 실패해야 합니다.

**확인 = 에디터 시나리오.** `billboardpick.scenario.xml`: 빛 하나가 있는 시험 씬을 열고, 빌보드가 남긴 이름표(`viewport.billboard.<이름>`)를 `EditorClick` 으로 누른 뒤 탐침 `Editor.SelectionCount` 가 1 이고 선택이 그 빛 오브젝트인지 봅니다.
툴바의 Icons 체크박스를 끄면 이름표가 사라지는지도 봅니다.

### R5 재생, 기즈모, 뷰포트 툴바, 커맨드 아이콘

**무엇.** 글자 버튼에 아이콘을 붙입니다. 상단 툴바(`EditorPlayToolbar`)의 Play, Simulate, Pause, Step, Stop 과 도구 패널(Animation Graph, Sequencer)의 재생 버튼은 아이콘과 글자(`EditorThemeUtil::makeIconLabel`)로 바꿉니다.
기즈모 모드(Translate, Rotate, Scale 라디오)와 뷰포트 툴바 토글(2D/3D, Stats, Grid, Cube, Surf, Bookmarks, Align)은 아이콘 토글 버튼과 툴팁으로 바꿉니다. 토글 버튼은 5b E4 가 둔 공용 위젯을 씁니다.
시각화 등록 줄에 아이콘 필드를 하나 더하고, 커맨드 표에서 아이콘이 빈 13 개를 채웁니다(맞는 것이 없으면 비워 둡니다). 아이콘 버튼이 되면 툴바 폭이 줄므로 숨김 임계값(320, 420, 520 px)을 스크린샷을 보고 낮춥니다.

**왜.** 글자 버튼은 좁은 뷰포트에서 잘리고, 커맨드 27 개 중 13 개가 아이콘이 없어 메뉴 줄이 들쭉날쭉합니다.

**상용 비교.** 세 엔진 모두 재생, 일시정지, 정지, 한 프레임, 이동, 회전, 크기, 로컬/월드, 스냅을 아이콘 버튼으로 둡니다.

**확인 = 에디터 시나리오.** `toolbaricons.scenario.xml`: 창을 960 px 로 줄인 상태(`ResizeWindow`)에서 상단 툴바의 Play 버튼 이름표 `toolbar.play` 를 눌러 Play 상태 탐침이 1 이 되는지,
씬 뷰를 400 px 로 좁혀 기즈모 토글(이름표 `gizmo.rotate`)을 눌러 기즈모 모드 탐침이 Rotate 인지 봅니다. 버튼이 잘려 숨겨지면 이름표가 없어 시나리오가 집니다.

### R9 콘텐츠 브라우저 종류 아이콘과 텍스처 썸네일

**무엇.** 두 부분입니다.
1. 썸네일을 그리는 동작이 없는 종류(Mesh, Skeleton, AnimClip, Rig, Heightfield, Data)는 지금 회색 사각형 위에 종류 글자를 씁니다. 대신 종류 아이콘 글리프를 타일 크기의 절반으로, 종류 색으로 가운데에 그립니다.
   가운데 맞춤은 R4 의 글리프 사각형 계산을 `EditorThemeUtil::drawCenteredGlyph` 로 떼어 함께 씁니다. 손그림 썸네일이 있는 종류(머티리얼, 씬, 프리팹, 셰이더, 오디오)는 그대로 둡니다.
2. 텍스처 썸네일은 지금 실제 텍스처가 아니라 체크무늬 위의 해와 산 그림입니다. 실제 텍스처를 보이려면 먼저 `TextureCache::acquire` 를 UI 스레드에서 불러도 되는지(업로드 큐 경로)를 확인합니다.
   그다음 썸네일 캐시(경로 → ImGui 텍스처 id, LRU 64 개, 해제는 `EditorDrawReleaseQueue`)를 두고, `IEditorAssetTypeActions::drawThumbnail` 에 경로 인자를 더합니다.

**왜.** 글자 대체는 종류를 한눈에 가리기 어렵고, 텍스처 폴더는 썸네일이 모두 같아 고를 수가 없습니다.

**상용 비교.** 언리얼 Content Browser 의 클래스 썸네일과 텍스처 미리보기, 유니티 Project 창 아이콘과 미리보기, Godot FileSystem 아이콘과 같습니다.

**확인 = 에디터 시나리오.** `contentthumbnails.scenario.xml`: 콘텐츠 브라우저를 타일 보기로 `engine/textures/` 를 열고 몇 프레임 기다린 뒤, 탐침 `Editor.ThumbnailCacheCount` 가 1 이상인지(실제 텍스처가 올라갔는지),
`Editor.ThumbnailFallbackGlyphCount` 가 Data 같은 종류에서 0 보다 큰지 봅니다. `Screenshot` 을 찍어 둡니다.

---

## 13. 추가 단위 — 패널 점검의 부족한 점(N1 ~ N12, Animation Graph)

패널 점검(panel-audit, 2026-10-07)은 에디터 패널을 모두 띄워 결함과 부족한 점을 찾았습니다. 결함(D1 ~ D26)은 5b 가 고치고, 상용 엔진과 비교해 빠진 기능은 이 계획으로 넘겼습니다.
아래 순서는 사용자가 체감하는 순서입니다. editor-plus 단위와 겹치는 부분은 그 단위를 적었습니다.

### N1 뷰포트 조작 기본기 ★(M, 선행 5b D1 · D2)

**무엇.** W/E/R 단축키로 기즈모 모드를 바꾸고 Q(유니티)나 스페이스(언리얼)로 순환합니다. 고른 오브젝트에 선택 외곽선을 그립니다. 후처리 외곽선 패스(`postoutline.hlsl`)가 이미 있으니 선택 id 를 넘기는 경로만 더합니다.
오른쪽 버튼을 누른 채 휠로 비행 속도를 바꾸고, 위, 앞, 옆 직교 보기와 뷰포트 최대화(언리얼 F11, 유니티 Shift+Space)를 더합니다. 보기 모드는 G1 입니다.

**왜.** 지금 기즈모 모드는 툴바와 인스펙터의 라디오로만 바뀌고, 고른 오브젝트를 화면에서 알아볼 표시가 없습니다. 비행 속도는 툴바 슬라이더뿐이고 직교 보기는 2D/3D 토글뿐입니다.

**상용 비교.** 유니티, 언리얼, Godot 모두 W/E/R 와 선택 외곽선(언리얼 노란색, 유니티 주황색), 우클릭 중 휠 속도 조절, 직교 보기를 기본으로 둡니다.

**확인 = 에디터 시나리오.** `viewportbasics.scenario.xml`: 오브젝트를 고르고 씬 뷰(이름표 `sceneView.canvas`)를 눌러 포커스를 준 뒤 `EditorKey key="E"` 로 기즈모 모드 탐침이 Rotate 인지, 선택 외곽선이 켜졌는지(`Screenshot` 과 `ExpectImage` 로 선택 테두리 영역의 색 지표)를 봅니다.

### N2 Hierarchy 편집 기본기 ★(M, 선행 5b D7 · D8)

**무엇.** 형제 순서를 끌어서 바꾸고, Shift 범위 선택, Ctrl+A, 화살표 탐색, 오브젝트 복사와 붙여넣기(Ctrl+C/V, 씬 사이 포함)를 더합니다. 뷰포트에서 고르면 계층에서 펼치고 그 줄로 스크롤합니다.
끌어 바꾸기를 하려면 형제 순서를 데이터로 저장해야 합니다. 씬 파일에 순서를 적고 5b D8 의 id 정렬을 대체합니다. 정리용 폴더와 프리팹 인스턴스 색 표시도 이 단위에 둡니다.

**왜.** 지금은 복제(Ctrl+D)만 있고 순서를 바꾸거나 범위로 고를 수 없습니다.

**상용 비교.** 유니티 sibling index, Godot 노드 순서, 언리얼 Outliner 의 폴더와 "선택 따라가기", 유니티의 파란 프리팹 이름과 같습니다.

**확인 = 에디터 시나리오.** `hierarchyedit.scenario.xml`: 형제 셋을 둔 시험 씬에서 첫 줄을 고르고 `EditorClick mods="shift"` 로 셋째 줄까지 골라 탐침 `Editor.SelectionCount` 가 3 인지 보고,
`EditorKey key="C" mods="ctrl"` 와 `EditorKey key="V" mods="ctrl"` 뒤 오브젝트 수 탐침이 3 늘었는지, `EditorKey key="Z" mods="ctrl"` 한 번에 돌아오는지 봅니다. 끌어 바꾸기는 끌기 단계가 생기면 더합니다.

### N3 콘텐츠 브라우저 에셋 관리 ★(M, 선행 5b D12 ~ D15 · A1)

**무엇.** 새 폴더와 새 에셋(머티리얼, 씬, 프리팹), 이름 바꾸기(F2), 복제(Ctrl+D), 폴더로 끌어 옮기기, 하위 폴더까지 검색, OS 휴지통으로 삭제를 더합니다.
이름 바꾸기와 옮기기는 참조를 고쳐야 하므로 A1 의 역색인 뒤에 둡니다. 텍스처 썸네일은 R9 입니다.

**왜.** 지금 오른쪽 클릭 메뉴는 탐색기 보기, 경로 복사, 잠금, 삭제뿐이고 검색은 지금 폴더만 봅니다.

**상용 비교.** 언리얼의 Fix Up Redirectors, 유니티 Project 창의 Create 메뉴와 F2 이름 바꾸기, 두 엔진의 프로젝트 전체 검색과 같습니다.

**확인 = 에디터 시나리오.** `assetmanage.scenario.xml`: 임시 폴더를 콘텐츠 루트로 연 상태에서 새 머티리얼을 만들고(`EditorClick` 메뉴), F2 로 이름을 바꾼 뒤, 그 머티리얼을 쓰는 시험 씬의 참조가 새 이름으로 바뀌었는지 탐침 `Editor.ReferenceCount.<경로>` 로 봅니다.

### N4 인스펙터 기본기 ★(S, 선행 I1)

**무엇.** 인스펙터에 검색이 되는 Add Component 단추를 둡니다. Hierarchy 의 메뉴 그리기 함수를 공용 위젯으로 옮겨 씁니다. 컴포넌트 순서 바꾸기(Move Up/Down), 인스펙터 잠그기, 에셋 참조 필드의 고르기 팝업과 "콘텐츠 브라우저에서 보기" 를 더합니다.
다중 편집과 기본값 되돌리기는 I1 과 I2 입니다.

**왜.** 컴포넌트를 더하려면 Hierarchy 의 오른쪽 클릭 메뉴로 가야 하고, 에셋 참조는 경로를 손으로 쳐야 합니다.

**상용 비교.** 유니티 Inspector 맨 아래와 언리얼 Details 위의 Add Component, 유니티 자물쇠와 언리얼의 여러 Details 창, 언리얼 에셋 피커와 같습니다.

**확인 = 에디터 시나리오.** `addcomponent.scenario.xml`: 오브젝트를 고르고 Add Component 단추(이름표 `inspector.addComponent`)를 눌러 `EditorText value="PointLight"` 와 `EditorKey key="Enter"` 를 보낸 뒤, 그 오브젝트의 컴포넌트 수 탐침이 하나 늘었는지 봅니다.

### N5 Output Log(S)

**무엇.** 같은 줄 접기(Collapse), Play 때 지우기, 오류에서 멈추기, 위로 스크롤하면 자동 스크롤 멈춤, 여러 줄을 끌어 골라 복사, 저장된 로그 파일 열기 단추를 더합니다.

**왜.** 위로 스크롤해 읽는 중에도 새 로그가 오면 맨 아래로 끌려갑니다(`ConsolePanel.cpp` 의 자동 스크롤). 크래시 뒤 로그를 보려면 파일을 직접 찾아야 합니다.

**상용 비교.** 유니티 Console 의 Collapse, Clear on Play, Error Pause, 언리얼 Output Log 의 여러 줄 선택과 같습니다.

**확인 = 에디터 시나리오.** `outputlog.scenario.xml`: 같은 로그 줄을 열 번 내는 개발 명령을 넣고 Collapse 를 켠 뒤 보이는 줄 수 탐침 `Editor.OutputLogVisibleRows` 가 1 인지 봅니다.

### N6 플레이 옵션(M, 선행 5b D4)

**무엇.** 새 창에서 플레이, 플레이 중 에디터 카메라로 빠져나오기(언리얼 F8 Eject), 플레이 때 뷰포트 최대화, 플레이 중 인스펙터 편집을 더합니다.
플레이 중 값을 바꾸면 Stop 이 되돌리는 것이 기본이고, 고른 값을 편집 씬에 남기는 명령(언리얼 "Keep Simulation Changes")을 둡니다.

**왜.** 플레이는 에디터 안의 게임 뷰에서만 되고(새 창 · 최대화 없음), 플레이 중에는 인스펙터가 통째로 비활성입니다. 플레이 중 에디터 카메라로 둘러보기는 V1 의 씬 뷰가 이미 한다 — Eject 는 게임 뷰의 플레이어 조종을 씬 뷰로 옮기는 것만 남았다.

**상용 비교.** 언리얼의 New Editor Window(PIE), Standalone, Eject, 유니티의 Maximize On Play 와 플레이 모드 편집 되돌리기와 같습니다.

**확인 = 에디터 시나리오.** `playedit.scenario.xml`: Play 를 누르고 인스펙터에서 오브젝트 위치를 바꾼 뒤 Stop 을 눌러 위치 탐침이 원래 값으로 돌아오는지, 같은 절차에서 "Keep" 명령을 쓰면 남는지 봅니다.

### N7 도구 문서 열기 흐름(S)

**무엇.** Material, Sequencer, Tile Map, Sprite Clip 도구 패널에 "열기" 단추와 최근 문서 목록을 둡니다. Quick Open 이나 콘텐츠 브라우저 더블클릭으로 에셋을 고르면 그 도구가 열립니다.
Prefab Editor 의 오버라이드 표는 "바뀐 것만" 을 기본으로 하고 표시 이름을 씁니다.

**왜.** 지금 도구 패널은 "포커스된 에셋" 이 있어야 열리고, Quick Open 으로 타일맵을 골라도 포커스만 바뀌고 Tile Map Tool 은 열리지 않습니다.

**상용 비교.** 유니티와 언리얼은 에셋을 열면 그 편집기가 열립니다. 유니티 Overrides 드롭다운은 바뀐 것만 보입니다.

**확인 = 에디터 시나리오.** `openasset.scenario.xml`: Quick Open(`EditorKey key="P" mods="ctrl"`)에 타일맵 이름을 치고 `EditorKey key="Enter"` 를 보낸 뒤 탐침 `Editor.PanelOpen.tilemap` 이 1 인지 봅니다.

### N8 Animation Graph 를 런타임 기능에 맞추기(M, 선행 T3)

**무엇.** 패널 점검은 Animation Graph 를 "미완성" 으로 분류했습니다. 노드 추가, 끌기, 연결, 저장과 dirty 표시는 동작합니다.
런타임 에셋(`Source/Engine/Animation/Graph/AnimGraphAsset.h`)은 전이 조건(`_parameter`, `_op`, `_threshold`), 블렌드(`_blendSeconds`), 반복(`_loopOverride`)을 이미 지원하지만 편집기에서는 고칠 수 없습니다.
"Link Selected" 는 선택이 아니라 목록의 마지막 두 노드를 잇습니다(`AnimGraphPanel.cpp`). 이 단위에서 상태 이름과 클립 지정, 전이 조건과 블렌드와 반복 편집, 오른쪽 클릭 노드 메뉴, "Link Selected" 고침, 플레이 중 현재 상태와 전이 강조를 넣습니다.
T3 의 공용 노드 그래프 틀(찾아 넣기, 핀 타입 색, 오류 노드 표시)을 그대로 씁니다. 백로그 1-6 애니메이션의 "에디터 그래프 패널이 조건과 블렌드를 편집" 과 같은 항목이므로, 이 단위를 끝내면 그 줄도 지웁니다.

**왜.** 편집기로 만든 그래프는 조건 없는 전이만 가질 수 있어, 런타임 기능을 쓰려면 XML 을 손으로 고쳐야 합니다.

**상용 비교.** 언리얼 AnimBP 의 상태 기계와 유니티 Animator 는 전이 조건과 블렌드 시간을 전이 선택으로 편집하고, 플레이 중 현재 상태와 전이를 강조합니다.

**확인 = 에디터 시나리오.** `animgraphedit.scenario.xml`: 시험 그래프 문서를 열고 전이 하나(이름표 `animGraph.link.0`)를 고른 뒤 조건 필드에 `EditorText` 로 파라미터 이름과 임계값을 넣고 저장합니다.
다시 읽은 에셋의 조건 수 탐침 `Editor.AnimGraphConditionCount` 가 1 인지, 노드 둘을 고르고 "Link Selected" 를 눌렀을 때 그 두 노드가 이어졌는지(링크 탐침) 봅니다.

### N9 Sequencer 키프레임 트랙(M~L, 선행 T1)

**무엇.** 시간마다 키를 찍는 트랜스폼과 프로퍼티 트랙, 커브 편집, 뷰포트 미리보기 스크럽, 카메라 컷 트랙을 더합니다. 지금은 클립 하나에 이동, 회전, 크기 한 벌입니다.

**상용 비교.** 언리얼 Sequencer 와 유니티 Timeline 의 기본 기능입니다.

**확인 = 에디터 시나리오.** `sequencerkeys.scenario.xml`: 시험 시퀀스를 열고 두 시각에 위치 키를 찍은 뒤 시간 막대를 가운데로 옮겨, 시험 오브젝트의 위치 탐침이 두 키 사이 값인지 봅니다.

### N10 머티리얼 미리보기(S)

**무엇.** 오프스크린 렌더 타깃에 구 하나를 그리는 간단한 미리보기를 머티리얼 패널에 둡니다. UI Preview 패널이 오프스크린 화면을 쓰는 방식과 같습니다. 9절 로드맵 9 의 다중 월드 툴 창이 오면 그것으로 바꿉니다.

**왜.** 지금은 "Apply to Selection" 으로 씬에 걸어 봐야 머티리얼을 볼 수 있습니다.

**상용 비교.** 언리얼 머티리얼 편집기의 미리보기 뷰포트, 유니티 Inspector 아래의 구 미리보기와 같습니다.

**확인 = 에디터 시나리오.** `materialpreview.scenario.xml`: 시험 머티리얼을 열고 `Screenshot` 으로 패널 영역을 찍어 `ExpectImage` 의 `meanRedMinusBlue` 가 머티리얼 색(빨강)을 따르는지 봅니다.

### N11 타일맵과 스프라이트 도구(S)

**무엇.** 타일맵에 사각형, 채우기, 스포이트, 브러시 미리보기, 확대와 이동을 더하고, 칠할 타일 목록에 아틀라스 그림을 보입니다(백로그 1-3 의 2D 항목과 같이 합니다). Sprite Clip 에는 아틀라스 그림 위에서 칸을 고르는 화면을 둡니다.

**상용 비교.** 유니티 Tile Palette 와 Sprite Editor 의 Slice 와 같습니다.

**확인 = 에디터 시나리오.** `tilemaptools.scenario.xml`: 시험 타일맵을 열고 채우기 도구로 빈 영역을 한 번 눌러 채워진 칸 수 탐침이 그 영역 크기와 같은지 봅니다.

### N12 편집기 숨김과 잠금(S, 선행 5b D6)

**무엇.** Hierarchy 의 눈 아이콘을 저장되지 않는 "에디터에서만 숨김" 으로 바꾸고, 고르지 못하게 잠그는 자물쇠를 더합니다. 오브젝트 활성 비트는 인스펙터의 체크박스로만 바꿉니다.

**왜.** 지금 눈 아이콘은 오브젝트 활성 비트를 바꿔 게임 동작까지 바뀌고 씬에 저장됩니다.

**상용 비교.** 유니티와 언리얼의 눈은 에디터에서만 숨기고 저장하지 않습니다. 두 엔진 모두 고르기 잠금(유니티 손가락 아이콘, 언리얼 잠금)을 둡니다.

**확인 = 에디터 시나리오.** `hidelock.scenario.xml`: 오브젝트의 눈 아이콘(이름표 `hierarchy.toggle.<이름>`)을 누른 뒤 탐침 `Editor.SceneDirty` 가 0 이고 오브젝트 활성 탐침이 1 인지(게임 동작은 그대로), 자물쇠를 누른 뒤 뷰포트에서 그 오브젝트를 클릭해도 선택 수가 0 인지 봅니다.

### 그 밖에 점검이 적은 것(작은 단위, 다른 단위에 붙여도 됩니다)

- **프로파일러**: 계층 호출 트리, 그래프의 한 프레임을 눌러 그 프레임 값 보기, 스파이크에서 멈추기, 캡처 저장과 열기. 스레드 타임라인은 G4 입니다.
- **렌더 타깃 보기**: 깊이와 그림자 맵 미리보기(선형화), 채널 고르기, 픽셀 값 읽기. RenderDoc 캡처는 이미 있습니다(`RenderDocCapture` · 상단 툴바 단추).
- **데이터 편집기 정리**: Data Table 의 게임 데이터 탭은 XML 글 상자라 이름과 달리 표가 아닙니다. 리플렉션 구조체 행을 표로 편집하는 일은 9절 로드맵 7(카탈로그 편집기)과 같이 합니다.
  Input Map Editor 의 시연용 단추와 9 개 탭을 정리하고, 언리얼 Enhanced Input 처럼 액션, 매핑, 트리거를 한 화면에 둡니다.
- **에디터 프레임당 할당 측정**: 메모리 탭의 `Editor` 태그 누적 할당이 6 분에 20 GB 였습니다. `-gv_profileAllocSites`(Debug App)로 프레임당 할당 위치를 세고, 새 로그가 올 때마다 최대 2048 줄을 복사하는 콘솔 스냅숏부터 봅니다.
