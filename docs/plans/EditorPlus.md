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
| `ICON_FA_EYE` 같은 Font Awesome 아이콘 | 5b 의 R2 뒤에는 `editoricon::k*`(`Source/Editor/Common/Gui/EditorIconGlyphs.h`). `ICON_FA_GEAR` → `kSettings`, `ICON_FA_ROTATE` → `kRefresh`, `ICON_FA_TRIANGLE_EXCLAMATION` → `kWarning`, 나머지는 같은 낱말(`kBug`, `kMap`, `kCamera`, `kEye`) |
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
| O10 | ThemePark 게임으로 띄워도 에디터는 Empty 의 `editortest` 씬을 연다 | `tp1.png` | 이 PC 의 로컬 설정 탓일 수 있다(`-gv_editorStartupScene` 은 비어 있다) — 적용 담당이 깨끗한 `Saved/` 로 한 번 더 볼 것. 같으면 [백로그](../06_Backlog.md) 1-4 에 한 줄 |

---

## 2. 단계 · 단위 · 규모 (우선순위 순 — 위가 아래의 바닥)

★ = **가장 체감이 큰 것**(매 실행 · 매 편집에서 보이거나, 지금 손으로 하는 일을 없앤다).

| 단계 | 단위 | 무엇 | 규모 | 선행 | 체감 |
|------|------|------|------|------|------|
| **1 확장 지점(C)** | C1 | EditorModule SHARED + `SW_EDITOR_API` + 등록 목록을 모듈 하나에 + 등록 세대 | M | | ★ |
| | C2 | 매니저가 등록 변화를 따라간다 + 모듈을 내릴 때 그 모듈의 인스턴스를 뗀다 + 시각화 켬/끔을 id 로 | M | C1 | |
| | C3 | ImGui 컨텍스트 결속기 + 커맨드 등록 줄 `SW_EDITOR_COMMAND` | M | C1 · C2 | |
| | C4 | 모듈 종류 `EditorExtension` · `sw_addEditorExtension` · ModuleHost 적재 · 게이트 | M | C3 | |
| | C5 | 첫 사용자 `GF_Editor_ThemePark` — 배치 시각화 + Park Layout 패널 + 자체 시험 | S | C4 | |
| **2 에디터 설정** | P1 | 리플렉션 객체 그리기를 `EditorPropertyGrid` 로 떼기(인스펙터 · 환경설정 · 프로젝트 설정이 같이 쓴다) | M | | |
| | P2 | 환경설정 창(`SW_EDITOR_SETTINGS` 섹션 등록 · `Saved/Editor/EditorPreferences.json`) | M | P1 · C1 | ★ |
| | P3 | 단축키 편집기(`Saved/Editor/Shortcuts.json` · 키 받기 · 충돌) | M | C3 | ★ |
| | P4 | 모듈 창(켜고 끄기 · 의존 미리보기 · 구성/빌드 버튼) | M | C4 | |
| **3 인스펙터 · 콘텐츠** | I1 | 다중 선택 편집(공통 프로퍼티 · 다른 값 표시 · 한 트랜잭션) | M | P1 | ★ |
| | I2 | 기본값과 다름 표시 · 기본값으로 · 프로퍼티 복사/붙여넣기 | S | P1 | ★ |
| | I3 | 프로퍼티 그리기 확장 `SW_EDITOR_PROPERTY_DRAWER`(유니티 PropertyDrawer) | S | C1 · P1 | |
| | A1 | 콘텐츠 브라우저 — 활성 팩만 + "어디서 쓰이나" 역색인 | M | | ★ |
| **4 캡처 · 디버그 · 품질** | G1 | 보기 모드 Normals · Depth · Overdraw | M | E3 | ★ |
| | G4 | 프로파일러 스레드 미니 타임라인 | M | | |
| | H2 | `bugit` / `bugitgo` — 버그 리포트 한 방 | M | | ★ |
| | H3 | 시험 패널(자체 시험 · 시나리오 · 시험 실행 파일 목록과 실행) | M | 3 차 B2 · gfx-editor-rest 8 | |
| **5 공용 편집 틀** | T1 | `FloatCurve` + 커브 편집 위젯 | M | I3 | |
| | T2 | 맵 검사 패널(Map Check — 씬 규칙 · 저장 때 · 클릭하면 선택) | M | | |
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

### C1 EditorModule 을 SHARED 로 내보내고, 등록 목록을 EditorModule 하나에 둔다

**목적.** 다른 DLL 이 EditorModule 의 등록부 · 위젯 · 선택 · Undo 를 링크할 수 있게. 등록 목록이 DLL 마다 갈라지지 않게.

**바꿀 파일.**

1) 새 `Source/Editor/Common/EditorExports.h`:
```cpp
/**
 * @file EditorExports.h
 * @brief EditorModule SHARED DLL 의 SW_EDITOR_API 입니다(Dev 전용 — 확장 모듈 `GF_Editor_*` · `SWGameEditor` 가 링크한다).
 */
#pragma once
#include "Core/Common/Macros.h"

// ------------------------------------------------------------------------------
// 1) SW_EDITOR_API — EditorModule 내보내기
//    Windows: dllexport/dllimport, 그 외: default visibility
//    EditorTest 처럼 소스를 직접 컴파일하는 곳(둘 다 정의 안 됨)에서는 빈 매크로
// ------------------------------------------------------------------------------
#if defined( SW_PLATFORM_WINDOWS )
    #if defined( SW_EDITOR_EXPORTS )
        #define SW_EDITOR_API __declspec( dllexport )
    #elif defined( SW_EDITOR_IMPORTS )
        #define SW_EDITOR_API __declspec( dllimport )
    #else
        #define SW_EDITOR_API
    #endif
#else
    #define SW_EDITOR_API __attribute__( ( visibility( "default" ) ) )
#endif
```

2) `Source/Editor/CMakeLists.txt` 1) 절:
```cmake
if(swBuildEditorModule)
	# SHARED — 확장 모듈(GF_Editor_* · SWGameEditor)이 링크한다(언리얼 UnrealEd 와 같은 자리). 핫 리로드는 키트와 같은 길(섀도 복사 + 지연 로드 결속)이다.
	add_library(EditorModule SHARED ${EDITOR_SOURCES})
	...
	target_compile_definitions(EditorModule
		PRIVATE
			"SW_LOG_TAG=\"Editor\""
			SW_MODULE_EXPORTS
			SW_EDITOR_EXPORTS
		INTERFACE
			SW_EDITOR_IMPORTS
	)
	...
	# 확장 모듈은 ImGui 헤더를 쓴다 — 링크는 확장이 스스로 한다(vcpkg imgui 는 정적이라 DLL 마다 사본, 결속기가 컨텍스트를 맞춘다).
	target_include_directories(EditorModule INTERFACE "${CMAKE_SOURCE_DIR}/Source")
```
그리고 맨 끝에 "Compile Editor" 가 짓는 묶음 타깃(C4 가 확장을 더한다):
```cmake
	# 에디터 빌드 버튼(Build > Compile Editor)이 짓는 묶음 — EditorModule + 모든 확장 모듈(C4 의 sw_addEditorExtension 이 의존을 더한다).
	add_custom_target(EditorAll DEPENDS EditorModule)
	set_target_properties(EditorAll PROPERTIES FOLDER "Source/Editor")
```

3) `Common/Workspace/EditorRegistry.h` — 목록을 EditorModule 하나에서 꺼내고, 바뀔 때마다 세대를 올린다:
```cpp
#include "Editor/Common/EditorExports.h"
...
    class SW_EDITOR_API EditorRegistrationList
    {
    public:
        explicit EditorRegistrationList( const utf8* pKindName );

        [[nodiscard]] bool addRegistration( const EditorRegistration& registration );
        void removeRegistration( const EditorRegistration& registration );

        const EditorRegistration*                findRegistration( string_view id ) const { return _registered.findByName( id ); }
        const vector<const EditorRegistration*>& getRegistrations() const { return _registered.getItems(); }
        const utf8*                              getKindName() const { return _pKindName; }
        /**
         * @brief 등록이 바뀔 때마다(넣기 · 빼기) 하나씩 오르는 세대입니다.
         * @details 인스턴스를 만들어 두는 매니저(패널 · 인스펙터 · 팝업 · 커맨드)는 자기가 맞춘 세대와 다르면 다시 맞춥니다 — 확장 모듈은 에디터가 뜬 뒤에도
         *          올라오고 내려간다(핫 리로드).
         */
        uint32 getGeneration() const { return _generation; }

    private:
        const utf8*                                _pKindName;
        RegistrationList<const EditorRegistration> _registered; ///< (순서, id) 사전순 · id 필수
        uint32                                     _generation;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 종류 이름 하나의 등록 목록입니다 — **EditorModule 이미지에 하나**입니다. 확장 모듈의 등록자도 이 함수를 거쳐 같은 목록에 오릅니다.
     * @details 템플릿 안의 함수 정적으로 두면 DLL 마다 사본이 생겨 확장의 등록이 EditorModule 에 보이지 않는다. 그래서 목록은 내보낸 함수 하나가 듭니다.
     *          목록은 EditorModule 이 내려갈 때 사라집니다 — 확장 모듈은 EditorModule 에 의존하므로 늘 먼저 내려간다(리로드 그래프 · 종료 순서).
     */
    SW_EDITOR_API EditorRegistrationList& getEditorRegistrationList( const utf8* pKindName );
} // namespace sw::editor

namespace sw::editor
{
    template <typename TRegistration>
    class EditorRegistry
    {
        static_assert( std::is_base_of_v<EditorRegistration, TRegistration>, "등록 줄은 EditorRegistration 을 상속해야 합니다" );

    public:
        /** @brief 이 종류의 목록입니다(EditorModule 의 것 — `getEditorRegistrationList`). 이 DLL 이 처음 부를 때 한 번 찾아 둡니다. */
        static EditorRegistrationList& getList()
        {
            static EditorRegistrationList& s_list = getEditorRegistrationList( TRegistration::kKindName );
            return s_list;
        }
        ... (getCount · getAt · find 그대로)
    };
```
`EditorRegistry.cpp`:
```cpp
namespace sw::editor
{
    SW_LOG_CALLER( "EditorRegistry" );

    namespace
    {
        struct EditorRegistryInternal
        {
            /** @brief 종류 이름 → 목록. 종류는 열 개 안팎이라 줄 찾기로 충분하다. 함수 정적이라 다른 TU 의 정적 등록자보다 늦게 만들어질 걱정이 없다. */
            static vector<unique_ptr<EditorRegistrationList>>& getLists()
            {
                static vector<unique_ptr<EditorRegistrationList>> s_listList;
                return s_listList;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorRegistrationList::EditorRegistrationList( const utf8* pKindName )
        : _pKindName{ pKindName }
        , _registered{ RegistrationOrder::ByOrderThenName, true }
        , _generation{ 0 }
    {
    }

    bool EditorRegistrationList::addRegistration( const EditorRegistration& registration )
    {
        ... (그대로) — Added 일 때만 ++_generation;
    }

    void EditorRegistrationList::removeRegistration( const EditorRegistration& registration )
    {
        if ( _registered.remove( &registration ) )   // RegistrationList::remove 가 뺐는지 돌려주지 않으면 findByName 전후로 본다
            ++_generation;
    }

    EditorRegistrationList& getEditorRegistrationList( const utf8* pKindName )
    {
        vector<unique_ptr<EditorRegistrationList>>& listList = EditorRegistryInternal::getLists();
        for ( const unique_ptr<EditorRegistrationList>& pList : listList )
        {
            if ( StringUtil::equals( pList->getKindName(), pKindName, false ) )
                return *pList;
        }
        listList.push_back( make_unique<EditorRegistrationList>( pKindName ) );
        return *listList.back();
    }
} // namespace sw::editor
```
(`RegistrationList::remove` 의 반환 — 지금 `(void)` 로 버린다. bool 을 돌려주면 그것으로, 아니면 `findRegistration( id ) == &registration` 을 빼기 전에 본다.)

4) 확장이 쓸 API 에 `SW_EDITOR_API` 를 붙인다(클래스 전체 — 인라인 함수는 그대로 인라인):
| 헤더 | 붙일 것 |
|---|---|
| `Common/Workspace/EditorService.h` | `getRawService` · `bindRawLocalService` · `getRawLocalService` · `getActiveScene` · `getActiveObjectManager` · `findGameObject` · `findComponent` |
| `Common/Workspace/EditorContext.h` | `class SW_EDITOR_API EditorContext`(`get()` 이 비인라인 정적) |
| `Common/Workspace/EditorSelection.h` · `EditorWorkspace.h` · `EditorTransaction.h` | 클래스 |
| `Common/Gui/IEditorPanel.h` · `EditorDocumentPanel.h` · `EditorChrome.h` · `EditorThemeUtil.h` · `EditorNotificationManager.h` | 클래스 · 정적 struct |
| `Common/Widgets/EditorWidgets.h` · `EditorListFilter.h` | 정적 struct · 클래스 |
| `Viewport/EditorViewportProjection.h` · `EditorVisualizerGeometry.h` | `EditorViewportProjectionUtil` · `EditorVisualizerGeometryUtil` · `EditorDebugDrawStats` |
| `Panels/Inspector/IInspectorComponent.h` · `IInspectorProperty.h` | 클래스(인터페이스 — vtable 은 각 DLL, 붙여도 무해) |
| `SelfTest/EditorSelfTest.h` | `EditorSelfTestContext` |
| `Common/Commands/EditorCommandRegistry.h` | `EditorCommandRegistry` |

EditorTest 는 이 소스들을 직접 컴파일하고 두 매크로 다 정의하지 않으므로 `SW_EDITOR_API` 는 빈 매크로다 — EditorTest CMake 는 그대로.

5) 문서: `Source/Editor/README.md` 첫 줄 "Dev 에서만 `EditorModule` MODULE 로" → "SHARED 로(확장 모듈이 링크한다)". CLAUDE.md Architecture 의 "`EditorModule` / `SWGame` / `GF_*` kits / `RHI_*` backends are dynamically loaded MODULEs" → "`SWGame` · `RHI_*` are MODULEs; `EditorModule`, `GameFramework` and `GF_*` kits are SHARED and hot-reload through the same graph", export 매크로 목록에 "`SW_EDITOR_API` (EditorModule symbols, used by editor extension modules)". AGENTS.md 의 DLL Export 목록에도 같은 한 줄.

**시험.** `Test/EditorTest/Common/Workspace/TestEditorRegistry.cpp` 에 둘:
```cpp
/**
 * @brief [EditorRegistryTest] 종류 이름이 같으면 어느 템플릿 인스턴스에서 찾든 같은 목록이다(DLL 마다 갈라지지 않는 모양)
 */
SW_TEST_CASE( EditorRegistryTest, KindNameResolvesToOneList )
{
    sw::editor::EditorRegistrationList& first  = sw::editor::getEditorRegistrationList( "test.kind" );
    sw::editor::EditorRegistrationList& second = sw::editor::getEditorRegistrationList( "test.kind" );
    SW_EXPECT_TRUE( &first == &second );
    SW_EXPECT_TRUE( &first != &sw::editor::getEditorRegistrationList( "test.other" ) );
}

/**
 * @brief [EditorRegistryTest] 넣고 빼면 세대가 오르고, 거절된 등록은 세대를 올리지 않는다
 */
SW_TEST_CASE( EditorRegistryTest, GenerationCountsChanges )
{
    sw::editor::EditorRegistrationList& list = sw::editor::getEditorRegistrationList( "test.generation" );
    const uint32 before = list.getGeneration();
    static const sw::editor::EditorRegistration kFirst{ "a", 1 };
    static const sw::editor::EditorRegistration kDuplicate{ "a", 2 };
    SW_EXPECT_TRUE( list.addRegistration( kFirst ) );
    SW_EXPECT_EQ( list.getGeneration(), before + 1 );
    {
        test::ScopedLogCollector capture;              // 같은 id 거절 Error 를 가로챈다(이 파일의 기존 시험이 쓰는 것에 맞춘다)
        SW_EXPECT_FALSE( list.addRegistration( kDuplicate ) );
    }
    SW_EXPECT_EQ( list.getGeneration(), before + 1 );
    list.removeRegistration( kFirst );
    SW_EXPECT_EQ( list.getGeneration(), before + 2 );
}
```
(로그 가로채기 이름은 이 파일의 기존 같은-id 시험을 따른다.)

**확인 = 에디터 시나리오.** `editorstartup.scenario.xml`: 에디터를 띄우고 몇 프레임 기다린 뒤 `ExpectLog` 로 `[Error]` 가 0 줄인지, 탐침 `Editor.PanelCount`(등록된 패널 수, 이 단위가 등록)가 SHARED 로 바꾸기 전과 같은지 봅니다.
등록 목록이 DLL 마다 갈라지면 패널 수가 줄어 이 시나리오가 집니다. 이후 C 단계의 시나리오가 이 파일을 바탕으로 씁니다.

**남길 교훈.** C 단계 전체가 끝나면(C5) 한 번에 적습니다.
**커밋 메시지:**
```
에디터 - EditorModule 을 SHARED 로 내보내고 등록 목록을 EditorModule 하나에 둔다(확장 지점 1/5)

문제점:
- EditorModule 이 MODULE DLL 이라 다른 DLL 이 링크할 수 없었고 내보낸 심볼도 없었다.
- 등록 목록(EditorRegistry<T>::getList)이 템플릿 안 함수 정적이라 DLL 마다 사본이 생긴다 — 키트가 SW_EDITOR_PANEL 을 써도
  자기 사본에 오를 뿐 에디터에 보이지 않는다.

해결방안:
- EditorModule 을 SHARED 로(SW_EDITOR_EXPORTS / INTERFACE SW_EDITOR_IMPORTS), SW_EDITOR_API(Common/EditorExports.h).
  확장이 쓸 등록부 · 서비스 · 선택 · 작업 공간 · Undo · 패널 기반 · 위젯 · 투영 · 자체 시험 문맥 · 커맨드 등록부를 내보낸다.
- 목록은 내보낸 getEditorRegistrationList( 종류 이름 ) 하나가 든다. 넣기 · 빼기마다 세대를 올린다(매니저가 따라가는 기준).
- 묶음 타깃 EditorAll(Compile Editor 가 짓는다).

결과:
- EditorRegistryTest.KindNameResolvesToOneList · GenerationCountsChanges 추가. 등록 줄 · 매크로 모양은 그대로라 기존 등록은 바뀌지 않는다.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```
**적용 뒤 확인:** re-configure(MODULE → SHARED), 빌드, `EditorTest --test_filter=EditorRegistryTest.*`, `AppSmokeTest.EditorRegistriesKeepTheirOrder`(host — 등록 줄 순서가 그대로), 에디터 핫 리로드 한 번(Build > Compile Editor 후 리로드 로그 `[Error]` 0) — 모듈 리로드 경로는 App 이 맡는다(`SmokeTest` 의 리로드 사례가 본다).
**위험.** (1) SHARED 는 Windows 에서 `.lib`(가져오기 라이브러리)를 낸다 — `sw_setModuleBinOutput` 이 ARCHIVE 출력을 정하지 않으므로 기본 자리(빌드 폴더)에 간다. 문제 없다.
(2) LiveReloadManager 가 EditorModule 을 이름으로 올리는 길은 MODULE · SHARED 를 가리지 않는다(키트가 SHARED 다). (3) `CheckSourceGlob` · `sw_declareUnbuiltSources` 는 그대로.

### C2 매니저가 등록 변화를 따라가고, 모듈이 내려가기 전에 그 모듈의 인스턴스를 뗀다 · 시각화 켬/끔을 id 로

**목적.** 지금 매니저는 에디터가 뜰 때 등록부를 한 번 읽어 인스턴스를 만든다(`registerDefaultPanels` · `registerDefaults` · `registerDefaultPopups`). 확장 모듈은 뒤에 올라오고(적재 순서 ·
핫 리로드) 내려가므로, (1) 세대가 바뀌면 다시 맞추고 (2) 이미지를 내리기 **전에** 그 이미지의 등록 줄로 만든 인스턴스를 지운다(지우는 소멸자 · vtable 이 그 이미지에 있다).
시각화 마스크는 등록 순서의 비트 자리라 줄이 끼거나 빠지면 다른 시각화가 켜진다 — id 로 바꾼다.

**바꿀 파일.**

1) `Panels/EditorPanelManager.h` — 항목에 등록 줄 포인터, 맞춤 · 떼기 함수:
```cpp
    struct EditorPanelEntry
    {
        string                         _id;
        string                         _title;
        unique_ptr<IEditorPanel>       _pInstance;
        const EditorPanelRegistration* _pRegistration{ nullptr }; ///< 이 인스턴스를 만든 등록 줄(직접 registerPanel 한 것은 nullptr)
        EditorPanelCategory            _category{ EditorPanelCategory::Core };
    };
...
        /**
         * @brief 등록부와 맞춥니다 — 새 줄은 인스턴스를 만들고(지난번에 열려 있던 id 면 열린 채로), 사라진 줄의 인스턴스는 내립니다. 세대가 같으면 바로 돌아갑니다.
         * @details 에디터 프레임 앞에서 부릅니다(`ImGuiEditor::updateUi`). 처음 부르면 `registerDefaultPanels` 와 같습니다.
         */
        void syncWithRegistry( IRHIDevice* pRhiDevice );
        /**
         * @brief 등록 줄이 [@p pBegin, @p pEnd)(모듈 이미지) 안인 패널을 내립니다 — `shutdown` 후 소멸. 미저장 문서는 경고하고 버립니다. 내린 수입니다.
         * @details 이미지를 내리기 **전에**(`IModuleUnloadListener`) 부릅니다. 열림 상태는 id 로 기억해 같은 id 가 다시 오르면 되살립니다.
         */
        uint32 releasePanelsWithin( const void* pBegin, const void* pEnd, IRHIDevice* pRhiDevice );
    private:
        vector<EditorPanelEntry> _listPanel;
        vector<string>           _listRememberedOpenId; ///< 내린 패널 가운데 열려 있던 id(다시 오르면 연다)
        uint32                   _syncedGeneration;     ///< 마지막으로 맞춘 등록 세대(처음은 UINT32_MAX)
```
`EditorPanelManager.cpp`:
```cpp
    void EditorPanelManager::syncWithRegistry( IRHIDevice* pRhiDevice )
    {
        using PanelRegistry               = EditorRegistry<EditorPanelRegistration>;
        const EditorRegistrationList& list = PanelRegistry::getList();
        if ( list.getGeneration() == _syncedGeneration )
            return;
        _syncedGeneration = list.getGeneration();

        // 1) 등록부에서 사라진 줄의 인스턴스를 내린다(정상 경로에서는 releasePanelsWithin 이 먼저 했다 — 여기는 직접 removeRegistration 한 경우)
        for ( size_t index = _listPanel.size(); index-- > 0; )
        {
            const EditorPanelEntry& entry = _listPanel[index];
            if ( entry._pRegistration == nullptr || PanelRegistry::find( entry._id ) == entry._pRegistration )
                continue;
            if ( entry._pInstance != nullptr )
                entry._pInstance->shutdown( pRhiDevice );
            _listPanel.erase( _listPanel.begin() + static_cast<ptrdiff_t>( index ) );
        }
        // 2) 새 줄은 만든다. 순서는 등록부 순서 그대로 — 그리기 순서 · Panel 메뉴 순서가 등록부와 같다
        vector<EditorPanelEntry> listSorted;
        listSorted.reserve( PanelRegistry::getCount() );
        for ( uint32 index = 0; index < PanelRegistry::getCount(); ++index )
        {
            const EditorPanelRegistration& registration = PanelRegistry::getAt( index );
            EditorPanelEntry*              pExisting    = EditorPanelManagerInternal::findEntryByRegistration( _listPanel, &registration );
            if ( pExisting != nullptr )
            {
                listSorted.push_back( std::move( *pExisting ) );
                continue;
            }
            EditorPanelEntry entry{};
            entry._pInstance     = registration._pCreate();
            entry._id            = registration._pId;
            entry._title         = entry._pInstance->getPanelTitle();
            entry._category      = registration._category;
            entry._pRegistration = &registration;
            if ( EditorPanelManagerInternal::takeRemembered( _listRememberedOpenId, entry._id ) )
                entry._pInstance->setOpen( true );
            listSorted.push_back( std::move( entry ) );
        }
        // 3) 등록 줄 없이 직접 넣은 패널(registerPanel)은 뒤에 그대로 둔다
        for ( EditorPanelEntry& entry : _listPanel )
        {
            if ( entry._pRegistration == nullptr && entry._pInstance != nullptr )
                listSorted.push_back( std::move( entry ) );
        }
        _listPanel = std::move( listSorted );
    }

    uint32 EditorPanelManager::releasePanelsWithin( const void* pBegin, const void* pEnd, IRHIDevice* pRhiDevice )
    {
        uint32 releasedCount{ 0 };
        for ( size_t index = _listPanel.size(); index-- > 0; )
        {
            EditorPanelEntry& entry = _listPanel[index];
            if ( IModuleUnloadListener::isAddressWithin( entry._pRegistration, pBegin, pEnd ) == false )
                continue;
            if ( entry._pInstance != nullptr )
            {
                if ( entry._pInstance->isDocumentDirty() )
                    SW_LOG_WARNING( "Panel '%#' had unsaved edits - discarded because its module is unloading", entry._title.c_str() );
                if ( entry._pInstance->isOpen() )
                    _listRememberedOpenId.push_back( entry._id );
                entry._pInstance->shutdown( pRhiDevice );
            }
            _listPanel.erase( _listPanel.begin() + static_cast<ptrdiff_t>( index ) );
            ++releasedCount;
        }
        return releasedCount;
    }
```
`registerDefaultPanels()` 는 `clear(); _syncedGeneration = UINT32_MAX; syncWithRegistry( nullptr );` 로 줄인다(호출부 그대로).
(헬퍼 `findEntryByRegistration` · `takeRemembered` 는 파일 위 `EditorPanelManagerInternal` 에.)

