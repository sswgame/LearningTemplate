# Editor (개발자용 에디터 모듈)

씬을 편집하고 디버깅하는 **에디터 UI(ImGui)** 입니다. Dev에서만 `EditorModule` SHARED 로 빌드됩니다(확장 모듈이 링크하고 `SW_EDITOR_API` 로 내보낸 API 를 씁니다).

루트에는 모듈 진입점만 둡니다: `IEditor.h` (`sw`, App 계약), `ImGuiEditor.*` (`sw::editor`, 구현).
에디터 폴더의 나머지 타입은 `sw::editor`에 둡니다.

## 디렉터리 구조

### 공통 (`Common/`)

에디터 기능이 공통으로 쓰는 프레임워크입니다. 새 패널을 만들 때 여기부터 찾으면 됩니다.

- **EditorUtil**: 폰트·프로젝트/설정 경로, 프리팹 스폰, 편집 허용 여부
  (애셋 종류 판별은 여기가 아니라 `Workspace/EditorAssetType` 의 `EditorAssetTypeRegistry` 가 정본입니다)
- **EditorColor.h**: ImGui 없는 색 값 타입(`Color4`)과 공통 색 상수(`style`) — 상태(`Workspace/EditorAssetType`)와 위젯이 함께 쓰므로
  어느 한쪽 폴더가 아니라 `Common/` 바로 아래에 둡니다
