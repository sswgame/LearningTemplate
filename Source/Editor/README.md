# Editor (개발자용 에디터 모듈)

씬을 편집하고 디버깅하는 **에디터 UI(ImGui)** 입니다. Dev에서만 `EditorModule` MODULE로 빌드됩니다.

루트에는 모듈 진입점만 둡니다: `IEditor.h` (`sw`, App 계약), `ImGuiEditor.*` (`sw::editor`, 구현).
에디터 폴더의 나머지 타입은 `sw::editor`에 둡니다.

## 디렉터리 구조

### 공통 (`Common/`)

에디터 기능이 공통으로 쓰는 프레임워크입니다. 새 패널을 만들 때 여기부터 찾으면 됩니다.

- **EditorUtil**: 폰트·프로젝트/설정 경로, 프리팹 스폰, 편집 허용 여부
  (애셋 종류 판별은 여기가 아니라 `Workspace/EditorAssetType` 의 `EditorAssetTypeRegistry` 가 정본입니다)
- **Backend/**: ImGui 백엔드 인터페이스 (`IImGuiPlatformBackend`, `IImGuiRendererBackend`)
  - `Backend/Platform/`: Win32 / OSX / X11
  - `Backend/Render/`: DX11 / DX12 / Vulkan / OpenGL
- **Gui/**: ImGui 를 **직접 그리는** 공용 셸 — `EditorChrome`, `EditorMenuBar`, `EditorDockLayout`,
  `EditorDocumentPanel`, `EditorThemeUtil`, `EditorNotificationManager`(토스트),
  `EditorActionMenuManager`(우클릭 메뉴), `EditorCommandGui`(커맨드 표 · 전역 단축키 · 메뉴 항목),
  인터페이스 `IEditorPanel` / `IEditorPopup`
- **Widgets/**: 검색, 헤더, 툴바 구분선, 노드 그래프 캔버스(`EditorNodeGraph`), 뷰포트 입력 오버레이
- **Workspace/**: ImGui 없는 **상태** — 컨텍스트·선택·트랜잭션(Undo)·서비스 로케이터·애셋 종류 ·
  플레이(PIE) 세션(`EditorPlaySession`, `EditorSessionPolicy`)
- **Commands/**: 패널이 쓰는 **ImGui 없는 로직** — 애셋/씬/트랜스폼/데이터테이블 변이와 파일 IO,
  그리고 커맨드 정의를 담는 `EditorCommandRegistry`.
  패널은 UI 만, 실제 동작은 여기입니다 (그래서 테스트가 붙습니다)
- **Asset/**: 텍스처 임포트·베이크 (`TextureBaker`, `TextureImportConfig`, `ImageUtil`). 감시는 `Common/Workspace/AssetHotReload` 하나뿐이다
- **Config/**: Host JSON(`EditorConfig`)과 XML 시드(`EditorData`)

### 기능

- **Panels/**: Hierarchy, Inspector, Game View, Content Browser, Console, Profiler,
  Sequencer, Animation Graph, Dialogue Graph, Prefab Editor, Tile Map, Sprite Clip
  - `Panels/Inspector/`: 프로퍼티·컴포넌트 인스펙터 확장 — 컴포넌트 확장은 `<Component>Inspector.cpp` 하나씩
- **Viewport/**: 뷰포트 클라이언트, 툴바, 에디터 카메라(`EditorCamera`),
  화면 투영(`EditorViewportProjection`), 컴포넌트 시각화 등록부(`EditorViewportVisualizer`)
  - `Viewport/Visualizers/`: 시각화 하나에 파일 하나
- **Popups/**: 커맨드 팔레트, 퀵 런처, 본 계층 팝업
- **AssetActions/**: 애셋 종류별 에디터 동작(썸네일 · 열기 · 뷰포트 드롭) — 종류마다 파일 하나

### 어디에 두나

| 새로 쓰는 것 | 자리 |
|---|---|
| 메뉴·단축키·커맨드 팔레트에 나타날 동작 | `Common/Gui/EditorCommandGui.cpp` 의 커맨드 표 (아래) |
| 저장되지 않을 수 있는 편집 | `IEditorPanel` 의 문서 계약 (아래) — 자기 dirty 플래그 금지 |
| ImGui 를 그린다 | `Common/Gui/` · `Common/Widgets/` · `Panels/` · `Popups/` |
| ImGui 없이 상태만 든다 | `Common/Workspace/` |
| ImGui 없이 무언가를 바꾸거나 읽고 쓴다 | `Common/Commands/` |

경계가 흐려지면 테스트가 먼저 막힙니다 — `Test/EditorTest` 는 ImGui 없이 도는 것만 검증합니다.

## 애셋 종류를 하나 더하려면

종류별 분기를 쓰지 않습니다. 종류는 세 곳에서만 정의됩니다.

1. `Common/Workspace/EditorAssetType.h` 의 `EditorAssetKind` 에 값을 하나 더합니다.
2. `Common/Workspace/EditorAssetType.cpp` 의 두 표에 줄을 더합니다 — `kArrAssetMatch`(어떤 경로가 그 종류인가, 핫 리로드
   캐시 · 임포터)와 `kArrKindInfo`(이름 · 브라우저 라벨 · 패널 제목 · 아이콘 · 색 · Other 제외 · 임포트). 줄이 빠지거나 이름 ·
   라벨 · 아이콘 칸이 비면 `static_assert` 가 컴파일을 멈춥니다. 아이콘 · 색 · 콘텐츠 브라우저 필터 · 퀵 런처 분류 ·
   프로파일러 리소스 카탈로그 · 도구 패널 열기는 모두 이 표에서 나옵니다.
3. 썸네일 · 열기 · 뷰포트 드롭이 따로 필요하면 `AssetActions/<Kind>AssetTypeActions.cpp` 에 `IEditorAssetTypeActions` 를
   구현하고 같은 파일에 `EditorAssetTypeActionsRegistrar<…>` 정적 객체를 둡니다. 없으면 일반 문서 썸네일과 도구 패널 열기로
   대신합니다.

## 패널 · 팝업 · 인스펙터 · 시각화를 하나 더하려면

중앙 파일을 고치지 않습니다. 자기 .cpp 에 등록 한 줄을 두면 정적 초기화 때 등록부
(`Common/Workspace/EditorRegistry.h` 의 `EditorRegistry<T>` · `EditorRegistrar<T>`)에 실립니다.

| 종류 | 자기 .cpp 에 둘 한 줄 | 순서가 정하는 것 |
|---|---|---|
| 패널 | `SW_EDITOR_PANEL( MyPanel, "my_panel", EditorPanelCategory::Tool, 1900 );` | Panel 메뉴 · 그리기 순서 |
| 팝업 | `SW_EDITOR_POPUP( MyPopup, 400 );` (클래스에 `kPopupId`) | 그리기 순서 |
| 컴포넌트 인스펙터 | `SW_EDITOR_INSPECTOR( MyComponent, MyComponentInspector );` | (타입 계층이 정함) |
| 뷰포트 시각화 | `SW_EDITOR_VISUALIZER( Name, "id", 300, "Lbl", "툴팁", true, &draw );` | 툴바 체크박스 · 마스크 비트 |

- **순서는 등록 순서가 아니라 순서 키**입니다(같으면 id 사전순). 번역 단위 사이의 정적 초기화 순서는 정해지지 않습니다.
  지금 값은 100 간격이니 사이에 끼우려면 그 사이 값을 씁니다.
- 같은 종류의 같은 id 는 둘째 등록이 오류와 함께 거절됩니다. 패널 id 는 `windows.ini` 가시성 키이자 `-gv_editorOpenPanel` 값입니다.
- EditorModule 은 MODULE DLL 이라 아무도 참조하지 않는 등록자도 링크에서 버려지지 않습니다. 정적 라이브러리로 묶는 구성이
  생기면 그 전제가 깨지므로 `AppSmokeTest.EditorRegistriesKeepTheirOrder` 부터 확인하십시오.
- 확인은 `App.exe -EnableEditor -gv_editorRegistryDump=1` — 등록부를 `EditorRegistry|<종류>|<id>|…` 한 줄씩 남깁니다.
  `AppSmokeTest.EditorRegistriesKeepTheirOrder` 가 그 줄을 기대 목록과 대조합니다(새 줄이 끼는 것은 괜찮고, 기존 줄의 순서 ·
  제목이 바뀌면 집니다 — 패널 제목은 기본 도킹 배치가 대조하는 이름이기도 합니다).

## 커맨드를 하나 더하려면

메뉴 항목 · 전역 단축키 · 커맨드 팔레트 항목은 **한 정의에서 나옵니다** —
`Common/Gui/EditorCommandGui.cpp` 의 `_s_arrCommandRow` 표입니다. 한 줄을 넣으면
팔레트에 바로 나타나고(`_bPaletteVisible`), 단축키를 적었으면 전역에서 바로 먹습니다.
메뉴에 **보이게** 하려면 그 줄의 마지막 두 칸 — 메뉴 경로(`"MainMenu/File"`, 툴바 정렬 팝업은
`commandmenu::kViewportAlign`)와 순서 — 를 채우십시오. 순서의 백의 자리가 바뀌는 자리에 구분선이 들어가고, 메인
메뉴바의 메뉴끼리도 가장 작은 순서로 줄 섭니다(File 1xxx · Edit 2xxx · Build 3xxx). 라벨·아이콘·단축키
표기·활성 조건·툴팁도 표에서 옵니다. 같은 메뉴의 같은 순서는 `validate` 가 잡습니다.
메인 메뉴바는 한 단계 메뉴만 그립니다. 그 밖의 경로는 코드가 `EditorCommandGui::drawMenuItems` 로 그리는 경로
(`commandmenu::kArrHostedMenuPath`)여야 하고, 아니면 `validate` 가 "그려지지 않는 메뉴 경로" 로 시작할 때 Error 를 남깁니다
(그 목록의 경로에 표의 줄이 없어도 Error). 에디터 스모크의 `[Error]` 0 건이 그것을 잡습니다.

예전에는 같은 커맨드가 메뉴·단축키 사다리·팔레트 목록 **세 곳**에 따로 적혀 있었고, 그래서
실제로 어긋났습니다: `F7`(게임 컴파일)은 어느 라벨에도 없었고, `Ctrl+Shift+Z`(다시 실행)는
Inspector 가 포커스일 때만 먹었고, `Ctrl+Z` 는 전역 처리기와 Inspector 가 같은 프레임에 모두
받아 **두 번 되돌렸습니다**(ImGui 의 `IsKeyPressed` 는 소비되지 않습니다). 정의를 모은 뒤에는
`EditorCommandRegistry::validate` 가 중복 id·중복 조합을 시작할 때 잡습니다
(`Test/EditorTest/TestEditorCommandRegistry.cpp`).

**단축키 라벨을 손으로 적지 마십시오.** 툴팁의 `(Ctrl+S)` 도 표의 조합에서 만들어 붙습니다 —
그래야 조합을 바꿀 때 라벨이 거짓말을 하지 않습니다.

## 테마 프리셋을 하나 더하려면

`Common/Gui/EditorThemeUtil.cpp` 의 프리셋 표(`getPresetRows`)에 한 줄을 넣고 열거형에 값을
하나 더하면 끝입니다 — 저장 이름 · 콤보 라벨 · 팔레트가 그 한 줄에 있고, 대화상자의 콤보와
`EditorConfig` 저장·복원이 모두 표에서 나옵니다. 표와 열거형의 개수는 `static_assert` 가 맞춥니다.

예전에는 네 곳을 맞춰 고쳐야 했고(팔레트 switch · 문자열→열거형 사다리 · 열거형→문자열 switch ·
이름 배열), 이름 배열은 **열거형 순서에 인덱스로 묶여** 있어서 순서를 바꾸면 콤보가 조용히 틀린
이름을 보여 줬습니다.

## 뷰포트에서 컴포넌트를 집거나 그리려면

**피킹**은 `Common/Commands/EditorViewportPick` 이 합니다(ImGui 없음 → 테스트 있음).
고유한 경계가 있는 종류는 그 안의 제공자 표에 한 줄을 넣고, 그렇지 않은 컴포넌트는
**아무것도 하지 않아도** 집힙니다 — 전용 제공자가 못 잡은 오브젝트는 그 오브젝트의 모든
`SceneComponent` 를 기본 반지름(`kFallbackRadius`)으로 훑기 때문입니다. 그래서 게임이 만든
컴포넌트도 클릭으로 선택됩니다(예전에는 주 컴포넌트 하나만 봐서 안 됐습니다).

**디버그 시각화**는 `Viewport/Visualizers/` 에 파일 하나와 `SW_EDITOR_VISUALIZER` 한 줄이면 됩니다(위 절) —
라벨·툴팁·기본값·그리기 함수가 한 줄에 있고, **툴바 체크박스도 그 등록부에서 만들어집니다.**

월드 → 화면 변환은 `Viewport/EditorViewportProjection` 을 씁니다. 선분은 반드시
`projectSegment` 로 — 점 단위로 투영하면 카메라를 가로지르는 선이 통째로 사라집니다.

화면 → 월드는 반대쪽 하나입니다: 마우스 아래의 레이는 `EditorViewportPick::makeRay`
(캔버스 정규 좌표 → 레이), 바닥·2D 평면과의 교점은 `rayHitsAxisPlane` 입니다. 피킹 · 자 ·
애셋 드롭이 같은 둘을 쓰고, ImGui 에 닿는 것은 `EditorViewportClient` 의 마우스 위치 한 줄뿐이라
나머지는 `EditorTest` 가 검증합니다. 뷰포트 도구를 하나 더할 때 NDC 계산을 다시 쓰지 마십시오.

## 저장되지 않은 편집을 다루는 법

패널이 편집을 들고 있으면 **`IEditorPanel` 의 문서 계약**을 씁니다. 파생이 할 일은 둘뿐입니다:
편집이 생겼을 때 `markDocumentDirty()` 를 부르고, `saveDocument()`(성공 시 true)와 되돌릴 것이
있으면 `revertDocument()` 를 구현하는 것. dirty 비트는 **기반이 듭니다.**

그러면 이것이 전부 자동으로 따라옵니다 — 제목의 미저장 표시(`UnsavedDocument`), `Ctrl+S`
(`EditorAssetCommands::saveFocusedOrScene` → 포커스된 더티 문서), 종료·씬 전환 확인 모달의
개수 집계(`EditorPanelManager::countDirtyDocuments`), 전체 저장·버리기.

**자기 dirty 플래그를 새로 만들지 마십시오.** 예전에는 이 계약이 네 개의 가상 함수였고, 세 패널
(`EditorDocumentPanel`·`DataTablePanel`·`GlobalVariablesPanel`)이 똑같은 구현을 각자 복사했으며,
`InputMapEditorPanel` 은 `_bDirty` 만 두고 계약을 아예 구현하지 않았습니다 — 그래서 화면에는
"* Unsaved changes" 를 띄우면서 `Ctrl+S` 는 InputMap 이 아니라 **씬을** 저장했고, 종료 확인은
그 편집을 세지 않아 조용히 사라졌습니다.

문서 하나가 애셋 경로와 연동되는 도구 패널은 `Common/Gui/EditorDocumentPanel` 을 상속하십시오
(포커스 추적 · Undo 기준선 · 문서 전환 확인 팝업까지 얹어 줍니다). 한 패널이 문서를 둘 이상
들면(`DataTablePanel`) 기반 비트는 "무언가 바뀌었다"만 말하므로, 어느 쪽인지는 패널이 자기
반쪽 비트로 알고 한곳에서 동기화합니다(`syncDocumentDirty`).

계약 자체는 ImGui 없이 컴파일되므로 테스트가 있습니다: `Test/EditorTest/TestEditorPanelDocument.cpp`.

## 그려진 결과를 검증하는 법

`Test/EditorTest` 가 ImGui 없이 도는 것만 본다는 뜻은, **패널을 비워 놓고도 테스트가 통과한다**는
뜻입니다. 화면을 직접 볼 수 없을 때(CI·자동화·원격)는 `-gv_editorPanelDump=N` 을 씁니다.

```powershell
./App.exe -gv_profileFrames=40 -dx12 -EnableEditor -gv_editorPanelDump=25
```

N 번째 ImGui 프레임에 창 하나당 한 줄(이름 · 크기 · **정점 수** · 활성/접힘/숨김)과 요약을 로그에
남깁니다. **보이는데 정점이 0인 패널**이 곧 빈 패널입니다. 컨테이너(자식이 내용을 든 창)와 순수
오버레이(`NoInputs` — ImGuizmo 의 `gizmo` 가 그렇습니다)는 정상적으로 비므로 빼고 셉니다.

구현은 `Common/Gui/EditorPanelDump.*` 이고, 스위치 선언은 `Engine/EngineLoop.cpp` 에 있습니다 —
커맨드라인은 모듈 로드 전에 파싱되므로 EditorModule 이 선언한 전역 변수는 `-gv_...` 로 설정할 수
없습니다(파서가 조용히 무시합니다). 기준선과 비교 방법은
[docs/06_Backlog.md](../../docs/06_Backlog.md) 0절에 있습니다.

## ⚠️ 핵심 특징 및 규칙
- **Dev 모드 전용**: 이 폴더의 코드는 개발(Dev) 모드에서만 `MODULE DLL`로 빌드되고 동작합니다. 배포(Shipping) 빌드를 할 때는 **코드가 통째로 날아갑니다.**
- **게임 로직 분리**: **절대 게임(Game) 로직이 이 폴더의 코드에 의존해서는 안 됩니다.** 게임 코드에서 `#include "Editor/"` 등을 호출하면 Shipping 빌드가 100% 터집니다.
에디터에서만 써야 할 기능이라면 매크로를 신중하게 사용하세요.