2) `InspectorComponentManager` 같은 모양 — `map<string, unique_ptr<IInspectorComponent>>` 를 `vector<InspectorEntry>`(`_typeName` · `_pInstance` · `_pRegistration`)로 바꾸고
`syncWithRegistry()` · `releaseInspectorsWithin( pBegin, pEnd )`. `find` 는 줄 찾기(인스펙터 확장은 열 개 안팎).
`EditorPopupManager` 도 `syncWithRegistry()` · `releasePopupsWithin(…)`(팝업 열림 상태는 기억하지 않는다 — 모달은 닫는다).

3) 떼기 리스너 — 새 `Common/Workspace/EditorModuleUnloadListener.h` · `.cpp`(EditorContext 가 하나 소유, 에디터가 서 있는 동안만 산다):
```cpp
namespace sw::editor
{
    /**
     * @class EditorModuleUnloadListener
     * @brief 확장 모듈 이미지가 내려가기 전에(핫 리로드 · 종료) 그 이미지의 등록 줄로 만든 에디터 인스턴스를 뗍니다.
     * @details 리스너 객체는 EditorModule 코드라 EditorModule 이 내려가면 vtable 도 사라진다 — 그래서 EditorContext 가 소유하고 에디터를 내릴 때 함께 없앤다
     *          (IModuleUnloadListener 의 주의: 모듈보다 오래 살면 안 된다). EditorModule 자신이 내려갈 때는 에디터 인스턴스가 먼저 통째로 내려가므로 할 일이 없다.
     */
    class EditorModuleUnloadListener final : public IModuleUnloadListener
    {
    public:
        explicit EditorModuleUnloadListener( EditorContext& context );
        ~EditorModuleUnloadListener() override;

        const utf8* getModuleUnloadListenerName() const override { return "editor extension instances"; }
        uint32      onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override;

    private:
        EditorContext& _context;
    };
} // namespace sw::editor
```
```cpp
    uint32 EditorModuleUnloadListener::onModuleUnloading( const void* pBegin, const void* pEnd, bool& /*outKeepImageMapped*/ )
    {
        IRHIDevice* pRhiDevice = _context.getRhiDevice();
        uint32 releasedCount   = _context.getPanelManager().releasePanelsWithin( pBegin, pEnd, pRhiDevice );
        releasedCount += _context.getPopupManager().releasePopupsWithin( pBegin, pEnd );
        releasedCount += _context.getInspectorComponentManager().releaseInspectorsWithin( pBegin, pEnd );
        releasedCount += _context.getInspectorPropertyManager().releaseDrawersWithin( pBegin, pEnd );   // I3 뒤
        releasedCount += EditorCommandGui::releaseCommandsWithin( pBegin, pEnd );                      // C3 뒤
        return releasedCount;
    }
```
**주의:** `releaseModuleCode` 는 뗀 것을 "모듈이 스스로 떼지 않고 남긴 것" 경고로 남긴다(ModuleImageUtil 주석). 확장 인스턴스는 **에디터가 떼는 것이 정상**이므로
이 리스너의 결과는 경고가 아니라 정보로 남기게 `IModuleUnloadListener::ReleaseResult` 에 칸 하나(`_bExpected`)를 더하고 이 리스너는 그것을 켠다 —
`ModuleImageUtil::releaseModuleCode` 의 경고 줄이 `_bExpected` 면 `SW_LOG_INFO` 로. (Core 헤더 변경 → 엔진 ABI 도장이 바뀐다 — 전체 빌드.)
EditorContext: `_pModuleUnloadListener = make_unique<EditorModuleUnloadListener>( *this );` 를 매니저들 다음에, `shutdown` 에서 매니저보다 **먼저** `reset()`.

4) `ImGuiEditor::updateUi` — "ImGui NewFrame / Dockspace" 블록 **앞**에서 맞춘다(그리기 전 · 메뉴가 패널 목록을 읽기 전):
```cpp
        // 확장 모듈은 에디터가 뜬 뒤에도 오르내린다 — 등록 세대가 바뀌었으면 인스턴스를 맞춘다(같으면 비교 하나로 끝).
        if ( _editorContext != nullptr )
            _editorContext->syncExtensionRegistrations();
```
`EditorContext::syncExtensionRegistrations()` 가 넷(패널 · 팝업 · 인스펙터 · 프로퍼티 그리기)과 커맨드(C3)를 부른다.

5) 시각화 켬/끔을 id 로 — `Viewport/EditorViewportToolbar.h` 의 `uint32 _visualizerMask` → `EditorVisualizerToggles _visualizerToggles`. 새 ImGui 없는 클래스(`Viewport/EditorVisualizerToggles.h` · `.cpp`):
```cpp
namespace sw::editor
{
    /** @brief 시각화 하나를 사용자가 바꾼 기록입니다(기본값과 다를 때만 남는다). */
    struct EditorVisualizerToggle
    {
        string _id;
        bool   _bOn{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorVisualizerToggles
     * @brief 뷰포트 시각화의 켬/끔입니다 — 등록 줄의 기본값 위에 사용자가 바꾼 것만 id 로 든다.
     * @details 예전 마스크는 등록 순서의 비트 자리라 확장 모듈이 줄을 끼우면 다른 시각화가 켜졌다. id 로 들면 줄이 오르내려도 각자의 상태가 남는다.
     */
    class EditorVisualizerToggles
    {
    public:
        /** @brief @p registration 이 켜져 있으면 true 입니다(바꾼 기록이 없으면 등록 줄의 기본값). */
        bool isOn( const EditorVisualizerRegistration& registration ) const;
        /** @brief 켜거나 끕니다. 기본값과 같아지면 기록을 지웁니다. */
        void setOn( const EditorVisualizerRegistration& registration, bool bOn );

    private:
        vector<EditorVisualizerToggle> _listToggle;
    };
} // namespace sw::editor
```
`EditorViewportVisualizer::drawAll( args, const EditorVisualizerToggles& toggles )` — 32 개 상한(`kMaxVisualizerCount`) · `getMaskBit` · `getDefaultMask` 를 지운다(그 경고 로그도).
툴바 체크박스는 `toggles.isOn( registration )` / `setOn`. 자체 시험 중 비트 마스크를 쓰는 곳이 있으면(`EditorSelfTestCases.cpp` grep `getMaskBit`) 같이 바꾼다.

**시험.**
- `Test/EditorTest/Viewport/TestEditorVisualizerToggles.cpp`(새, `EditorVisualizerTogglesTest` — `EditorTest/CMakeLists.txt` 에 시험 · 소스 한 줄씩):
```cpp
SW_TEST_CASE( EditorVisualizerTogglesTest, DefaultsComeFromTheRegistration )
{
    const sw::editor::EditorVisualizerRegistration kOnByDefault{ { "on", 100 }, "On", "", true, nullptr };
    const sw::editor::EditorVisualizerRegistration kOffByDefault{ { "off", 200 }, "Off", "", false, nullptr };
    sw::editor::EditorVisualizerToggles toggles;
    SW_EXPECT_TRUE( toggles.isOn( kOnByDefault ) );
    SW_EXPECT_FALSE( toggles.isOn( kOffByDefault ) );
}

SW_TEST_CASE( EditorVisualizerTogglesTest, StateFollowsTheIdNotTheOrder )
{
    const sw::editor::EditorVisualizerRegistration kFirst{ { "first", 100 }, "A", "", false, nullptr };
    const sw::editor::EditorVisualizerRegistration kInserted{ { "inserted", 150 }, "B", "", false, nullptr };   // 확장 모듈이 사이에 끼운 줄
    const sw::editor::EditorVisualizerRegistration kSecond{ { "second", 200 }, "C", "", false, nullptr };
    sw::editor::EditorVisualizerToggles toggles;
    toggles.setOn( kSecond, true );
    SW_EXPECT_FALSE( toggles.isOn( kInserted ) );   // 비트 마스크였으면 둘째 자리가 끼운 줄로 옮겨 갔다
    SW_EXPECT_TRUE( toggles.isOn( kSecond ) );
    toggles.setOn( kSecond, false );                // 기본값과 같아지면 기록이 지워진다
    SW_EXPECT_FALSE( toggles.isOn( kSecond ) );
}
```
- 매니저 맞춤은 ImGui 없이 시험할 수 있는 것이 `InspectorComponentManager` 뿐이다(패널 · 팝업은 ImGui 를 링크한다). `Test/EditorTest/Panels/TestInspectorComponentSync.cpp`(새, `InspectorComponentSyncTest`):
  등록자 하나를 시험 안에서 만들고(`EditorRegistrar<EditorInspectorRegistration>` 지역 객체) `syncWithRegistry` → `find` 가 찾음, 등록자 소멸 → `syncWithRegistry` → 못 찾음,
  `releaseInspectorsWithin( &registration, &registration + 1 )` → 1. (`InspectorComponentManager.cpp` 를 EditorTest 소스 목록에 더한다 — ImGui 를 쓰지 않는지 `CheckTestSuites` 가 본다.)
- 패널 쪽은 C5 의 자체 시험(확장 모듈 패널이 Panel 메뉴에 있다)과 리로드 확인(적용 뒤 확인)이 덮는다.

**변이:** `syncWithRegistry` 의 세대 비교를 `return` 없이 지우는 것은 성능만 바뀐다 — 대신 `releaseInspectorsWithin` 의 `isAddressWithin` 조건을 뒤집으면 시험이 진다.
**확인 = 에디터 시나리오.** `visualizertoggle.scenario.xml`: 뷰포트 툴바의 시각화 체크박스(이름표 `viewport.visualizer.<id>`, 이 단위가 남김)를 `EditorClick` 으로 켜고 끈 뒤,
탐침 `Editor.VisualizerOn.<id>` 로 누른 것만 바뀌었는지 봅니다. 확장 모듈을 내렸다 올리는 경우는 C5 의 시나리오가 봅니다.

**남길 교훈.** `Source/Editor/README.md` 함정 · 계약 절의 확장 등록 줄(`EditorRegistry<T>` …)에 덧붙임: `매니저는 등록 세대를 따라가고(syncWithRegistry), 확장 모듈이 내려가기 전에 EditorModuleUnloadListener 가 그 이미지의 등록 줄로 만든 인스턴스를 뗀다. 시각화 켬/끔은 id(EditorVisualizerToggles) — 비트 자리는 줄이 끼면 엉뚱한 것을 켠다.`
**커밋 메시지:**
```
에디터 - 매니저가 등록 변화를 따라가고 확장 모듈이 내려가기 전에 그 인스턴스를 뗀다(확장 지점 2/5)

문제점:
- 패널 · 인스펙터 · 팝업 매니저가 에디터가 뜰 때 등록부를 한 번 읽어 인스턴스를 만들었다. 뒤에 올라오는 모듈의 등록은 보이지 않고,
  모듈이 내려가면 그 모듈 코드(vtable · 소멸자)를 가리키는 인스턴스가 남는다.
- 시각화 마스크가 등록 순서의 비트 자리라 줄이 끼거나 빠지면 다른 시각화가 켜졌다. 32 개 상한도 있었다.

해결방안:
- 매니저마다 syncWithRegistry(세대가 바뀌면 새 줄은 만들고 사라진 줄은 내림, 열림 상태는 id 로 기억) · release*Within(이미지 범위의
  등록 줄로 만든 인스턴스를 내림). EditorModuleUnloadListener(IModuleUnloadListener)가 이미지를 내리기 전에 부른다.
  에디터가 떼는 것은 정상이라 ReleaseResult::_bExpected 로 경고 대신 정보로 남긴다.
- EditorVisualizerToggles: 기본값 위에 바꾼 것만 id 로. 비트 마스크 · 32 개 상한 삭제.

결과:
- EditorVisualizerTogglesTest 둘 · InspectorComponentSyncTest 하나 추가.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```
**적용 뒤 확인:** Core 헤더(`ModuleUnloadListener.h`) 변경 → 전체 빌드. `EditorTest --test_filter=EditorVisualizerToggles*:InspectorComponentSync*`, 자체 시험 전부(`-gv_editorSelfTest=*`), `AppSmokeTest.EditorRegistriesKeepTheirOrder`.
**겹침:** gfx-editor-rest 10(`EditorPanelManager` 계측 — `drawOpenPanels` 에 스코프를 더한다) · 11(Hierarchy). 같은 파일 다른 함수.

### C3 ImGui 컨텍스트 결속기 + 커맨드 등록 줄 `SW_EDITOR_COMMAND`

**목적.** (1) 확장 DLL 은 vcpkg 정적 imgui 의 **자기 사본**을 갖는다 — 그 사본의 `GImGui` · 할당자 · ImPlot 컨텍스트를 EditorModule 이 만든 것으로 맞춰야 확장의 `ImGui::Button` 이 같은 창에 그려진다.
(2) 확장이 메뉴 · 단축키 · 팔레트에 커맨드를 넣는 길(D4).

**(1) 결속기 — 새 `Common/Gui/EditorUiContext.h` · `.cpp`(ImGui 헤더를 include 하지 않는다 — 컨텍스트는 `void*`):**
```cpp
namespace sw::editor
{
    /** @brief 지금 에디터 UI 컨텍스트입니다. 확장 DLL 이 자기 ImGui 사본에 건다. */
    struct EditorUiContextState
    {
        void* _pImGuiContext{ nullptr };  ///< ImGuiContext*
        void* _pImPlotContext{ nullptr }; ///< ImPlotContext*
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorUiBinderRegistration
     * @brief DLL 하나의 결속 함수입니다. 확장 모듈마다 CMake 가 만든 소스(`<모듈>UiBinder.cpp`)가 하나 둔다.
     * @details 결속 함수는 **그 DLL 의** ImGui 사본에 `SetAllocatorFunctions` · `SetCurrentContext` 를 겁니다(Dear ImGui FAQ "DLL 경계").
     *          할당자는 엔진 Memory 라 어느 DLL 이 잡고 풀어도 같다.
     */
    struct EditorUiBinderRegistration : EditorRegistration
    {
        static constexpr const utf8* kKindName = "uibinder";

        void ( *_pfnBind )( const EditorUiContextState& state );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorUiContext
     * @brief 에디터가 ImGui 컨텍스트를 만들거나 지울 때 알리는 곳입니다. 알릴 때마다 등록된 결속 함수를 모두 부릅니다.
     */
    struct SW_EDITOR_API EditorUiContext
    {
        /** @brief `ImGuiEditor::initialize` 가 컨텍스트를 만든 직후(상태), `shutdown` 이 지우기 직전(빈 상태)에 부릅니다. */
        static void publish( const EditorUiContextState& state );
        /** @brief 지금 상태입니다. 컨텍스트가 없으면 두 칸 모두 nullptr 입니다. */
        static const EditorUiContextState& getCurrent();
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorUiBinderRegistrar
     * @brief 결속 함수를 등록하고, 이미 컨텍스트가 있으면 **바로** 겁니다(에디터가 뜬 뒤 올라온 확장 모듈 — 핫 리로드).
     */
    class EditorUiBinderRegistrar : public EditorRegistrar<EditorUiBinderRegistration>
    {
    public:
        explicit EditorUiBinderRegistrar( const EditorUiBinderRegistration& registration )
            : EditorRegistrar<EditorUiBinderRegistration>{ registration }
        {
            if ( EditorUiContext::getCurrent()._pImGuiContext != nullptr && registration._pfnBind != nullptr )
                registration._pfnBind( EditorUiContext::getCurrent() );
        }
    };
} // namespace sw::editor
```
`EditorUiContext.cpp`:
```cpp
    namespace
    {
        struct EditorUiContextInternal
        {
            static EditorUiContextState& getState()
            {
                static EditorUiContextState s_state{};
                return s_state;
            }
        };
    } // namespace
...
    void EditorUiContext::publish( const EditorUiContextState& state )
    {
        EditorUiContextInternal::getState() = state;
        using BinderRegistry = EditorRegistry<EditorUiBinderRegistration>;
        for ( uint32 index = 0; index < BinderRegistry::getCount(); ++index )
        {
            const EditorUiBinderRegistration& registration = BinderRegistry::getAt( index );
            if ( registration._pfnBind != nullptr )
                registration._pfnBind( state );
        }
    }

    const EditorUiContextState& EditorUiContext::getCurrent() { return EditorUiContextInternal::getState(); }
```
`ImGuiEditor::initialize` — `ImPlot::CreateContext();` 바로 뒤: `EditorUiContext::publish( EditorUiContextState{ ImGui::GetCurrentContext(), ImPlot::GetCurrentContext() } );`.
`shutdownPartialInitialization`(컨텍스트 지우기 직전): `EditorUiContext::publish( EditorUiContextState{} );`. EditorModule 자신은 결속기를 두지 않는다(자기 사본에 `CreateContext` 했다).

새 `cmake/Engine/EditorExtensionUiBinder.cpp.in`(C4 의 `sw_addEditorExtension` 이 확장마다 `configure_file` — 손으로 쓰지 않는다):
```cpp
// AUTO-GENERATED by sw_addEditorExtension — @SW_EDITOR_EXTENSION_NAME@ 의 ImGui 사본을 에디터 컨텍스트에 건다. 고치지 말 것.
#include "pch.h"

#include "Core/Memory/Memory.h"

#include "Editor/Common/Gui/EditorUiContext.h"

#include <imgui.h>
#include <implot.h>

namespace sw::editor
{
    namespace
    {
        struct EditorExtensionUiBinderInternal
        {
            static void* allocateForImGui( size_t size, void* /*pUserData*/ ) { return Memory::allocate( size ); }
            static void  freeForImGui( void* pPtr, void* /*pUserData*/ ) { Memory::free( pPtr ); }

            static void bind( const EditorUiContextState& state )
            {
                ImGui::SetAllocatorFunctions( &allocateForImGui, &freeForImGui, nullptr );
                ImGui::SetCurrentContext( static_cast<ImGuiContext*>( state._pImGuiContext ) );
                ImPlot::SetCurrentContext( static_cast<ImPlotContext*>( state._pImPlotContext ) );
            }
        };
    } // namespace

    static const EditorUiBinderRegistrar sw_editorUiBinder_@SW_EDITOR_EXTENSION_NAME@{ EditorUiBinderRegistration{ { "@SW_EDITOR_EXTENSION_NAME@", 0 }, &EditorExtensionUiBinderInternal::bind } };
} // namespace sw::editor
```
(ImGuizmo 를 쓰는 확장은 자기 코드에서 `ImGuizmo::SetImGuiContext( ImGui::GetCurrentContext() )` 를 그리기 전에 부른다 — README 한 줄.)

