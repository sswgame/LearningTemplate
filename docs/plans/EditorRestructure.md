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

- 2a. 틀을 제자리로: `EditorPanelManager` · `EditorPopupManager` 를 `Common/GUI` 로(인터페이스 옆). `EditorViewTargetUtil`(ImGui 없는 뷰 RT 판정)을 `Common/Workspace` 로.
  `EditorThemeConfig` 를 `Common/Config/EditorThemeConfig.h` 로. `EditorAssetTypeActions`(인터페이스 + 등록부)를 `Common/Workspace` 로. `EditorExternalToolJob` 을 `Common` 루트로.
- 2b. 알림을 모델 · 뷰로: `EditorNotificationManager`(큐 · `push` · 시간 진행, ImGui 없음)는 `Common/Workspace`, 그리기는 `Common/GUI/EditorNotificationView`.
- 2c. 패널 창구 인터페이스 `IEditorPanelHost`(`Common/Workspace`): 열림 확인 · 열고 닫기 · 앞으로 · 더러운 문서 저장 · 버리기 · 세기. `EditorPanelManager` 가 구현하고 `EditorContext::getPanelHost()` 가 돌려준다.
  Commands · Viewport · Popups 는 이것만 본다.
- 2d. UI 를 만지는 개발 명령을 그 UI 옆으로: 레이아웃 · 패널 열고 닫기 · 최대화(`EditorDevCommands` · `EditorPlayCommands`) → `Common/GUI`, 테마 → `EditorThemeUtil.cpp`,
  씬 뷰 오버레이 → `SceneViewPanel.cpp`, 디버그 그리기 데모(에디터 카메라) → `Viewport`.
- 2e. 남은 위 방향(메뉴 막대의 맵 검사 수, 커맨드 GUI 의 팝업 토글, 플레이 툴바의 카메라, 도킹의 자체 시험 요청, `EditorUtil.cpp`, `EditorWorkspace.h` → 변환 명령)은 하나씩 보고 옮기거나 인터페이스로 뒤집는다.
- 위험: 중간. 등록 줄을 옮기면 정적 등록 순서가 바뀔 수 있다(등록부는 순서 키로 정렬하므로 메뉴 순서는 그대로여야 한다 — `EditorRegistryDump` 로 전후 비교).
  검증: Debug 경고 0, EditorTest, `-L nogpu`, 에디터 시나리오(레이아웃 · 패널 · 테마 명령을 쓰는 것).

### 3. `EditorContext` 의존 줄이기 — 비용 대비 효과가 큰 곳만
- 3a. `EditorContext::get()` 의 이중 조회를 없앤다: 정적 포인터 하나로 돌려주고 로컬 서비스 등록을 지운다(창구가 하나가 된다).
  비용은 **잰다**: 에디터 프레임당 `get()` 호출 수(Debug 계수기)와 호출당 시간(Release) — 숫자가 작으면 "단순화" 로만 적고 최적화라 부르지 않는다.
- 3b. 생성자 · 인자 주입은 Commands 의 정적 함수에서 효과가 크다(EditorTest 가 전역 없이 부른다). 패널은 등록부가 인자 없이 만들고 상용 엔진도 전역 창구를 쓴다
  (언리얼 `GEditor->GetEditorSubsystem<T>()`, 유니티 `Selection.activeObject` · `EditorWindow.GetWindow<T>()`) — 패널 225곳을 바꾸는 효과는 작다. 그래서 패널은 하지 않는다.
- 3c. `EditorService` 와의 역할: `EditorService` = 호스트(엔진) 서비스와 시험용 로컬 바인딩, `EditorContext` = 에디터가 소유한 하위 시스템. 3a 로 겹침(자기 등록)이 사라진다.
- 위험: 낮다. 검증: EditorTest(로컬 서비스 바인딩을 쓰는 시험), 에디터 시나리오.

### 4. `Panels/` 를 큰 패널별 하위 폴더로
- 도우미 파일이 딸린 패널은 자기 폴더(`Profiler/` · `InputMap/` · `MapCheck/` · `ContentBrowser/` · `Console/` · `Material/` · `Packaging/` · `RenderTarget/` · `TestRunner/` · `TileMap/` · `UIPreview/`),
  `InspectorPanel` 은 `Inspector/` 로, 혼자인 패널은 영역 폴더(`Scene/` · `Animation/` · `Data/` · `Settings/`)로.
- `git mv` → include 경로 일괄 갱신 → CMake 글롭 · `Test/EditorTest/CMakeLists.txt` 소스 목록 갱신 → 재구성. 옮긴 헤더의 `.gen.cpp` 는 mtime 판정이 낡을 수 있으니 생성 폴더를 지우고 다시 만든다.
- 위험: 낮다(기계적). 다른 에디터 작업과 충돌이 크다 — 적용 담당이 먼저 넣는다. 검증: Debug 경고 0 · EditorTest · `-L nogpu` · 린트(include 순서).

### 5. 임포터(`Common/Asset`)를 라이브러리로 뗄 가치 — 조사만
- 결론은 이 절에 적는다(아래 "5 단계 결론").

## 진행 기록
