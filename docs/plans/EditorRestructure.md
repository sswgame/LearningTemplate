# 에디터 구조 개편 계획

`Source/Editor` 의 폴더 사이 의존 방향을 정하고 게이트로 지킨 뒤, 아래 층이 위 층(패널 · 팝업 · 도킹)을 직접 아는 곳을 뒤집는다.
그다음 `EditorContext` 정적 조회의 비용 대비 효과를 보고, 평평한 `Panels/` 를 큰 패널별 하위 폴더로 나눈다. 임포터 분리는 조사만 한다.

## 측정한 현재 상태 (2026-10-11, `Source/Editor` 의 `#include "Editor/…"` 를 층 단위로 센 것)

층은 `Common/<하위 폴더>` 와 최상위 폴더(`Panels` · `Popups` · `Viewport` · `AssetActions` · `SelfTest`), 그리고 루트 파일(`ImGuiEditor`)이다.

| 아래 → 위 방향(어겨진 것) | include 수 | 대표 |
|---|---|---|
| Commands → GUI | 9 | `EditorNotificationManager.h` · `EditorDockLayout.h` · `EditorThemeUtil.h` · `EditorIconGlyphs.h` |
| Commands → Panels · Viewport · AssetActions | 4 · 2 · 1 | `EditorPanelManager.h` · `SceneViewPanel.h` · `EditorCamera.h` · `EditorAssetTypeActions.h` |
| Workspace → Panels · Popups · Viewport · GUI | 8 · 2 · 1 · 5 | 조립 파일 둘(`EditorContextLifecycle.cpp` · `EditorModuleUnloadListener.cpp`)과 `EditorContext.h` 의 `EditorThemeUtil.h` · `EditorViewTargetUtil.h` |
| Workspace → Commands | 3 | `EditorWorkspace.h` → `EditorTransformCommands.h` |
| GUI → Panels · Popups · Viewport · SelfTest | 3 · 2 · 1 · 5 | `EditorMenuBar` → `MapCheckPanel.h`, `EditorCommandGUI` → 팔레트 · 런처 팝업, `EditorPlayToolbar` → `EditorCamera.h` |
| Viewport · Popups → Panels | 3 · 1 | `EditorPanelManager.h` · `SceneViewPanel.h` |
| Common 루트 → Workspace | 7 | `EditorUtil.cpp`(프리팹 스폰 · 편집 허용 판정) · `EditorProfile.h`(`EditorService.h`) |
| Asset · SourceControl → Commands | 1 · 1 | `EditorExternalToolJob.h`(프로세스 실행 도우미 — 명령이 아니다) |

- 조사 보고의 "`SequenceTimingUtil.h` 에 ImGui 호출" 은 주석의 낱말이었다. Commands 폴더에서 ImGui 를 include 하는 파일은 없다 — 어긴 것은 ImGui 가 아니라 **층**이다.
- 패널 매니저(`Panels/EditorPanelManager`)와 팝업 매니저(`Popups/EditorPopupManager`)는 ImGui 를 모르는 틀이다. 그 인터페이스(`IEditorPanel` · `IEditorPopup`)는 이미 `Common/GUI` 에 있다.
- `EditorContext::get()` 은 62개 파일 225곳이다. 그 몸체가 매번 `getService<EditorContext>()`(정렬 `map` 조회)를 먼저 하고 정적 포인터로 물러난다. 두 값은 늘 같다 — 둘 다 `initialize` 에서 같은 `this` 로 걸고 `shutdown` 에서 푼다.
- `EditorService.h` 는 로케이터 2층째가 아니라 **호스트(엔진) 서비스 창구**다. 겹치는 곳은 `EditorContext` 가 자기를 로컬 서비스로도 거는 한 줄뿐이다.

## 목표 층(아래부터, 같은 층끼리는 서로 include 해도 된다)

| 층 | 폴더 | 무엇 |
|---|---|---|
| 0 | `Common`(루트) · `Common/Backend` · `Common/Config` | 경로 · 내보내기 매크로 · ImGui 백엔드 · 설정 표 |
| 1 | `Common/Asset` · `Common/Localization` · `Common/SourceControl` | UI 없는 도구(임포터 · 검증 · 로컬라이즈 · 버전 관리) |
| 2 | `Common/Workspace` | 편집 상태(작업 공간 · 선택 · 플레이 세션 · 핫 리로드)와 아래 층이 쓰는 창구 인터페이스 |
| 3 | `Common/Commands` | ImGui 없는 동작과 커맨드 등록부 |
| 4 | `Common/GUI` · `Common/Widgets` | 패널 · 팝업 틀과 매니저, 도킹, 메뉴, 알림 그리기, 테마, 공용 위젯 |
| 5 | `Viewport` · `Popups` | 뷰포트 클라이언트 · 시각화 · 팝업 |
| 6 | `Panels` · `AssetActions` | 구체 패널 · 에셋 종류별 동작 |
| 7 | `SelfTest` | 자체 시험 · 시나리오 단계(모든 것을 내려다본다) |
| 8 | 루트(`ImGuiEditor`) | 셸 |