**(2) 커맨드 등록 줄 — `Common/Commands/EditorCommandRegistry.h` 끝에:**
```cpp
namespace sw::editor
{
    /**
     * @struct EditorCommandRegistration
     * @brief 확장 모듈(또는 EditorModule 의 아무 파일)이 커맨드 하나를 더하는 등록 줄입니다. 표(`EditorCommandGui.cpp` 의 `_s_arrCommandRow`)와 같은 칸이고,
     *        등록부가 표 + 등록 줄을 합쳐 메뉴 · 단축키 · 팔레트를 만듭니다. id 는 표와도 겹치면 안 됩니다(`validate`).
     * @details 문자열은 리터럴. 메뉴 경로는 기존 메뉴(`"MainMenu/Tools"` …)거나 새 한 단계 메뉴(`"MainMenu/ThemePark"`)다.
     */
    struct EditorCommandRegistration : EditorRegistration
    {
        static constexpr const utf8* kKindName = "command";

        const utf8*           _pLabel;
        const utf8*           _pIcon;
        const utf8*           _pCategory;
        const utf8*           _pTooltip;
        const utf8*           _pDetail;
        EditorCommandShortcut _shortcut;
        void ( *_pfnAction )();
        bool ( *_pfnEnabled )();
        const utf8* _pMenuPath; ///< nullptr 이면 메뉴에 없다(팔레트 · 단축키만)
    };
} // namespace sw::editor

/**
 * @brief 커맨드 하나를 그 커맨드의 .cpp 에서 등록합니다. `_order` 는 메뉴 안 순서(표의 `_menuOrder` 와 같은 뜻 — 백의 자리가 바뀌면 구분선).
 * @code SW_EDITOR_COMMAND( ParkReload, "themepark.reloadLayout", 9100, "Reload Park Layout", ICON_FA_ROTATE, "ThemePark", "배치 파일을 다시 읽습니다",
 *                          "Reload rides.xml", {}, &reloadLayout, nullptr, "MainMenu/ThemePark" ); @endcode
 */
#define SW_EDITOR_COMMAND( name, pId, menuOrder, pLabel, pIcon, pCategory, pTooltip, pDetail, shortcut, pfnAction, pfnEnabled, pMenuPath )        \
    SW_EDITOR_REGISTER( ::sw::editor::EditorCommandRegistration, Command_##name, { pId, menuOrder }, pLabel, pIcon, pCategory, pTooltip, pDetail, \
                        shortcut, pfnAction, pfnEnabled, pMenuPath )
```
`EditorCommandGui.h/.cpp`:
```cpp
        /** @brief 표 + 등록 줄로 커맨드 등록부를 다시 만듭니다(단축키 덮어쓰기 — P3 — 도 여기서 입힌다). 등록 세대가 같으면 아무것도 하지 않는다. */
        static void syncWithRegistry();
        /** @brief 등록 줄이 [@p pBegin, @p pEnd) 안인 커맨드를 빼고 다시 만듭니다. 뺀 수입니다. */
        static uint32 releaseCommandsWithin( const void* pBegin, const void* pEnd );
```
`registerDefaults()` 의 표 반복 뒤에 등록 줄 반복을 더한다(제외 범위를 받는 내부 함수 `rebuild( pExcludeBegin, pExcludeEnd )` 하나로 묶는다):
```cpp
        using CommandRegistry = EditorRegistry<EditorCommandRegistration>;
        for ( uint32 index = 0; index < CommandRegistry::getCount(); ++index )
        {
            const EditorCommandRegistration& row = CommandRegistry::getAt( index );
            if ( IModuleUnloadListener::isAddressWithin( &row, pExcludeBegin, pExcludeEnd ) )
                continue;
            EditorCommandDesc desc{};
            desc._id              = row._pId;
            desc._label           = row._pLabel;
            desc._icon            = row._pIcon != nullptr ? row._pIcon : "";
            desc._category        = row._pCategory;
            desc._tooltip         = row._pTooltip;
            desc._detail          = row._pDetail;
            desc._shortcut        = row._shortcut;
            desc._bPaletteVisible = true;
            desc._menuPath        = row._pMenuPath != nullptr ? row._pMenuPath : "";
            desc._menuOrder       = row._order;
            if ( row._pfnAction != nullptr )
                desc._action = row._pfnAction;
            if ( row._pfnEnabled != nullptr )
                desc._enabledPredicate = row._pfnEnabled;
            registry.registerCommand( std::move( desc ) );
        }
```
메인 메뉴바는 "한 단계 메뉴" 를 메뉴 경로에서 만든다(README) — 확장이 `MainMenu/ThemePark` 을 쓰면 새 메뉴가 생긴다. 메뉴끼리의 순서는 가장 작은 `_menuOrder` 라 확장은 9000 대를 쓴다(README 표에 "확장 9xxx" 한 줄).

**시험.**
- `TestEditorCommandRegistry.cpp`(EditorTest — 이미 있다)에 하나: 등록 줄을 지역 등록자로 만들고 `EditorCommandRegistry` 를 표 없이 등록 줄만으로 만들어(`rebuild` 의 ImGui 없는 반쪽을
  `EditorCommandTableUtil::appendRegistrations( registry, pExcludeBegin, pExcludeEnd )` 로 빼서 그것을 부른다) id · 메뉴 경로 · 단축키가 옮겨졌는지, 제외 범위면 빠지는지.
- 결속기는 EditorTest 로는 못 본다(ImGui 없음). C5 의 자체 시험(확장 패널이 그린 정점 수 > 0 — 결속이 틀리면 확장의 `ImGui::Begin` 이 다른 컨텍스트에 그려 패널 덤프의 정점이 0)이 덮는다.

**확인 = 에디터 시나리오.** 결속기는 확장 모듈이 생기는 C5 의 시나리오가 확인합니다. 이 단위는 커맨드 등록 줄만 봅니다.
`commandpalette.scenario.xml`: `EditorKey key="P" mods="ctrl+shift"` 로 커맨드 팔레트를 열고 `EditorText` 로 등록 줄 커맨드의 라벨을 친 뒤 `EditorKey key="Enter"`, 그 커맨드가 실행됐다는 `ExpectLog` 를 봅니다.
시험용 등록 줄 커맨드는 자체 시험 파일에 하나 둡니다(실행하면 로그 한 줄).

**남길 교훈.** `Source/Editor/README.md` 함정 · 계약 절의 "오른쪽 클릭 메뉴의 확장 지점은 `EditorCommandRegistry` / `SW_EDITOR_*` 하나다" 줄에 덧붙임: `확장 모듈의 커맨드는 SW_EDITOR_COMMAND(표와 같은 칸) — 등록부가 표 + 등록 줄을 합친다. 확장 DLL 의 ImGui 는 CMake 가 만든 결속기가 에디터 컨텍스트에 건다(vcpkg imgui 는 정적이라 DLL 마다 사본).`
**커밋 메시지:**
```
에디터 - 확장 DLL 의 ImGui 사본을 에디터 컨텍스트에 거는 결속기와 커맨드 등록 줄(확장 지점 3/5)

문제점:
- vcpkg imgui 는 정적 라이브러리라 DLL 마다 GImGui · 할당자 사본이 생긴다. 확장 DLL 의 ImGui 호출은 컨텍스트가 없어 죽거나 다른 곳에 그린다.
- 커맨드(메뉴 · 단축키 · 팔레트)는 EditorCommandGui.cpp 의 정적 표 하나라 다른 모듈이 더할 수 없었다.

해결방안:
- EditorUiContext::publish(컨텍스트를 만든 직후 · 지우기 직전) + 결속 등록 줄(uibinder). 등록자는 이미 컨텍스트가 있으면 바로 건다(핫 리로드).
  결속 소스는 cmake/Engine/EditorExtensionUiBinder.cpp.in 을 확장마다 configure_file 로 만든다(SetAllocatorFunctions · SetCurrentContext · ImPlot).
- SW_EDITOR_COMMAND 등록 줄: 표와 같은 칸, 등록부가 표 + 등록 줄을 합친다. 세대가 바뀌면 다시 만들고, 모듈이 내려가기 전에 그 줄을 뺀다.

결과:
- EditorCommandRegistryTest 에 등록 줄 합치기 · 제외 범위 시험 추가.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```
**적용 뒤 확인:** `EditorTest --test_filter=EditorCommandRegistryTest.*`, 에디터 기동 `[Error]` 0(메뉴 `validate`). **위험:** imgui 판이 EditorModule 과 확장에서 같아야 한다(같은 vcpkg 설치라 같다) — 결속기에 `IMGUI_CHECKVERSION()` 를 넣으면 구조체 크기까지 대조한다(넣는다: `bind` 첫 줄).

### C4 모듈 종류 `EditorExtension` · `sw_addEditorExtension` · ModuleHost 적재 · 게이트

**바꿀 파일.**

1) 종류 이름 표 두 곳(C++ · CMake — 둘이 같아야 한다):
- `Source/Engine/Module/ModuleCatalog.h` `ModuleKind` 끝에 `EditorExtension, ///< 에디터 확장(`GF_Editor_<키트>` · `SWGameEditor`) — Dev 전용, `-EnableEditor` 일 때만 올린다`.
- `ModuleCatalog.cpp:18` `kArrKindName` 에 `"EditorExtension"`. `cmake/Engine/ModuleManifest.cmake:17` `SW_MODULE_KINDS ... EditorExtension`.
- 매니페스트 검사: 종류가 `EditorExtension` 이면 `_listConfiguration` 은 `["Dev"]` 만, `_listDependency` 에 `EditorModule` 이 있어야 한다 — `ModuleCatalog::resolve` 의 오류로(모르는 키와 같은 자리).

2) `cmake/Engine/TargetRules.cmake` — 새 함수(키트 함수 옆):
```cmake
# ------------------------------------------------------------------------------
# 에디터 확장 모듈(GF_Editor_<키트> · SWGameEditor) — Dev 전용 SHARED. EditorModule 을 링크하고(언리얼 <X>Editor 모듈이 UnrealEd 를 링크하듯),
# 매니페스트의 의존(키트 · GameFramework)을 링크한다. ImGui 결속 소스를 만들어 넣는다(손으로 빠뜨릴 수 없게).
# ------------------------------------------------------------------------------
function(sw_addEditorExtension TARGET_NAME)
	file(GLOB_RECURSE swExtensionSources CONFIGURE_DEPENDS "*.cpp" "*.h")
	if(SW_SHIPPING_BUILD)
		sw_declareUnbuiltSources(${swExtensionSources})
		return()
	endif()
	sw_skipInactiveModule(${TARGET_NAME} swSkip)
	sw_isModuleActive(EditorModule swEditorActive)
	if(swSkip OR NOT swEditorActive)
		sw_declareUnbuiltSources(${swExtensionSources})
		return()
	endif()

	set(SW_EDITOR_EXTENSION_NAME ${TARGET_NAME})
	set(swBinderSource "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}UiBinder.cpp")
	configure_file("${CMAKE_SOURCE_DIR}/cmake/Engine/EditorExtensionUiBinder.cpp.in" "${swBinderSource}" @ONLY)

	add_library(${TARGET_NAME} SHARED ${swExtensionSources} "${swBinderSource}")
	target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

	# 의존 모듈 중 Kit · GameFramework 를 링크한다(EditorModule 은 아래에서 늘). 목록을 CMake 에 다시 적지 않는다 — 매니페스트가 정본.
	get_property(swDependencies GLOBAL PROPERTY SW_MODULE_${TARGET_NAME}_DEPENDENCIES)
	set(swLinkedModules "")
	set(swDelayDlls EditorModule.dll)
	foreach(swDependency IN LISTS swDependencies)
		get_property(swDependencyKind GLOBAL PROPERTY SW_MODULE_${swDependency}_KIND)
		if(swDependencyKind STREQUAL "Kit" OR swDependencyKind STREQUAL "GameFramework")
			list(APPEND swLinkedModules ${swDependency})
			list(APPEND swDelayDlls "${swDependency}.dll")
		endif()
	endforeach()

	target_link_libraries(${TARGET_NAME}
		PRIVATE
			EditorModule
			Engine
			RuntimeAPI
			${swLinkedModules}
			imgui
			implot
			sw_global_options
	)
	target_compile_definitions(${TARGET_NAME} PRIVATE "SW_LOG_TAG=\"${TARGET_NAME}\"")
	sw_configurePch(${TARGET_NAME} "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
	sw_setModuleBinOutput(${TARGET_NAME})
	if(WIN32)
		sw_addDelayloadHook(${TARGET_NAME} DLLS ${swDelayDlls})
	endif()
	sw_registerDynamicModule(${TARGET_NAME} editorextension)
	set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Source/EditorExtensions")
	add_dependencies(EditorAll ${TARGET_NAME})
	sw_addReflectionStep(${TARGET_NAME} INCLUDES "${CMAKE_SOURCE_DIR}/Source")
endfunction()
```
(`imgui` · `implot` 타깃 이름은 `Source/Editor/CMakeLists.txt` 의 링크 목록과 같다.)

3) 키트 · 게임 함수가 `Editor/` 를 빼고 하위 폴더로 넘긴다 — `sw_addGameFrameworkKit`:
```cmake
	file(GLOB_RECURSE kitSources CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")
	# 키트의 에디터 확장(Editor/)은 따로 짓는 모듈이다(GF_Editor_<키트>) — 키트 DLL 에 섞지 않는다.
	list(FILTER kitSources EXCLUDE REGEX "/Editor/")
	...
	sw_addReflectionStep(${KIT_NAME}
		INCLUDES "${CMAKE_SOURCE_DIR}/Source"
		EXCLUDE_REGEX "/Editor/"
	)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Editor/CMakeLists.txt")
		add_subdirectory(Editor)
	endif()
```
`sw_addGameModule` 도 같은 두 줄(EXCLUDE · add_subdirectory). `cmake/Engine/ReflectionCodeGen.cmake`: `cmake_parse_arguments(ARG "" "OUTPUT_DIR;EXCLUDE_REGEX" "HEADERS;INCLUDES" ${ARGN})`,
자동 훑기 직후 `if(ARG_EXCLUDE_REGEX) list(FILTER _all_headers EXCLUDE REGEX "${ARG_EXCLUDE_REGEX}") endif()`.
키트 · 게임의 `Editor/` 하위 폴더가 Shipping 에서 안 지어지는 것은 `sw_addEditorExtension` 의 `sw_declareUnbuiltSources` 가 `CheckSourceGlob` 에 알린다.

4) `Source/ModuleHost/ModuleHost.cpp` — `loadModuleImages` 에서 확장 목록을 모아 두고(카탈로그는 `initialize` 에 없다), 에디터 블록에서 EditorModule 다음에 등록한다:
```cpp
            if ( pManifest->_kind == ModuleKind::EditorExtension )
            {
                // 에디터 확장은 에디터를 켤 때만 올린다(initialize 의 에디터 블록). 여기서는 이름과 의존만 적어 둔다.
                EditorExtensionModule extension{};
                extension._name = moduleName;
                for ( const ModuleDependency& dependency : pManifest->_listDependency )
                    extension._listDependency.push_back( dependency._name );
                _listEditorExtension.push_back( std::move( extension ) );
                continue;
            }
```
에디터 블록(`registerModule( config::kTargetEditorModule )` 성공 뒤):
```cpp
                // 확장 모듈은 EditorModule 과 자기 키트에 의존한다 — 리로드 그래프가 의존이 바뀌면 확장을 다시 올린다(키트 → SWGame 과 같은 길).
                // 인스턴스는 없다: 정적 등록자가 등록부에 오르고, 에디터 매니저가 다음 프레임에 맞춘다(EditorContext::syncExtensionRegistrations).
                for ( const EditorExtensionModule& extension : _listEditorExtension )
                {
                    if ( _pLiveReloadManager->registerModule( extension._name, extension._listDependency ) == false )
                    {
                        // 확장 하나가 못 올라와도 에디터는 뜬다 — 그 확장의 패널만 없다(언리얼도 에디터 모듈 실패는 그 플러그인만 끈다).
                        SW_LOG_ERROR( "Editor extension module %# could not be loaded", extension._name.c_str() );
                    }
                }
```
`ModuleHost.h` private: `struct EditorExtensionModule { string _name; vector<string> _listDependency; };` 와 `vector<EditorExtensionModule> _listEditorExtension;`(생성자 초기화 목록 `_listEditorExtension{}`).
종료: `ModuleHost::shutdown` 의 `suspendModules` 뒤 LiveReloadManager 가 의존 역순으로 내리는지 확인(적용 때 — 확장이 EditorModule 보다 먼저 내려가야 등록 목록이 살아 있다). 아니면 확장 이름을 먼저 `unloadModule` 한다.
헤드리스 임포트(`--import-*` — EditorModule 을 인스턴스 없이 올림)는 확장을 올리지 않는다(에디터 블록 밖).

5) `EditorCommandGui.cpp` 의 `commandCompileEditor` 가 짓는 타깃 `EditorModule` → `EditorAll`(C1 의 묶음 타깃), 라벨 `"Compile Editor (EditorModule + extensions)"`.

6) 게이트:
- `Scripts/lint/gate/CheckGameFrameworkLayers.py` 규칙 4: `Kits/<묶음>/<키트>/Editor/` 아래 파일은 `Editor/` include 를 허용한다(`_kForbiddenPrefixes` 검사 앞에 `"/Editor/" in relativeFilePath` 면 건너뜀). 규칙 3(키트끼리)은 확장에도 그대로(확장은 자기 키트만).
  selfTestCases 에 둘: 키트 본체가 `Editor/...` include → 실패, `Kits/X/Y/Editor/a.cpp` 가 `Editor/Common/Workspace/EditorRegistry.h` include → 통과.
- 새 `Scripts/lint/gate/CheckEditorBoundary.py`: (a) `Editor/` 를 include 하는 파일은 `Source/Editor/**` · `Source/**/Editor/**`(확장 폴더) · `Test/EditorTest/**` 뿐 — 게임 본체 · 키트 본체 · Engine 은 금지
  (Engine 은 `CheckEngineLayers` 가 이미 본다 — 겹쳐도 메시지가 다르니 둔다) (b) 확장 폴더의 `*.module.json` 은 종류 `EditorExtension` · 구성 `["Dev"]` · 이름 접두 `GF_Editor_`(키트) 또는 `SWGameEditor`(게임).
  2 차의 `CheckModuleTargets`(접두 규칙 게이트)가 먼저 들어왔으면 (b) 는 그 게이트에 `GF_Editor_` 줄로 넣고 이 게이트는 (a) 만.
- `CheckTestSuites` 규칙(EditorTest 손 목록)은 그대로.

**시험.**
- `Test/EngineTest/Module/TestModuleCatalog.cpp`(있으면 그 파일, 스위트 `ModuleCatalogTest`)에 셋: `EditorExtension` 종류를 읽는다 · `_listConfiguration` 에 `Shipping` 이 있으면 해석 오류 · `EditorModule` 의존이 없으면 해석 오류.
- 게이트 selfTestCases(위).
- 적재 · 리로드는 C5 의 자체 시험 + 적용 뒤 확인(핫 리로드 한 번).

**확인 = 에디터 시나리오.** 적재는 C5 의 시나리오가 확인합니다(확장 모듈이 하나도 없으면 볼 것이 없습니다). 이 단위는 C1 의 `editorstartup.scenario.xml` 이 그대로 통과하는지만 봅니다.

**남길 교훈.** `Source/Engine/Module/README.md` 함정 · 계약 절에 한 줄: `- **에디터 확장 모듈(종류 EditorExtension)은 EditorModule 과 자기 키트에 의존해 리로드 그래프에 오른다** — 인스턴스가 없고(정적 등록자), 에디터를 켤 때만 올린다. 하나가 실패해도 에디터는 뜬다(그 확장만 없다).`
**커밋 메시지:**
```
모듈 - 에디터 확장 모듈 종류(EditorExtension)와 sw_addEditorExtension · 적재 · 경계 게이트(확장 지점 4/5)

문제점:
- 키트 · 게임이 Dev 전용 에디터 코드를 둘 모듈 종류 · CMake 함수 · 적재 길이 없었다. 키트 폴더에 에디터 코드를 두면 키트 DLL(Shipping 포함)에 섞인다.

해결방안:
- ModuleKind::EditorExtension(C++ · CMake 종류 표), 매니페스트 검사(Dev 만 · EditorModule 의존 필수).
- sw_addEditorExtension: SHARED, EditorModule · 의존 키트 · imgui · implot 링크, 지연 로드 결속, ImGui 결속 소스 자동 생성, EditorAll 에 의존.
  키트 · 게임 함수는 Editor/ 를 소스 · 리플렉션 훑기에서 빼고 하위 폴더로 넘긴다(sw_addReflectionStep EXCLUDE_REGEX).
- ModuleHost: 확장을 에디터 블록에서 EditorModule 다음에 registerModule(의존 = 매니페스트) — 실패해도 에디터는 뜬다.
  Compile Editor 는 EditorAll 을 짓는다.
- 게이트: CheckGameFrameworkLayers 가 키트의 Editor/ 폴더만 Editor include 를 허용, CheckEditorBoundary(새)가 Editor include 자리 ·
  확장 매니페스트(종류 · Dev · 이름 접두)를 본다.

결과:
- ModuleCatalogTest 셋, 게이트 selfTestCases 추가.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```
**적용 뒤 확인:** re-configure, `ctest -L lint`(게이트 · selftest), `EngineTest --test_filter=ModuleCatalogTest.*`.
**겹침:** 2 차 net-kits · online-ops 의 `CheckModuleTargets`(접두 규칙) · 층 게이트 정정 — 같은 게이트를 고치면 한 커밋 안에서 합친다. `ModuleHost.cpp` 는 3 차 possess-auto(B1 `requestQuit`)도 고친다(다른 함수).

### C5 첫 사용자 `GF_Editor_ThemePark` — 배치 시각화 + Park Layout 패널 + 자체 시험

**목적.** 옛 백로그가 정한 첫 사용자. ThemePark 키트의 배치 파일(`game/themepark/data/rides.xml` — `ParkLayout`)을 에디터 뷰포트에 겹쳐 보이고(놀이기구 발자국 상자 · 이름 · 입구 · 코스터 트랙 선),
패널에서 놀이기구 목록(비용 · 정원 · 탑승 시간)을 보이며 고르면 그 자리로 에디터 카메라를 옮긴다. 파일이 바뀌면(핫 리로드) 다시 읽는다.
키트 데이터를 읽기만 하므로 **게임 모듈 없이**(어느 게임으로 띄워도) 돈다 — 자체 시험이 Empty 게임 에디터에서 그것을 본다.