- **Backend/**: ImGui 백엔드 인터페이스 (`IImGuiPlatformBackend`, `IImGuiRendererBackend`), 렌더 스레드가 그릴 draw 데이터 사본
  (`EditorDrawDataSnapshot`), UI 스레드가 놓은 GPU 자원의 해제 순서(`EditorDrawReleaseQueue` — 아래 절)
  - `Backend/Platform/`: Win32 / X11
  - `Backend/Render/`: DX11 / DX12 / Vulkan / OpenGL. 네이티브 객체는 `IRHIDevice` 가 판 번호를 대조해 내주는 `RHINativeHandles`
    로만 받습니다 — 백엔드 디바이스 클래스로 캐스팅하지 않습니다.
- **GUI/**: ImGui 를 **직접 그리는** 공용 셸 — `EditorChrome`, `EditorMenuBar`, `EditorDockLayout`,
  `EditorDocumentPanel`, `EditorThemeUtil`, `EditorFontSetup`, `EditorNotificationManager`(토스트), `EditorPanelDump`(아래 "그려진 결과"),
  `EditorCommandGUI`(커맨드 표 · 전역 단축키 · 메뉴 항목),
  인터페이스 `IEditorPanel` / `IEditorPopup`
- **Widgets/**: 검색, 헤더, 툴바 구분선, 노드 그래프 캔버스(`EditorNodeGraph`), 뷰포트 입력 오버레이
- **Workspace/**: ImGui 없는 **상태** — 컨텍스트·선택·트랜잭션(Undo)·서비스 로케이터·애셋 종류 ·
  플레이(PIE) 세션(`EditorPlaySession`, `EditorSessionPolicy`) · 에디터 확장 등록부의 공통 모양(`EditorRegistry<T>` · `EditorRegistrar<T>`) ·
  에셋 파일 감시(`AssetHotReload` · `FileWatchDispatcher`)
- **Commands/**: 패널이 쓰는 **ImGui 없는 로직** — 애셋/씬/트랜스폼/데이터테이블 변이와 파일 IO,
  그리고 커맨드 정의를 담는 `EditorCommandRegistry`. 외부 도구(파이썬 검증 · git)를 전용 스레드에서 띄우는 잡은 `EditorExternalToolJob`.
  패널은 UI 만, 실제 동작은 여기입니다 (그래서 테스트가 붙습니다)
- **Asset/**: 원본 임포트 — 텍스처(`TextureImporter`, `TextureImportConfig`, `ImageUtil`) · 모델(`ModelImporter`, `ModelImportConfig`)과 둘이 쓰는
  스탬프 절차(`AssetImportStamp`) · 규칙의 경로 조건(`AssetImportPathFilter`), 헤드리스 임포트 진입점(`AssetImportEntry.cpp` — 아래 "텍스처는 들일 때 임포트한다" · "모델도 들일 때 임포트한다"), 저장 · 임포트 직후 검증(`EditorAssetValidation` — 아래 "저장 · 임포트 직후 에셋 검증"). 감시는 `Common/Workspace/AssetHotReload` 하나뿐이다
- **Localization/**: 로컬라이제이션 도구 — 글 수집 · PO 교환의 본문(`LocalizationTools`)과 헤드리스 진입점(`LocalizationToolEntry.cpp`). 아래 "로컬라이제이션 도구"
- **SourceControl/**: 버전 관리 잠금(체크아웃) — 공급자 추상(`ISourceControlProvider`: git LFS · 없음)과 창구(`EditorSourceControl`). 아래 "버전 관리 잠금"
- **Config/**: Host JSON(`EditorConfig`)과 XML 시드(`EditorToolDefaults`)

### 기능

- **Panels/**: Hierarchy, Inspector, Scene(에디터 카메라 · 편집 보조선), Game(게임 카메라 출력), Content Browser, Console, Profiler(CPU 구간 · GPU 패스 · 카운터 실시간 표 + 프레임 그래프 — 집계는 ImGui 없는
  `ProfilerScopeHistory`, "Open Tracy" 는 `Common/Commands/EditorTracyLauncher` 가 같은 판 Tracy 뷰어를 띄워 localhost 에 붙인다),
  Sequencer, Animation Graph, Animation Rewind(기록된 포즈 · 상태를 시간 막대로 훑기 — 훑으면 PIE 를 멈춘다), Dialogue Graph, Prefab Editor, Tile Map,
  Sprite Clip, User Settings(플레이어 옵션을 메뉴 바인딩 API 로 바꿔 보는 창 —
  셀프 시험 `userSettings.panelDrawsEveryTab`), UI Preview(런타임 UI 문서를 게임 UI 와 따로 오프스크린 화면으로 지어 보는 창 — 해상도 견본 · UI/글자 배율 ·
  안전 영역 · 테마 · 레이아웃 사각형 · 위젯 트리 선택, 판단은 ImGui 없는 `UIPreviewLogic` — EditorTest `UIPreviewLogicTest`)
  - `Panels/Inspector/`: 프로퍼티·컴포넌트 인스펙터 확장 — 컴포넌트 확장은 `<Component>Inspector.cpp` 하나씩
- **Viewport/**: 뷰포트 클라이언트(씬 뷰), 툴바, 에디터 카메라(`EditorCamera`), 뷰 RT 판정(`EditorViewTargetUtil` — 요청 규칙 · 크기 · 화면 비율, ImGui 없이 EditorTest 가 시험),
  화면 투영(`EditorViewportProjection`), 컴포넌트 시각화 등록부(`EditorViewportVisualizer`),
  시각화가 그릴 월드 도형(`EditorVisualizerGeometry` — ImGui 없이 만들어 EditorTest 가 검증한다)
  - `Viewport/Visualizers/`: 시각화 하나에 파일 하나
- **Popups/**: 커맨드 팔레트, 퀵 런처, 본 계층 팝업
- **AssetActions/**: 애셋 종류별 에디터 동작(썸네일 · 열기 · 뷰포트 드롭) — 인터페이스와 등록부(`EditorAssetTypeActions`) +
  종류마다 파일 하나. 다른 확장(패널 · 인스펙터 · 시각화)처럼 계약과 구현이 한 폴더에 있다
- **SelfTest/**: 에디터 안에서 도는 시험(`SW_EDITOR_SELF_TEST`)과 실행기(`-gv_editorSelfTest`), 등록부 덤프
  (`EditorRegistryDump` — `-gv_editorRegistryDump=1`, ImGui 없이 로그로 남겨 `AppSmokeTest` 가 대조한다)

### 어디에 두나

| 새로 쓰는 것 | 자리 |
|---|---|
| 메뉴·단축키·커맨드 팔레트에 나타날 동작 | `Common/GUI/EditorCommandGUI.cpp` 의 커맨드 표 (아래) |
| 저장되지 않을 수 있는 편집 | `IEditorPanel` 의 문서 계약 (아래) — 자기 dirty 플래그 금지 |
| ImGui 를 그린다 | `Common/GUI/` · `Common/Widgets/` · `Panels/` · `Popups/` |
| ImGui 없이 상태만 든다 | `Common/Workspace/` |
| ImGui 없이 무언가를 바꾸거나 읽고 쓴다 | `Common/Commands/` |

경계가 흐려지면 테스트가 먼저 막힙니다 — `Test/EditorTest` 는 ImGui 없이 도는 것만 검증합니다.

## 애셋 종류를 하나 더하려면

종류별 분기를 쓰지 않습니다. 종류는 세 곳에서만 정의됩니다.

1. `Common/Workspace/EditorAssetType.h` 의 `EditorAssetType` 에 값을 하나 더합니다.
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
| 팝업 | `SW_EDITOR_POPUP( MyPopup, 400 );` (클래스에 `kPopupID`) | 그리기 순서 |
| 컴포넌트 인스펙터 | `SW_EDITOR_INSPECTOR( MyComponent, MyComponentInspector );` | (타입 계층이 정함) |
| 뷰포트 시각화 | `SW_EDITOR_VISUALIZER( Name, "id", 300, editoricon::kBug, "Lbl", "툴팁", true, &draw );` | 툴바 토글 순서 |
| 프로퍼티 타입 그리기 | `SW_EDITOR_PROPERTY_DRAWER( Name, "TypeName", Drawer );` | (타입 이름이 정함, 내장 위젯보다 이김) |

- **순서는 등록 순서가 아니라 순서 키**입니다(같으면 id 사전순). 번역 단위 사이의 정적 초기화 순서는 정해지지 않습니다.
  지금 값은 100 간격이니 사이에 끼우려면 그 사이 값을 씁니다.
- 같은 종류의 같은 id 는 둘째 등록이 오류와 함께 거절됩니다. 패널 id 는 `windows.ini` 가시성 키이자 `-gv_editorOpenPanel` 값입니다.
- EditorModule 은 DLL 이라 아무도 참조하지 않는 등록자도 링크에서 버려지지 않습니다. 정적 라이브러리로 묶는 구성이
  생기면 그 전제가 깨지므로 `AppSmokeTest.EditorRegistriesKeepTheirOrder` 부터 확인하십시오.
- 확인은 `App.exe -EnableEditor -gv_editorRegistryDump=1` — 등록부를 `EditorRegistry|<종류>|<id>|…` 한 줄씩 남깁니다.
  `AppSmokeTest.EditorRegistriesKeepTheirOrder` 가 그 줄을 기대 목록과 대조합니다(새 줄이 끼는 것은 괜찮고, 기존 줄의 순서 ·
  제목이 바뀌면 집니다 — 패널 제목은 기본 도킹 배치가 대조하는 이름이기도 합니다).

## 확장 모듈 — 키트 · 게임의 에디터 코드

키트나 게임이 에디터에 패널, 시각화, 인스펙터, 커맨드를 더하려면 그 폴더의 `Editor/` 하위 폴더에 에디터 확장 모듈을 둡니다(키트는 `GF_Editor_<키트>`, 게임은 `SWGameEditor`).
확장 모듈은 Dev 전용 SHARED DLL 이고 EditorModule 을 링크합니다. 등록은 EditorModule 과 똑같이 자기 파일의 `SW_EDITOR_*` 한 줄이고, 등록 줄은 EditorModule 의 목록 하나에 오릅니다.
만드는 절차는 [Kits README](../GameFramework/Kits/README.md) "키트에 에디터 도구를 붙일 때" 절이고, 본보기는 `GF_Editor_ThemePark` 입니다.

- 확장 모듈이 언로드되면(핫 리로드, 종료) 그 모듈의 등록 줄로 만든 패널, 팝업, 인스펙터, 커맨드가 이미지를 언로드하기 **전에** 지워집니다(`EditorModuleUnloadListener`).
- 확장 DLL 의 ImGui 호출은 CMake 가 만든 결속기가 에디터 컨텍스트에 겁니다. ImGuizmo 를 쓰면 그리기 전에 `ImGuizmo::SetImGuiContext( ImGui::GetCurrentContext() )` 를 부릅니다.
  ImDrawList 콜백(`AddCallback`)은 쓰지 않습니다. 렌더 스레드가 늦게 부르므로 그때는 확장이 이미 언로드되었을 수 있습니다.
- 확장이 쓰는 에디터 API 는 `SW_EDITOR_API` 로 내보낸 것뿐입니다. 링크 오류(undefined symbol)가 나면 그 클래스에 `SW_EDITOR_API` 를 붙입니다.
- 씬 뷰 카메라를 옮기는 진입점은 `EditorSceneViewUtil::focusOn` 입니다. 시나리오 `editor/extensionpanel`, 자체 시험 `themepark.*`.

## 커맨드를 하나 더하려면

메뉴 항목 · 전역 단축키 · 커맨드 팔레트 항목은 **한 정의에서 나옵니다** —
`Common/GUI/EditorCommandGUI.cpp` 의 `_s_arrCommandRow` 표입니다. 한 줄을 넣으면
팔레트에 바로 나타나고(`_bPaletteVisible`), 단축키를 적었으면 전역에서 바로 먹습니다.
메뉴에 **보이게** 하려면 그 줄의 마지막 두 칸 — 메뉴 경로(`"MainMenu/File"`, 툴바 정렬 팝업은
`commandmenu::kViewportAlign`)와 순서 — 를 채우십시오. 순서의 백의 자리가 바뀌는 자리에 구분선이 들어가고, 메인
메뉴바의 메뉴끼리도 가장 작은 순서로 줄 섭니다(File 1xxx · Edit 2xxx · Build 3xxx). 라벨·아이콘·단축키
표기·활성 조건·툴팁도 표에서 옵니다. 같은 메뉴의 같은 순서는 `validate` 가 잡습니다.
메인 메뉴바는 한 단계 메뉴만 그립니다. 그 밖의 경로는 코드가 `EditorCommandGUI::drawMenuItems` 로 그리는 경로
(`commandmenu::kArrHostedMenuPath`)여야 하고, 아니면 `validate` 가 "그려지지 않는 메뉴 경로" 로 시작할 때 Error 를 남깁니다
(그 목록의 경로에 표의 줄이 없어도 Error). 에디터 스모크의 `[Error]` 0 건이 그것을 잡습니다.

`EditorCommandRegistry::validate` 는 중복 id·중복 조합도 시작할 때 잡습니다(`Test/EditorTest/Common/Commands/TestEditorCommandRegistry.cpp`).
**패널에서 단축키를 따로 처리하지 마십시오** — ImGui 의 `IsKeyPressed` 는 소비되지 않으므로, 전역 처리기와 패널이 같은 조합을 보면
같은 프레임에 두 번 실행됩니다(예: `Ctrl+Z` 가 두 번 되돌림).

**단축키 라벨을 손으로 적지 마십시오.** 툴팁의 `(Ctrl+S)` 도 표의 조합에서 만들어 붙습니다 —
그래야 조합을 바꿀 때 라벨이 거짓말을 하지 않습니다.

## 테마 프리셋을 하나 더하려면

`Common/GUI/EditorThemeUtil.cpp` 의 프리셋 표(`getPresetRows`)에 한 줄을 넣고 열거형에 값을
하나 더하면 끝입니다 — 저장 이름 · 콤보 라벨 · 팔레트가 그 한 줄에 있고, 대화상자의 콤보와
`EditorConfig` 저장·복원이 모두 표에서 나옵니다. 표와 열거형의 개수는 `static_assert` 가 맞춥니다.

## 뷰포트에서 컴포넌트를 집거나 그리려면

**피킹**은 `Common/Commands/EditorViewportPick` 이 합니다(ImGui 없음 → 테스트 있음).
고유한 경계가 있는 종류는 그 안의 제공자 표에 한 줄을 넣고, 그렇지 않은 컴포넌트는
**아무것도 하지 않아도** 집힙니다 — 전용 제공자가 못 잡은 오브젝트는 그 오브젝트의 모든
`SceneComponent` 를 기본 반지름(`kFallbackRadius`)으로 훑기 때문입니다. 그래서 게임이 만든
컴포넌트도 클릭으로 선택됩니다.

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

**자기 dirty 플래그를 새로 만들지 마십시오.** 계약 밖의 플래그는 `Ctrl+S` 가 포커스된 문서 대신 씬을 저장하게 하고,
종료 확인이 그 편집을 세지 않아 편집이 조용히 사라집니다.

문서 하나가 애셋 경로와 연동되는 도구 패널은 `Common/GUI/EditorDocumentPanel` 을 상속하십시오
(포커스 추적 · Undo 기준선 · 문서 전환 확인 팝업까지 얹어 줍니다). 한 패널이 문서를 둘 이상
들면(`DataTablePanel`) 기반 비트는 "무언가 바뀌었다"만 말하므로, 어느 쪽인지는 패널이 자기
반쪽 비트로 알고 한곳에서 동기화합니다(`syncDocumentDirty`).

계약 자체는 ImGui 없이 컴파일되므로 테스트가 있습니다: `Test/EditorTest/Common/GUI/TestEditorPanelDocument.cpp`.

## 그려진 결과를 검증하는 법

`Test/EditorTest` 가 ImGui 없이 도는 것만 본다는 뜻은, **패널을 비워 놓고도 테스트가 통과한다**는
뜻입니다. 화면을 직접 볼 수 없을 때(CI·자동화·원격)는 `-gv_editorPanelDump=N` 을 씁니다.

```powershell
./App.exe -gv_profileFrames=40 -dx12 -EnableEditor -gv_editorPanelDump=25
```

N 번째 ImGui 프레임에 창 하나당 한 줄(이름 · 크기 · **정점 수** · 활성/접힘/숨김)과 요약을 로그에
남깁니다. **보이는데 정점이 0인 패널**이 곧 빈 패널입니다. 컨테이너(자식이 내용을 든 창)와 순수
오버레이(`NoInputs` — ImGuizmo 의 `gizmo` 가 그렇습니다)는 정상적으로 비므로 빼고 셉니다.

구현과 스위치 선언은 `Common/GUI/EditorPanelDump.*` 에 있습니다 — 모듈의 전역 변수도 모듈을 올릴 때 커맨드라인 값을 받습니다.
기준선과 비교 방법은 [검증과 측정](../../docs/08_Verification.md) 1절에 있습니다.

## 텍스처는 들일 때 임포트한다

런타임은 DDS 만 읽습니다. 원본 이미지(PNG · JPG …)는 `textures_raw/` 에 두고, 같은 상대 경로의 `textures/*.dds` 로 임포트합니다
(`TextureImporter::makeImportedTexturePath`, 폴더 규칙은 `Scripts/lint/gate/CheckTextureFolders.py`).

- **에디터가 떠 있을 때**: 핫 리로드가 원본 변경을 받으면 `TextureImporter::importChangedSourceImage` 가 임포트하고, 임포트된 DDS 의 쓰기가
  다음 감시 이벤트로 와서 텍스처 캐시가 다시 읽습니다. 임포트 설정(`TextureImportConfig.json`)은 매번 읽습니다.
- **헤드리스**: `App --import-textures`(어긋난 것을 임포트하고 스탬프 갱신) · `--check-textures`(쓰지 않고 대조만). App 이 에디터 모듈을 인스턴스
  없이 올려 `importEditorAssets`(`AssetImportEntry.cpp`, 종류 `EditorImportKind`)를 부릅니다. Dev 빌드에서만 됩니다.
- **스탬프**: `textures_raw/` 폴더마다 `import.stamp` 에 `<원본 해시> <DDS 해시> <상대 경로>` 한 줄씩. 원본 해시는 원본 바이트 + 적용한 규칙 +
  임포터 버전(`computeSourceHash`)이라 규칙만 바꿔도 어긋남이고, DDS 해시로 손댄 DDS 도 잡힙니다. 판정은 파일 시간이 아니라 **내용**입니다
  (git 이 시간 순서를 뒤집습니다). 원본이 사라진 줄도 어긋남입니다 — `ImportStale` 은 줄만 지우고 남은 DDS 는 사람이 정리합니다.
- `.hdr` 는 stb(8비트)를 거치지 않고 DirectXTex `LoadFromHDRFile` 로 부동소수점으로 읽어 BC6H_UF16(`"format": "bc6h"`) · RGBA16F(`"rgba16f"`)로만 임포트합니다 — 규칙의 포맷이 8 비트면 그 원본의 실패로 보고합니다(쓰는 쪽이 생기면 `"*.hdr"` 규칙에 `"format": "bc6h"`, `"srgb": false`).
- 시험: `TextureImportStampTest.RepositoryRawTexturesMatchTheirDDS`(저장소의 원본과 DDS 가 맞는지), `AppSmokeTest.TextureCheckRunsHeadlessThroughTheEditorModule`.
- 폴더 훑기 · 스탬프 · 어긋남 판정은 모델과 같은 한 벌입니다(`AssetImportStampUtil` + 종류마다 `IRawAssetImporter`).

## 모델도 들일 때 임포트한다

glTF 원본(`.glb` · `.gltf` · `.vrm`)은 `models_raw/` 에 두고 같은 상대 경로의 `models/<이름>.mesh`(스킨드 모델이면 옆 폴더의 스켈레톤 · 부착 메시 · 클립까지)로
임포트합니다(`ModelImporter`, cgltf + meshoptimizer).
런타임은 `.mesh` 만 읽고(`MeshCache`), `MeshComponent::_meshID` 에 그 경로를 적습니다.

- **변환**: (스킨 없는 모델) 기본 씬의 노드 계층을 월드 변환째 한 메시로 합칩니다. glTF(오른손 · +Y 위 · 앞 +Z)를 엔진(왼손 · +Y 위 · 앞 +Z, 앞면 = 시계 방향)으로
  옮기려고 **X 를 뒤집고 삼각형마다 감김을 뒤집습니다**(노드가 거울상이면 한 번 더). 노멀이 없으면 면 노멀, 색은 baseColorFactor × COLOR_0.
  삼각형이 아닌 프리미티브는 경고하고 건너뜁니다. 텍스처는 로그로만 알리고, 머티리얼은 게임이 `PrimitiveLook` 으로 고릅니다.
- **원본은 고치지 않습니다**: 씬 뿌리 목록에 부모가 있는 노드를 적은 비표준 파일(UniGLTF — Kenney 키트)은 cgltf 가 파싱에서 거부하므로, 임포터가 넘기기
  전에 JSON 의 `scenes[].nodes` 만 맨 위 조상으로 바꾸고 경고합니다(GLB 는 BIN 청크를 그대로 옮깁니다).
- **임포트 규칙**: `Config/Editor/ModelImportConfig.json` 의 `rules` — 경로 조건은 텍스처 규칙과 같고(`include_patterns` · `exclude_patterns` ·
  `include_paths` · `exclude_paths`, 첫 매칭이 이김), 옵션은 `translation`(glTF 원본 공간에서 더하는 이동 — 원본의 배치 오프셋 지우기)과
  `recenter`(`none` 기본 · `xz` 경계의 XZ 중심을 원점으로 · `bottom-center` 거기에 더해 가장 낮은 Y 를 0 으로), 적용은 그 순서입니다. 규칙은 원본
  해시에 섞여 바꾸면 스탬프가 어긋남이 됩니다. 모르는 값은 설정 전체를 거부합니다.
- **헤드리스 · 스탬프**: `App --import-models` · `--check-models`. `models_raw/import.stamp` 는 텍스처와 같은 형식이고 원본 해시에 임포터 버전 ·
  `.mesh` 형식 버전 · 적용한 규칙 · `.gltf` 의 외부 버퍼가 섞입니다. 임포트 동작을 바꾸면 `ModelImporterInternal::kImporterVersion` 을 올립니다.
- **스킨드 모델**(스킨이 있는 glTF — 모든 스킨의 관절 노드가 한 스켈레톤): `models/<이름>.mesh` 는 스킨드 메시들을 합친 바인드 포즈 메시 +
  정점마다 본 넷 · 가중치(`.mesh` 2 판의 스킨 스트림)이고, 관절이 아닌 노드의 스킨 없는 메시는 뿌리 본에 가중치 1 로 합칩니다. 나머지는 옆 폴더
  `models/<이름>/` 에 씁니다 — `<이름>.skeleton.json`(본 = 관절, 부모가 앞, 관절 위 비관절 노드의 변환은 뿌리 본에 접음 · 역 바인드 · 부착 표) ·
  `parts/<노드>.mesh`(관절 아래 스킨 없는 메시 — 무기 · 투구, 노드 로컬 공간. 부모 본 · 로컬 변환은 스켈레톤의 부착 표에 **읽기 전용 임포트 데이터**로
  남아 나중에 소켓 파일을 만들 근거가 된다 — 소켓 자체는 따로 둔 원본 에셋이다) · `clips/<클립>.animclip`(애니메이션마다 하나 — 관절마다 균일 표본으로
  다시 뽑아 규칙의 코덱으로 압축, 클립마다 압축률 · 최대 오차(mm)를 로그로 보고). 옆 폴더는 임포트마다 지우고 다시 쓰며, 스탬프의 결과 해시는
  `.mesh` 와 옆 폴더 파일 전부를 섞습니다(`IRawAssetImporter::computeImportedHash`). 스킨드 모델에 `translation` · `recenter` 는 오류입니다(바인드 행렬이 어긋난다).
- **모프 타깃**: 프리미티브의 `targets`(POSITION · NORMAL 차이)를 합친 메시의 정점 번호로 옮겨 `.mesh` 의 모프 덩어리(`MRPH`)에 싣습니다 — 움직이는 정점만,
  이름은 메시의 `extras.targetNames`(없으면 `target<n>`), 같은 이름의 타깃은 프리미티브를 건너 하나로 묶입니다. 노드 변환 · X 거울상은 위치 · 노멀과 같이
  겁니다. 애니메이션의 `weights` 채널은 타깃 이름의 커브로 클립에 실리고(표본은 클립과 같은 율), 런타임 애니메이터가 이름이 같은 타깃의 가중치로 겁니다.
  저장소의 시험 머리는 `game/empty/models_raw/testhead.gltf`(합성 — 타깃 일곱 · Talk 가중치 클립).
- **애니메이션 규칙 키**: `animations`(기본 참) · `clips`(가져올 클립 이름, 비면 모두 — 원본에 없는 이름은 임포트 오류) · `animation_codec`(`raw` · `acl`,
  모르는 이름은 설정 오류) · `animation_sample_rate` · `animation_precision` · `animation_shell_distance`(미터) · `root_motion_bone`(루트 모션 트랙) ·
  `attachments`(기본 참). 모르는 키는 설정 전체를 거부합니다.
- **곁 데이터**: 원본 옆 `<이름>.clips.json` — `{ "clips": { "<클립>": { "loop", "notifies": [ { "name", "time", "duration" } ], "curves": { "<이름>": [ [시각, 값] ] } } } }`.
  클립의 반복 · 알림 · 커브는 glTF 에 없으므로 사람이 여기 적습니다. 원본 해시에 섞이고, 모르는 키 · 원본에 없는 클립 이름은 임포트 오류입니다.
- **핫 리로드**: `models_raw/` 원본이 바뀌면 일괄 임포트하고, 쓰인 `.mesh` 를 메시 캐시가 같은 `Mesh` 에 제자리로 다시 읽습니다.
- 시험: `ModelImporterTest`(좌표계 · 감김 · 노드 변환 · 색 · 스탬프 · 비표준 씬 뿌리 · 규칙 · 스킨드 모델(KayKit 기사 41 본 · 클립 76 · 부착 · 곁 데이터),
  저장소 원본 ↔ 결과 대조), 엔진 쪽은 `MeshAssetTest` · `SkeletalAnimationTest`.

### VRM — MToon 머티리얼을 툰 머티리얼로

VRM(`.vrm` — glTF 바이너리 + `extensions.VRM`(0.x) 또는 `VRMC_*`(1.0))도 같은 길로 임포트합니다. 머티리얼에 MToon 값이 있으면(`VrmMaterialImporter`):

- **머티리얼마다 구간**: 본 메시와 별도로 머티리얼 하나가 쓰는 삼각형만 모은 `models/<이름>/sections/<머티리얼>.mesh`(스킨드면 같은 스켈레톤 · 본 영향)와
  `models/<이름>/materials/<머티리얼>.material`(엔진 `engine/materials/toon.material` 이 틀 — 프로퍼티 · 퍼뮤테이션은 그 파일 하나가 정본)을 씁니다.
  엔진 메시는 머티리얼 하나라(구간 · 머티리얼 칸이 없다) 오브젝트마다 구간 하나를 `SkeletalMeshComponent` 로 겁니다(`game/empty/maps/toonshowcase.scene.xml`).
  VRM 은 정점 색에 baseColorFactor 를 굽지 않습니다 — 머티리얼이 듭니다.
- **텍스처**: 구간 머티리얼이 쓰는 내장 이미지를 바이트 그대로 `<x>/textures_raw/<이름>/<이미지>.png` 로 꺼내고(바이트가 같으면 건드리지 않는다), 머티리얼은
  텍스처 임포트가 만들 `<x>/textures/<이름>/<이미지>.dds` 를 가리킵니다. 그래서 VRM 은 `App --import-models` 뒤에 `App --import-textures` 를 돌립니다
  (에디터는 핫 리로드가 둘을 잇는다). 원본 PNG 는 텍스처 임포트의 스탬프가 지킵니다.
- **모르는 키는 임포트 오류**입니다(이 저장소의 규칙 — 철자가 틀린 키가 조용히 기본값이 되지 않게). 아는데 옮기지 않는 키는 값이 효과를 낼 때만 경고 한 줄로
  모아 알립니다(`ModelImportResult::_listIgnoredMaterialKey`).

| VRM 0.x (`materialProperties[]`) | 엔진 툰 머티리얼 | 옮기는 법 |
|---|---|---|
| `shader` | — | `VRM/MToon` 만 MToon. `VRM_USE_GLTFSHADER` · `VRM/UnlitTexture` · `VRM/UnlitCutout` · `VRM/UnlitTransparent` · `VRM/UnlitTransparentZWrite` 는 glTF 머티리얼을 평면 툰(그림자색 = 기본색)으로. 그 밖은 오류 |
| `_Color` · `_ShadeColor` · `_EmissionColor` · `_RimColor` · `_OutlineColor` | `baseColor` · `shadeColor` · `emissiveColor` · `rimColor` · `outlineColor` | 감마 → 선형 |
| `_MainTex` · `_ShadeTexture` · `_EmissionMap` · `_SphereAdd` | `baseColorMap` · `shadeMap` · `emissiveMap` · `matcapMap` | glTF 텍스처 번호 → DDS 경로 |
| `_ShadeShift` · `_ShadeToony` | `shadingShift` · `shadingToony` | 0.x 구간 [shift, lerp(1, shift, toony)] 와 같은 경계: shift₁ = −(아래 + 위)/2, toony₁ = 1 − (위 − 아래)/2 |
| `_ReceiveShadowRate` | `shadowReceive` | 그대로 |
| `_RimFresnelPower` · `_RimLift` · `_RimLightingMix` | `rimFresnelPower` · `rimLift` · `rimLightingMix` | 그대로 |
| `_OutlineWidthMode`(0 · 1 · 2) · `_OutlineWidth` | `Outline` 스위치 · `outlineWidthMode` · `outlineWidth` | 월드: cm → m(× 0.01), 화면: NDC 1 % → 화면 높이 비율(× 0.005) |
| `_OutlineScaledMaxDistance` | `outlineMaxDistance` | 화면 모드만 |
| `_OutlineColorMode` · `_OutlineLightingMix` | `outlineLightingMix` | 고정 색(0)은 0 |
| `_BlendMode`(0 불투명 · 1 컷아웃 · 2 · 3 반투명) · `_Cutoff` | `AlphaCutoff` 스위치 · `alphaCutoff` · blendMode Transparent + `MATERIAL_BLEND_TRANSLUCENT` | 3(깊이 쓰는 반투명)은 반투명 |
| `_CullMode`(0 끔 · 2 후면) | `TwoSided` 스위치 | 1(앞면)은 오류 |
| 알지만 옮기지 않음 | — | `_BumpMap` · `_BumpScale`(노멀 맵 없음) · `_ShadingGradeTexture` · `_ShadingGradeRate` · `_ReceiveShadowTexture` · `_RimTexture` · `_OutlineWidthTexture` · `_UvAnimMaskTexture` · `_UvAnimScrollX/Y` · `_UvAnimRotation` · `_LightColorAttenuation` · `_IndirectLightIntensity` · `_OutlineCullMode`(늘 앞면) · 텍스처 타일링 벡터(늘 [0,0,1,1] 이어야 한다) · 블렌드 상태(`_SrcBlend` · `_DstBlend` · `_ZWrite` · `_AlphaToMask`) · `_DebugMode` · `_MToonVersion` · `keywordMap` · `tagMap` · `renderQueue` |

| VRM 1.0 (`VRMC_materials_mtoon` + glTF 머티리얼) | 엔진 툰 머티리얼 |
|---|---|
| `pbrMetallicRoughness.baseColorFactor` · `baseColorTexture` | `baseColor` · `baseColorMap` |
| `shadeColorFactor` · `shadeMultiplyTexture` | `shadeColor` · `shadeMap` |
| `shadingShiftFactor` · `shadingToonyFactor` | `shadingShift` · `shadingToony` |
| `emissiveFactor` · `emissiveTexture` · `KHR_materials_emissive_strength` | `emissiveColor` · `emissiveMap` · `emissiveStrength` |
| `matcapFactor` · `matcapTexture` | `matcapColor` · `matcapMap` |
| `parametricRimColorFactor` · `parametricRimFresnelPowerFactor` · `parametricRimLiftFactor` · `rimLightingMixFactor` | `rimColor` · `rimFresnelPower` · `rimLift` · `rimLightingMix` |
| `outlineWidthMode`(`none` · `worldCoordinates` · `screenCoordinates`) · `outlineWidthFactor` · `outlineColorFactor` · `outlineLightingMixFactor` | `Outline` 스위치 · `outlineWidthMode` · `outlineWidth` · `outlineColor` · `outlineLightingMix` |
| `alphaMode` · `alphaCutoff` · `doubleSided` | `AlphaCutoff` 스위치 · blendMode · `alphaCutoff` · `TwoSided` 스위치 |
| 알지만 옮기지 않음 | `shadingShiftTexture` · `rimMultiplyTexture` · `outlineWidthMultiplyTexture` · `uvAnimation*` · `giEqualizationFactor`(환경광이 균일) · `transparentWithZWrite` · `renderQueueOffsetNumber` · `normalTexture` · `KHR_texture_transform` · `texCoord` ≠ 0 |
| MToon 확장이 없는 머티리얼 | 평면 툰(그림자색 = 기본색) |

시험: `VrmMaterialImporterTest`(0.x · 1.0 값 대응, 모르는 키 오류, 만든 `.material` 이 툰 머티리얼로 읽힘) · `ModelImporterTest.VrmSplitsMeshByToonMaterial`.

## 높이장도 들일 때 임포트한다

지형 높이장 원본(16 비트 회색 PNG · 작은 엔디언 `.r16`)은 `heightfields_raw/` 에 두고 같은 상대 경로의 `heightfields/<이름>.heightfield` 로 임포트합니다
(`HeightfieldImporter`, 형식은 Engine `Environment/Terrain/HeightfieldData.h`). 원본 옆 `<이름>_holes.png`(128 미만 = 구멍 칸)는 곁 파일이라 원본으로 세지 않고
그 원본의 구멍 마스크가 되며 원본 해시에 섞입니다. 헤드리스: `App --import-heightfields` · `--check-heightfields`(같은 `import.stamp` 절차). 임포트 동작을 바꾸면
`HeightfieldImporterInternal::kImporterVersion` 을 올립니다. 시험: `HeightfieldImporterTest`(16 비트 PNG · `.r16` 값, 구멍, 스탬프, 저장소 원본 ↔ 에셋 대조).

## 로컬라이제이션 도구

글 수집(`App --gather-text` · `--check-text`)과 PO 교환(`--export-po` · `-import-po=<파일>`)은 소스 트리를 읽는 개발 도구라 에디터 모듈에 있습니다
(`Common/Localization/LocalizationTools`, 규칙과 파일 형식은 [Localization README](../Engine/Localization/README.md)). App 이 헤드리스로 에디터 모듈을 인스턴스 없이
올려 `runEditorLocalizationTask`(`LocalizationToolEntry.cpp`, 작업 `EditorLocalizationTask`)를 부릅니다 — 임포트와 같은 길이고 Dev 빌드에서만 됩니다.
엔진의 `Headless` 단계가 타입 공급자 모듈 뒤에 서므로 글 수집이 게임 · 키트 타입의 `Meta = "Localizable"` 프로퍼티를 봅니다.
CI 는 App 을 띄우지 않고 `EditorTest` 의 `LocalizationGatherTest.RepositoryProjectsAreUpToDate` 로 같은 확인을 합니다. 그 시험은 `Engine` 만 링크하므로 엔진 타입의
`Localizable` 프로퍼티만 봅니다 — 게임 · 키트 타입에 그 메타를 더하고 저장소 데이터가 쓰면 `App --check-text` 와 결과가 갈라집니다(지금 저장소 데이터는 엔진 타입뿐).

## UI 스레드가 놓은 GPU 자원

ImGui 텍스처 · 게임 뷰 렌더 타깃은 UI 스레드가 놓지만 그리는 것은 렌더 스레드이고, 렌더 스레드는 UI 가 다음 스냅샷을 내기 전까지 **같은 draw
스냅샷을 여러 프레임에 다시 그립니다.** 그래서 놓은 자원은 바로 지우지 않고 `IImGuiRendererBackend::getDrawReleaseQueue()` 의
`EditorDrawReleaseQueue::enqueue` 에 맡깁니다. 큐는 해제마다 "다음에 낼 스냅샷 번호" 를 찍어 두고, 렌더 스레드가 그 번호 이상의 스냅샷을
기록하는 프레임에서 `IRHIDevice::enqueueGPURelease` 로 넘깁니다 — 그 프레임의 GPU 완료 뒤에 실제로 풀립니다.
주의: UI 스레드에서 읽은 펜스 값으로 해제하면 뒤에 줄 선 프레임이 놓인 자원을 씁니다.
새 렌더 타깃은 그리기 전 패킷이 샘플링할 수 있어 만들 때 클리어 색으로 채운다(Vulkan UNDEFINED 레이아웃).

## ImGui 할당자

`ImGuiEditor::initialize` 는 컨텍스트를 만들기 **전에** `ImGui::SetAllocatorFunctions` 로 ImGui 할당을 sw 할당자(`Memory::allocate` · `free`)에
보냅니다 — ImGui 메모리가 메모리 태그(호스트가 에디터 진입점에 건 `Editor`) · 누수 검사에 잡히고, 이 모듈 안에서 잡고 풉니다. 컨텍스트도 `shutdown` 이 이 모듈 안에서 지웁니다.

## 에디터 안에서 시험하는 법

패널 · 위젯 · 도킹처럼 에디터 컨텍스트와 ImGui 프레임이 모두 서 있어야 재현되는 동작은 **에디터 자체 시험**으로 잽니다(UE Automation ·
Unity EditMode 의 자리). 시험은 자기 .cpp 에서 한 줄로 등록합니다.

```cpp
SW_EDITOR_SELF_TEST( HierarchyTag, "hierarchy.tagFilter", 600, &runHierarchyTagFilter );
```

본문은 에디터 프레임마다 한 번(패널을 그린 뒤) 불리고 `EditorSelfTestStep::Continue` 를 돌려주면 다음 프레임에 다시 불립니다 — 패널이 한 번
그려지기를 기다릴 때 씁니다. 실패는 `context.expect( 조건, "이유" )` 로 적습니다. 시험이 만든 오브젝트 · 바꾼 테마는 시험이 되돌립니다.

```powershell
./App.exe -dx12 -EnableEditor "-gv_editorSelfTest=*" -gv_editorSelfTestReport=Saved/selftest.txt -gv_profileFrames=1200
```

패턴은 `*` 와 쉼표(`hierarchy.*,theme.*`)를 받습니다. 시험마다 `EditorSelfTest|PASS|<id>` · `EditorSelfTest|FAIL|<id>|<이유>` 한 줄, 끝에
`EditorSelfTest|DONE|<통과>|<실패>` 를 로그와 보고서에 남기고 앱을 닫습니다. 같은 실행기를 **Test Runner 창**(Panel 메뉴, `test_runner`)이 `EditorSelfTestRunner::requestRun( 패턴, false )` 로
그 자리에서 돌린다(앱을 닫지 않는다, 도는 동안 창은 입력을 받지 않는다). 그 창의 Scenarios · Unit Tests 탭은 시나리오(`App -scenario … -unattended`)와 `TestBin/<이름>Test.exe --test_filter=`(작업 폴더 Bin)를
새 프로세스로 돌리고 출력을 `EditorTestOutputParser` 로 읽는다. 시나리오 `editor/testrunner`. 실행 중에는 저장된 레이아웃(`imgui.ini` · `windows.ini`)을 읽지도
쓰지도 않고 기본 가시성 · 기본 도킹 배치로 뜹니다. `AppSmokeTest.EditorSelfTestsPassInsideTheEditor`(hostgpu)가 이렇게 띄워 알려진 시험이 모두
PASS 인지 봅니다 — 시험을 더하면 그 목록에도 한 줄 더합니다.

## 씬 뷰 · 게임 뷰 · 상단 툴바

유니티의 Scene · Game 뷰와 같습니다. 이름에 `Game` 은 게임 카메라 출력에만 씁니다.

- **씬 뷰**(`SceneViewPanel`, 제목 "Scene"): 에디터 카메라(`EditorCamera::ensure`)로 그린 씬 위에 격자 · 기즈모 · 피킹 · 눈금자 · 시각화 · 디버그 선 ·
  통계 · 방향 큐브를 ImGui 로 얹는다(`EditorViewportClient`). Play · Simulate 중에도 에디터 카메라라 둘러보며 볼 수 있다.
- **게임 뷰**(`GameViewPanel`, 제목 "Game"): 활성 씬의 게임 카메라 출력만 — 화면 UI · 화면 사각형 뷰(분할 화면)까지 든 게임 화면이고 편집 보조선이 없다.
  화면 비율은 자유 · 16:9(레터박스). 게임 카메라가 없거나 꺼져 있으면 "No camera rendering". Play 중 게임 입력은 이 패널이 포커스 · 호버일 때만 게임으로 간다.
- **상단 툴바**(`EditorPlayToolbar`, 메뉴 막대 아래): 어느 뷰를 열어 두든 같은 자리의 재생 단추.
  - **Play / Simulate**: Play 는 플레이어가 조종하는 세션(게임 모듈 업데이트 · 게임 입력), Simulate 는 **월드만** 돈다 — 씬은 틱하지만
    게임 모듈 업데이트와 게임 입력이 꺼진다(언리얼 Simulate). 도는 중에 서로 바꿀 수 있다. 호스트는 `IEditor::isPlaying`
    (= `EditorPlaySession::isPlayerActive`)으로 게임 모듈을 켜고, 씬 틱은 `isPaused` 가 정한다. 미저장 씬이면 확인 모달(`toolbar.playAnyway`)이 뜬다.
  - **Step · Step N**: 한 프레임 / 칸에 적은 프레임 수만큼 진행하고 일시정지한다(`EditorPlaySession::stepFrames`).
  - **Cam**(카메라에서 시작): Play 를 에디터 카메라 위치에서 시작한다 — `Player` 태그를 단 오브젝트, 없으면 게임 카메라를 든 오브젝트의 맨 위 조상을
    순간이동한다. 월드 시작 직후와 첫 프레임 뒤 두 번 옮긴다(첫 틱에 스폰 자리로 되돌리는 게임이 있다).
  - **시간 배율**(`x1.00` 칸): `gv_timeScale`(`Engine/Utility/GameTimeScale`). 끌어서 바꾸고 오른쪽 클릭으로 1 로 되돌린다. 호스트의 프레임 시간이
    곱해 게임 업데이트 · 씬 틱 · 고정 스텝이 같이 느려지거나 빨라진다. 에디터 UI · 에디터 카메라는 자기 시간으로 돈다.
  - **Auto**(자동 플레이): 게임이 `SW_GAME_AUTOPLAY` 로 등록했으면 서고, 누르면 그 게임의 자동 플레이 전역 변수를 켜고 끈다
    (`GameAutoplay::setOn` — 전역 변수 표를 거쳐 써서 패널 · 콘솔과 같은 값이다). 자체 시험 `toolbar.autoplayButton`.
- **디버그 드로우**: 게임 코드가 `DebugDrawQueue`(엔진 서비스)에 넣은 선 · 구 · 상자 · 화살표 · 글자를 씬 뷰의 `debug_draw` 시각화(툴바 `Dbg`)가 그린다.
  지속 시간(초)과 카테고리를 받는다. 씬 뷰 툴바의 `Dbg Cat` 팝업이 카테고리를 켜고 끈다. 2D 뷰(직교 카메라가 Z 를 본다)에서는 구가 XY 원 하나다.
- **HUD**(디버그 오버레이): 게임이 `DebugOverlayState` 에 쓴 값을 게임 뷰 왼쪽 아래에 키 순서로 그린다.
- 시험: `EditorPlaySessionTest`(Simulate · Step N · 카메라에서 시작), `EditorViewTargetUtilTest`, `DebugDrawQueueTest`, `DebugOverlayStateTest`, `FixedTimestepTest.TimeScale…`,
  에디터 자체 시험 `sceneView.*` · `gameView.*` · `toolbar.*`, 에디터 시나리오 `sceneviewgameview`.

## Output Log · 설정 · 선택 · 레이아웃

- **로그 줄 → IDE**: 줄을 더블 클릭(또는 오른쪽 클릭 `Open in IDE`)하면 그 줄이 가리키는 소스 위치를 IDE 로 연다. 메시지 안의 위치
  (`경로(줄,열)` · `경로:줄:열` — 컴파일러 · 셰이더 오류)가 먼저, 없으면 로그를 쓴 자리다. 명령 틀은 환경설정 General 의
  `_ideOpenCommand`(`{file}` · `{line}`), 비우면 VS Code(`code -g`, Windows 는 `cmd /c`)다. 판정은 `Common/Commands/EditorLogCommands`.
- **카테고리 필터**: 툴바 `Tags` 팝업이 로그 카테고리(로그를 쓴 자리 `SW_LOG_CALLER`, 없으면 모듈 태그)마다 보이기를 켜고 끈다(`EditorLogTagFilter`).
- **설정 파일 핫 리로드**: `Common/Workspace/ConfigHotReload` 가 `Config/` 의 `.json` 을 감시한다(에셋과 같은 `FileWatchDispatcher`, 루트만 다름).
  호스트 설정(`ConfigManager` 가 파일에서 읽은 EngineConfig · GameConfig)은 `ConfigManager::reloadConfigFile` 이 **제자리에서** 다시 읽고
  `onConfigReloaded` 로 알린다 — App 은 프레임 시간 정책, EngineLoop 는 게임 설정 활성본 · 선호 수직 동기화(다음 스왑체인부터). 에디터 도구 시드
  (`editortooldefaults.json`)는 에디터가 다시 읽는다. 앱이 다시 쓰는 `EditorPreferences.json` 은 다시 읽지 않는다.
- **같은 종류 · 태그 모두 선택**: Hierarchy 오른쪽 클릭 `Select All With` — 그 오브젝트의 컴포넌트 종류(파생 포함) · 태그(아래 계층 포함)마다
  (`EditorSceneCommands::collectObjectsWithComponent` · `collectObjectsWithTag` · `selectObjects`).
- **이름 붙인 레이아웃**: `Panel > Layouts` — 이름을 적고 Save, 목록에서 고르면 불러오고 `x` 로 지운다. 도킹 배치(`<이름>.imgui.ini`)와 패널
  가시성(`<이름>.windows.ini`)이 `Saved/Editor/Layouts/` 에 남는다(git 무시). 불러오기는 다음 프레임 `NewFrame` 앞에서 한다
  (`EditorDockLayout::applyPendingNamedLayout`) — 프레임 안에서 ImGui 설정을 읽으면 이미 있는 창 · 도킹 노드에 적용되지 않는다.
- 시험: `EditorLogCommandsTest` · `ConfigHotReloadTest` · `EditorLayoutStoreTest` · `EditorSceneCommandsTest.CollectObjectsByComponentTypeAndTag`
  (EditorTest), `ConfigManagerTest.ReloadConfigFileUpdatesInPlaceAndNotifies`(EngineTest), 에디터 자체 시험 `console.tagFilter` ·
  `hierarchy.selectAllWith` · `layout.namedRoundTrip`.

## 개발 콘솔(Output Log 입력 줄)

Output Log 아래 입력 줄이 개발 콘솔(`Engine/Console/DevConsole`)입니다 — `help`, `gv_이름 [값]` · `get` · `set`, 개발 명령(`SW_DEV_COMMAND`),
Tab 자동완성(후보가 여럿이면 로그에 줄로 보인다), ↑↓ 기록. 답은 로그(`DevConsole`)로 남아 같은 패널에 보입니다. 에디터가 등록하는 명령은
`Common/Commands/EditorDevCommands.cpp` — `editor <커맨드 id>`(커맨드 팔레트의 id) · `play` · `simulate` · `pause` · `stop` · `step [N]` ·
`select.type <컴포넌트 타입>` · `select.tag <태그>` · `component.add <컴포넌트 타입>`(고른 오브젝트마다 — 메뉴에 숨긴 시험 타입도) · `layout.save <이름>` · `layout.load <이름>` · `debugdraw.demo [초]`(뷰포트 카메라 앞에 상자 · 구 · 화살표 · 글자와 HUD 값 하나 — 시각화가 도는지 보는 용도). 엔진 명령(`timescale` · `teleport` ·
`debugdraw.category` …)은 소유 코드 옆에 있습니다(`Source/Engine/README.md` 의 개발 명령 절). 에디터 없이 띄운 게임 창에서는 `~` 오버레이가 같은 콘솔입니다(`Source/App/README.md`).
시험: `DevConsoleTest` · `DevCommandRegistryTest` · `DevConsoleControllerTest`(EngineTest), `DevCommandShippingTest`(AppTest), 자체 시험 `console.devCommands`.

## ⚠️ 핵심 특징 및 규칙
- **Dev 모드 전용**: 이 폴더의 코드는 개발(Dev) 모드에서만 `SHARED DLL`로 빌드되고 동작합니다. 배포(Shipping) 빌드를 할 때는 **코드가 통째로 날아갑니다.**
- **게임 로직 분리**: **절대 게임(Game) 로직이 이 폴더의 코드에 의존해서는 안 됩니다.** 게임 코드에서 `#include "Editor/"` 등을 호출하면 Shipping 빌드가 100% 터집니다.
에디터에서만 써야 할 기능이라면 매크로를 신중하게 사용하세요.


## 저장 · 임포트 직후 에셋 검증

언리얼 Data Validation 의 "저장할 때 검증" 자리다. 규칙은 `Config/Editor/AssetValidationRules.json`(이름 · 텍스처 형식 · 밉 · 메시 예산 · 없는 참조 ·
고아 파일 · 머티리얼 퍼뮤테이션 · 컴포넌트 타입 · 엔티티 id · 프리팹 guid · 팩별 카탈로그 참조)이고 검사는 파이썬 한 곳(`Scripts/common/AssetValidation.py`)이다 —
커밋 훅 게이트(`CheckAssetRules`, 오류만), `py -3 -m Scripts validate-assets`(경고까지), 에디터가 같은 코드를 부른다.

- **언제**: 핫 리로드 감시가 받은 `Resource/` 의 쓰기(에디터 저장 · 임포트 산출물 · 바깥 도구)마다, 그리고 씬을 저장한 뒤(`EditorAssetCommands::saveActiveScene` —
  씬은 감시 확장자가 아니다). 경로를 모았다가 한 번에 `Scripts/qa/ValidateAssets.py --files …` 를 띄운다(`EditorAssetValidation::update`, 에디터 프레임마다).
- **결과**: 오류는 Error 로그, 경고는 Warning 로그(`[AssetValidation]`). 저장을 막지 않는다 — 고치는 것은 사람이다.
- **꺼지는 경우**: 저장소 밖(스크립트가 없다) · 파이썬을 못 띄운다 — 한 번 알리고 그 세션 동안 꺼진다.
- 시험: `EditorAssetValidationTest`(결과 줄 나누기 · 명령), 규칙 자체는 `PythonTest_TestAssetValidation`.

## 버전 관리 잠금(체크아웃)

언리얼 에디터의 Source Control(Perforce 체크아웃 · 잠금)을 작게 한 것이다. 공급자는 명령을 만들고 출력을 읽기만 하고(`ISourceControlProvider`),
띄우기 · 기다리기는 `EditorSourceControl` 이 전용 스레드(`EditorExternalToolJob`)로 한다 — UI 가 git 을 기다리지 않는다.

- **공급자**: 에디터가 뜰 때 `git lfs version` 을 묻는다. 되면 git LFS(`git lfs locks --json` · `lock` · `unlock`), 안 되면 아무것도 안 하는 공급자.
- **보이는 것**: 콘텐츠 브라우저의 자물쇠 — 주황은 잠김(도구 설명에 소유자), 회색은 읽기 전용 파일(git LFS `lockable` 파일은 잠그기 전까지 읽기 전용이다).
  읽기 전용 여부는 폴더 목록을 만드는 워커가 잰다(`EditorFolderListingEntry::_bReadOnly`, `FileUtil::isReadOnlyFile`).
- **하는 것**: 오른쪽 클릭 메뉴의 Check Out (Lock) · Release Lock · Refresh Source Control. **스스로 잠그지 않는다** — 저장 · 열기가 자동으로 체크아웃하지 않는다
  (잠금 서버 없는 저장소에서 저장마다 실패가 쌓이는 것을 피했다). 읽기 전용 씬 파일에는 저장하지 않고 이유를 알린다.
- 시험: `EditorSourceControlTest`(잠금 목록 읽기 · 명령 · 경로 · 상태 글), `FileTest.ReadOnlyFileIsReported`.

## 함정 · 계약

- **씬 뷰 · 게임 뷰는 보이는 패널만 그린다.** 패널이 `drawContent` 에서 `EditorContext::markViewDrawn` 을 부른 뷰만 셸이 호스트에 알린다
  (`EditorViewTargetUtil::shouldRequestSceneView` · `shouldRequestGameView`) — 같은 영역의 다른 탭 · 접힘 · 닫힘이면 그 RT 요청이 0 이다.
  둘 다 안 보이면 씬 뷰 RT 를 알린다(알릴 RT 가 없으면 호스트가 백버퍼에 그려 에디터 UI 밑에 깐다). 탭 뒤의 패널은 이름표도 남기지 않으므로,
  자체 시험 · 시나리오는 먼저 그 탭을 앞으로 가져온다(`ImGui::SetWindowFocus` · `panel.focus <id>`).
  씬 뷰가 보이는지는 `Editor.SceneViewDrawn` 으로 본다 — `Editor.SceneViewRequested` 는 두 뷰가 다 가려져도 1 이라 가려진 씬 뷰를 놓친다.
- **기본 배치의 앞 탭은 붙인 순서로 정해지지 않는다.** ImGui 는 새로 붙은 탭 가운데 마지막 것을 고르고 처음 나타난 창에 포커스를 준다. 열린 채 시작하는
  도구 패널(Prefab Editor)이 가운데 영역에 붙으면 그 탭이 앞에 서서 씬 뷰가 그려지지 않는다. 그래서 `EditorDockLayout::updateDefaultTabSelection` 이
  패널을 다 그린 뒤 Scene 탭이 골라질 때까지 포커스를 준다(첫 실행 · `layout.reset` · Reset Default Layout).
  저장된 레이아웃(`Saved/Editor/imgui.ini` · `windows.ini`)이 있으면 이 규칙은 돌지 않는다. `AppScenarioTest` 는 실행마다 빈 상태 폴더(`-gv_editorStateDir`)로
  시작하므로 늘 기본 배치를 보고(`defaultlayout`), 저장된 배치를 읽는 분기는 묶음 시나리오 `savedstate.1` · `savedstate.2` 가 본다.
- **주 출력은 게임 뷰다.** 게임 뷰가 보이면 그것이 엔진의 주 출력(게임 카메라 · 화면 UI · 화면 사각형 뷰 · 스크린샷 캡처)이고 씬 뷰는 호스트 타깃 추가 뷰다.
  씬 뷰만 보이면 씬 뷰가 주 출력이 된다(화면 UI · 화면 사각형 뷰를 빼고). 그래서 `<Screenshot>` · `-gv_screenshot` 은 게임 뷰가 보일 때 게임 뷰를,
  아니면 씬 뷰를 찍고, 격자 · 기즈모는 ImGui 오버레이라 어느 쪽 그림에도 없다 — 격자는 창 캡처나 `Editor.Grid*` 탐침으로 본다.
- **두 뷰가 함께 보일 때 그림자 볼륨은 주 출력(게임 카메라)의 것이다.** 그림자 행렬이 프레임 공통이라 씬 뷰가 게임 카메라에서 먼 곳을 보면 그림자가 빠진다.
  보기 모드는 뷰마다다 — 씬 뷰 툴바(`FrameRenderer::setSceneViewMode`, 이름표 `viewport.viewMode` · `viewport.viewMode.<모드>`)는 씬 뷰에만 걸리고 게임 뷰는 Lit 이다(시나리오 `editor/viewmodes`).
- **뷰 RT 는 1 픽셀 떨림을 무시한다**(`EditorViewTargetUtil::needsResize`). 그래서 같은 영역의 두 탭이 앞 배치의 RT 를 1 픽셀 다르게 들고 있을 수 있다 —
  두 뷰 스크린샷을 `differentFrom` 으로 비교하는 시나리오는 먼저 창 크기를 크게 바꿔 두 RT 를 새로 짓게 한다.
- **기본 배치의 창 이름을 바꾸면 도크스페이스 id 의 판 번호(`EditorMainDockSpace_v7`)를 올린다** — 저장된 `imgui.ini` 의 옛 도크 트리가 새 창을 모른 채
  남으면 새 창이 떠 있는 창으로 뜬다. 이름 붙인 레이아웃에 옛 창(`[Window][Game View]`)만 있으면 기본 배치로 다시 짓는다.

- **뷰포트 격자 선의 모양은 월드 인덱스로만 정한다(`EditorGridUtil`).** 굵은 선은 월드 좌표가 `5 × 간격` 의 배수인 선이다. 카메라 기준 인덱스로 고르면 카메라가 1 m 지날 때마다 굵은 선이 다른 월드 선으로 미끄러진다. 간격은 카메라 높이(직교 뷰는 보이는 반 높이)로 1 · 10 · 100 m 를 고르고 단계 뒤 절반에서 가는 선을 흐리며 굵은 선 판정도 섞는다 — 경계 양쪽 모양이 같다(`EditorGridUtilTest`). 반지름은 높이에 연속이고 가장자리는 조각마다 알파로 흐린다. 시나리오 `editor/viewportgrid` 가 카메라를 날려 `Editor.GridMisplacedMajorLines` 0 을 본다.
- **씬 뷰 카메라 비행은 엔진 프레임의 실제 경과(`GameTimeScale::getUnscaledDeltaTime`)로 움직인다** — ImGui `DeltaTime` 은 벽시계라, 그것을 쓰면 고정 프레임 시간 시나리오의 이동 거리가 실행마다 달라진다.
- **에디터 실행의 스크린샷은 게임 뷰 그림이다.** `-EnableEditor` 에서 `-gv_screenshot` 과 시나리오 `<Screenshot>` 은 Present 캡처가 게임 뷰 렌더 타깃을 복사한 것이라 에디터 UI 는 들어가지 않는다.
  스크린샷 단추(게임 뷰 `gameView.screenshot` · 씬 뷰 `sceneView.screenshot` · F9 `viewport.screenshot`)는 `EditorScreenshotCommands` 가 `RenderThread::requestScreenshot` 에 넘긴다 —
  게임 뷰는 Present 캡처(주 출력), 씬 뷰는 씬 뷰 RT 를 직접 읽는다. 단축키는 패널보다 먼저 처리되므로 "포커스가 있는 뷰" 는 패널이 그릴 때 적어 둔 마지막 포커스(`noteFocusedView`)다. 시나리오 `editor/screenshotbutton`.
- **버그 리포트는 엔진 명령 하나다.** Edit 메뉴 Report Bug (BugIt) · Ctrl+Shift+F12(`help.reportBug`)와 콘솔 `bugit <메모>` 가 같은 `BugItReport::capture` 를 부른다.
  `bugitgo` 가 에디터 카메라를 옮기면 씬 뷰가 그 자리를 이어받는다 — `EditorViewportClient` 는 지난 프레임에 건 자리와 다른 에디터 카메라 자리를 바깥이 옮긴 것으로 받아들인다(`teleport` 도 같다). 시나리오 `editor/bugit`.
- **RenderDoc 단추(상단 툴바 `toolbar.renderDoc` · Ctrl+F12 `viewport.renderDocCapture` · 프로파일러 패널)는 RenderDoc 이 붙었을 때만 산다**(`RenderDocCapture::isAvailable` — `-renderdoc` 또는 RenderDoc 에서 실행).
  회색 단추의 이유는 `ImGuiHoveredFlags_AllowWhenDisabled` 로 띄운다 — `EditorWidgets::drawTooltip` 은 회색 위젯 위에서 뜨지 않는다. 시나리오 `editor/renderdocbutton` 은 붙지 않은 쪽만 본다.
- **창 제목(`<게임> — <씬>[*] — SW Editor`)은 셸이 프레임마다 다시 건다**(`EditorWindowTitleUtil::makeTitle` → `IWindow::setTitle`, 같은 제목이면 건너뛴다). 게임 이름은 `GameConfig::_windowTitle` 하나에서 오고, 에디터가 내려가면 그 값으로 되돌린다.
  `setTitle` 은 빈 제목을 받지 않는다(`recreate` 가 빈 제목을 거절한다). 탐침 `Editor.WindowTitleDirty`, 시나리오 `editor/windowtitle`.
- **에디터 ImGui 의 픽셀 리터럴은 UI 단위이며 `EditorThemeUtil::getDpiScale()` 을 곱한다**(창 · 열 · 항목 폭, 단추 · 차트 크기). ImGui 스타일 값은 테마 적용이 이미 곱하므로 거듭 곱하지 않는다.
- **위젯 크기에 픽셀 상수를 쓰지 않는다.** `GetFrameHeight` 와 글자 폭에서 잰다. 24 px 고정 단추가 150 % 배율에서 잘렸다(자체 시험 `hierarchy.visibilityToggleFits`).
  이름표 줄 바꿈은 `EditorWidgets::drawClampedLabel` 을 쓴다(공백, `_`, `-`, `.` 뒤에서 먼저 바꾸고 넘치면 말줄임과 툴팁). ImGui TextWrap 은 공백만 본다.
- **에디터 동작을 바꾸면 에디터 시나리오로 확인한다.** 시나리오 자리와 단계는 [Automation README](../Engine/Automation/README.md) "에디터 시나리오" 절이다.
- **에디터 아이콘은 `Scripts/common/EditorIconFont.py` 의 `kListIcon` 이 원본이다** — 아이콘을 더하면 `GenerateEditorIcons.py` 로 폰트 · 헤더를 다시 만들고 함께
  커밋한다(`CheckEditorIcons`). 구멍 윤곽(cut)은 채움 하나 안에만 둔다 — 밖으로 나가거나 구멍끼리 겹치면 그 부분이 반대로 칠해진다. 코드에서는
  `editoricon::k…` 상수만 쓴다(목록 순서가 코드포인트다). 아이콘 세트도 CC0 급 · 이 저장소 것만 — Font Awesome 같은 CC BY · OFL 세트는 넣지 않는다.
  `EditorFontSetup` 이 `editor/fonts/sweditoricons.ttf` 를 본문 폰트에 합친다(못 찾으면 오류 로그 한 줄, 화면은 빈 상자 — 자체 시험 `font.iconGlyphs`).
  상수는 `const utf8*` 라 문자열 리터럴과 이어 붙일 수 없다 — 라벨 앞에는 `EditorThemeUtil::makeIconLabel( editoricon::kSave, "Save" )`.
  vcpkg 가 함께 설치하는 `IconsFontAwesome6.h` 의 `ICON_FA_*` 는 쓰지 않는다 — 그 글리프는 합친 폰트에 없어 화면에 `?` 상자로 나온다(Hierarchy 의 보이기 토글이 그랬다).
- **모델 임포트의 옆 폴더(`models/<모델>/`)는 임포트마다 통째로 지워진다**(`ModelImporter::importModel`) — 손으로 쓴 캐릭터 데이터(소켓 · 알림 표 · 물리 에셋 ·
  몸 영역)는 `game/<게임>/characters/<캐릭터>/` 처럼 임포트 산출물 밖에 둔다. 클립 알림은 원본 옆 `<모델>.clips.json` 에 적고 `App --import-models`.
- **Debug App 의 `--import-textures` 는 BC7 1024² 한 장에 20 분을 넘긴다**(CPU 압축기가 최적화 없이 돈다) — 색 칸 아틀라스(KayKit)는 BC1 규칙
  (`TextureImportConfig.json` 의 `Character_Atlases`)이라 몇 초다. 큰 BC7 은 Release App 으로 굽는다.
- **텍스처는 들일 때 임포트한다(사용자 결정 2026-10-03 — UE 임포트 방식).** 런타임은 DDS 만 읽고, 원본은 `<domain>/textures_raw/` 에만 둔다(`CheckTextureFolders`).
  원본 ↔ DDS 대조는 원본 폴더마다 `import.stamp`(원본 바이트 + 해석한 규칙 + 임포터 버전의 해시, DDS 해시) — `App --import-textures` · `--check-textures`(헤드리스로
  에디터 모듈을 올린다, Shipping 은 이유를 남기고 실패), CI 대조는 `TextureImportStampTest`. 함정: 임포트 동작을 바꾸면 `TextureImporterInternal::kImporterVersion` 을 올려야
  모든 스탬프가 어긋남이 된다. Debug 의 DirectXTex BC7 은 블록당 수백 ms 라 큰 원본은 Release App 으로 임포트한다(Debug 로 돌리면 시작할 때 경고 한 줄). 밉 · 변환은 `TEX_FILTER_FORCE_NON_WIC`(결정적).
- **BC 압축은 높이가 4 의 배수인 가로 띠로 나눠 작업 시스템이 병렬로 한다**(`BandCompressJobInternal`, 띠 64 줄, 모든 밉의 띠를 한 목록으로). BC 블록이 4×4 독립이라 결과는
  통째 압축과 같은 바이트다 — `TEX_COMPRESS_DEFAULT` 일 때만이다(디더링 · 오차 확산 플래그를 켜면 블록 사이가 이어져 깨진다). DirectXTex 의 `TEX_COMPRESS_PARALLEL` 은
  OpenMP 로만 돌고 vcpkg 빌드는 OpenMP 없이 지어져 효과가 없다. 헤드리스 `--import-textures` 는 에디터 인스턴스가 없어 `importEditorAssets` 가 호스트의 서비스 표를 받아
  그 호출 동안 묶는다 — 표가 없으면 한 스레드로 돌고 로그의 `on 0 workers` 가 그것이다. 파일 단위 병렬은 하지 않는다: 한 장의 띠가 이미 워커를 다 쓰고, 2048² 여러 장을
  동시에 들면 장마다 수십 MB 밉 체인이 겹친다(측정은 [검증과 측정](../../docs/08_Verification.md) 2 절).
- **모델도 같은 스탬프 절차다** — `models_raw/` 의 glTF → `models/*.mesh`(`App --import-models` · `--check-models`, `AssetImportStampUtil` + 종류마다
  `IRawAssetImporter`). glTF → 엔진은 X 반전 + 삼각형 감김 뒤집기(노드 행렬식 < 0 이면 한 번 더). `.mesh` 는 지금 형식만 읽는다 — `RHIVertex` 를 바꾸면
  `MeshAssetFormat::kVersion` 을 올리고 다시 임포트. 메시 캐시는 약한 참조라 쓰는 쪽이 없으면 리로드할 것도 없다(다음 `acquire` 가 새로 읽는다).
  원본 glb 는 내려받은 그대로 둔다 — 비표준 씬 뿌리는 임포터가 받고, 배치 오프셋은 `ModelImportConfig.json` 규칙으로 지운다. 경계 상자 중심
  (`recenter: xz`)은 모양이 치우친 모델을 옮기므로 원점이 정해진 키트에는 `translation` 이 맞다.
- **머티리얼 캐시는 잡을 때 `.meta` 를 지어 붙인다(`AssetDatabase::ensureMeta`)** — 임포트 결과 옆 폴더(`models/<이름>/`)에 머티리얼을 쓰면 첫 실행이 실행마다 다른 GUID 의 `.meta` 를 만들어 스탬프가 "손으로 바꿨다" 가 된다. 임포터가 경로에서 정해지는 GUID 로 `.meta` 를 미리 쓴다(`ModelImporterInternal::makeImportedGuid`).
- **`.meta`(GUID)는 그 에셋을 쓰는 시스템이 만듭니다**(머티리얼 캐시 · 프리팹 · 씬 저장 · 임포트). 목록을 보기만 하는 화면(콘텐츠 브라우저)이 `.meta` 를 쓰면 폴더를 한 번 연 것만으로 추적되지 않는 파일이 수십 개 생깁니다. 시험 `contentBrowser.browsingWritesNoMeta`.
- **콘텐츠 브라우저의 게임 루트는 활성 팩(`GameConfig::_packRoot`) 하나가 기본이다.** 툴바 All packs 를 켜면 `game/` 의 팩마다 루트 하나가 붙고, 처음 값은 환경설정 Content Browser 의
  `_bShowAllPacksByDefault` 다. 참조 찾기(오른쪽 클릭 Find References · Show Dependencies, 삭제 확인의 참조 수)는 `EditorReferenceIndex` 다. 텍스트 에셋(xml, json, material, hlsl)의 글 가운데
  실제로 있는 파일의 리소스 id 만 세고, `Resource/` 변경 번호가 바뀌면 워커가 통째로 다시 훑는다(Debug 1.1 초, 377 파일). 바이너리 안의 경로와 확장자를 뗀 이름은 참조로 잡지 않는다.
  콘솔 `content.open <리소스 폴더>` 가 그 폴더를 연다. 시험 `EditorReferenceIndexTest`, 자체 시험 `contentBrowser.showsActivePackOnly`, 시나리오 `editor/contentbrowser`.
- **패키징 창(Packaging, `packaging`)은 `Scripts/dev/MakePackage.py` 를 새 프로세스로 띄울 뿐이다** — 사람과 CI 가 같은 진입점을 쓴다. 진행은 그 스크립트의 `[package] step k/n` ·
  `done <폴더> <바이트>` · `FAILED <단계> <이유>` 줄을 `PackagingProgressParser`(ImGui 없음)가 읽는다. `EditorExternalToolJob` 은 끝난 뒤 줄을 한꺼번에 주므로 도는 동안의 막대는 단계가 아니라 움직이는 표시다.
  Skip build 는 이 에디터의 Bin 을 스테이징한다(그 타깃의 Shipping 빌드가 없어도 흐름을 본다). 스테이징은 그 타깃의 실행 파일, 맨 위 DLL, `Packs/`, 서드파티 고지와 서버의 `Config/Server/<게임>.json` 만 베낀다.
  시험 `PackagingProgressParserTest`, `PythonTest_TestMakePackage`, 시나리오 `editor/packaging`.
- **맵 검사(Map Check 창, `map_check`)는 `ValidationIssueLog` 를 그대로 보인다** — 로드 · 저장 · 인스펙터 편집 때 모인 검증 결과이고, Check Map 단추는 활성 씬 전부를 다시 검증한다.
  창과 상태줄(메뉴 막대 오른쪽의 경고 수 `statusBar.mapCheck`)은 `ValidationIssueLog::getRevision` 이 바뀔 때만 다시 센다. 활성 씬이 바뀐 첫 프레임에 오류가 있으면 토스트를 한 번 띄운다.
  줄 클릭은 그 오브젝트를 고르고 더블클릭은 씬 뷰를 그리로 옮긴다. 지운 오브젝트의 결과는 남아 있을 수 있어 그 줄은 고를 것이 없다(도구 설명).
  판단은 `MapCheckRows`(ImGui 없음, `MapCheckRowsTest`), 자체 시험 `mapCheck.selectsIssueObject`, 시나리오 `editor/mapcheck`(시험 씬 `engine/automation/editor/mapcheck/` — 반경 0 점광).
  일부러 검증 결과를 내는 시험 데이터는 `ResourceDataSchemaTest` 의 `kArrValidationFixture` 에 적는다(그 파일만 검증 경고를 받는다).
- **콘텐츠 브라우저의 에셋 관리는 `EditorAssetFileCommands` 다**(Add > New Folder · Material · Scene · Prefab, 우클릭 · F2 Rename, Ctrl+D Duplicate, Del, 폴더 타일 · 트리로 끌어 놓기).
  이름 바꾸기와 옮기기는 역색인이 아는 텍스트 에셋의 글을 새 리소스 id 로 바로 고치고 짝 `.meta` 를 GUID 째 옮긴다(이름 칸은 확장자를 뺀 줄기다). 폴더 이름 바꾸기는 없다.
  지우기는 OS 휴지통이다(Windows `SHFileOperationW` + `FOF_ALLOWUNDO`, 다른 플랫폼은 지운다). 검색어가 있으면 목록이 하위 폴더의 파일까지다.
  자동화 시나리오(`*.scenario.xml`)는 참조자로 세지 않는다 — 세면 이름 바꾸기가 돌고 있는 시나리오 파일까지 고쳐 쓴다. 옮기기 전에 시작한 역색인 훑기는 다시 요청해 버린다(안 그러면 낡은 결과가 고친 색인을 덮는다).
  시험 `EditorAssetFileCommandsTest`, 시나리오 `editor/assetmanage`(시험 폴더 `engine/automation/editor/assetmanage/` 를 바꿨다가 되돌린다).
- **텍스처 썸네일은 엔진 텍스처 캐시에서 빌린 실제 텍스처다**(`EditorThumbnailCache` — `TextureCache::acquire` → `registerTexture`, 64 개 LRU). DDS 읽기와 GPU 업로드가 UI 스레드에서 돌므로
  프레임마다 하나만 읽는다. 놓을 때는 ImGui 등록을 먼저 풀고(`unregisterTexture` 는 그린 스냅샷이 끝난 뒤 디스크립터를 놓는다) 텍스처 참조를 놓는다. 패널 `shutdown` 이 렌더 백엔드보다 먼저 비운다.
  이미지 썸네일은 종류 동작의 `hasImagePreview` 로 정하고, 그림 썸네일이 없는 종류는 종류 아이콘을 가운데에 그린다(`EditorThemeUtil::drawCenteredGlyph`). 시나리오 `editor/contentthumbnails`.
- **콘텐츠 브라우저는 디스크 목록을 들고 있으므로 `AssetHotReload::getContentChangeSerial` 이 바뀌면 다시 읽습니다** — 탐색기 · git 의 변경도 이 번호가 셉니다. 에디터 안의 삭제처럼 결과를 바로 아는 경로는 번호를 기다리지 않고 그 자리에서 다시 읽기로 합니다(감시는 한두 프레임 늦다). 시험 `contentBrowser.deleteRefreshesTheList`.
- **인스펙터 위젯 · CallInEditor 인자는 `ReflectBuiltins.xxx` 를 펼친 표 하나**(`InspectorBuiltinValue.h`) — 내장 타입을 더하면 `InspectorWidgetFor<T>` 특수화가
  없으면 컴파일이 선다. .xxx 의 문자열 줄은 `std::string`, 프로퍼티는 `sw::string`(`InspectorBuiltinCppType` 이 메운다).
- **오른쪽 클릭 메뉴의 확장 지점은 `EditorCommandRegistry` / `SW_EDITOR_*` 하나다** — 등록이 하나도 없던 `EditorActionMenuManager` 는 지웠다.
  표 밖의 파일 · 확장 모듈은 커맨드를 `SW_EDITOR_COMMAND`(표와 같은 값) 한 줄로 더한다. 레지스트리는 표와 등록 줄을 합치고(`EditorCommandTableUtil::appendRegistrations`),
  등록 세대가 바뀌면 다시 만든다. 확장 DLL 의 ImGui 는 CMake 가 만든 결속기(`cmake/Engine/EditorExtensionUIBinder.cpp.in`)가 에디터 컨텍스트에 건다 —
  vcpkg imgui 는 정적이라 DLL 마다 `GImGui` · 할당자 사본이 있다(`EditorUIContext::publish`). ImGuizmo 를 쓰는 확장은 그리기 전에
  `ImGuizmo::SetImGuiContext( ImGui::GetCurrentContext() )` 를 부르고, ImDrawList 콜백(`AddCallback`)은 쓰지 않는다(렌더 스레드가 늦게 부른다). 시나리오 `editor/commandpalette`.
- **Undo 의 오브젝트 편집은 엔진 데이터 명령(`ObjectUndoUtil` — 오브젝트 id · 이름 · 스냅샷)으로 기록한다** — 리로드를 넘어야 할 기록은 Engine 코드로 만든다.
  모듈 람다 명령은 에디터 리로드 때 `CommandStack::releaseCodeWithin` 이 뗀다(묶음은 안쪽 하나라도 걸리면 통째로). 대상 조회는 id, 같은 프레임에 지우고 되살린
  오브젝트는 지연 파괴 때문에 새 id 를 받으므로 이름으로 다시 찾는다. 선택 · dirty 는 `ObjectEditListener` 로.
- **에디터 동작 검증은 에디터 안 자체 시험** — `SW_EDITOR_SELF_TEST` 로 등록하고 `AppSmokeTest.EditorSelfTestsPassInsideTheEditor` 의 기대 목록에 한 줄 더한다
  (`-gv_editorSelfTest=<패턴>`, 실행 중에는 사용자 `imgui.ini` 를 읽지도 쓰지도 않는다). 워크스페이스는 오브젝트 GUID · 프리팹 경로 사본을 들지 않는다(id · 씬이 정본).
  입력은 `EditorSelfTestInput`(플랫폼 newFrame 뒤 · NewFrame 앞에 넣는다 — 실제 커서보다 뒤라 이긴다), 누를 위젯은 그린 직후 `EditorSelfTestMarks::note( "키" )`.
  클릭은 누르기 · 떼기를 단계 둘로, 단계마다 이름표 위로 다시 옮긴 뒤(ImGui 는 한 프레임의 누름 · 뗌을 흘려 처리하고 플랫폼이 실제 커서를 다시 넣는다).
  떠 있는 창(자기 플랫폼 창)은 플랫폼이 실제 커서로 "커서 아래 뷰포트" 를 넣어 호버가 그리로 간다 — 이름표가 든 뷰포트를 마우스 위치와 함께 넣는다
  (`AddMouseViewportEvent`, `moveMouseToMark` 가 한다). 시험이 그리는 창은 주 뷰포트 안에 둔다(`SetNextWindowViewport`).
  설정 파일을 다시 쓰는 경로(테마 저장)를 지나는 시험은 파일 바이트를 떠 두었다 되돌린다(`input.classicDarkSwatch`).
  입력 시험이 이 PC 에서만 지면 모니터 배율부터 본다 — 시험은 100 % 와 150 % 에서 다른 스타일 크기를 본다.
- **도크 칸은 창 크기를 비율로 따른다** — ImGui 는 중앙 노드 옆 칸에 마지막 픽셀 크기(`SizeRef`)를 그대로 줘서, 창을 줄이면 옆 패널은 그대로이고 게임 뷰가 32 px 로 눌렸다. `EditorDockLayout::scaleDockSizeToViewport` 가 `DockSpaceOverViewport` 앞에서 `SizeRef` 를 같은 비율로 맞춘다(기준은 마지막 실제 크기 · 처음엔 저장된 루트 크기, 최소화 0×0 은 건너뛴다). 패널 밖으로 넘칠 수 있는 떠 있는 바는 부르는 패널의 뷰포트에 묶는다(`beginFloatingBar` — 묶지 않으면 멀티 뷰포트가 OS 창으로 떼어 낸다). 에디터 창 최소 크기 960×540(`IWindow::setMinimumClientSize`). 시험 `dock.followsWindowSize`.
- **테마 적용은 스타일 크기를 ImGui 기본(96 DPI)에서 다시 시작한다**(`EditorThemeInternal::resetSizesToDefault`) — `ScaleAllSizes` 는 테마가 적지 않는
  크기(도킹 구분선 · 테두리 호버 여백 · 창 최소 크기)까지 곱하므로, 안 그러면 1 이 아닌 배율에서 적용마다 거듭 곱해져 구분선 호버 여백(84 px)이 옆 패널의 클릭을 가로챈다. 시험 `theme.reapplyKeepsSizes`.
- **DebugDrawQueue 는 `endFrame` 에 비워진다** — 에디터 UI 보다 먼저 채운 것(게임 업데이트)만 보인다(`debug_draw` 시각화). 틱에서 채우는 생산자가 생기면
  이중 버퍼로. `ActionRoom::drawDebug` 를 부르는 곳은 아직 없다. 메뉴 경로는 `EditorCommandRegistry::validate` 가 "그려지지 않는 경로" 를 잡는다.
- **패널 · 팝업 · 인스펙터 · 시각화는 자기 .cpp 의 `SW_EDITOR_PANEL` · `SW_EDITOR_POPUP` · `SW_EDITOR_INSPECTOR` · `SW_EDITOR_VISUALIZER` 한 줄로 등록한다**
  (`EditorRegistry<T>`, (order, id) 정렬, 같은 id 거절). 메뉴 배치는 커맨드 표 줄의 `_menuPath` · `_menuOrder`(백의 자리가 바뀌면 구분선). 매니저 · 메뉴바에
  손 목록을 다시 만들지 말 것. 등록 목록은 EditorModule 에 하나다(`getEditorRegistrationList`) — 확장 모듈의 등록도 같은 목록에 오른다.
  매니저(패널 · 팝업 · 인스펙터)는 등록 세대를 따라가고(`syncWithRegistry`, 에디터 프레임 앞), 확장 모듈이 언로드되기 전에 `EditorModuleUnloadListener` 가
  그 이미지의 등록 줄로 만든 인스턴스를 지운다(열려 있던 패널은 id 로 기억해 다시 로드되면 연다). 시각화 켬/끔은 id 로 보관한다(`EditorVisualizerToggles`) —
  등록 순서의 비트 위치로 보관하면 줄이 끼어들 때 엉뚱한 시각화가 켜진다. 시나리오 `editor/visualizertoggle`.
- **에셋 종류 하나 = `EditorAssetType` 한 값 + `EditorAssetType.cpp` 의 `kArrAssetMatch`(판정 · 핫 리로드 칸) · `kArrKindInfo`(이름 · 라벨 · 패널 · 아이콘 · 색 ·
  임포트) 각 한 줄 + 필요하면 `Source/Editor/AssetActions/<Kind>AssetTypeActions.cpp`(썸네일 · 열기 · 드롭, 정적 등록).** 종류별 if-체인을 다시 만들지 말 것 —
  칸이 빠지면 static_assert 가 막는다. 도구 문서 IO 는 `loadToolDocument` / `saveToolDocument<TAsset>` + `ToolDocumentDesc` 하나.
- **에디터는 `-EnableEditor` 로 켜야 뜬다.** 에디터 스모크에는 `-gv_profileFrames` 를 꼭 붙인다(`-gv_editorPanelDump` 는 스스로 끝나지 않는다). 창 수가 모자라면 코드보다 로컬
  `Saved/Editor/` 의 `windows.ini` · `imgui.ini` 를 먼저 본다(추적하지 않는 파일 — 세션 간 픽셀 비교도 이것 때문에 안 된다). `-gv_editorOpenPanel=<id|all>`.
- **사용자 단축키는 `Saved/Editor/Shortcuts.json`(`EditorShortcutOverrides` — 기본과 다른 커맨드만)이 표 · 등록 줄 위에 입힌다**(`EditorCommandGUI::rebuild` 가
  등록 줄 뒤 · validate 앞에서). 원래 조합은 `EditorCommandDesc::_defaultShortcut` 에 남는다. 창은 Keyboard Shortcuts(`ShortcutsPanel` — Set 은 다음 조합을 받고
  받는 동안 전역 단축키를 멈춘다). 키를 더하려면 `EditorCommandKey` 의 Space 뒤 + 이름 표 + `EditorCommandGUI.cpp` 의 `kArrExtraKey`. 시나리오 `editor/shortcuts`.
- **검색 칸 뒤에 단추를 같은 줄로 두지 않는다.** `drawSearchField` 는 폭 0 이면 남은 폭을 다 써서 뒤의 단추가 창 밖으로 밀린다 — 단추를 앞에 둔다.
- **에디터 커맨드 정본은 `Common/GUI/EditorCommandGUI.cpp` 의 표 하나**(메뉴 · 단축키 · 팔레트, `EditorCommandRegistry::validate` 가 중복 조합을 잡는다). 한 줄짜리 래퍼는 이유가 있어
  남았다(파일 머리) — "마저 정리" 하지 말 것. 확장자 정본은 `EditorAssetTypeRegistry`(`kArrAssetMatch` 한 줄), 핫 리로드 경로도 같은 줄의 칸(`_pCacheKindName` · `_pfnImportSource`)이다. 복합 접미사
  `.prefab.xml` 은 접미사 비교로(`hasExtension` 은 마지막 점 뒤만 본다).
- **nullable 조회는 받아서 확인하고 쓴다** — `editor::getService<T>()` · `game::getService<T>()` · `EditorContext::get()`. 나중에 불리는 람다 안에서는 다시 받는다. `getService<…>()->` 꼴은
  `CheckNullableServiceUse` 가 막는다. `game::areGameServicesBound()` 는 SceneManager 슬롯 하나만 본다. 진단용 서비스는 `OPT` 로 등록한다(required 면 `areEngineServicesBound()` 가 영영 false).
- **문서 저장 계약** — dirty 비트는 `IEditorPanel` 이 든다(패널이 자기 `_bDirty` 를 만들면 Ctrl+S · 종료 확인에서 빠진다). `saveDocumentAndClearDirty` 가 성공했을 때만 지운다. 로드 실패는
  `markDocumentLoadFailed` 로 저장을 막는다. 씬 dirty 는 되돌리기 · 다시 하기 · 스냅샷 되읽기에서도 찍는다. 커맨드 스택이 없어도 `markActiveSceneDirty()` 는 찍는다.
- **플레이** — 스냅샷은 활성 씬의 세대 · 이름 · 소스 경로를 함께 적는다(Stop 때 세대가 다르면 로드를 거두고 편집하던 씬을 다시 세운 뒤 되돌린다). 플레이 중 인스펙터 직접 편집은 바로
  적용한다(의도 — Stop 이 되돌린다). 씬을 여는 중의 Play 는 `Starting` 으로 미뤘다가 로드가 끝난 프레임에 시작한다.
- **되돌리기** — 자식 있는 오브젝트는 서브트리를 후위 순서로 한 트랜잭션에(`recordDestruction`), 생성 · 삭제는 `recordObjectLifetime` 한 절차. 제자리 로드는 지우기 전에 다른 오브젝트의
  자식을 (자식 핸들, 부모 **안정 키**)로 적고 되붙인다. 오브젝트 → GUID 표와 GUID → 오브젝트 표는 서로의 역이어야 한다(`EditorWorkspace::setGuid`). 모듈 DLL 주소(람다)는 모듈이 내려가기 전에 걷는다.
- **인스펙터** — 리플렉션 객체 그리기는 `EditorPropertyGrid` 하나다(인스펙터 · 환경설정 · 다중 선택이 쓴다). 대상은 `EditorPropertyGridTarget`(인스턴스 · 타입 ·
  통지와 Undo 의 주인 · 씬 밖 객체의 `_onEdited`)이고, 값 칸마다 이름표 `inspector.property.<타입>.<프로퍼티>` 를 남긴다(드래그 칸은 Ctrl+클릭이 글 입력 — 시나리오 `editor/inspectoredit`).
  기본값과 다른 프로퍼티는 이름 칸 끝에 되돌리기 단추가 생긴다 — 기본은 컴포넌트 타입마다의 기본 인스턴스(`EditorDefaultObjects`, 언리얼 CDO)이고, 모듈이 언로드되면 모두 지운다.
  오른쪽 클릭 메뉴는 Reset to Default · Copy Value · Paste Value(프로퍼티 글 하나 — 다른 프로퍼티를 건드리지 않는다). 시나리오 `editor/propertyreset`.
  둘 이상 고르면 공통 컴포넌트만 그리고(`EditorMultiEditUtil` — 교집합 · 혼합 판정 · 고친 프로퍼티 하나만 나머지에 입히기), 혼합 값은 이름 앞에 "—" 다.
  되돌리기 기록은 위젯이 풀린 순간이 아니라 편집 통지 뒤(`InspectorPropertyUndo::commitFinishedEdits`)에 선택한 오브젝트 모두를 한 트랜잭션으로 남긴다 —
  풀린 순간에 "뒤" 스냅샷을 뜨면 나머지에 입힌 값이 빠져 Ctrl+Z 가 주 선택만 되돌린다(시나리오 `editor/multiedit`).
  타입 그리기 확장(`SW_EDITOR_PROPERTY_DRAWER`, id = 리플렉션 타입 이름)은 `draw( 인스턴스, 프로퍼티, 그리드 )` 를 받는다. ImGui 위젯 하나로 끝나지 않는 편집(팝업 편집기)은
  작업 사본을 고치고 마칠 때 `grid.applyPropertyTextAsEdit` 로 한 번 입힌다 — 끄는 동안 값에 바로 쓰면 되돌리기의 "앞" 스냅샷이 이미 바뀐 값이다.
  첫 사용처가 `FloatCurve` 의 미리보기 + 팝업 편집기(`EditorCurveEditor`, 좌표는 ImGui 없는 `EditorCurveView`)다(시나리오 `editor/curveedit` — 시험 컴포넌트
  `EditorCurveProbeComponent` 는 메뉴에 숨겨 두고 `component.add` 로 단다).
  타입 사슬 전부의 확장을 기반 → 파생 순으로(`collectForType`), 확장은 자기가 그린 프로퍼티만 알린다. 각도는 라디안으로 저장하고 에디터만 도로 보인다(`Units=rad`),
  0..1 비율은 `Units=ratio`, `PropertyUnitsTest.UnitsMatchHowValuesAreStored` 가 본다. 검색은 `EditorListFilter`, 0 건 안내는 `drawNoSearchResultHint`(손으로 쓴 `stristr` 술어는 빈 필터에서
  목록을 지운다).
- **ImGui 수명 짝** — 플랫폼 백엔드 `shutdown()` 은 `BackendPlatformUserData` 를 확인한 뒤에만, 초기화 실패 경로도 전역을 걷는다, 팝업에 `p_open=&_bOpen` 을 넘기지 말 것(X 버튼이 `onClose`
  를 건너뛴다). 모달이 떠 있으면 키가 `InputManager` 까지 오지 않는다. 에디터 draw 스냅샷은 획득 → present **또는 포기**(`abandonPendingDraw`)로 끝난다. 입력 위젯은 `drawTextField` 하나.
- **에디터 환경설정은 섹션 하나가 리플렉션 구조체 하나다**(`SW_EDITOR_SETTINGS` — 확장 모듈도 등록한다). 저장은 `Saved/Editor/EditorPreferences.json` 에 기본과 다른 값만(`EditorPreferencesStore`),
  테마(`EditorConfig`)도 섹션 Appearance 다. 창은 Edit > Preferences(`PreferencesPanel` — 전체 검색 · Modified only · Reset Section, 저장 단추 없이 0.5 초 뒤 저장).
  `-gv_editorUiScale` · `-gv_editorStartupScene` 은 주어지면 환경설정을 이긴다(자동화). 섹션의 바뀐 뒤 동작은 ImGui 컨텍스트보다 먼저 불릴 수 있다(기동 때 파일을 읽는다). 시나리오 `editor/preferences`.
- **저장소 파이썬 스크립트는 `EditorUtil::kPythonCommand`(Windows `py -3`)로 띄운다**(에셋 검증, 패키징). 실행기를 파일마다 따로 적지 않는다.
- **에디터 상태 · 설정** — 설정 파일 경계는 "앱이 다시 쓰는가": 앱이 쓰는 상태(`EditorPreferences.json` 환경설정 · 테마, 도킹 · 레이아웃 · 캔버스 · gv 프리셋)는 `Saved/Editor/`(git 무시), 사람이 쓰는 것만 `Config/Editor/`, 에디터 자기 파일 · 폴더 이름은 코드 상수(`EditorUtil::k…FileName`, `config::kDirConfigEditor`) — 설정 파일이 제 위치를 정하지 않는다. 씬 뷰 · 게임 뷰 클리어 색은
  `_clearColor`. 상태를 소유자에게 옮길 때는 그 소유자가 언제 서는지부터 본다(테마가 `EditorContext::initialize()` 전에 읽혀 조용히 버려졌다). DPI: 96 DPI 기준값 × 배율, 테마에서 곱하고
  되읽을 때 나눈다(짝이 깨지면 이중 배율). 모니터를 옮기면 ImGui 는 FontScaleDpi 만 덮는다 — `beginFrame` 이 그 값을 따라 `setDpiScale` 로 여백까지 맞춘다.
  WM_DPICHANGED 는 게시(PostMessage)하면 창 프로시저에 닿지 않는다 — 시험은 보내기(SendMessage)로. 에셋 핫 리로드는 에디터 소유(`FileWatchDispatcher`), 감시 접두어는 절대 경로.
- **기계 훑기의 알려진 오탐** — 델리게이트로 묶인 `&Class::method` 는 "죽은 함수" 로 잡힌다. `EditorThemeUtil` 팔레트 · 킷의 소비자 없는 세터 · 게터는 정상이다. 쓰이는지는 `= delete` 로
  바꾸고 빌드해 센다.
- **떠 있는 도구 창의 첫 크기 · 자리는 `IEditorPanel::getInitialPanelSize`(기본 640×420, 96 DPI 기준)를 `EditorChrome::setNextPanelSize` 가 UI 배율로 곱하고 주 뷰포트 작업 영역의 90 % 로 잘라 가운데에 엽니다.** `-gv_editorOpenPanel=all` 은 모든 창에 900×620 을 주므로 첫 크기 결함을 가립니다 — 첫 크기는 깨끗한 `imgui.ini` 로 패널을 하나씩 열어 봅니다. 도구 창은 생성자에서 `IEditorPanel( false )` 로 닫힌 채 시작합니다(Prefab Editor 만 기본 도킹 탭이라 열림). 시험 `panels.toolWindowsOpenAtAUsableSize`.
- **패널 시각 검증 사각** — 피킹 클릭 · 기즈모 우선순위는 사람이 눌러야 보인다. 그리기 회귀는 씬 뷰(`Scene`) 정점 수로 전후를 비교한다.
- **에셋 핫 리로드의 경계: 임포트 · 감시 · 씬 알림은 에디터, 런타임 파일의 제자리 다시 읽기는 엔진 캐시.** `AssetHotReload` 에 종류별 코드를 넣지
  말 것 — 새 종류는 엔진에 `IAssetCache` 등록 + `EditorAssetTypeRegistry` 줄의 `_pCacheKindName`(· 임포트하는 종류는 `_pfnImportSource`).
  컴포넌트 알림은 `AssetHotReload::notifyAssetUsers` 가 `PROPERTY( AssetPath )` 값으로 찾아 `onPropertyChanged` 를 부른다 — 에셋에서 계산한 상태는
  `onPropertyChanged` 가 **값이 같아도** 다시 맞춰야 한다. 리로드 전용 컴포넌트 훅 · `#if !SW_SHIPPING` 가드는 두지 않는다.
- **에디터 draw 스냅샷(`EditorDrawDataSnapshot`)은 ImGui 내부에 기댑니다.** `ImDrawList::CloneOutput()` 뒤 쓰기 커서를 "다 썼음" 으로 맞추고, `OwnerViewport` 는 원본 것을 두며, `Textures` 는 비우고 텍스처 갱신은 UI 스레드의 `processTextureUpdates` 가 합니다.
  떠 있는 뷰포트는 UI 스레드가 그리고, GL 처럼 컨텍스트가 스레드에 묶인 백엔드는 `requiresRenderThreadContext()` 가 참이라 그 GPU 호출을 렌더 스레드의 present 훅에서 합니다. ImGui 버전을 올리면 이 셋을 먼저 다시 확인합니다.
- **`ed::EndCreate()` 는 `ed::BeginCreate()` 의 반환값과 상관없이 늘 부릅니다.** `BeginCreate` 는 false 를 돌려줘도 내부 활성 상태를 세워 두므로, if 안에서만 닫으면 다음 프레임에 라이브러리 단언으로 멈춥니다.
- **노드 편집기 캔버스는 창의 첫 그리기가 되지 않게 한다.** imgui-node-editor 의 캔버스는 마지막 그리기 명령이 비어 있으면 자기 클립 사각형을 그 명령에 덮어쓰고
  화면 좌표로 되돌리지 않는다. 그러면 확대 · 축소가 1 이 아닐 때 배경과 노드가 패널 일부에서 잘린다(Dialogue Graph 를 처음 열 때 그랬다).
  `EditorNodeGraph::beginCanvas` 가 같은 색 배경을 먼저 그려 마지막 명령을 채우고, 남은 영역이 거의 없는 프레임(도킹 직후)에는 캔버스를 열지 않는다.
  화면은 시나리오 단계 `CaptureWindow`(에디터 UI 까지 든 실제 화면 PNG)로 본다 — 시나리오 `editor/dialoguegraph` · `editor/uiscale`.
- **컴포넌트 아이콘은 `EditorComponentIcon` 표 하나다**(Hierarchy 오브젝트 · 컴포넌트 줄, 인스펙터 카드 머리, 다음의 뷰포트 빌보드가 같이 쓴다). 찾는 순서는 타입 이름(자신 → 부모)
  → 리플렉션 Category → 기본. 에디터는 GameFramework 를 링크하지 않아 짧은 타입 이름으로 맞추므로 타입 이름을 바꾸면 그 줄이 조용히 죽는다 —
  `EditorComponentIconTest` 가 표의 이름이 레지스트리에 있는지 본다. 오브젝트 줄은 빌보드 종류(빛 · 카메라 · 오디오)가 있으면 그 아이콘이다(시나리오 `editor/componenticons`).
- **Add Component 목록은 `EditorComponentMenu` 하나다**(Hierarchy 오른쪽 클릭 메뉴와 인스펙터 맨 아래 단추 — 검색 Enter 는 맨 위 줄). 카드 메뉴의 Move Up/Down 은
  `GameObject::moveComponent` 로 이웃과 바꾸고, 주 씬 컴포넌트(뿌리 트랜스폼)는 옮기지도 그 자리로 끼워 넣지도 않는다. 에셋 경로 칸의 고르기 팝업 · 지우기 · 드롭은
  `grid.applyPropertyTextAsEdit` 로 입힌다 — 값에 바로 쓰면 ImGui 편집 플래그가 서지 않아 통지 · 되돌리기가 빠진다(시나리오 `editor/addcomponent`).
- **씬 뷰 단축키는 씬 뷰 위(호버 · 포커스)에서 비행 중이 아닐 때만 듣는다** — W · E · R 기즈모, Q · Space 순환, 키패드 7 · 1 · 3 직교 보기(5 는 원근), Shift+Space 최대화
  (다른 패널을 닫고 직전 배치를 상태 폴더의 Temp 레이아웃으로 둔다), 오른쪽 단추 + 휠은 비행 속도. 선택 표시는 후처리 외곽선이 아니라 경계 상자(시각화 `selection_bounds`,
  Godot 의 선택 상자)다. 이름 붙인 레이아웃을 읽으면 다시 연 패널이 포커스를 가져가므로 Scene 탭을 두 프레임 이상 앞으로 당긴다(`updateDefaultTabSelection`).
  시나리오는 가운데 단추를 씬 뷰 빈 곳에 누른 채(`state="down"` 이 커서를 붙잡는다) 키를 보낸다(`editor/viewportbasics`).
- **씬 뷰 호버 강조는 클릭 선택과 같은 피킹(`findObjectUnderMouse` — 빌보드 → 레이)을 마우스가 움직인 프레임에만 한 번 부른다.** 재 보니 시험 씬(Debug)에서
  한 번에 72 us, 메시 8000 개(`-gv_benchMeshes=8000`, Debug)에서 20.5 ms 였다(레이 피킹이 오브젝트 수에 비례) — 2 ms 를 넘으면 움직이는 동안 0.1 초에 한 번만 찾는다.
  캔버스 위가 아니거나(다른 창 · 팝업) 끌기 · 비행 · 궤도 · 기즈모 위면 비우고, 고른 오브젝트는 호버가 아니다. 캔버스의 호버 · 클릭은 이미지 바로 뒤에 읽는다 —
  시각화가 이름표 자리로 항목을 더하면(빌보드) "마지막 항목" 이 바뀐다(시나리오 `editor/viewporthover`, 단계 `EditorHover`).
- **Hierarchy 의 줄 순서는 지난 프레임에 그린 순서다**(Shift 범위 선택 · Ctrl+A · ↑↓ 가 그것을 본다 — 접힌 자식은 빠진다). Ctrl+C/V 는 상태 바이트 클립보드라
  씬을 바꿔도 붙여 넣고(루트로 들어간다), 바깥(뷰포트)에서 주 선택이 바뀌면 조상을 펼쳐 그 줄로 스크롤한다. 프리팹 인스턴스 이름은 파랗다(시나리오 `editor/hierarchyedit`).
- **Hierarchy 의 눈은 에디터에서만 숨긴다**(`GameObject::setHiddenInEditor` — PROPERTY 가 아니라 저장 · 되돌리기 · 씬 dirty 에 남지 않고 게임 동작도 그대로).
  렌더러의 프리미티브 수집이 그 비트를 보므로 숨기는 쪽(`EditorSceneCommands::setHiddenInEditor`)이 메시 렌더 상태를 더럽혀야 다음 프레임에 빠진다.
  활성 비트는 인스펙터의 Active 체크박스로만 바꾼다. 자물쇠는 워크스페이스의 잠금 목록이고 뷰포트(빌보드 · 레이 피킹)만 막는다(시나리오 `editor/hidelock`).
- **뷰포트 빌보드(`EditorViewportBillboard`, 시각화 `billboard` · 툴바 Icons)는 등록부만 훑는다**(카메라 · 빛 · 바람). 모든 오브젝트의 컴포넌트를 훑으면
  오브젝트 8000 개 벤치에서 Debug 로 프레임마다 약 10.7 ms 였고 등록부로 37 us 가 됐다 — 등록부가 없는 종류(오디오 · 2D 빛)는 등록부가 생길 때 더한다.
  클릭 선택은 레이 피킹보다 먼저 빌보드를 화면 거리로 찾는다(메시가 없어 레이로 안 집힌다). 시나리오 `editor/billboardpick`.
- **노드 그래프 틀은 `EditorNodeGraph`(내보냄) + 템플릿 `EditorGraphDocumentPanel` 이다.** 틀이 찾아 넣기(빈 곳 오른쪽 클릭 → 검색 → Enter 는 맨 위 줄, 고른 노드는
  그 자리에 — `placeNodeOnNextDraw`), 링크 판정(`queryNewLink` — 방향 · 핀 타입이 맞지 않으면 빨갛게 거절하고 이유 툴팁), 문제 노드 빨간 테두리(`setNodeIssues`)를 한다.
  판단은 ImGui 없는 `EditorNodeGraphRules`(EditorTest). 캔버스는 `beginGraphCanvas` · `endGraphCanvas` 로 연다 — 확장 모듈은 imgui-node-editor 를 정적으로 따로
  링크해 틀이 건 지금 편집기를 모르므로 템플릿(확장 안에서 컴파일된다)이 자기 사본에도 건다. 이것을 건너뛰고 `_nodeGraph.beginCanvas` 를 바로 부르면 확장의
  `ax::NodeEditor` 호출이 편집기 없음으로 멈춘다. 시나리오 `editor/graphaddnode`(탐침 `Editor.GraphNodeCount` 는 가장 최근에 그린 캔버스의 노드 수).