예외는 둘뿐이다. 어느 층에서 include 해도 되는 잎 헤더(`prelude:` — 위 층의 것을 include 하지 않는 헤더)와, 모든 매니저를 만들어야 해서 위 층을 include 하는 조립 파일(`wiring:`)이다.
나머지 위반은 게이트의 예외 목록에 이유와 함께 두고 2 단계에서 지운다.

## 단계

### 1. Editor 층 게이트(`CheckEditorLayers`)
- `Scripts/lint/gate/CheckEditorLayers.py` + `Scripts/lint/rules/CheckEditorLayers.toml`. 본보기는 `CheckEngineLayers` 의 티어 · `prelude:` · `wiring:` 와 `CheckCoreLayers` 의 표.
- 지금 위반은 모두 `violation:<파일> -> <include>` 예외로 적는다(이유와 "2 단계에서 지운다"). 낡은 예외(더는 위반이 아닌 줄)는 실패한다 — 고친 만큼 표가 줄어든다.
- 위험: 낮다(코드 무변경). 검증: 게이트 OK · 자체 시험(`py -3 -m Scripts selftest`) · `ctest -L lint`.

### 2. 의존 역전 — 아래 층이 패널 · 팝업 · 도킹을 모르게
상용 엔진의 선: 언리얼은 탭 매니저(`FTabManager`)가 Slate 틀에 있고 구체 탭은 모듈이 등록한다. 에셋 종류 동작(`IAssetTypeActions`)은 AssetTools 의 등록부이고 편집기는 그 위다.
UI 를 조작하는 명령(탭 열기 · 레이아웃)은 그 UI 를 가진 모듈이 등록한다. 그래서 여기서도 "틀은 아래, 구체는 위, UI 를 만지는 명령의 등록 줄은 그 UI 의 .cpp" 로 한다.

- 2a. 틀을 제자리로(파일 이동): `EditorPanelManager` · `EditorPopupManager` → `Common/GUI`(인터페이스 옆), `EditorViewTargetUtil`(ImGui 없는 뷰 RT 판정) ·
  `EditorAssetTypeActions`(인터페이스 + 등록부) · `EditorCamera`(씬 안 에디터 카메라 오브젝트, ImGui 없음) → `Common/Workspace`, `EditorExternalToolJob` · `EditorBackgroundTask` → `Common` 루트,
  `EditorSceneViewUtil`(씬 뷰 패널에 초점) → `Panels`, `TestRunnerPanel` · `EditorTestOutputParser`(자체 시험 실행 창) → `SelfTest`.
- 2b. 알림을 모델 · 뷰로: `EditorNotificationManager`(큐 · `push` · `update`, ImGui 없음)는 `Common/Workspace`, 그리기는 `Common/GUI/EditorNotificationView`.
  테마 값(`EditorThemePreset` · `EditorThemeConfig`)은 `Common/Config/EditorThemeConfig.h` — `EditorContext.h` 가 GUI 를 include 하지 않는다.
- 2c. 패널 창구 `IEditorPanelHost`(`Common/Workspace`): 패널 앞으로 · 최대화 · 최대화 되돌리기 · dirty 문서 저장 · 버리기 · 세기. 구현 `EditorPanelHost`(GUI)가 패널 매니저와 도킹으로 하고
  `EditorContext::getPanelHost()` 가 돌려준다. `EditorPlayCommands::maximizePanel` · `restoreMaximizedLayout` 은 여기로 옮겼다(플레이 때 최대화 · 씬 뷰 Shift+Space 가 쓴다).