**새 파일 (`Source/GameFramework/Kits/Genre/Simulation/ThemePark/Editor/`).**
- `CMakeLists.txt`:
```cmake
# ==============================================================================
# @file Source/GameFramework/Kits/Genre/Simulation/ThemePark/Editor/CMakeLists.txt
# @brief ThemePark 키트의 에디터 확장 — 배치 시각화 · Park Layout 패널 (Dev 전용)
# ==============================================================================

sw_addEditorExtension(GF_Editor_ThemePark)
```
- `GF_Editor_ThemePark.module.json`:
```json
{
    "_name": "GF_Editor_ThemePark",
    "_version": "1.0.0",
    "_kind": "EditorExtension",
    "_description": "ThemePark 키트의 에디터 확장 — 배치 파일(rides.xml)을 뷰포트에 겹쳐 보이고 놀이기구 목록 패널을 둔다",
    "_listDependency": [
        { "_name": "EditorModule" },
        { "_name": "GF_ThemePark" }
    ],
    "_listPlatform": [ "Windows", "Linux" ],
    "_listConfiguration": [ "Dev" ],
    "_bEnabledByDefault": true
}
```
- `ParkLayoutPreview.h` · `.cpp` — ImGui 없는 상태(읽기 · 다시 읽기 · 월드 도형):
```cpp
/**
 * @file ParkLayoutPreview.h
 * @brief ThemePark 배치 파일을 읽어 에디터가 겹쳐 그릴 도형을 만듭니다(ImGui 없음).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Viewport/EditorVisualizerGeometry.h"

namespace sw::editor
{
    /** @brief 놀이기구 하나의 미리보기입니다. */
    struct ParkRidePreview
    {
        string _name;
        float3 _position{};
        float3 _size{};
        float4 _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        int32  _buildCost{ 0 };
        int32  _capacity{ 0 };
        float32 _loadTime{ 0.0f };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class ParkLayoutPreview
     * @brief 배치 파일 하나(기본 `game/themepark/data/rides.xml`)를 읽어 든 것입니다. 파일 내용 해시가 바뀌면 다시 읽습니다.
     */
    class ParkLayoutPreview
    {
    public:
        ParkLayoutPreview();

        /** @brief 파일이 바뀌었으면 다시 읽습니다. 읽었으면(처음 · 바뀜) true. 파일이 없거나 틀리면 미리보기를 비우고 경고 한 번. */
        bool refresh( string_view layoutPath );
        /** @brief 놀이기구 발자국(바닥 사각형) · 입구 표시 · 코스터 트랙의 월드 선분을 @p outListSegment 뒤에 붙입니다. */
        void appendSegments( vector<EditorWorldSegment>& outListSegment ) const;

        const vector<ParkRidePreview>& getRides() const { return _listRide; }
        const float3&                  getGatePosition() const { return _gatePosition; }
        bool                           isLoaded() const { return _bLoaded; }

        /** @brief 에디터 하나가 쓰는 미리보기입니다(패널과 시각화가 같이 본다). */
        static ParkLayoutPreview& get();

    private:
        vector<ParkRidePreview>    _listRide;
        vector<vector<float3>>     _listTrackPoint; ///< 코스터마다 트랙 점(CoasterTrackBuilder 결과)
        float3                     _gatePosition;
        uint64                     _contentHash;
        bool                       _bLoaded;
    };
} // namespace sw::editor
```
`.cpp` 의 `refresh`: 파일 바이트를 읽어 `computeHash64`(같으면 false) → `CoasterLayoutCatalog layouts; layouts.load…`(키트의 카탈로그 경로 — `ParkDirectorComponent.cpp:176` 근처가 쓰는 길을 그대로) → `ParkLayout::loadFromXMLText` →
`getPlacements()` 를 `ParkRidePreview` 로, 코스터 배치는 `CoasterTrackBuilder` 로 점을 만든다(키트 API 그대로). `appendSegments`: 발자국 = 위치 중심 `_size.x × _size.z` 바닥 사각형 네 변 + 높이 기둥 넷(색 = 배치 색),
입구 = 높이 3 m 의 십자, 트랙 = 점 사이 선분.
- `ParkLayoutVisualizer.cpp` — 시각화 등록(뷰포트 툴바 체크박스 `Park`):
```cpp
#include "pch.h"

#include "ParkLayoutPreview.h"

#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ParkLayoutVisualizerInternal
        {
            static constexpr const utf8* kLayoutPath = "game/themepark/data/rides.xml";

            static void draw( const EditorViewportVisualizerArgs& args )
            {
                ParkLayoutPreview& preview = ParkLayoutPreview::get();
                (void)preview.refresh( kLayoutPath );
                if ( preview.isLoaded() == false )
                    return;
                static vector<EditorWorldSegment> s_listSegment;   // 프레임마다 다시 채우는 재사용 버퍼
                s_listSegment.clear();
                preview.appendSegments( s_listSegment );
                for ( const EditorWorldSegment& segment : s_listSegment )
                {
                    ImVec2 screenFrom;
                    ImVec2 screenTo;
                    if ( EditorViewportProjectionUtil::projectSegment( *args._pViewProj, segment._from, segment._to, args._canvasPos, args._canvasSize, screenFrom, screenTo ) == false )
                        continue;
                    args._pDrawList->AddLine( screenFrom, screenTo, ImGui::ColorConvertFloat4ToU32( ImVec4{ segment._color._x, segment._color._y, segment._color._z, segment._color._w } ), 1.5f );
                }
                for ( const ParkRidePreview& ride : preview.getRides() )
                {
                    ImVec2 labelPos;
                    const float3 top = ride._position + float3{ 0.0f, ride._size._y + 0.5f, 0.0f };
                    if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, top, args._canvasPos, args._canvasSize, labelPos ) )
                        args._pDrawList->AddText( labelPos, IM_COL32( 255, 230, 120, 255 ), ride._name.c_str() );
                }
            }
        };
    } // namespace

    SW_EDITOR_VISUALIZER( ParkLayout, "themepark.layout", 900, "Park", "ThemePark 배치 파일(rides.xml)의 놀이기구 발자국 · 입구 · 코스터 트랙", false,
                          &ParkLayoutVisualizerInternal::draw );
} // namespace sw::editor
```
(`projectSegment` 의 실제 시그니처는 `EditorViewportProjection.h` 에 맞춘다 — README 가 "선분은 반드시 projectSegment" 라 한 그 함수.)
- `ParkLayoutPanel.h` · `.cpp` — `SW_EDITOR_PANEL( ParkLayoutPanel, "themepark.layout", EditorPanelCategory::Custom, 9000 );` 제목 `"Park Layout"`. 표(이름 · 비용 · 정원 · 탑승 시간), 줄을 더블클릭하면
  에디터 카메라를 그 놀이기구로(`EditorContext::get()->…` 의 뷰포트 클라이언트 "Focus" 창구 — 기존 `F` 키(선택 오브젝트로 초점)가 쓰는 함수에 위치를 받는 판이 없으면 `EditorViewportClient::focusOnPoint( float3, float32 radius )` 를 내보낸다),
  위에 "Reload" 단추(= `refresh` 강제), 파일이 없으면 `EditorWidgets::drawEmptyHint( "game/themepark/data/rides.xml not found" )`.
- `ParkLayoutCommands.cpp` — `SW_EDITOR_COMMAND( ParkLayoutOpen, "themepark.openLayoutPanel", 9100, "Park Layout", ICON_FA_MAP, "ThemePark", "놀이기구 배치 패널을 엽니다", "Open the park layout panel", {}, &openPanel, nullptr, "MainMenu/Tools" );`
  (`MainMenu/Tools` 가 없으면 `"MainMenu/Panel"` — 메뉴 경로는 적용 때 실제 메뉴에 맞춘다.)
- `ParkLayoutSelfTest.cpp` — 자체 시험 둘:
```cpp
    /** @brief 확장 패널이 등록부에 올라 그려진다 — 결속기가 틀리면 확장의 ImGui::Begin 이 다른 컨텍스트로 가 이 창이 에디터에 없다. */
    EditorSelfTestStep runExtensionPanelDraws( EditorSelfTestContext& context )
    {
        EditorContext* pContext = EditorContext::get();
        if ( context.expect( pContext != nullptr, "no editor context" ) == false )
            return EditorSelfTestStep::Done;
        if ( context.getStepIndex() == 0 )
        {
            context.expect( pContext->getPanelManager().setPanelOpen( "themepark.layout", true ), "extension panel is not registered" );
            return EditorSelfTestStep::Continue;   // 한 프레임 그린다
        }
        const ImGuiWindow* pWindow = ImGui::FindWindowByName( "Park Layout" );
        context.expect( pWindow != nullptr && pWindow->DrawList->VtxBuffer.Size > 0, "extension panel drew nothing in the editor's ImGui context" );
        pContext->getPanelManager().setPanelOpen( "themepark.layout", false );
        return EditorSelfTestStep::Done;
    }
    SW_EDITOR_SELF_TEST( ParkLayoutPanel, "themepark.extensionPanelDraws", 9000, &runExtensionPanelDraws );

    /** @brief 배치 파일을 읽어 놀이기구가 하나 이상이고 선분이 나온다(키트 로더 · 트랙 빌더가 확장 안에서 돈다). */
    EditorSelfTestStep runLayoutPreviewLoads( EditorSelfTestContext& context )
    {
        ParkLayoutPreview preview;
        (void)preview.refresh( "game/themepark/data/rides.xml" );
        context.expect( preview.isLoaded() && preview.getRides().empty() == false, "rides.xml did not load through the ThemePark kit" );
        vector<EditorWorldSegment> listSegment;
        preview.appendSegments( listSegment );
        context.expect( listSegment.size() >= preview.getRides().size() * 4u, "every ride should give at least its four footprint edges" );
        return EditorSelfTestStep::Done;
    }
    SW_EDITOR_SELF_TEST( ParkLayoutPreview, "themepark.layoutPreviewLoads", 9010, &runLayoutPreviewLoads );
```
`AppSmokeTest.EditorSelfTestsPassInsideTheEditor` 기대 목록에 두 줄(`themepark.extensionPanelDraws` · `themepark.layoutPreviewLoads`). **AppSmokeTest 는 Empty 게임으로 돈다** — 키트가 기본 켜짐이라
GF_ThemePark · 확장이 Empty 빌드에도 지어지고 올라온다(확인: 적용 때 `Bin/Modules/GF_Editor_ThemePark.dll` 이 있는지). Empty 팩에서 `game/themepark/...` 경로가 읽히지 않으면(팩 마운트가 활성 게임만이면)
둘째 시험은 `getRides().empty()` 대신 "파일이 없다는 경고 한 줄 · 크래시 없음" 으로 바꾸고, 내용 시험은 ThemePark 프리셋 손 확인으로 넘긴다.

**확인 = 에디터 시나리오.** `extensionpanel.scenario.xml`: 메뉴의 확장 커맨드(`themepark.openLayoutPanel`)를 커맨드 팔레트로 실행해 Park Layout 패널을 열고,
탐침 `Editor.PanelOpen.themepark.layout` 이 1 인지, 패널이 남긴 이름표(`themepark.layout.reload`)를 `EditorClick` 으로 눌렀을 때 `ExpectLog` 로 다시 읽은 로그가 나오는지 봅니다.
결속기가 틀리면 확장의 ImGui 호출이 다른 컨텍스트로 가서 이름표가 남지 않고, 시나리오의 클릭 단계가 이름표를 찾지 못해 집니다.

**남길 교훈.** `Source/Editor/README.md` 함정 · 계약 절에 C 단계 교훈을 함께 적는다(C1 ~ C4 의 덧붙임 줄 포함). 한 줄 더: `- **키트 · 게임의 에디터 코드는 그 폴더의 Editor/ 확장 모듈(GF_Editor_<키트> · SWGameEditor)** — SW_EDITOR_* 를 EditorModule 과 똑같이 쓴다. 본보기 GF_Editor_ThemePark(시각화 · 패널 · 커맨드 · 자체 시험).`
README(`Source/Editor/README.md`) 에 "확장 모듈" 절: 위 규칙 셋 + 폴더 · 매니페스트 본보기 + "ImGuizmo 는 직접 컨텍스트를 건다" + "ImDrawList 콜백 금지".
**커밋 메시지:**
```
에디터 - 첫 확장 모듈 GF_Editor_ThemePark: 배치 시각화 · Park Layout 패널 · 커맨드 · 자체 시험(확장 지점 5/5)

문제점:
- 확장 지점의 첫 사용자로 정한 ThemePark 배치 시각화가 없었다. rides.xml 의 배치는 플레이해야만 보였다.

해결방안:
- Kits/Simulation/ThemePark/Editor/ 에 GF_Editor_ThemePark(EditorExtension): ParkLayoutPreview(ImGui 없음 — 키트 로더로 읽고
  내용 해시가 바뀌면 다시 읽음, 발자국 · 입구 · 코스터 트랙 선분), 시각화 themepark.layout(툴바 "Park"), Park Layout 패널
  (목록 · 더블클릭 초점 · Reload), 커맨드 themepark.openLayoutPanel.
- 자체 시험 themepark.extensionPanelDraws(확장 패널이 에디터 컨텍스트에 그려진다 — 결속기 검증) · layoutPreviewLoads.

결과:
- AppSmokeTest.EditorSelfTestsPassInsideTheEditor 기대 목록에 둘, 시나리오 extensionpanel. 계획 문서(docs/plans/EditorPlus.md)에서 C 단계 삭제.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```
**적용 뒤 확인:** re-configure, `App.exe -dx12 -EnableEditor "-gv_editorSelfTest=themepark.*" -gv_profileFrames=600`, `-gv_editorRegistryDump=1` 로 `EditorRegistry|panel|themepark.layout` 줄,
**핫 리로드 둘**: (a) `ParkLayoutPanel.cpp` 문자열 하나 바꾸고 Build > Compile Editor → 패널이 내려갔다 다시 오른다(열려 있었으면 열린 채) · `[Error]` 0
(b) 키트 `ThemePark.cpp` 를 바꾸고 Compile Game → GF_ThemePark · SWGame · GF_Editor_ThemePark 가 함께 다시 오른다. ThemePark 프리셋(`Ninja-Debug-ThemeParkTycoon`)은 병합 담당의 게임별 빌드 때.
**게임 소스: 없음**(키트 폴더 안 새 모듈).
---

## 5. 단계 2 — 에디터 설정: 프로퍼티 그리드 · 환경설정 · 단축키 · 모듈 창 ★

**상용 비교.** 언리얼 Editor Preferences · Project Settings 는 `UDeveloperSettings` 파생 클래스를 등록하면 섹션이 생기고, 같은 **Details 뷰**(`IDetailsView` — 인스펙터와 같은 위젯)로 그린다.
저장은 사용자별 ini(기본에서 바뀐 것만). Keyboard Shortcuts 는 같은 창의 한 섹션(커맨드마다 키 받기 · 충돌 경고), Plugins 창은 켜고 끄면 "Restart Now".
유니티는 `SettingsProvider` 등록(Preferences · Project Settings 두 창, 같은 IMGUI/UIElements 그리기), Shortcuts Manager(키보드 그림 + 프로필, 바뀐 것만 저장, 충돌 표시),
Package Manager 가 모듈 켜기/끄기. Godot 은 Editor Settings(검색 · 섹션 트리 · "바뀐 것만 보기") · Shortcuts 탭 · Project Settings > Plugins.
공통점: **(1) 설정 = 리플렉션 객체, 그리기는 인스펙터와 같은 위젯 (2) 섹션은 등록으로 늘어난다(확장 모듈도) (3) 사용자 파일에는 바뀐 것만.**

### P1 리플렉션 객체 그리기를 `EditorPropertyGrid` 로 뗀다

**목적.** 인스펙터(`InspectorPanel`)만 리플렉션 프로퍼티를 그린다 — 그리기 · 편집 통지 · Undo · 검색 · EditCondition · 컨테이너 · enum · 중첩 구조체 · 메서드 · 이벤트가 패널 안 private 함수다.
환경설정(P2) · 프로젝트 설정 · 다중 선택(I1) · 커브(T1) · 확장 패널이 같은 그리기를 써야 한다(언리얼 `IDetailsView` 를 아무 창에서 만드는 것과 같은 자리).

**바꿀 것 — 기계적 이동 + 대상 추상 하나.**
1) 새 `Panels/Inspector/EditorPropertyGrid.h` · `.cpp`(내보냄 `SW_EDITOR_API`):
```cpp
namespace sw::editor
{
    /**
     * @struct EditorPropertyGridTarget
     * @brief 그리드가 그리고 고치는 대상 하나입니다 — 리플렉션 인스턴스와, 고쳤을 때 알릴 곳 · Undo 를 남길 오브젝트.
     */
    struct EditorPropertyGridTarget
    {
        void*                             _pInstance{ nullptr };
        const TypeInfo*                   _pType{ nullptr };
        Component*                        _pComponent{ nullptr };  ///< 컴포넌트면 onPropertyChanged 를 받는다
        GameObject*                       _pObject{ nullptr };     ///< 오브젝트면 onPropertyChanged · Undo 의 주인(컴포넌트면 그 owner)
        Delegate<void( const PropertyInfo& )> _onEdited;           ///< 씬 밖 객체(환경설정 · 문서)가 바뀜을 받는 곳. 비어 있으면 위 둘만
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorPropertyGrid
     * @brief 리플렉션 객체의 프로퍼티(상속분 · 카테고리 · EditCondition · 컨테이너 · enum · 중첩 구조체)를 그리고 고칩니다. 인스펙터 · 환경설정 · 다중 선택이 같이 씁니다.
     * @details 언리얼 `IDetailsView` 의 자리입니다. 값을 고치면 대상에 알리고(컴포넌트 · 오브젝트의 onPropertyChanged, 그 밖은 _onEdited),
     *          씬 오브젝트면 Undo 를 남깁니다(`InspectorPropertyUndo`). 검색 칸은 그리드가 든다.
     */
    class SW_EDITOR_API EditorPropertyGrid
    {
    public:
        EditorPropertyGrid();

        /** @brief 검색 칸을 그립니다(그리드 위 한 줄). */
        void drawSearchBar();
        /**
         * @brief 대상의 프로퍼티를 그립니다. @p listDrawnName 은 이미 다른 곳(인스펙터 확장)이 그린 이름 — 다시 그리지 않습니다.
         * @param pSectionTitle 그릴 것이 있을 때만 위에 구분선 제목. nullptr 이면 없음
         */
        void drawProperties( const EditorPropertyGridTarget& target, const utf8* pSectionTitle, const vector<hashed_string>& listDrawnName );
        /** @brief 대상 타입의 메서드(FUNCTION) · 이벤트를 그립니다(인스펙터의 Methods · Events 구역). */
        void drawMethodsAndEvents( const EditorPropertyGridTarget& target );

    private:
        ... (InspectorPanel 에서 옮긴 함수들 — 아래 표)
        fixed_string<constant::kMaxBuffer64>                  _propertyFilter;
        unordered_map<uint64, vector<InspectorMethodArgSlot>> _mapMethodArgSlot;
        fixed_string<constant::kMaxBuffer256>                 _lastInvokeResult;
        const EditorPropertyGridTarget*                       _pTarget;            ///< drawProperties 동안만 — 통지 · Undo 대상
        int32                                                 _propertyDrawDepth;
    };
} // namespace sw::editor
```
2) 이동 표(`InspectorPanel.cpp` → `EditorPropertyGrid.cpp`, 본문 그대로 — `_pEditTargetComponent` · `_pEditTargetObject` 를 `_pTarget->_pComponent` · `_pTarget->_pObject` 로, `notifyPropertyEdited` 끝에 `_pTarget->_onEdited` 호출을 더함):

| InspectorPanel (줄) | EditorPropertyGrid |
|---|---|
| `drawTypeProperties` (459) | `drawProperties`(대상 인자) |
| `drawPropertyWidget` · `notifyPropertyEdited` · `drawPropertyWidgetBody` (555 · 573 · 586) | private 같은 이름 |
| `drawEnumProperty` · `drawContainerProperty` · `drawMapContainer` · `drawKeyedSequenceContainer` · `drawContainerAddRow` · `drawStructOrStringProperty` (628 ~ 1013) | private 같은 이름 |
| `getEditOwner` (973) | private `getUndoOwner()` |
| `drawTypeMethods` · `drawTypeEvents` · `invokeTypeMethod` (1014 ~ 끝) | `drawMethodsAndEvents` + private |
| 멤버 `_propertyFilter` · `_mapMethodArgSlot` · `_lastInvokeResult` · `_propertyDrawDepth` | 그리드로 |
| `InspectorPanelInternal::applyObjectEdit` | `EditorPropertyGridInternal` 로(인스펙터가 아직 쓰면 둘 다 — 공용이면 `InspectorPropertyUndo` 로) |

`InspectorPanel` 은 `EditorPropertyGrid _propertyGrid;` 를 들고, 컴포넌트 구역에서 `EditorPropertyGridTarget{ pComp, pComp->getTypeInfo(), pComp, pComp->getOwner(), {} }` 로 부른다.
3) 인스펙터 동작은 바뀌지 않는다 — 시험은 기존 자체 시험(`inspector.*`) · `InspectorPropertyLayoutTest` · `InspectorBuiltinValueTest` 가 그대로 통과하는 것.

**확인 = 에디터 시나리오.** `inspectoredit.scenario.xml`: 시험 씬의 오브젝트를 `EditorClick mark="hierarchy.row.<이름>"` 으로 고르고, 인스펙터의 위치 X 칸(이름표 `inspector.property.<컴포넌트>.<프로퍼티>`, 이 단위가 그리드에 남김)을 눌러
`EditorText` 로 값을 바꾼 뒤 탐침 `Editor.UndoCount` 가 하나 늘었는지, `EditorKey key="Z" mods="ctrl"` 뒤 값이 돌아왔는지 봅니다. 이동 전후로 같은 결과여야 합니다.
값은 오브젝트 위치 탐침(이 단위가 등록, 시험 씬의 고정 이름 오브젝트)으로 읽습니다.

**남길 교훈.** `Source/Editor/README.md` 함정 · 계약 절의 인스펙터 줄에 덧붙임: `리플렉션 객체 그리기는 EditorPropertyGrid 하나(인스펙터 · 환경설정 · 다중 선택이 쓴다) — 대상은 EditorPropertyGridTarget(통지 · Undo 주인).`
**커밋 메시지:**
```
에디터 - 리플렉션 프로퍼티 그리기를 인스펙터에서 EditorPropertyGrid 로 뗀다

문제점:
- 리플렉션 프로퍼티 그리기(카테고리 · EditCondition · 컨테이너 · enum · 중첩 구조체 · 메서드 · 이벤트 · 편집 통지 · Undo)가
  InspectorPanel 의 private 함수라 환경설정 · 다중 선택 · 확장 패널이 같은 위젯을 쓸 수 없었다.

해결방안:
- EditorPropertyGrid(SW_EDITOR_API) 로 함수 · 상태를 옮긴다(본문 그대로). 대상은 EditorPropertyGridTarget — 인스턴스 · 타입 ·
  컴포넌트 · 오브젝트(통지 · Undo 주인) · 씬 밖 객체의 _onEdited.
- InspectorPanel 은 그리드 하나를 들고 컴포넌트마다 대상을 만들어 부른다.

결과:
- 동작 변화 없음 — 인스펙터 자체 시험 · InspectorPropertyLayoutTest · InspectorBuiltinValueTest 그대로 통과.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```
**적용 뒤 확인:** 빌드, `-gv_editorSelfTest=inspector.*`, `EditorTest --test_filter=Inspector*`, 에디터에서 컴포넌트 값 하나 고치고 Ctrl+Z.
**겹침:** 2 차 gfx-editor-rest 8(자체 시험 — 인스펙터 콤보 직접 편집), object-game-rest(인스펙터의 프리팹 오버라이드 표시)가 `InspectorPanel.cpp` 를 고치면 **이동 전에** 넣는다.

### P2 환경설정 창 — `SW_EDITOR_SETTINGS` 섹션 등록 · `Saved/Editor/EditorPreferences.json` ★

**목적.** 사람이 바꾸는 에디터 설정이 흩어져 있다 — 테마는 대화상자(`EditorConfig`), UI 배율 · 시작 씬은 `-gv_` 스위치, IDE 열기 명령은 사람이 쓰는 선택 파일 `Config/Editor/editortooldefaults.json` 의 `_ideOpenCommand`,
카메라 속도 · 스냅 기본값은 코드(`EditorViewportToolbarSettings` 기본값). 한 창(Edit > Preferences…)에서 검색해 고치고, 사용자 파일에는 **바뀐 것만** 남긴다.

**새 파일.**
1) `Common/Config/EditorSettingsRegistry.h`(ImGui 없음, 내보냄):
```cpp
namespace sw::editor
{
    /**
     * @struct EditorSettingsRegistration
     * @brief 환경설정 섹션 하나(리플렉션 구조체 하나)의 등록 줄입니다. 그 구조체의 .cpp 가 `SW_EDITOR_SETTINGS` 로 둡니다.
     * @details id 는 저장 파일의 키(`"viewport"`)이고, 라벨은 창의 왼쪽 목록에 보이는 이름(`"Editor/Viewport"` — '/' 앞이 묶음)입니다.
     *          인스턴스는 그 섹션 파일의 함수 정적 하나입니다(`_pfnGetInstance`).
     */
    struct EditorSettingsRegistration : EditorRegistration
    {
        static constexpr const utf8* kKindName = "settings";

        const utf8* _pLabel;
        const TypeInfo* ( *_pfnGetType )();
        void* ( *_pfnGetInstance )();
        void ( *_pfnOnChanged )(); ///< 값이 바뀐 뒤(창에서 고침 · 파일을 다시 읽음). nullptr 이면 부르지 않는다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorPreferencesStore
     * @brief `Saved/Editor/EditorPreferences.json` 을 읽고 씁니다 — 섹션 id 마다 JSON 오브젝트 하나, **기본값과 다른 프로퍼티만**.
     * @details 기본값은 그 타입을 새로 만든 인스턴스입니다(`TypeInfo` 의 기본 생성). 모르는 섹션 · 모르는 키는 경고하고 건너뜁니다(사용자 파일이라 기동을 막지 않는다 —
     *          사람이 커밋하는 설정 파일의 엄격 읽기와 다르다: 확장 모듈을 끄면 그 섹션이 사라진다).
     */
    struct SW_EDITOR_API EditorPreferencesStore
    {
        /** @brief 등록된 섹션마다 파일의 값을 입힌다(없으면 기본값 그대로). 바뀐 섹션의 `_pfnOnChanged` 를 부른다. 파일이 없으면 false(정상 — 처음). */
        [[nodiscard]] static bool loadAll( string_view filePath );
        /** @brief 등록된 섹션을 모두 기본과의 차이만으로 씁니다. 차이가 없는 섹션은 빼고, 모두 없으면 빈 오브젝트 `{}` 를 씁니다. */
        [[nodiscard]] static bool saveAll( string_view filePath );
        /** @brief @p pCurrent 가 @p pDefault 와 다른 프로퍼티만 담은 JSON 오브젝트 글입니다(시험이 쓰는 반쪽). */
        static string makeDifferenceJson( const TypeInfo& type, const void* pCurrent, const void* pDefault );
        /** @brief JSON 오브젝트 글을 인스턴스에 입힙니다(없는 키는 그대로). 모르는 키는 @p outListUnknownKey 에. 형식이 틀리면 false. */
        [[nodiscard]] static bool applyJson( const TypeInfo& type, void* pInstance, string_view jsonText, vector<string>& outListUnknownKey );
        /** @brief 저장 파일 경로입니다(`Saved/Editor/EditorPreferences.json`, 프로젝트 루트 기준). */
        static string getDefaultFilePath();
    };
} // namespace sw::editor

/**
 * @brief 환경설정 섹션 하나를 등록합니다. 예: `SW_EDITOR_SETTINGS( EditorViewportPreferences, "viewport", "Editor/Viewport", 300, &onViewportPreferencesChanged );`
 * @param TSettings 리플렉션 구조체(`REFLECT()` · `PROPERTY()`), 기본 생성자의 값이 기본값
 */
#define SW_EDITOR_SETTINGS( TSettings, pId, pLabel, order, pfnOnChanged )                                                              \
    SW_EDITOR_REGISTER( ::sw::editor::EditorSettingsRegistration, Settings_##TSettings, { pId, order }, pLabel, &TSettings::StaticType, \
                        []() -> void* { static TSettings s_instance{}; return &s_instance; }, pfnOnChanged )
```
(람다 하나 — 인스턴스 함수 정적을 등록 줄에 담는 가장 짧은 길. AGENTS "람다는 피한다" 의 예외로 주석.) `makeDifferenceJson` 은 프로퍼티마다 `SerializerUtil` 의 글 쓰기
(`writePropertyText` — 인스펙터의 Reset to Default 가 쓰는 `applyPropertyText` 의 짝)로 두 인스턴스를 글로 바꿔 다르면 담는다. 비트필드 · 중첩 구조체는 글 비교로 함께 된다.

2) 첫 섹션 셋(`Common/Config/EditorPreferences.h` · `.cpp`, 리플렉션 — `Source/Editor/CMakeLists.txt` 의 `sw_addReflectionStep` HEADERS 에 한 줄):
```cpp
    /** @brief 일반 — 시작 · 외부 도구. */
    REFLECT()
    struct EditorGeneralPreferences
    {
        REFLECT_BODY();
        PROPERTY( Tooltip = "에디터를 열 때 이 씬을 연다(비우면 게임의 첫 씬). -gv_editorStartupScene 이 주어지면 그것이 이긴다" )
        string _startupScene{};
        PROPERTY( Tooltip = "로그 줄을 IDE 로 열 때 쓰는 명령({file} · {line}). 비우면 VS Code(code -g)" )
        string _ideOpenCommand{};
        PROPERTY( Tooltip = "UI 배율. 0 이면 모니터 DPI 를 따른다. -gv_editorUiScale 이 주어지면 그것이 이긴다", ClampMin = 0, ClampMax = 3 )
        float32 _uiScale{ 0.0f };
    };

    /** @brief 뷰포트 — 새 뷰포트의 기본값(이미 연 뷰포트는 툴바에서 바꾼다). */
    REFLECT()
    struct EditorViewportPreferences
    {
        REFLECT_BODY();
        PROPERTY( ClampMin = 0.1, ClampMax = 100 )
        float32 _cameraSpeed{ 5.0f };
        PROPERTY( ClampMin = 0.01 )
        float32 _gridSnapValue{ 1.0f };
        PROPERTY( Units = deg, ClampMin = 1, ClampMax = 90 )
        float32 _rotationSnapValue{ 15.0f };
        PROPERTY( ClampMin = 0.01 )
        float32 _scaleSnapValue{ 0.1f };
        PROPERTY()
        bool _bShowStats{ true };
        PROPERTY()
        bool _bShowGrid{ true };
        PROPERTY()
        bool _bShowOrientationCube{ true };
    };
```
세 번째 섹션 "Editor/Appearance" = **기존 `EditorConfig`(테마)** — 구조체를 그대로 섹션으로 등록하고(`SW_EDITOR_SETTINGS( EditorConfig, "appearance", "Editor/Appearance", 100, &EditorThemeUtil::applyActiveConfig )`),
`EditorConfig::loadFromHost` · `saveToHost`(지금 `Saved/Editor/EditorConfig.json`)는 **지운다** — 테마 대화상자의 저장은 `EditorPreferencesStore::saveAll`. 옛 파일은 읽지 않는다(별칭 금지 — 테마를 한 번 다시 고르면 된다; 커밋 메시지에 적는다).
`_ideOpenCommand` 는 `EditorToolDefaults` 에서 **지우고**(호출부 `EditorLogCommands` 는 `getPreferences<EditorGeneralPreferences>()._ideOpenCommand`), `gv_editorStartupScene` · `gv_editorUiScale` 는 남기되 기본을 환경설정에서 읽는다(스위치가 주어지면 스위치 — 시험 · 자동화가 쓴다).
`EditorViewportToolbarSettings` 의 기본값(위 일곱 칸)은 뷰포트를 만들 때 환경설정에서 복사한다(`EditorViewportClient` 생성).
읽기 도우미: `template <typename T> const T& getPreferences()` — 등록부에서 `T::StaticType()` 이 같은 줄을 찾아 인스턴스를 돌려준다(없으면 assert + 정적 기본값).

3) 창 — `Panels/PreferencesPanel.h` · `.cpp`, `SW_EDITOR_PANEL( PreferencesPanel, "preferences", EditorPanelCategory::Tool, 2000 );` 제목 `"Preferences"`. 왼쪽 섹션 목록(라벨의 '/' 로 묶음),
오른쪽은 P1 의 `EditorPropertyGrid`(대상 `_onEdited` = "고침 표시 + 등록 줄의 `_pfnOnChanged`"), 위에 검색 칸(그리드의 것 — **모든 섹션을 훑어** 맞는 프로퍼티가 있는 섹션만 목록에 남긴다),
"Modified only" 체크(Godot), 아래 "Reset Section" 단추. 저장은 **값이 바뀔 때마다 0.5 초 뒤 한 번**(창을 닫거나 에디터를 끌 때도) — 언리얼처럼 저장 단추가 없다.
커맨드 표에 한 줄: `{ "editor.preferences", "Preferences...", ICON_FA_GEAR, "Editor", "에디터 환경설정 창을 엽니다", "Open editor preferences", {Comma, Ctrl}, {}, &openPreferences, nullptr, true, "MainMenu/Edit", 2210 }`
(Ctrl+, 는 P3 에서 키를 넓힌 뒤 — 그 전엔 단축키 없음). 테마 대화상자 줄(`editor.themeSettings`)은 "Preferences 의 Appearance 섹션 열기" 로 바꾼다.

**시험 — `Test/EditorTest/Common/Config/TestEditorPreferencesStore.cpp`(새, `EditorPreferencesStoreTest`).** 리플렉션 시험 구조체는 `Test/EditorTest/EditorPreviewProbe.h`(이미 반사 단계가 있다)에 하나 더:
```cpp
    REFLECT()
    struct EditorPreferencesProbe
    {
        REFLECT_BODY();
        PROPERTY()
        float32 _speed{ 5.0f };
        PROPERTY()
        string _path{};
        PROPERTY()
        bool _bFlag{ true };
    };
```
```cpp
SW_TEST_CASE( EditorPreferencesStoreTest, DifferenceHoldsOnlyChangedProperties )
{
    sw::editor::EditorPreferencesProbe current{};
    const sw::editor::EditorPreferencesProbe defaults{};
    current._speed = 9.0f;
    const sw::string json = sw::editor::EditorPreferencesStore::makeDifferenceJson( *sw::editor::EditorPreferencesProbe::StaticType(), &current, &defaults );
    SW_EXPECT_TRUE( json.find( "_speed" ) != sw::string::npos );
    SW_EXPECT_TRUE( json.find( "_path" ) == sw::string::npos );
    SW_EXPECT_TRUE( json.find( "_bFlag" ) == sw::string::npos );
}

SW_TEST_CASE( EditorPreferencesStoreTest, ApplyKeepsMissingKeysAndReportsUnknown )
{
    sw::editor::EditorPreferencesProbe probe{};
    probe._path = "keep";
    sw::vector<sw::string> listUnknownKey;
    SW_ASSERT_TRUE( sw::editor::EditorPreferencesStore::applyJson( *sw::editor::EditorPreferencesProbe::StaticType(), &probe,
                                                                   R"({ "_speed": 2.5, "_gone": 1 })", listUnknownKey ) );
    SW_EXPECT_NEAR( probe._speed, 2.5f, 1e-6f );
    SW_EXPECT_TRUE( probe._path == "keep" );
    SW_ASSERT_EQ( listUnknownKey.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( listUnknownKey[0] == "_gone" );
}

SW_TEST_CASE( EditorPreferencesStoreTest, RoundTripThroughTheFile )
{
    // 시험 등록자 하나를 지역으로 두고 saveAll → 값을 기본으로 → loadAll → 값이 돌아온다(임시 폴더 — 이 파일의 다른 시험이 쓰는 ScopedTempDirectory)
}
```
자체 시험 `preferences.searchFiltersSections`(2 차 단위 8 의 입력 창구로 검색 칸에 `snap` 을 쳐 Viewport 섹션만 남는지) + 기대 목록 한 줄.

**확인 = 에디터 시나리오.** `preferences.scenario.xml`: Edit > Preferences 를 커맨드 팔레트로 열고, 검색 칸(이름표 `preferences.search`)에 `EditorText value="snap"` 을 친 뒤
탐침 `Editor.PreferencesVisibleSections` 가 1(Viewport 만)인지 봅니다. 카메라 속도 칸을 바꾸고 0.5 초 넘게 기다린 뒤 `ExpectLog` 로 저장 로그를 보고,
`Saved/Editor/EditorPreferences.json` 에 그 키 하나만 있는지는 탐침 `Editor.PreferencesSavedKeyCount` 로 봅니다. 시험은 임시 저장 경로를 쓰게 시작 인자로 경로를 바꿉니다.

**남길 교훈.** `Source/Editor/README.md` 함정 · 계약 절의 에디터 상태와 설정 줄을 고친다: `사람이 바꾸는 에디터 설정은 환경설정 섹션(SW_EDITOR_SETTINGS — 리플렉션 구조체) 하나, 저장은 Saved/Editor/EditorPreferences.json(기본과 다른 값만). 테마(EditorConfig)도 섹션이다. -gv_editor* 스위치는 주어지면 이긴다(자동화).`
**커밋 메시지:**
```
에디터 - 환경설정 창(섹션 등록 SW_EDITOR_SETTINGS · Saved/Editor/EditorPreferences.json, 바뀐 값만)

문제점:
- 사람이 바꾸는 에디터 설정이 테마 대화상자(EditorConfig) · -gv_ 스위치 · editortooldefaults.json · 코드 기본값으로 흩어져 있었다.
  한곳에서 찾고 바꾸고 저장하는 창이 없었다.

해결방안:
- EditorSettingsRegistration(SW_EDITOR_SETTINGS — 리플렉션 구조체 = 섹션, 확장 모듈도 등록), EditorPreferencesStore(섹션마다 기본과
  다른 프로퍼티만 JSON, 모르는 섹션 · 키는 경고 후 건너뜀).
- 섹션 General(시작 씬 · IDE 열기 명령 · UI 배율) · Viewport(카메라 속도 · 스냅 · 표시 기본값) · Appearance(EditorConfig).
  EditorConfig 의 자체 파일 읽기/쓰기와 EditorToolDefaults::_ideOpenCommand 는 지운다(옛 파일은 읽지 않는다 — 테마는 한 번 다시 고른다).
- PreferencesPanel: 섹션 목록 · EditorPropertyGrid · 전체 검색 · Modified only · Reset Section, 바뀌면 0.5 초 뒤 저장.

결과:
- EditorPreferencesStoreTest 셋, 자체 시험 preferences.searchFiltersSections.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```
**적용 뒤 확인:** re-configure(새 `REFLECT` 헤더), `EditorTest --test_filter=EditorPreferencesStoreTest.*`, 자체 시험, 에디터에서 카메라 속도 바꾸고 다시 띄워 남는지 · `Saved/Editor/EditorPreferences.json` 에 그 키 하나만.
**겹침:** 없음. config-docs 의 `Saved/Editor/` 이동은 이미 들어갔습니다. `EditorToolDefaults` 에는 `_ideOpenCommand` 말고도 사람이 쓰는 시드(`_defaultMap` 등)가 남습니다.

### P3 단축키 편집기 — `Saved/Editor/Shortcuts.json` · 키 받기 · 충돌 ★

**목적.** 커맨드 단축키는 표(`_s_arrCommandRow`)와 등록 줄(C3)에 박혀 있다. 사용자가 바꾸는 길이 없다(유니티 Shortcuts Manager · 언리얼 Keyboard Shortcuts).
쓸 수 있는 키도 A–Z · F1–F12 · Space 뿐이라 `Ctrl+,` · `Delete` · 숫자 · 화살표를 못 건다.

**바꿀 것.**
1) 키 넓히기 — `EditorCommandRegistry.h` 의 `EditorCommandKey` 에 **Space 뒤에**(계약 주석대로): `Num0 … Num9, Delete, Insert, Home, End, PageUp, PageDown, Left, Right, Up, Down, Tab, Enter, Escape, Backspace, Minus, Equal, Comma, Period, Slash, Count`.
   `EditorCommandRegistry.cpp` 의 이름 표에 같은 순서로 이름, `EditorCommandGui.cpp` 의 `toImGuiKey` 는 A..Z · F1..F12 뺄셈 뒤 **새 키는 표**(`kArrExtraKey[]` = `{ EditorCommandKey::Num0, ImGuiKey_0 }, …`).
   Space 뒤 static_assert 하나 더(`Num0 == 40`).
2) 덮어쓰기 저장 — 새 `Common/Commands/EditorShortcutOverrides.h` · `.cpp`(ImGui 없음):
```cpp
namespace sw::editor
{
    /** @brief 커맨드 하나의 사용자 단축키입니다. */
    struct EditorShortcutOverride
    {
        string                _commandId;
        EditorCommandShortcut _shortcut;
        EditorCommandShortcut _altShortcut;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorShortcutOverrides
     * @brief 사용자가 바꾼 단축키입니다(기본과 다른 커맨드만). `Saved/Editor/Shortcuts.json` — `{ "edit.undo": [ "Ctrl+Z", "" ] }`(첫 칸 주 조합, 둘째 칸 보조, "" 는 없음).
     * @details 커맨드 등록부를 만든 뒤(표 + 등록 줄) 입힙니다. 없는 커맨드 id 는 경고 한 번(확장 모듈을 끈 경우) 하고 남겨 둔다 — 다시 켜면 돌아온다.
     */
    class SW_EDITOR_API EditorShortcutOverrides
    {
    public:
        [[nodiscard]] bool loadFromFile( string_view filePath );
        [[nodiscard]] bool saveToFile( string_view filePath ) const;
        /** @brief 등록부의 커맨드에 덮어쓰기를 입힙니다. 입힌 수입니다. */
        uint32 applyTo( EditorCommandRegistry& registry ) const;
        /** @brief @p commandId 의 덮어쓰기를 둡니다. 기본(@p defaultShortcut · @p defaultAlt)과 같으면 지웁니다. */
        void setOverride( string_view commandId, const EditorCommandShortcut& shortcut, const EditorCommandShortcut& altShortcut,
                          const EditorCommandShortcut& defaultShortcut, const EditorCommandShortcut& defaultAlt );
        void removeOverride( string_view commandId );
        void clear() { _listOverride.clear(); }
        const vector<EditorShortcutOverride>& getOverrides() const { return _listOverride; }

        /** @brief "Ctrl+Shift+B" 를 읽습니다(formatShortcutLabel 의 짝). 빈 글은 None. 모르는 키 이름이면 false. */
        [[nodiscard]] static bool parseShortcut( string_view text, EditorCommandShortcut& outShortcut );

    private:
        vector<EditorShortcutOverride> _listOverride;
    };
} // namespace sw::editor
```
`EditorCommandGui::syncWithRegistry`(C3)가 등록부를 다시 만든 뒤 `applyTo` → `validate`. 기본값(표 · 등록 줄의 원래 조합)은 등록부가 `EditorCommandDesc::_defaultShortcut` · `_defaultAltShortcut` 두 칸으로 든다(덮어쓰기 전 값 — "Reset" 이 쓴다).
3) 창 — `Panels/ShortcutsPanel.h` · `.cpp` `SW_EDITOR_PANEL( ShortcutsPanel, "shortcuts", EditorPanelCategory::Tool, 2010 );` 제목 `"Keyboard Shortcuts"`(환경설정 창의 왼쪽 목록 맨 아래에도 링크).
   표: 커맨드(아이콘 · 라벨) · 묶음 · 단축키 · 보조 · 단추. "Set" 을 누르면 그 칸이 **다음 키 조합을 받는다**(수정자만 누른 동안은 기다림, Esc 는 취소, Backspace 는 지움 —
   받는 동안은 전역 단축키 처리(`processHotkeys`)를 멈춘다: `EditorCommandGui::setHotkeysSuspended( true )`). 받은 조합이 다른 커맨드와 같으면 **두 줄을 빨갛게** 하고
   "Replace (clear <다른 커맨드>)" / "Cancel" 을 묻는다. 줄마다 기본과 다르면 "↺" 단추(Reset), 위에 "Reset All" · 검색(라벨 · id · 조합 글자 — `Ctrl+S` 로도 찾는다).
   ImGuiKey → `EditorCommandKey` 는 `toImGuiKey` 의 역(표 하나를 양쪽이 쓴다).

**시험 — `Test/EditorTest/Common/Commands/TestEditorShortcutOverrides.cpp`(새, `EditorShortcutOverridesTest`):**
```cpp
SW_TEST_CASE( EditorShortcutOverridesTest, ParseIsTheInverseOfTheLabel )
{
    sw::editor::EditorCommandShortcut shortcut{};
    SW_ASSERT_TRUE( sw::editor::EditorShortcutOverrides::parseShortcut( "Ctrl+Shift+B", shortcut ) );
    SW_EXPECT_TRUE( shortcut._key == sw::editor::EditorCommandKey::B );
    SW_EXPECT_EQ( shortcut._modifier, static_cast<uint8>( sw::editor::commandmodifier::kCtrl | sw::editor::commandmodifier::kShift ) );
    SW_EXPECT_TRUE( sw::editor::EditorShortcutOverrides::parseShortcut( "Ctrl+Comma", shortcut ) );   // 넓힌 키
    SW_EXPECT_FALSE( sw::editor::EditorShortcutOverrides::parseShortcut( "Ctrl+Banana", shortcut ) );
}

SW_TEST_CASE( EditorShortcutOverridesTest, ApplyChangesOnlyOverriddenCommands )
{
    sw::editor::EditorCommandRegistry registry;
    registry.registerCommand( makeCommand( "edit.undo", { sw::editor::EditorCommandKey::Z, sw::editor::commandmodifier::kCtrl } ) );
    registry.registerCommand( makeCommand( "edit.redo", { sw::editor::EditorCommandKey::Y, sw::editor::commandmodifier::kCtrl } ) );
    sw::editor::EditorShortcutOverrides overrides;
    overrides.setOverride( "edit.undo", { sw::editor::EditorCommandKey::U, sw::editor::commandmodifier::kCtrl }, {},
                           { sw::editor::EditorCommandKey::Z, sw::editor::commandmodifier::kCtrl }, {} );
    SW_EXPECT_EQ( overrides.applyTo( registry ), 1u );
    SW_EXPECT_TRUE( registry.find( "edit.undo" )->_shortcut._key == sw::editor::EditorCommandKey::U );
    SW_EXPECT_TRUE( registry.find( "edit.redo" )->_shortcut._key == sw::editor::EditorCommandKey::Y );
}

SW_TEST_CASE( EditorShortcutOverridesTest, SettingTheDefaultRemovesTheOverride )
{
    sw::editor::EditorShortcutOverrides overrides;
    const sw::editor::EditorCommandShortcut kDefault{ sw::editor::EditorCommandKey::Z, sw::editor::commandmodifier::kCtrl };
    overrides.setOverride( "edit.undo", kDefault, {}, kDefault, {} );
    SW_EXPECT_TRUE( overrides.getOverrides().empty() );
}

SW_TEST_CASE( EditorShortcutOverridesTest, ConflictIsReportedByValidateAfterApply )
{
    // 덮어쓰기로 redo 를 Ctrl+Z 로 → registry.validate 가 중복 조합을 적는다(창은 저장 전에 같은 판정으로 빨갛게 한다)
}
```
(`makeCommand` 는 파일 위 익명 이름공간 도우미.) 자체 시험 `shortcuts.captureAssignsCombo`(2 차 단위 8 의 입력 창구 — "Set" 클릭 → `Ctrl+K` 입력 → 그 커맨드 조합이 바뀌고 저장 파일 없이 되돌림).

**확인 = 에디터 시나리오.** `shortcuts.scenario.xml`: Keyboard Shortcuts 창을 열고 Undo 줄의 "Set" 단추(이름표 `shortcuts.set.edit.undo`)를 누른 뒤 `EditorKey key="U" mods="ctrl"` 을 보냅니다.
이어서 오브젝트를 하나 만들고 `EditorKey key="U" mods="ctrl"` 로 되돌려지는지(탐침 `Editor.UndoIndex`), 충돌 조합(`Ctrl+Y`)을 받으면 탐침 `Editor.ShortcutConflictCount` 가 1 인지 봅니다.
끝에 "Reset All" 을 눌러 원래대로 돌리고, 저장 경로는 임시 경로입니다.