- 2d. UI 를 만지는 개발 명령을 그 UI 옆으로: `layout.*` · `panel.focus` · `panel.close` · `editor.theme` → `Common/GUI/EditorUIDevCommands.cpp`, `overlay.*` → `Panels/Scene/SceneViewDevCommands.cpp`.
- 2e. 나머지: 메뉴 막대의 맵 검사 수는 상태 막대 등록 줄(`SW_EDITOR_STATUS_BAR_ITEM`, `Common/GUI/EditorStatusBar.h`)로 뒤집어 `MapCheckPanel.cpp` 가 등록한다.
  도킹은 자체 시험을 묻지 않고 셸이 `EditorDockLayout::setPanelOverrideRequested` 로 알린다. `EditorUtil::areSceneEditsAllowed` → `EditorPlaySession::areSceneEditsAllowed`,
  `spawnPrefabFromAssetPath` → `EditorAssetCommands` 내부. 작업 공간의 붙여넣기 · 프리셋 전달자는 지우고 `EditorTransformCommands::pasteCopiedComponent*` 가 클립보드를 읽는다.
  `Popups` 는 GUI 와 같은 티어(4)로 — 셸 팝업(커맨드 팔레트 · 빠른 열기)은 기본 커맨드 표가 토글을 가리키고, 팝업은 패널 · 뷰포트를 모른다.
- 위험: 중간. 등록 줄을 옮기면 정적 등록 순서가 바뀔 수 있다(등록부는 순서 키로 정렬하므로 메뉴 순서는 그대로여야 한다 — `EditorRegistryDump` 로 전후 비교).
  검증: Debug 경고 0, EditorTest, `-L nogpu`, 에디터 시나리오(레이아웃 · 패널 · 테마 명령을 쓰는 것).

### 3. `EditorContext` 의존 줄이기 — 비용 대비 효과가 큰 곳만
- 3a. `EditorContext::get()` 의 이중 조회를 없앴다: 정적 포인터 하나를 돌려주고, `initialize` 의 로컬 서비스 자기 등록(`bindLocalService( this )`)을 지웠다.
  두 경로는 늘 같은 값이었다(같은 `this` 로 걸고 같이 푼다). 잰 값(Debug, DX12 에디터 기본 배치 600 프레임, 계수기 · 반복 100 만 번은 측정용 임시 코드):
  프레임당 `get()` 57 회, 호출당 550 ns → 8 ns. 프레임당 약 31 us → 무시할 만한 값이 된다. Debug 값이라 Release 이득은 이보다 훨씬 작다 — **최적화가 아니라 창구 하나로 줄인 단순화**로 적는다.
- 3b. 생성자 · 인자 주입은 하지 않는다. 호출 224 곳(62 파일) 가운데 SelfTest 52 · Panels 51 · Commands 43 · GUI 38 · Popups 17 · Viewport 13 · Workspace 9 다.
  패널은 등록부가 인자 없이 만들고(`createEditorPanel<T>`), 상용 엔진도 에디터 하위 시스템을 전역 창구로 준다(언리얼 `GEditor->GetEditorSubsystem<T>()`,
  유니티 `Selection.activeObject` · `EditorWindow.GetWindow<T>()`). 시험이 닿아야 하는 곳은 이미 상태를 인자로 받는다(`EditorPlaySession::update( data )` ·
  `EditorTransformCommands::pasteCopiedComponentValues( workspace, … )` · `EditorSceneGenerationSync::apply( workspace, … )`) — EditorTest 는 UI 매니저 때문에
  `EditorContext` 를 만들 수 없으므로, 컨텍스트를 인자로 바꿔도 시험이 늘지 않는다. 시험할 판단이 생기면 그 함수가 필요한 서비스 하나(작업 공간 · 선택)를 인자로 받는다.
- 3c. `EditorService` 와의 역할: `EditorService.h` = 호스트(엔진) 서비스 창구(`getService<T>()`)와 시험용 로컬 바인딩 · 활성 씬 바로가기,
  `EditorContext` = 에디터가 소유한 하위 시스템(작업 공간 · 선택 · 패널 매니저 · 알림 …). 3a 로 겹침(컨텍스트의 자기 등록)이 사라졌다. 둘을 합치지 않는다 —
  엔진 서비스는 호스트가 C-ABI 표로 넘기고(수명은 엔진), 에디터 하위 시스템은 컨텍스트가 소유한다(수명은 에디터). 언리얼도 `GEngine` 서브시스템과 `GEditor` 서브시스템을 가른다.
- 위험: 낮다. 검증: EditorTest, 에디터 시나리오.