**남길 교훈.** `Source/Editor/README.md` 함정 · 계약 절의 에디터 커맨드 줄에 덧붙임: `사용자 조합은 Saved/Editor/Shortcuts.json(EditorShortcutOverrides — 기본과 다른 커맨드만)이 표 · 등록 줄 위에 입힌다. 키를 더하면 EditorCommandKey 의 Space 뒤 + toImGuiKey 표.`
**커밋 메시지:**
```
에디터 - 단축키 편집기(Saved/Editor/Shortcuts.json · 키 받기 · 충돌 표시)와 키 넓히기

문제점:
- 커맨드 단축키는 표 · 등록 줄에 박혀 사용자가 바꿀 수 없었다. 쓸 수 있는 키가 A-Z · F1-F12 · Space 뿐이었다.

해결방안:
- EditorCommandKey 를 Space 뒤로 넓힌다(숫자 · 편집 · 화살표 · 구두점 25 개), toImGuiKey 는 새 키를 표로.
- EditorShortcutOverrides(기본과 다른 커맨드만 · parseShortcut 은 라벨의 역) — 등록부를 만든 뒤 입히고 validate.
  EditorCommandDesc 에 원래 조합(_defaultShortcut · _defaultAltShortcut).
- Keyboard Shortcuts 창: 검색(조합 글자 포함) · Set(다음 조합을 받음, 받는 동안 전역 단축키 멈춤) · 충돌 두 줄 빨강 + 바꾸기/취소 · Reset · Reset All.

결과:
- EditorShortcutOverridesTest 넷, 자체 시험 shortcuts.captureAssignsCombo.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```
**적용 뒤 확인:** `EditorTest --test_filter=EditorShortcutOverridesTest.*:EditorCommandRegistryTest.*`, 자체 시험, 에디터에서 Undo 를 Ctrl+U 로 바꾸고 재시작 뒤 남는지.

### P4 모듈 창 — 켜고 끄기 · 의존 미리보기 · 구성/빌드

**목적.** 모듈(키트 · 에디터 확장 · RHI 백엔드)을 켜고 끄는 길은 프로젝트 매니페스트(`SWGame.module.json` 의 `_listModuleOverride`)를 손으로 고치고 re-configure 하는 것뿐이다.
게다가 **매니페스트 내용을 바꿔도 CMake 가 다시 구성되지 않는다**(`GLOB_RECURSE CONFIGURE_DEPENDS` 는 파일 목록만 본다 — `file(READ)` 한 내용은 구성 의존이 아니다) — 손으로 고쳐도 빌드가 옛 답으로 돈다.

**바꿀 것.**
1) (작은 결함) `cmake/Engine/ModuleManifest.cmake:135` 뒤 한 줄 — 매니페스트 내용이 바뀌면 다음 빌드가 다시 구성한다:
```cmake
	# 매니페스트의 **내용**(켜짐 · 의존 · 종류)이 무엇을 지을지 정한다 — 내용이 바뀌면 다시 구성해야 한다(GLOB 의 CONFIGURE_DEPENDS 는 목록만 본다).
	set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${swListManifest})
```
2) ImGui 없는 도우미 — `Common/Commands/EditorModuleOverrides.h` · `.cpp`:
```cpp
    struct SW_EDITOR_API EditorModuleOverrideUtil
    {
        /**
         * @brief 프로젝트 매니페스트 글(@p manifestJson)의 `_listModuleOverride` 에서 @p moduleName 을 @p bEnabled 로 둔 새 글을 씁니다.
         * @details 그 모듈의 기본(`_bEnabledByDefault`)과 같아지면 줄을 지운다 — 덮어쓰기는 기본과 다를 때만 남는다(설정 파일 원칙).
         *          키 순서 · 들여쓰기는 저장소의 JSON 모양(4 칸)으로 다시 쓴다. 형식이 틀리면 false.
         */
        [[nodiscard]] static bool setOverride( string_view manifestJson, string_view moduleName, bool bEnabled, bool bEnabledByDefault, string& outManifestJson );
        /**
         * @brief @p catalog 를 지금 덮어쓰기에 @p moduleName = @p bEnabled 를 더한 것으로 다시 풀어, 켜짐이 바뀌는 모듈(그 모듈 + 의존 때문에 함께 바뀌는 것)을 모읍니다.
         * @return 풀기에 실패하면(순환 · 없는 의존) false 이고 @p outError 에 이유
         */
        [[nodiscard]] static bool previewToggle( const ModuleCatalog& catalog, const ModuleResolveContext& context, string_view moduleName, bool bEnabled,
                                                 vector<ModuleInactiveEntry>& outListNewlyInactive, vector<string>& outListNewlyActive, string& outError );
    };
```
`ModuleCatalog` 이 프로젝트 덮어쓰기를 바꿔 다시 풀 수 있게 `ModuleCatalog::setProjectOverride( name, bEnabled )`(사본에서) 를 더한다 — 적용 때 `ModuleCatalog.h` 의 `resolve` 가 덮어쓰기를 어디서 읽는지 보고 맞춘다.
3) 창 — `Panels/ModulesPanel.h` · `.cpp` `SW_EDITOR_PANEL( ModulesPanel, "modules", EditorPanelCategory::Tool, 2020 );` 제목 `"Modules"`. `Bin/Modules/*.module.json`(빌드가 복사한 카탈로그)을 읽어
   종류별 묶음(GameFramework · Kit · EditorExtension · Rhi · Editor · Game) 표: 이름 · 판 · 설명 · 의존 · 상태(켜짐 · 꺼짐 + 이유 — `ModuleResolution::_listInactive` 의 이유 글).
   체크박스를 바꾸면 미리보기 팝업("이것도 함께 꺼진다: GF_Editor_ThemePark (needs GF_ThemePark)") → 확인하면 **프로젝트 매니페스트 소스**(`Source/Games/<활성 게임>/SWGame.module.json`)를 고쳐 쓴다
   (`EditorSourceControl` 의 체크아웃 상태를 먼저 본다 — 읽기 전용이면 이유를 알린다). 위에 노란 띠: "Module set changed — Build to apply, then restart" + "Build" 단추(= `build.compileAll`,
   1) 덕에 ninja 가 다시 구성한다) + "Restart Editor" 단추(빌드 성공 뒤 활성 — 지금 실행 인자 그대로 새 프로세스를 띄우고 이 프로세스는 종료 확인 경로로 닫는다).
   Editor · Game 종류 줄은 끌 수 없다(회색 — 끄면 이 창이 사라진다). RHI 백엔드를 끄면 "이 백엔드로 실행 중" 이면 막는다.

**시험 — `Test/EditorTest/Common/Commands/TestEditorModuleOverrides.cpp`(`EditorModuleOverridesTest`):**
- `SetOverrideAddsAndRemovesLine` — 기본 켜짐 모듈을 끄면 줄 하나, 다시 켜면 줄이 사라진다(빈 배열).
- `PreviewIncludesDependents` — 시험 카탈로그(키트 A, A 에 의존하는 확장 B)에서 A 를 끄면 새로 꺼지는 목록이 A · B, B 의 이유가 의존.
- `PreviewReportsCycleError` — 순환 카탈로그면 false 와 이유.
(`ModuleCatalog::addManifest` 로 시험 카탈로그를 만든다 — `ModuleCatalogTest` 가 쓰는 길.)
자체 시험 `modules.panelListsKits` — 창을 열어 `GF_ThemePark` 줄이 있는지(쓰기는 하지 않는다).

**확인 = 에디터 시나리오.** `modules.scenario.xml`: Modules 창을 열고 `GF_ThemePark` 의 체크박스(이름표 `modules.toggle.GF_ThemePark`)를 눌러 미리보기 팝업이 뜨는지(탐침 `Editor.ModulePreviewNewlyInactive` 가 2 — 키트와 그 확장)를 봅니다.
"Cancel" 을 눌러 매니페스트를 쓰지 않고 끝냅니다. 실제 끄기, 빌드, 재시작은 시나리오로 돌리지 않습니다(빌드 시간이 길고 소스 파일을 바꿉니다). 그 부분은 `EditorModuleOverridesTest` 가 봅니다.

**남길 교훈.** `Source/Engine/Module/README.md` 함정 · 계약 절에 한 줄: `- **매니페스트 내용도 구성 의존이다**(CMAKE_CONFIGURE_DEPENDS) — 켜짐을 바꾸면 다음 빌드가 다시 구성한다. 켜고 끄기는 Modules 창(프로젝트 _listModuleOverride, 기본과 다를 때만 줄).`
**커밋 메시지:**
```
에디터 - 모듈 창(켜고 끄기 · 의존 미리보기 · 빌드/재시작)과 매니페스트 내용을 구성 의존으로

문제점:
- 모듈을 켜고 끄려면 프로젝트 매니페스트를 손으로 고치고 re-configure 해야 했다. 매니페스트 내용은 구성 의존이 아니라
  고쳐도 다음 빌드가 옛 답으로 돌았다(GLOB CONFIGURE_DEPENDS 는 파일 목록만 본다).

해결방안:
- ModuleManifest.cmake: 매니페스트 파일을 CMAKE_CONFIGURE_DEPENDS 에 더한다.
- EditorModuleOverrideUtil: 프로젝트 매니페스트의 _listModuleOverride 고쳐 쓰기(기본과 같으면 줄 삭제) · 켜고 끔의 미리보기(카탈로그를 다시 풀어
  함께 바뀌는 모듈과 이유).
- Modules 창: 종류별 표 · 상태와 이유 · 미리보기 확인 · 소스 매니페스트 쓰기(체크아웃 확인) · Build · Restart Editor.

결과:
- EditorModuleOverridesTest 셋, 자체 시험 modules.panelListsKits.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```
**적용 뒤 확인:** `EditorTest --test_filter=EditorModuleOverridesTest.*`, 손으로: 키트 하나 끄기 → Build(ninja 가 cmake 를 다시 도는지 로그) → Restart → 그 키트 DLL 이 안 올라옴. **WSL:** CI 로 확인(CMake 변경).
---

## 6. 단계 3 — 인스펙터 · 콘텐츠 브라우저 ★

**상용 비교.** 언리얼 Details 는 여러 액터를 고르면 **공통 프로퍼티를 함께 고치고** 값이 다르면 "Multiple Values" 를 보이며, 기본값과 다른 프로퍼티 옆에 **노란 되돌리기 화살표**,
오른쪽 클릭 Copy/Paste(프로퍼티 글), 타입별 그리기는 `IPropertyTypeCustomization`. 유니티 Inspector 도 다중 편집(`—` 표시, `[CanEditMultipleObjects]`), 프리팹 오버라이드는 굵은 글씨 ·
"Revert", `PropertyDrawer`(`[CustomPropertyDrawer(typeof(T))]`). Godot 인스펙터도 다중 편집 · 되돌리기 아이콘 · `EditorInspectorPlugin`.
콘텐츠 쪽: 언리얼 Content Browser 는 프로젝트 콘텐츠가 기본(엔진 · 플러그인 콘텐츠는 보기 옵션), **Reference Viewer**(참조하는 것 · 참조되는 것), 이름 바꾸면 리디렉터 + "Fix Up".
유니티 Project 창 "Find References In Scene" · 의존 검색(`AssetDatabase.GetDependencies`). 우리 인스펙터는 다중 선택에서 첫 오브젝트만 고치고(O8), 되돌리기는 오른쪽 클릭 메뉴에만 숨어 있다.

### I1 다중 선택 편집 — 공통 컴포넌트 · 프로퍼티를 모든 선택에, 다른 값 표시, 한 트랜잭션 ★

**목적.** O8 — `InspectorPanel::drawSelectionSection` 은 둘 이상 고르면 "Multi-Selection (N objects)" 한 줄을 그리고 **주 선택만** 그린다(나머지는 바뀌지 않는다).

**설계.** 언리얼 · 유니티와 같다: (1) 모든 선택이 가진 컴포넌트 타입(교집합 — 같은 타입이 여럿이면 각 오브젝트의 첫 것)만 카드로 그린다. (2) 그리는 값은 주 선택의 것, 다른 오브젝트와
값이 다르면 이름 칸에 `—`(혼합) 표시 · 툴팁 "Multiple values". (3) 값을 고치면 **고친 프로퍼티 하나의 글**(`SerializerUtil` 글 쓰기)을 나머지 오브젝트의 같은 컴포넌트에 입힌다 —
고친 프로퍼티만이라 다른 프로퍼티의 혼합 값은 그대로다. Undo 는 오브젝트들을 한 트랜잭션(`EditorTransaction::beginTransaction( "Edit N objects" )`)으로. (4) GameObject 헤더(이름 · 태그)는
이름은 주 선택만, 활성 · 태그 더하기는 모두에.

**새 파일 — ImGui 없는 판단 `Common/Commands/EditorMultiEdit.h` · `.cpp`:**
```cpp
namespace sw::editor
{
    /** @brief 다중 편집의 공통 컴포넌트 하나 — 오브젝트마다 그 타입의 첫 컴포넌트(선택 순서). */
    struct EditorMultiEditComponent
    {
        const TypeInfo*    _pType{ nullptr };
        vector<Component*> _listComponent; ///< 선택 순서 — 첫 원소가 주 선택의 것
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorMultiEditUtil
     * @brief 여러 오브젝트를 한 번에 고치는 판단입니다(ImGui 없음 — EditorTest 가 본다).
     */
    struct SW_EDITOR_API EditorMultiEditUtil
    {
        /** @brief 모든 오브젝트가 가진 컴포넌트 타입을 주 선택의 컴포넌트 순서대로 모읍니다(먼저 비운다). 오브젝트가 하나면 그 오브젝트의 컴포넌트 전부. */
        static void collectCommonComponents( const vector<GameObject*>& listObject, vector<EditorMultiEditComponent>& outListCommon );
        /** @brief 프로퍼티 @p prop 이 @p listInstance 사이에서 다르면 true 입니다(프로퍼티 글로 비교). */
        static bool hasMixedValues( const PropertyInfo& prop, const vector<const void*>& listInstance );
        /**
         * @brief 첫 인스턴스의 @p prop 값을 나머지에 입힙니다(프로퍼티 글 하나 — 다른 프로퍼티는 건드리지 않는다). 입힌 수입니다.
         * @details 컴포넌트마다 `onPropertyChanged` 를 부릅니다. Undo 기록은 부르는 쪽(인스펙터)이 트랜잭션으로 감쌉니다.
         */
        static uint32 copyPropertyToOthers( const PropertyInfo& prop, const vector<Component*>& listComponent );
    };
} // namespace sw::editor
```
**인스펙터.** `drawSelectionSection` 에서 `getSelectedObjects( listObject )` → 2 개 이상이면 `drawMultiSelection( listObject )`: 헤더(N objects · 주 선택 이름), 공통 컴포넌트마다 카드 —
`EditorPropertyGridTarget` 의 `_onEdited` 를 "그 프로퍼티를 나머지에 복사 + 트랜잭션" 으로 두고, 그리드에 **혼합 판정 콜백**(`EditorPropertyGridTarget::_pfnIsMixed` + `_pMixedUserData`)을 더해
이름 칸 앞에 `—` 를 그린다(P1 의 그리드에 칸 하나 · 그리기 한 줄). 공통이 아닌 컴포넌트 수는 맨 아래 회색 한 줄("3 components are not on every selected object").
Add Component 는 모두에(같은 타입이 이미 있는 오브젝트는 건너뜀).

**시험 — `Test/EditorTest/Common/Commands/TestEditorMultiEdit.cpp`(`EditorMultiEditTest`, `EditorTestServices.h` 의 씬 도우미):**
- `CommonComponentsAreTheIntersection` — A(Transform · Mesh · Light) · B(Transform · Mesh) → Transform · Mesh.
- `MixedValuesAreDetected` — 두 오브젝트 위치가 다르면 `_position` 혼합 true, 같은 스케일은 false.
- `CopyChangesOnlyThatProperty` — A 의 스케일만 바꿔 복사 → B 의 스케일은 같아지고 B 의 위치는 그대로.
자체 시험 `inspector.multiEditAppliesToAll` — 오브젝트 둘 만들어 고르고(시험이 선택을 넣는다) 2 차 단위 8 의 입력 창구로 스케일 칸에 값을 쳐 둘 다 바뀌는지, Ctrl+Z 한 번에 둘 다 돌아오는지.

**확인 = 에디터 시나리오.** `multiedit.scenario.xml`: 시험 씬의 큐브 둘을 `EditorClick` 과 `mods="ctrl"` 로 함께 고르고(탐침 `Editor.SelectionCount` 가 2), 인스펙터의 스케일 칸에 값을 친 뒤
두 오브젝트의 스케일 탐침이 모두 바뀌었는지, `EditorKey key="Z" mods="ctrl"` 한 번에 둘 다 돌아오는지 봅니다.

**남길 교훈.** 없음.
**커밋 메시지:**
```
에디터 - 인스펙터 다중 선택 편집(공통 컴포넌트 · 혼합 값 표시 · 한 트랜잭션)

문제점:
- 오브젝트를 둘 이상 고르면 인스펙터가 "Multi-Selection (N objects)" 한 줄과 주 선택만 그렸다. 고친 값은 주 선택에만 들어갔다.

해결방안:
- EditorMultiEditUtil(ImGui 없음): 공통 컴포넌트(교집합, 주 선택 순서) · 혼합 판정(프로퍼티 글 비교) · 고친 프로퍼티 하나만 나머지에 복사.
- 인스펙터: 공통 컴포넌트 카드를 그리고 혼합이면 이름 앞에 "—", 고치면 나머지에 복사 + 한 트랜잭션. 공통이 아닌 컴포넌트 수 표시,
  Add Component 는 모두에.

결과:
- EditorMultiEditTest 셋, 자체 시험 inspector.multiEditAppliesToAll.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```
**적용 뒤 확인:** `EditorTest --test_filter=EditorMultiEditTest.*`, 자체 시험, 손으로 큐브 셋 골라 스케일 · 색 바꾸고 Ctrl+Z.

### I2 기본값과 다름 표시 · 되돌리기 화살표 · 프로퍼티 값 복사/붙여넣기 ★

**목적.** "Reset to Default" 는 이미 있다 — 그러나 오른쪽 클릭 메뉴 안이라 **무엇이 기본과 다른지 보이지 않는다**(언리얼 노란 화살표 · 유니티 굵은 글씨). 값 복사도 없다.

**바꿀 것 — `EditorPropertyGrid::drawProperties`(P1 뒤):**
1) 이름 칸 오른쪽 끝에, `prop->_metadata._defaultValue` 가 있고 지금 값의 글(`SerializerUtil` 글 쓰기)이 그것과 다르면 작은 `ICON_FA_ROTATE_LEFT` 단추 — 누르면 기존 Reset 경로(같은 람다를 함수로 뺀 `resetPropertyToDefault`).
   글 비교는 프레임마다 프로퍼티마다 하므로 **보이는 줄만**(테이블 클리퍼 안) 한다. 숫자는 글이 아니라 값 비교가 맞다(`1` vs `1.0`) — 판정은 ImGui 없는 `EditorPropertyDefaultUtil::isDefaultValue( prop, pInstance )` 하나
   (기본값 글을 같은 타입 임시 값으로 읽어(`applyPropertyText` → 임시 인스턴스 대신 `PropertyInfo` 의 값 크기 버퍼) 값 비교 — 실수는 상대 1e-6).
2) 오른쪽 클릭 메뉴에 "Copy Value"(프로퍼티 글을 클립보드로) · "Paste Value"(클립보드 글을 `applyPropertyText` — 실패하면 경고, Undo 기록은 Reset 과 같은 길).
3) 기본값 메타가 없는 프로퍼티는 표시하지 않는다(리플렉션 파서가 기본값을 뽑지 못한 것 — 많으면 백로그 1-1 에 한 줄).

**시험 — `Test/EditorTest/Panels/TestEditorPropertyDefault.cpp`(`EditorPropertyDefaultTest`):** `EditorPreviewProbe.h` 의 시험 구조체로 기본 그대로 → true, 바꾸면 false, `1.0` 기본에 `1` 이 들어 있어도 true(글이 아닌 값 비교).

**확인 = 에디터 시나리오.** `propertyreset.scenario.xml`: 오브젝트를 고르고 프로퍼티 하나를 바꾼 뒤, 되돌리기 화살표(이름표 `inspector.reset.<컴포넌트>.<프로퍼티>`, 기본과 다를 때만 남음)를 `EditorClick` 으로 누르고 값 탐침이 기본값으로 돌아왔는지 봅니다.
값이 기본과 같을 때는 그 이름표가 없어야 하므로, 클릭 단계가 이름표를 못 찾는 경우를 `Expect` 탐침(`Editor.MarkVisible.<이름>`)으로 따로 봅니다.

**커밋 메시지:**
```
에디터 - 인스펙터가 기본값과 다른 프로퍼티에 되돌리기 화살표를 보이고 값 복사/붙여넣기를 둔다

문제점:
- Reset to Default 가 오른쪽 클릭 메뉴 안에만 있어 어느 값이 기본과 다른지 보이지 않았다. 프로퍼티 값을 다른 오브젝트로 옮길 길도 없었다.

해결방안:
- EditorPropertyDefaultUtil::isDefaultValue(ImGui 없음, 기본값 글을 같은 타입 값으로 읽어 값 비교 — 실수 상대 1e-6).
- 그리드: 기본과 다르면 이름 칸 끝에 되돌리기 단추(보이는 줄만 판정), 메뉴에 Copy Value · Paste Value(글 — Undo 는 Reset 과 같은 길).

결과:
- EditorPropertyDefaultTest 셋.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```

### I3 프로퍼티 그리기 확장 `SW_EDITOR_PROPERTY_DRAWER` — 유니티 PropertyDrawer

**목적.** 타입별 위젯(`IInspectorProperty`)은 이미 있다 — 그러나 `InspectorPropertyManager::registerDefaults` 가 내장 타입 표(`ReflectBuiltins.xxx`)만 등록하고 확장 · 게임이 더할 줄이 없다.
확장 모듈이 자기 값 타입(예: 키트의 `ParkRidePlacement`, T1 의 `FloatCurve`)에 위젯을 다는 길을 만든다.

**바꿀 것.**
- `Panels/Inspector/IInspectorProperty.h` 끝:
```cpp
namespace sw::editor
{
    /**
     * @struct EditorPropertyDrawerRegistration
     * @brief 프로퍼티 타입 하나의 그리기 확장 등록 줄입니다. id 는 타입 이름(리플렉션 `_typeName` 과 같은 글 — 내장은 `float32` · 구조체는 `sw::FloatCurve`).
     * @details 같은 타입에 둘째 등록은 거절됩니다. 내장 타입 표(`ReflectBuiltins.xxx`)의 위젯도 이 줄로 오른다(EditorModule 이 표를 펼쳐 등록 줄을 둔다).
     */
    struct EditorPropertyDrawerRegistration : EditorRegistration
    {
        static constexpr const utf8* kKindName = "propertydrawer";

        unique_ptr<IInspectorProperty> ( *_pCreate )();
    };
} // namespace sw::editor

/** @brief 프로퍼티 타입 그리기를 등록합니다. 예: `SW_EDITOR_PROPERTY_DRAWER( FloatCurve, "sw::FloatCurve", FloatCurvePropertyDrawer );` */
#define SW_EDITOR_PROPERTY_DRAWER( name, pTypeName, TDrawer )                                                       \
    SW_EDITOR_REGISTER( ::sw::editor::EditorPropertyDrawerRegistration, PropertyDrawer_##name, { pTypeName, 0 }, \
                        &::sw::editor::createInspectorProperty<TDrawer> )
```
- `InspectorPropertyManager` 를 C2 의 다른 매니저와 같은 모양으로: `registerDefaults` → `syncWithRegistry()` + `releaseDrawersWithin(…)`; 내장 표는 `InspectorPropertyManager.cpp` 에서
  `SW_REFLECT_BUILTIN_TYPE` 매크로로 **등록 줄을 펼친다**(지금 `registerType` 호출을 펼치는 것과 같은 표 한 번).
- README 의 "패널 · 팝업 · 인스펙터 · 시각화를 하나 더하려면" 표에 한 줄: `| 프로퍼티 타입 그리기 | SW_EDITOR_PROPERTY_DRAWER( Name, "sw::Type", Drawer ); | (타입 이름이 정함) |`.

**시험.** `InspectorBuiltinValueTest` 그대로 통과 + `AppSmokeTest.EditorRegistriesKeepTheirOrder` 의 기대 목록이 `propertydrawer` 줄을 받는다(덤프에 새 종류 — `EditorRegistryDump` 가 종류 목록을 돈다면 한 줄 더).
`InspectorComponentSyncTest`(C2)에 그리기 확장 하나(지역 등록자 → 찾음 → 소멸 → 못 찾음).

**확인 = 에디터 시나리오.** 이 단위만으로는 새 그리기 확장이 없습니다. I2 와 P1 의 시나리오가 그대로 통과하는지(내장 위젯이 등록 줄로 바뀌어도 같은 동작) 보고, 첫 사용자인 T1 이 커브 시나리오를 더합니다.

**남길 교훈.** `Source/Editor/README.md` 함정 · 계약 절의 "인스펙터 위젯 · CallInEditor 인자는 `ReflectBuiltins.xxx` 를 펼친 표 하나" 줄에 덧붙임: `확장 · 게임 타입은 SW_EDITOR_PROPERTY_DRAWER(내장 표도 같은 등록 줄로 펼친다).`
**커밋 메시지:**
```
에디터 - 프로퍼티 타입 그리기 확장 SW_EDITOR_PROPERTY_DRAWER(유니티 PropertyDrawer 자리)

문제점:
- 타입별 위젯(IInspectorProperty)은 내장 타입 표로만 등록돼 확장 모듈 · 게임이 자기 값 타입에 위젯을 달 수 없었다.

해결방안:
- EditorPropertyDrawerRegistration + SW_EDITOR_PROPERTY_DRAWER(id = 리플렉션 타입 이름). 내장 표도 같은 등록 줄로 펼친다.
- InspectorPropertyManager 를 다른 매니저와 같은 모양으로(syncWithRegistry · releaseDrawersWithin).

결과:
- 내장 위젯 동작 그대로(InspectorBuiltinValueTest), InspectorComponentSyncTest 에 그리기 확장 줄 추가. 인스펙터 개선(I1 ~ I3) 끝.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```

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

## 7. 단계 4 — 캡처 · 디버그 · 품질 ★

**상용 비교.** 언리얼: 뷰포트 View Mode(Lit · Unlit · Wireframe · World Normal · Scene Depth · Quad Overdraw · Shader Complexity), `HighResShot` · 뷰포트 스크린샷, RenderDoc 플러그인(뷰포트 단추 · `renderdoc.CaptureFrame`),
Insights(별도 앱 — 타임라인), `ensure` 실패 대화상자(무인 실행에선 로그만), `BugIt`(스크린샷 + 카메라 자리 + 로그를 `Saved/BugIt/` 에, `BugItGo` 로 재현), Session Frontend > Automation(시험 목록 · 실행 · 결과).
유니티: Scene view Draw Mode(Shaded · Wireframe · Overdraw · Mipmaps · Normals …), Game view 스크린샷(`ScreenCapture`), Frame Debugger · RenderDoc 캡처 단추, Profiler Timeline(에디터 안), Test Runner 창(EditMode · PlayMode).
우리: 보기 모드 셋(Lit, Unlit, Wireframe), Tracy(바깥), 자체 시험 · 시나리오는 명령줄로만, assert 는 디버거 없으면 프로세스가 죽는다.

### G1 보기 모드 Normals · Depth · Overdraw ★

**바꿀 것.**
1) `Engine/Renderer/Frame/FrameRendererUtil.h` — 열거형과 define 표:
```cpp
    enum class RenderViewMode : uint8
    {
        Lit = 0,   ///< 조명 · 그림자를 모두 계산한 기본 화면
        Unlit,     ///< 알베도만. 조명 항이 셰이더에서 컴파일 아웃됨
        Wireframe, ///< 삼각형 외곽선만(RHIFillMode::Wireframe)
        Normals,   ///< 월드 노멀을 색으로(0.5 + 0.5 n)
        Depth,     ///< 카메라에서의 거리(선형, kViewModeDepthRange 에서 흰색)
        Overdraw,  ///< 겹쳐 그린 횟수 — 단색을 깊이 테스트 없이 가산(유니티 Overdraw)

        Count
    };

    /** @brief 보기 모드 하나의 표 줄입니다. 셰이더 define · PSO 조정 · 이름이 한 줄에 있다. */
    struct RenderViewModeInfo
    {
        const utf8* _pName;
        const utf8* _pShaderDefine;   ///< nullptr 이면 셰이더를 바꾸지 않는다(Lit · Wireframe)
        bool        _bWireframe;
        bool        _bAdditiveNoDepth; ///< Overdraw — 가산 블렌드, 깊이 테스트 · 쓰기 끔, 컬링 끔
    };

    /** @brief 보기 모드 표 — 쿠커 · PSO 변형 · 툴바 · 로그가 모두 이것을 읽는다. 줄 순서 = 열거값. */
    inline constexpr RenderViewModeInfo kArrRenderViewModeInfo[] = {
        { "Lit", nullptr, false, false },
        { "Unlit", "SW_VIEWMODE_UNLIT=1", false, false },
        { "Wireframe", nullptr, true, false },
        { "Normals", "SW_VIEWMODE_NORMALS=1", false, false },
        { "Depth", "SW_VIEWMODE_DEPTH=1", false, false },
        { "Overdraw", "SW_VIEWMODE_OVERDRAW=1", false, true },
    };
    static_assert( std::size( kArrRenderViewModeInfo ) == static_cast<size_t>( RenderViewMode::Count ), "보기 모드 표와 열거형의 개수가 다릅니다" );
```
`kViewModeUnlitDefine` 상수와 `findViewModeDefine` 은 표를 읽게 바꾼다(`return kArrRenderViewModeInfo[index]._pShaderDefine;`). `FrameRendererPSO.cpp` `applyViewModeToDesc`:
```cpp
            const RenderViewModeInfo& info = kArrRenderViewModeInfo[static_cast<uint8>( viewMode )];
            if ( info._bWireframe )
            {
                desc._fillMode = RHIFillMode::Wireframe;
                desc._cullMode = RHICullMode::None;
                bChanged       = true;
            }
            if ( info._bAdditiveNoDepth )
            {
                // 겹쳐 그린 수를 세려면 가려진 것도 그려야 한다 — 깊이를 끄고 단색을 더한다(유니티 Overdraw 와 같다).
                desc._bEnableBlend      = 1;
                desc._blendMode         = RHIBlendMode::Additive;   // RHI 블렌드 칸 이름은 RHIPipelineStateDesc 에 맞춘다
                desc._bEnableDepthTest  = 0;
                desc._bEnableDepthWrite = 0;
                desc._cullMode          = RHICullMode::None;
                bChanged                = true;
            }
```
(`RHIPipelineStateDesc` 에 가산 블렌드가 없으면 — 2 차 d9a5ffd7 의 머티리얼 블렌드 모드 표에 `Additive` 가 있는지 본다. 없으면 이 단위가 표에 한 줄 더하고 ABI +1.)
`setViewMode` 로그의 이름 배열 → 표의 `_pName`. 에디터 툴바 콤보(`EditorViewportToolbar.cpp:80` 근처)도 표를 돈다. `gv_viewMode` 설명 글 "(0 Lit / 1 Unlit / 2 Wireframe / 3 Normals / 4 Depth / 5 Overdraw)".
2) 셰이더 — `lighting.hlsli`(E3 의 매크로 옆):
```hlsl
static const float kViewModeDepthRange   = 100.0f; // Depth 보기: 이 거리(m)에서 흰색
static const float kViewModeOverdrawStep = 0.08f;  // Overdraw 보기: 한 번 그릴 때 더하는 밝기(12 겹이면 흰색)

// 보기 모드가 셰이딩을 바꾸면 그 색을 돌려준다(true). Lit 은 false — 부르는 쪽이 조명을 계산한다.
bool swApplyViewMode( float3 albedo, float3 worldNormal, float3 worldPosition, out float3 outColor )
{
#if defined( SW_VIEWMODE_UNLIT )
	outColor = albedo;
	return true;
#elif defined( SW_VIEWMODE_NORMALS )
	outColor = worldNormal * 0.5f + 0.5f;
	return true;
#elif defined( SW_VIEWMODE_DEPTH )
	outColor = saturate( length( worldPosition - g_CameraPos.xyz ) / kViewModeDepthRange ).xxx;
	return true;
#elif defined( SW_VIEWMODE_OVERDRAW )
	outColor = float3( kViewModeOverdrawStep, kViewModeOverdrawStep * 0.5f, kViewModeOverdrawStep * 0.15f );
	return true;
#else
	outColor = float3( 0.0f, 0.0f, 0.0f );
	return false;
#endif
}
```
(카메라 위치 상수는 `g_CameraPos`. E3 은 5b 에서 들어가므로 아래의 매크로 이름과 시험 이름은 5b 가 실제로 둔 것에 맞춘다.) E3 의 `#if SW_VIEWMODE_SKIPS_LIGHTING` 갈래를 `float3 viewModeColor; if ( swApplyViewMode( albedo.rgb, normal, input.worldPosition, viewModeColor ) ) return swStoreSurface( float4( viewModeColor, 1.0f ), albedo, normal );` 로 바꾼다
(define 마다 컴파일 아웃되므로 런타임 분기가 아니다). `SW_VIEWMODE_SKIPS_LIGHTING` 는 이 함수로 대체되어 지운다. 디퍼드: `deferredlighting.hlsl` 이 G버퍼 노멀 · 깊이로 같은 함수를 부른다(Normals · Depth),
Overdraw 는 G버퍼 패스가 가산(알베도 칸에 단색)이고 Lighting 은 알베도를 그대로. 톤맵이 색을 바꾸지 않게 보기 모드가 Lit 이 아니면 톤맵 패스는 통과(`tonemap.hlsl` 에 같은 define — Present 패스 종류에 kAppliesViewMode).
3) **적용 뒤 `App.exe --cook-shaders`** — 쿠커가 표를 끝까지 돈다(셰이더 변형 수가 모드 수만큼 는다: 머티리얼 셰이더 × 셋 — 매니페스트 크기를 커밋 메시지에 적는다).

**시험.** `RenderPassGpuTest.ViewModesProduceDistinctPictures`(E3 의 시험을 넓힌다): 포워드 · 디퍼드에서 Lit · Unlit · Normals · Depth · Overdraw 다섯 장이 서로 100 픽셀 넘게 다르다,
Normals 는 위를 보는 면의 G 채널 평균 > 200(노멀 +Y → 0.5 + 0.5 = 1.0), Overdraw 는 겹친 큐브 둘의 겹친 자리가 하나뿐인 자리보다 밝다(모서리 기준 배경을 빼고 평균으로 비교한다. 특정 색 픽셀 수는 톤매핑에 무너진다).

**확인 = 에디터 시나리오.** `viewmodes.scenario.xml`: 뷰포트 툴바의 보기 모드 콤보(이름표 `viewport.viewMode`)로 모드를 하나씩 고르고 그때마다 `Screenshot` 을 찍습니다.
`ExpectImage metric="differentFrom"` 으로 각 장이 Lit 장과 다른지 보고, Normals 장은 위를 보는 면 영역의 지표로 봅니다. 에디터 실행의 스크린샷은 주 출력 그림입니다 — 게임 뷰가 보이면 게임 뷰, 씬 뷰만 보이면 씬 뷰(V1). 보기 모드는 씬 뷰 툴바에 있으므로 Scene 탭을 앞에 둔 채 찍습니다.
지금 보기 모드는 `FrameRenderer` 전역이라 씬 뷰와 게임 뷰가 같이 보일 때 게임 뷰에도 걸린다(V1 에서 남긴 것) — G1 에서 `RenderViewSettings` 로 뷰마다 갖게 해 게임 뷰는 늘 Lit 으로 둔다(유니티 Scene 뷰 Draw Mode 와 같다).

**커밋 메시지:**
```
렌더러 - 보기 모드 Normals · Depth · Overdraw(표 하나로 define · PSO 조정 · 이름)

문제점:
- 보기 모드가 Lit · Unlit · Wireframe 셋이라 노멀 · 깊이 · 겹쳐 그리기를 볼 길이 없었다. 모드마다 define 상수 · 로그 이름 배열 · 툴바
  목록이 따로였다.

해결방안:
- RenderViewModeInfo 표(이름 · 셰이더 define · 와이어프레임 · 가산/깊이 끔) 하나를 쿠커 · PSO 변형 · 툴바 · 로그가 읽는다.
- lighting.hlsli swApplyViewMode — 다섯 셰이더 · deferredlighting · tonemap 이 define 으로 갈래를 고른다(런타임 분기 없음).
  Overdraw 는 깊이를 끄고 단색을 더한다.
- 셰이더 다시 쿠킹(변형 수 증가: …).

결과:
- RenderPassGpuTest.ViewModesProduceDistinctPictures(다섯 장 서로 다름 · 위 면 노멀 G · 겹친 자리 밝기).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```
**적용 뒤 확인:** 쿠킹 → host 시험 네 백엔드, `-gv_viewMode=3/4/5` 스크린샷 넷 × 셋을 눈으로. **겹침:** E3(앞), gfx-editor-rest 3 · shadow-fix(셰이더 다른 줄), 4 차 runtime-ui(UI 패스는 보기 모드를 받지 않는다 — 표 플래그 확인).

### G4 프로파일러 스레드 미니 타임라인

**바꿀 것.**
1) 새 `Engine/Profiling/ProfilerTimeline.h` · `.cpp` — 켤 때만 사건을 스레드별 링 버퍼에 남긴다:
```cpp
namespace sw
{
    /** @brief 타임라인 사건 하나 — 계측 구간 하나의 시작 · 끝(단조 시계 나노초)과 깊이. */
    struct ProfilerTimelineEvent
    {
        uint64 _beginNanos{ 0 };
        uint64 _endNanos{ 0 };
        uint32 _slot{ 0 };  ///< FrameProfiler 슬롯(이름은 FrameProfiler 가 든다)
        uint16 _depth{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ProfilerTimeline
     * @brief 계측 구간(SW_PROFILE_SCOPE)을 스레드마다 링 버퍼(스레드당 8192 사건)에 남깁니다. 꺼져 있으면 ScopedFrameProfile 의 비용은 bool 하나 읽기다.
     * @details 쓰는 쪽은 자기 스레드 버퍼 하나뿐이라 잠금이 없다. 읽는 쪽(에디터 패널)은 버퍼마다 쓰기 위치를 읽고 그 앞을 복사한다 —
     *          복사 중 덮인 사건은 세대 번호로 버린다. 프레임 경계는 게임 스레드의 `FrameProfiler::beginFrame` 시각을 따로 링에 남긴다.
     */
    class SW_API ProfilerTimeline
    {
    public:
        static ProfilerTimeline& get();

        void setRecording( bool bRecording );
        bool isRecording() const { return _bRecording.load( std::memory_order_relaxed ); }
        /** @brief 이 스레드의 사건 하나를 남깁니다(ScopedFrameProfile 소멸자가 부른다). */
        void recordEvent( uint32 slot, uint64 beginNanos, uint64 endNanos, uint16 depth );
        /** @brief 프레임 시작 시각을 남깁니다(게임 스레드 beginFrame). */
        void recordFrameBegin( uint64 nanos );
        /** @brief 최근 @p frameCount 프레임 구간의 사건을 스레드마다 모읍니다(스레드 이름 포함). */
        void collectRecentFrames( uint32 frameCount, vector<ProfilerTimelineThread>& outListThread, uint64& outBeginNanos, uint64& outEndNanos ) const;
        ...
    };
} // namespace sw
```
`ScopedFrameProfile` 은 깊이를 스레드 지역 카운터로 세고(생성자 ++ · 소멸자 --), 소멸자에서 `if ( ProfilerTimeline::get().isRecording() ) recordEvent(…)`. 스레드 이름은 Core 의 `setCurrentThreadName` 이 붙인 이름을 읽는다(읽는 함수가 없으면 더한다) — 없으면 "Thread <id>".
2) `ProfilerPanel` 에 탭 "Timeline": 녹화 단추(켜면 `FrameProfiler` 도 켠다), 최근 프레임 수(1 · 4 · 16), 스레드마다 한 줄(깊이만큼 겹), 구간 = 슬롯 이름 해시 색 사각형(폭이 2 px 미만이면 이름 생략),
   호버 툴팁(이름 · 길이 µs · 깊이), 휠 = 확대, 끌기 = 이동, 프레임 경계 세로선, "Freeze"(녹화는 계속 · 보기만 멈춤). 그리기 계산(사건 → 사각형 · 겹 · 보이는 범위 잘라내기)은 ImGui 없는
   `ProfilerTimelineLayout`(Editor/Panels) — `ProfilerScopeHistory` 처럼 EditorTest 가 본다.
**시험.** `ProfilerTimelineTest`(EngineTest nogpu): 두 스레드에서 구간을 남기고 `collectRecentFrames` 가 스레드별로 · 시간 순으로 · 깊이를 지켜 돌려준다, 링이 넘치면 오래된 것부터 버린다, 꺼져 있으면 0.
`ProfilerTimelineLayoutTest`(EditorTest): 사건 → 사각형 x 범위 · 겹 줄 · 보이는 범위 밖 잘라냄.
측정: `-gv_profileFrames=600` 의 `GT.Frame` p50 을 녹화 끔 · 켬으로 잰다(Release — Debug 는 레이스 검출기 때문에 컨테이너 비용이 과장된다) — 켬이 +2 % 를 넘으면 커밋 메시지에 숫자와 함께.
**확인 = 에디터 시나리오.** `profilertimeline.scenario.xml`: 프로파일러 패널의 Timeline 탭과 녹화 단추(이름표 `profiler.timeline.record`)를 누르고 몇 프레임 뒤 탐침 `Editor.ProfilerTimelineThreadCount` 가 2 이상(게임 스레드와 렌더 스레드)인지 봅니다.

**남길 교훈.** `Source/Engine/Profiling/README.md` 함정과 주의 절에 "빠른 확인은 패널 타임라인, 깊은 분석은 Tracy" 한 줄.
**커밋 메시지:**
```
프로파일러 - 패널의 스레드 미니 타임라인(켤 때만 스레드별 링 버퍼)

문제점:
- 프로파일러 패널은 표 · 그래프뿐이라 스레드 사이 겹침 · 기다림을 보려면 Tracy 를 따로 띄워야 했다.

해결방안:
- ProfilerTimeline: 켤 때만 계측 구간을 스레드마다 잠금 없는 링(8192)에, 프레임 시작 시각 링. ScopedFrameProfile 이 깊이를 세고 남긴다.
- ProfilerPanel Timeline 탭: 녹화 · 최근 1/4/16 프레임 · 스레드 줄 · 겹 · 툴팁 · 확대/이동 · 프레임 경계 · Freeze. 배치 계산은
  ImGui 없는 ProfilerTimelineLayout.

결과:
- ProfilerTimelineTest · ProfilerTimelineLayoutTest. 녹화 켬 비용 GT.Frame p50 …(Release 600 프레임).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```

### H2 `bugit` / `bugitgo` — 버그 리포트 한 방 ★

**목적.** 버그를 남길 때 스크린샷 · 로그 · 씬 · 카메라 자리 · 입력을 손으로 모은다. 언리얼 `BugIt` 처럼 명령 하나로 폴더에 모으고 `BugItGo` 로 그 자리에 돌아간다(D13).

**바꿀 것 — 엔진 개발 명령(게임 창 · 에디터 모두), 새 `Engine/DevTools/BugItCommands.cpp` · `BugItReport.h` · `.cpp`:**
```cpp
namespace sw
{
    /** @brief bugit 한 번에 남길 것입니다(ImGui 없음 · 파일 쓰기는 BugItReport::write). */
    struct BugItReport
    {
        string _note;
        string _scenePath;
        string _rhiBackend;
        string _adapterName;
        string _buildConfiguration; ///< SW_BUILD_CONFIG_NAME
        string _gamePreset;
        float3 _cameraPosition{};
        float3 _cameraRotationDegrees{}; ///< pitch · yaw · roll
        uint64 _frameNumber{ 0 };

        /** @brief `info.txt`(키 = 값 한 줄씩)를 씁니다. */
        [[nodiscard]] bool writeInfo( string_view directory ) const;
        /** @brief `info.txt` 를 읽습니다(bugitgo). 모르는 키는 건너뛴다. */
        [[nodiscard]] static bool readInfo( string_view directory, BugItReport& outReport );
        /** @brief `Saved/BugIt/<yyyyMMdd-HHmmss>` 를 만듭니다(같은 초면 `-2` …). */
        static string makeDirectory( string_view savedRoot, uint64 unixSeconds );
    };
} // namespace sw
```
`bugit [메모…]` 가 하는 일(순서대로): 폴더 만들기 → `info.txt`(위 칸 — 카메라는 활성 카메라(에디터면 에디터 카메라)) → `RenderThread::requestScreenshot( dir/screenshot.png )` →
로그를 비우고(`Logger::flush` — 로거가 비동기라 비우지 않으면 마지막 줄이 빠진다) 지금 로그 파일을 `log.txt` 로 복사(`FileLogOutput::getFilePath()` 를 더한다) →
입력 녹화가 돌고 있으면(3 차 A2 `InputReplay` — 녹화 중) 지금까지를 `input.swreplay` 로 → 에디터면 씬이 더러우면 사본을 `scene.scene.xml` 로(`SceneManager` 의 다른 이름 저장 — 활성 씬 경로는 바꾸지 않는 판 —
없으면 `saveSceneCopy( path )` 를 더한다) → `repro.txt`:
```
App.exe -<backend> [-EnableEditor] -gv_devConsoleExec="bugitgo <폴더>"
```
→ 답 `"bugit -> Saved/BugIt/20261006-101530 (screenshot next frame)"`, 에디터면 토스트 + "Show in Explorer".
`bugitgo <폴더>`: `info.txt` 를 읽어 씬이 다르면 연다(사본 `scene.scene.xml` 이 있으면 그것을 — 읽기 전용으로 열고 경고), 카메라를 그 자리 · 각도로(에디터 카메라 또는 플레이어 카메라 `teleport` 길).
에디터: Help 메뉴(없으면 `MainMenu/Edit` 끝)에 "Report Bug (BugIt)…" — 메모 한 줄 입력 팝업 → `bugit <메모>`, 단축키 Ctrl+Shift+F12. 게임 창: `~` 콘솔에서 `bugit`.
**시험 — `BugItReportTest`(EngineTest nogpu):** `writeInfo` → `readInfo` 왕복(한글 메모 · 공백 · 소수 카메라 값), `makeDirectory` 가 같은 초에 두 번이면 `-2`, 모르는 키 무시.
`DevCommandRegistryTest` 에 `bugit` · `bugitgo` 등록 확인. 스크린샷 · 로그 복사는 손 확인(적용 뒤: 게임 창 `bugit 테스트` → 폴더 내용 다섯 · `bugitgo` 로 같은 자리).
**확인 = 에디터 시나리오.** `bugit.scenario.xml`: 개발 콘솔에 `bugit scenario` 를 넣고(Output Log 입력 줄, 이름표 `console.input` 에 `EditorText` 와 `EditorKey key="Enter"`) 답 줄의 폴더를 `ExpectLog` 로 봅니다.
같은 폴더로 `bugitgo` 를 넣은 뒤 에디터 카메라 위치 탐침이 기록과 같은지 봅니다. 게임 창 쪽은 엔진 시나리오 하나(에디터 없이)로 같은 두 명령을 봅니다.