### 4. `Panels/` 를 큰 패널별 하위 폴더로
- 도우미 파일이 딸린 패널은 자기 폴더: `Profiler/` · `InputMap/` · `MapCheck/` · `ContentBrowser/` · `Console/` · `Material/` · `Packaging/` · `RenderTarget/` · `TileMap/` · `UIPreview/`,
  `InspectorPanel` 은 `Inspector/` 로. 혼자인 패널은 영역 폴더: `Scene/`(씬 뷰 · 게임 뷰 · 계층 · 프리팹 · 되돌리기 기록 · 씬 뷰 초점 도우미 · 씬 뷰 개발 명령),
  `Animation/`(애니메이션 그래프 · 되감기 · 시퀀서 · 스프라이트 클립), `Data/`(데이터 테이블 · 대화 그래프), `Settings/`(전역 변수 · 환경설정 · 단축키 · 사용자 설정 · 모듈).
  Test Runner 는 2 단계에서 `SelfTest/` 로 갔다(자체 시험을 부르는 창이라 그 아래 층에 둘 수 없다). `Panels/` 바로 아래 파일은 없다.
- `git mv` 81 개 → include · `Test/EditorTest/CMakeLists.txt` · ThemePark 확장 · 문서 · 린트 표(`CheckProductNames`)의 경로를 일괄 갱신. 시험 파일도 같은 하위 폴더로(`Test/EditorTest/Panels/<폴더>/`).
  `Panels/` 에 리플렉션 헤더가 없어 `.gen.cpp` 스테일 함정은 해당하지 않는다. CMake 는 `GLOB_RECURSE ... CONFIGURE_DEPENDS` 라 목록을 고칠 곳이 없다.
- 위험: 낮다(기계적). 다른 에디터 작업과 충돌이 크다 — 적용 담당이 먼저 넣고, 겹친 단위는 새 경로로 다시 적용한다.

### 5. 임포터(`Common/Asset`)를 라이브러리로 뗄 가치 — 조사 결론: 떼지 않는다
- 쓸 곳: 명령 줄 임포트(`App --import-textures` · `--import-models` · `--import-heightfields` 와 `--check-*`)는 이미 있다 — App 이 에디터 모듈을 인스턴스 없이 올려
  C-ABI 진입점 `importEditorAssets`(`Common/Asset/AssetImportEntry.cpp`)를 부른다(`EditorModuleHost::importAssetsWithEditorModule`). 쿠킹(`CookAssets`)은 임포트 산출물(`.dds` · `.mesh`)을
  읽을 뿐 임포터를 부르지 않는다. 전용 서버 · Shipping 은 임포트하지 않는다.
- 상용 엔진도 임포트는 에디터 바이너리가 머리 없이 돈다(언리얼 `UnrealEditor-Cmd -run=…` 커맨드릿 · Interchange 가 에디터 모듈, 유니티 `-batchmode`, Godot `--headless --import`).
- 이미 층이 떨어져 있다: `Common/Asset` 은 티어 1 이고 ImGui 를 include 하지 않으며, 위로는 잎 헤더(`EditorService.h`)와 `Common` 루트만 본다(`CheckEditorLayers` 가 지킨다).
  쓸 곳이 생기면(에디터 없는 빌드 팜 · 쿠킹 서버에서 임포트) 그때 폴더를 정적 라이브러리 타깃으로 묶는 비용이 작다. 지금 떼면 타깃 · 내보내기 매크로 · 시험 링크만 늘고 쓰는 곳은 그대로다.

## 진행 기록

- 1 단계(2026-10-11): 게이트와 위반 37 줄 예외. RunLintSuite 63 개 OK.
- 2 단계(2026-10-11): 위반 37 → 0(예외는 prelude 넷 · wiring 둘만 남음). Debug all + AllTests 경고 0, ctest -L editor 3/3,
  AppTest `*Editor*`(에디터 시나리오 네 백엔드 · `EditorRegistriesKeepTheirOrder` — 상태 막대 등록 줄 한 줄 추가) 통과(GL sequencerkeys 는 부하 중 한 번 졌고 단독 3/3 통과).
- 3 단계(2026-10-11): `get()` 이중 조회 제거(호출당 550 → 8 ns, Debug). 주입 확대와 5 단계 분리는 하지 않기로 했다(위 결론).
- 4 단계(2026-10-11): Panels 하위 폴더로 81 개 이동. Debug 경고 0, ctest -L editor 3/3, RunLintSuite OK.

## 남은 일

- 3 · 4 단계 뒤에는 네 백엔드 에디터 시나리오(`AppTest --test_filter=*Editor*`)를 다시 돌리지 않았다(2 단계 뒤에는 돌렸다). 배치 끝 검증에서 본다.
- Shipping 빌드 · `-L hostgpu` · 게임별 프리셋 빌드(ThemePark 밖의 에디터 확장 모듈)는 확인하지 않았다.
- 패널 · 커맨드의 `EditorContext::get()` 224 곳은 그대로 둔다(3b). 시험할 판단이 생기면 그 함수만 필요한 서비스를 인자로 받는다.