**남길 교훈.** 없음. **커밋 메시지:**
```
개발 도구 - bugit / bugitgo: 스크린샷 · 로그 · 씬 · 카메라 · 입력을 폴더 하나에 모으고 그 자리로 돌아간다

문제점:
- 버그를 남길 때 스크린샷 · 로그 · 씬 · 카메라 자리 · 재현 명령을 손으로 모았다. 그 자리로 돌아가는 길도 없었다.

해결방안:
- 개발 명령 bugit [메모](게임 창 · 에디터): Saved/BugIt/<시각>/ 에 info.txt(씬 · 백엔드 · 어댑터 · 구성 · 프리셋 · 카메라 · 프레임 · 메모) ·
  screenshot.png(다음 프레임) · log.txt(로거를 비운 뒤 사본) · 입력 녹화(녹화 중이면) · 더러운 씬 사본 · repro.txt.
- bugitgo <폴더>: 씬을 열고 카메라를 그 자리로. 에디터 메뉴 Report Bug(메모 팝업) · Ctrl+Shift+F12.

결과:
- BugItReportTest 셋, DevCommandRegistryTest 에 등록 확인.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```
**겹침:** 3 차 A2(`InputReplay` 판 4). 스크린샷 요청 창구(`RenderThread::requestScreenshot` · `ScreenshotPathUtil`)는 이미 있다.

### H3 시험 패널 — 자체 시험 · 시나리오 · 시험 실행 파일을 에디터에서

**목적.** 시험을 돌리는 길이 명령줄뿐이다(`-gv_editorSelfTest` 는 기동 때만, 끝나면 앱을 닫는다). 언리얼 Session Frontend > Automation · 유니티 Test Runner 처럼 목록에서 골라 돌리고 결과를 본다.

**바꿀 것.**
1) `EditorSelfTestRunner` 에 실행 중 요청 — `requestRun( string_view pattern, bool bQuitWhenDone )`: 지금의 `-gv_editorSelfTest` 경로는 `bQuitWhenDone = true` 로 같은 함수를 부르고, 패널은 false.
   결과는 `EditorSelfTestRunner::getResults()`(id · 통과 · 이유 · 걸린 프레임). 시험이 패널을 열고 닫으므로 **도는 동안 시험 패널은 자기 창만 그리고 입력을 받지 않는다**(시험이 그린 결과를 해치지 않게).
2) `Panels/TestRunnerPanel.h` · `.cpp` — `SW_EDITOR_PANEL( TestRunnerPanel, "test_runner", EditorPanelCategory::Tool, 2030 );` 제목 `"Test Runner"`, 탭 셋:
   - **Editor Self Tests** — 등록부 목록(확장 모듈 것 포함, id 의 '.' 앞으로 묶음), 체크 · 검색 · "Run Selected" · "Run All", 결과 색(통과 초록 · 실패 빨강 + 이유).
   - **Scenarios** — `Resource/**/automation/*.scenario.xml`(3 차 B2 의 자리) 목록. 시나리오는 프로세스 하나를 통째로 쓰므로 **새 프로세스로** 돌린다:
     `App.exe -<지금 백엔드> -scenario=<경로> -unattended`(`EditorExternalToolJob` — 전용 스레드), 결과 = 종료 코드 + 로그 끝 40 줄 + `[Error]` 수, 실패 줄 더블클릭 = 로그 줄 IDE 열기(기존 Output Log 의 판정 재사용).
   - **Unit Tests** — `build/<지금 프리셋>/Bin`(Shipping 은 `TestBin`)의 `*Test.exe` 를 `--test_list` 로 펼쳐 스위트 · 케이스 나무, 선택을 `--test_filter=` 로 새 프로세스(작업 디렉터리 `Bin` — CLAUDE.md 규칙),
     출력의 `[ RUN ]` · `[  FAILED  ]` 줄을 읽어 케이스마다 결과. host 스위트는 `--host_suites=` 를 붙이지 않는다(이 PC 에는 GPU 가 있다).
   출력 줄 읽기(`[ RUN ]` · `[       OK ]` · `[  FAILED  ]` · 요약)는 ImGui 없는 `EditorTestOutputParser`(EditorTest 가 본다).
3) 커맨드 `editor.testRunner`(메뉴 `MainMenu/Tools` 또는 Panel).
**시험.** `EditorTestOutputParserTest`(EditorTest): 실제 시험 출력 견본(통과 · 실패 · 건너뜀 · 반복 `--test_repeat` 의 반복 번호)을 케이스 결과로. 자체 시험 `testRunner.runsSelfTestInPlace` — 패널에서 다른 자체 시험 하나(`hierarchy.tagFilter`)를 돌려 결과가 PASS 로 차는지.
**확인 = 에디터 시나리오.** `testrunner.scenario.xml`: Test Runner 창의 Editor Self Tests 탭에서 자체 시험 하나(`hierarchy.tagFilter`)를 골라 "Run Selected" 를 누르고, 결과 탐침 `Editor.TestRunnerPassCount` 가 1 인지 봅니다.
시나리오와 단위 테스트 탭은 새 프로세스를 띄우므로 시나리오 안에서 돌리지 않습니다.

**남길 교훈.** 없음.
**커밋 메시지:**
```
에디터 - Test Runner 창(자체 시험은 그 자리에서, 시나리오 · 단위 시험은 새 프로세스로)

문제점:
- 자체 시험 · 자동화 시나리오 · 단위 시험을 돌리는 길이 명령줄뿐이었다. 자체 시험은 기동 때만 돌고 끝나면 앱을 닫았다.

해결방안:
- EditorSelfTestRunner::requestRun( 패턴, 끝나면 닫을지 ) · getResults — -gv_editorSelfTest 도 같은 함수.
- Test Runner 창: 자체 시험(등록부 · 확장 포함, 그 자리에서), 시나리오(App -scenario -unattended 새 프로세스 — 종료 코드 · 로그 끝 · Error 수),
  단위 시험(*Test.exe --test_list 나무 → --test_filter 새 프로세스, 작업 디렉터리 Bin). 출력 읽기는 ImGui 없는 EditorTestOutputParser.

결과:
- EditorTestOutputParserTest, 자체 시험 testRunner.runsSelfTestInPlace.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```
**겹침:** 3 차 B2(시나리오 자리 · `-scenario`), 2 차 단위 8(자체 시험 입력). 시험 실행은 `-unattended` 를 준다(단언 대화상자를 걸지 않는다).
---

## 8. 단계 5 — 공용 편집 틀(커브 · 맵 검사 · 노드 그래프 · 패키징)

**상용 비교.** 언리얼은 값 타입(`FRichCurve` · `FRuntimeFloatCurve`)이 있고 Details 안 작은 미리보기 + Curve Editor 탭, Map Check(로드 · 빌드 때 경고 목록 — 줄을 누르면 액터 선택),
그래프 편집기는 `SGraphEditor`(블루프린트 · 머티리얼 · 애님 그래프 · 사운드 큐가 같은 틀 — 노드 찾아 넣기 · 핀 타입 색 · 컴파일 오류 표시), Project Launcher/Package Project(플랫폼 · 구성 · 쿠킹 · 진행 로그).
유니티는 `AnimationCurve` + CurveField · Curve Editor 창, Console 의 클릭 → 오브젝트 핑, GraphView(셰이더 그래프 · VFX 그래프의 공용 틀), Build Settings 창. 우리는 노드 그래프 틀이 EditorModule 안에만 있고(애님 · 대화 둘이 쓴다),
검증 결과 저장소(`ValidationIssueLog` — "맵 검사 패널이 읽는다" 고 주석에 적힌)는 있는데 패널이 없다.

### T1 `FloatCurve` 값 타입 + 커브 편집 위젯

**새 파일 `Source/Engine/Utility/Curve/FloatCurve.h` · `.cpp`(Utility 층 — 리플렉션):**
```cpp
namespace sw
{
    /** @brief 키 사이 보간입니다(언리얼 ERichCurveInterpMode · 유니티 키 접선 모드와 같은 셋). */
    ENUM()
    enum class CurveInterpolation : uint8
    {
        Constant, ///< 다음 키까지 이 키의 값
        Linear,   ///< 직선
        Cubic,    ///< 에르미트 — 키의 나가는 접선 · 다음 키의 들어오는 접선
    };
} // namespace sw

namespace sw
{
    /** @brief 커브 키 하나입니다. */
    REFLECT()
    struct FloatCurveKey
    {
        REFLECT_BODY();
        PROPERTY()
        float32 _time{ 0.0f };
        PROPERTY()
        float32 _value{ 0.0f };
        PROPERTY()
        float32 _arriveTangent{ 0.0f }; ///< 들어오는 기울기(값/초)
        PROPERTY()
        float32 _leaveTangent{ 0.0f };  ///< 나가는 기울기(값/초)
        PROPERTY()
        CurveInterpolation _interpolation{ CurveInterpolation::Cubic };
        PROPERTY()
        bool _bAutoTangent{ true };     ///< 이웃 키로 접선을 정한다(캣멀-롬). 손으로 접선을 끌면 꺼진다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct FloatCurve
     * @brief 시간 → 값 커브입니다(키 · 키마다 보간 · 접선). 첫 키 앞 · 끝 키 뒤는 끝 값입니다. 키가 없으면 대체값입니다.
     * @details 컴포넌트 · 데이터 에셋의 `PROPERTY()` 로 두면 인스펙터가 작은 미리보기와 편집기를 그린다(`FloatCurvePropertyDrawer`).
     */
    REFLECT()
    struct SW_API FloatCurve
    {
        REFLECT_BODY();
        PROPERTY()
        vector<FloatCurveKey> _listKey; ///< 시각 순(편집기 · 읽기가 정렬한다)

        /** @brief @p time 의 값입니다. 키가 없으면 @p fallback. */
        float32 evaluate( float32 time, float32 fallback = 0.0f ) const;
        /** @brief 키를 시각 순 자리에 넣고 자동 접선을 다시 셉니다. 넣은 자리입니다. */
        uint32 addKey( float32 time, float32 value, CurveInterpolation interpolation = CurveInterpolation::Cubic );
        /** @brief 키를 시각 순으로 정렬하고 `_bAutoTangent` 키의 접선을 이웃으로 다시 셉니다. */
        void sortAndComputeAutoTangents();
        /** @brief 키 값의 최소 · 최대(곡선이 넘는 부분은 표본 64 개로 넓힌다 — 편집기 화면 맞추기). 키가 없으면 false. */
        bool computeValueRange( float32& outMin, float32& outMax ) const;
    };
} // namespace sw
```
`evaluate`: 이분 탐색으로 구간 → Constant 는 앞 키 값, Linear 는 직선, Cubic 은 `h00·p0 + h10·dt·m0 + h01·p1 + h11·dt·m1`(m0 = 앞 키 `_leaveTangent`, m1 = 뒤 키 `_arriveTangent`).
자동 접선 = `(next.value − prev.value) / (next.time − prev.time)`(끝 키는 0 — 평평).

**편집 위젯 — `Common/Widgets/EditorCurveEditor.h` · `.cpp` + ImGui 없는 `EditorCurveView`(화면 ↔ 커브 좌표 · 키/접선 손잡이 맞힘 · 화면 맞추기):**
- 프로퍼티 줄: 폭 전체 · 높이 2 줄의 미리보기(꺾은선 64 점) — 누르면 팝업 편집기(창 크기 640 × 360, 크기 조절 가능).
- 편집기: 격자(초 · 값 눈금 자동), 키 끌기(Shift = 시간만 · Ctrl = 값만), 접선 손잡이(Cubic · 자동 접선 끔), 오른쪽 클릭 = 키 더하기 / 지우기 / 보간 바꾸기, F = 전체 보기, 휠 = 확대, 가운데 끌기 = 이동,
  선택 키의 시각 · 값 숫자 칸. 끌기를 **놓을 때** 한 번 편집 통지(Undo 한 줄 — 그리드의 `_onEdited`).
- `SW_EDITOR_PROPERTY_DRAWER( FloatCurve, "sw::FloatCurve", FloatCurvePropertyDrawer );`(I3).
**시험.** `FloatCurveTest`(EngineTest nogpu): 키 없음 = 대체값, 끝 밖 = 끝 값, Constant · Linear · Cubic 값(손으로 센 값), 자동 접선(가운데 키 기울기 = 이웃 평균), JSON 왕복(`_listKey` 는 JSON 배열 — 시퀀스는 배열, 맵은 오브젝트).
`EditorCurveViewTest`(EditorTest): 좌표 왕복 · 키 맞힘 반경 · 화면 맞추기가 키를 모두 담는다. 자체 시험 `curve.dragKeyRecordsOneUndo`(시험 구조체를 가진 시험 컴포넌트 — `EditorPreviewProbe` 처럼 EditorModule 안 시험 전용 타입이 없으면 자체 시험이 `FloatCurve` 지역 값으로 그리드를 직접 연다).
**확인 = 에디터 시나리오.** `curveedit.scenario.xml`: 커브 프로퍼티를 가진 시험 컴포넌트의 미리보기(이름표 `inspector.curve.<프로퍼티>`)를 눌러 편집기를 열고, 빈 곳 오른쪽 클릭으로 키를 하나 더한 뒤
탐침 `Editor.UndoCount` 가 하나 늘었는지, `EditorKey key="Z" mods="ctrl"` 뒤 키 수 탐침이 원래대로인지 봅니다. 끌기 한 번이 Undo 한 줄인지는 자체 시험 `curve.dragKeyRecordsOneUndo` 가 봅니다.

**남길 교훈.** [백로그](../06_Backlog.md) 1-6 에 남은 일 한 줄: `- **GameCurve(키트 꺾은선)는 FloatCurve 로 옮기지 않았다** — 데이터(<Curve time scale>)를 다시 써야 한다. 쓰는 곳(SpawnTable · AIDirectorProfile)을 고칠 때 같이.`
**커밋 메시지:**
```
엔진 · 에디터 - FloatCurve 값 타입(Constant · Linear · Cubic 키)과 커브 편집 위젯

문제점:
- 엔진에 리플렉션 커브 값 타입이 없어(키트의 GameCurve 는 꺾은선 · XML 손 읽기) 컴포넌트가 커브를 PROPERTY 로 들 수 없었고 편집 위젯도 없었다.

해결방안:
- FloatCurve · FloatCurveKey · CurveInterpolation(리플렉션): 에르미트 평가 · 자동 접선(이웃 기울기, 끝은 평평) · 값 범위.
- EditorCurveEditor(미리보기 + 팝업 편집기 — 격자 · 키/접선 끌기 · 메뉴 · 전체 보기 · 놓을 때 Undo 한 줄), 좌표 계산은 ImGui 없는
  EditorCurveView. SW_EDITOR_PROPERTY_DRAWER 로 sw::FloatCurve 에 건다.

결과:
- FloatCurveTest · EditorCurveViewTest, 자체 시험 curve.dragKeyRecordsOneUndo.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```
**적용 뒤 확인:** re-configure(새 `REFLECT` 헤더 — CLAUDE.md Gotchas), 생성 폴더의 `FloatCurve.gen.cpp` 확인.

### T2 맵 검사 패널 — `ValidationIssueLog` 를 보이고, 누르면 그 오브젝트로

**목적.** 검증 결과는 이미 모인다(`ObjectValidation` — 로드 · 저장 · 인스펙터 편집 때 `ValidationIssueLog` 에). 보는 곳이 경고 로그뿐이다.

**바꿀 것.**
1) `ValidationIssueLog` 에 바뀜 번호 — `uint32 getRevision() const`(바꿀 때마다 +1, 원자) — 패널이 프레임마다 목록을 다시 모으지 않게. (Engine 헤더 → 엔진 ABI 도장.)
2) `Panels/MapCheckPanel.h` · `.cpp` — `SW_EDITOR_PANEL( MapCheckPanel, "map_check", EditorPanelCategory::Tool, 2040 );` 제목 `"Map Check"`. 표: 무게(아이콘) · 오브젝트(`_sourceLabel`) · 타입 · 프로퍼티 · 메시지,
   무게 필터(Error · Warning) · 검색, 줄 클릭 = 그 오브젝트 선택(`_sourceId` = 오브젝트 id → `GameObjectManager::findGameObjectById` → `EditorSelection::selectObject`) + 더블클릭 = 뷰포트 초점,
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
- **검증(묶음 끝 한 번):** Debug 빌드 경고 0, `ctest -L nogpu`, `ctest -L lint`, Shipping `-L hostgpu`(G1 의 RenderPassGpuTest 네 백엔드, 에디터 시나리오), 에디터 실행 넷(`-dx12 · -dx11 · -vk · -gl -EnableEditor -gv_profileFrames=40`) `[Error]` 0,
  자체 시험 전부, **핫 리로드 둘(C5 의 확인)**, 리눅스는 CI 로 확인(C1 SHARED · C4 CMake). 게임별 빌드(ThemeParkTycoon 프리셋)에서 `GF_Editor_ThemePark` 가 빌드되는지.

**겹치는 파일.** 원문이 적은 다른 제안서(2차 ~ 4차)는 모두 main 에 들어갔으므로, 겹침은 이 문서 안의 단위끼리만 봅니다.

| 파일 | 단위 | 처리 |
|---|---|---|
| `InspectorPanel.cpp` | P1(이동) · I1 · I2 · R3 · N4 | P1 이동을 먼저 넣는다 |
| `HierarchyPanel.cpp` | R3 · N2 · N12 | R3(라벨 줄) 뒤에 N2 · N12 |
| `EditorViewportClient.cpp` · `EditorViewportToolbar.*` | C2 · G1 · R4 · R5 · N1 | C2(마스크 → id) 먼저. R4 의 `getMaskBitById` 는 C2 의 `EditorVisualizerToggles` 로 바꿔 쓴다 |
| `ContentBrowserPanel.*` | A1 · R9 · N3 | A1 의 역색인 뒤에 N3(이름 바꾸기 · 옮기기) |
| `ModuleHost.cpp` · `ModuleCatalog.*` · `ModuleManifest.cmake` · `ModuleTargets.cmake` | C4 · P4 | 게이트(`CheckModuleTargets`)는 한 커밋에서 |
| `EditorCommandGui.cpp` · `EditorCommandRegistry.*` | C3 · P3 · R5 · 여러 단위의 표 한 줄 | 표 줄은 메뉴 순서 값이 겹치지 않게(`validate` 가 잡는다) |
| `EditorSelfTestCases.cpp` · `AppSmokeTest` 기대 목록 | 11 줄 | 줄 더하기 — 순서 키 겹침만 본다 |
| `AnimGraphPanel.cpp` | T3 · R5 · N8 | T3 → N8 |

**확신 수준.**
- **실행으로 확인할 것:**
  - C 단계 — (a) 확장이 EditorModule 보다 먼저 언로드되는지(종료 · 리로드): 순서가 틀리면 등록자 소멸이 지운 목록을 만진다 → `ModuleHost::shutdown` 에서 확장 이름을 먼저 언로드한다.
    (b) 결속기: `themepark.extensionPanelDraws` 가 PASS(정점 > 0)면 맞다. 실패하면 확장 DLL 의 `GImGui` 가 null 이거나 다른 컨텍스트 — 결속 소스가 생성 · 링크됐는지(`<모듈>UiBinder.cpp`).
    (c) EditorModule SHARED 의 리눅스 링크(CI).
  - G1 Overdraw — RHI 에 가산 블렌드 상태가 있는지(없으면 ABI +1).
  - R4 — 오브젝트가 수천 개인 씬(`-gv_benchMeshes=8000 -EnableEditor`)에서 빌보드 수집이 1 ms 를 넘는지.
- O10(ThemePark 로 띄워도 editortest 씬)은 그 PC 의 로컬 상태일 수 있다 — 깨끗한 `Saved/` 로 다시 볼 것.

---

## 11. 남길 교훈 요약 (단위 커밋마다 그 줄만)

| 단위 | 남길 곳 | 남길 것 |
|---|---|---|
| G1 | `Source/Engine/Renderer/README.md` 함정과 주의 | 보기 모드는 표 하나, 조명하는 셰이더는 모두 `swApplyViewMode` |
| C1 ~ C5 | `Source/Editor/README.md` 함정 · 계약, `Source/Engine/Module/README.md` 함정 · 계약 | 확장 모듈 위치, 등록 세대, 언로드 리스너, 결속기, `SW_EDITOR_COMMAND`, EditorExtension 종류 |
| P2 · P3 · P4 | 에디터 README, 모듈 README | 에디터 설정은 환경설정 섹션, 사용자 단축키 덮어쓰기, 매니페스트 내용도 configure 의존 |
| I1 ~ I3 | 에디터 README | 리플렉션 그리기는 `EditorPropertyGrid`, 타입 그리기 확장은 `SW_EDITOR_PROPERTY_DRAWER` |
| A1 | 에디터 README | 활성 팩 기본, 참조 찾기는 `EditorReferenceIndex` |
| G4 | 프로파일링 README | 패널 타임라인과 Tracy 의 역할 |
| T1 | [백로그](../06_Backlog.md) 1-6 | `GameCurve` 는 `FloatCurve` 로 옮기지 않음(남은 일) |
| R3 · R4 | 에디터 README | 컴포넌트 아이콘 테이블, 빌보드 클릭이 레이 피킹보다 먼저 |
| 9절 로드맵 | [백로그](../06_Backlog.md) 1-4 | 이 문서를 지울 때 남은 로드맵 줄을 옮긴다 |

---

## 12. 추가 단위 — 아이콘(editor-res 의 R3, R4, R5, R9)

editor-res 제안서(2026-10-07)는 에디터 리소스를 아홉 단위로 나눴습니다. 5b 는 아이콘 폰트와 Font Awesome 교체(R1, R2), 누락 텍스처(R6), 프로토타입 격자(R7), 앱 아이콘(R8)을 넣고,
아래 넷을 이 계획으로 넘겼습니다. 아이콘은 모두 5b 의 R1 이 만든 아이콘 폰트(`Resource/editor/fonts/sweditoricons.ttf`, 글리프 이름은 `editoricon::k*`)를 씁니다.
새 아이콘이 필요하면 `Scripts/common/EditorIconFont.py` 에 그리기 함수를 더하고 `Scripts/generate/GenerateEditorIcons.py` 를 실행합니다(5b R1 이 둔 스크립트입니다).

### R3 컴포넌트와 오브젝트 아이콘(Hierarchy, 인스펙터) ★

**무엇.** 컴포넌트 타입에서 아이콘과 색을 찾는 테이블 `EditorComponentIcon`(`Source/Editor/Common/Gui/EditorComponentIcon.h`, 새 파일)을 두고, Hierarchy 의 오브젝트 줄과 컴포넌트 줄, 인스펙터의 컴포넌트 카드 머리에 붙입니다.
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
