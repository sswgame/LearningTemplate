# Editor (개발자용 에디터 모듈)

씬을 편집하고 디버깅하는 **에디터 UI(ImGui)** 입니다. Dev에서만 `EditorModule` MODULE로 빌드됩니다.

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
- **Gui/**: ImGui 를 **직접 그리는** 공용 셸 — `EditorChrome`, `EditorMenuBar`, `EditorDockLayout`,
  `EditorDocumentPanel`, `EditorThemeUtil`, `EditorFontSetup`, `EditorNotificationManager`(토스트), `EditorPanelDump`(아래 "그려진 결과"),
  `EditorCommandGui`(커맨드 표 · 전역 단축키 · 메뉴 항목),
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
- **SourceControl/**: 버전 관리 잠금(체크아웃) — 공급자 추상(`ISourceControlProvider`: git LFS · 없음)과 창구(`EditorSourceControl`). 아래 "버전 관리 잠금"
- **Config/**: Host JSON(`EditorConfig`)과 XML 시드(`EditorToolDefaults`)

### 기능

- **Panels/**: Hierarchy, Inspector, Game View, Content Browser, Console, Profiler(CPU 구간 · GPU 패스 · 카운터 실시간 표 + 프레임 그래프 — 집계는 ImGui 없는
  `ProfilerScopeHistory`, "Open Tracy" 는 `Common/Commands/EditorTracyLauncher` 가 같은 판 Tracy 뷰어를 띄워 localhost 에 붙인다),
  Sequencer, Animation Graph, Animation Rewind(기록된 포즈 · 상태를 시간 막대로 훑기 — 훑으면 PIE 를 멈춘다), Dialogue Graph, Prefab Editor, Tile Map,
  Sprite Clip, User Settings(플레이어 옵션을 메뉴 바인딩 API 로 바꿔 보는 창 —
  셀프 시험 `userSettings.panelDrawsEveryTab`)
  - `Panels/Inspector/`: 프로퍼티·컴포넌트 인스펙터 확장 — 컴포넌트 확장은 `<Component>Inspector.cpp` 하나씩
- **Viewport/**: 뷰포트 클라이언트, 툴바, 에디터 카메라(`EditorCamera`),
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
| 메뉴·단축키·커맨드 팔레트에 나타날 동작 | `Common/Gui/EditorCommandGui.cpp` 의 커맨드 표 (아래) |
| 저장되지 않을 수 있는 편집 | `IEditorPanel` 의 문서 계약 (아래) — 자기 dirty 플래그 금지 |
| ImGui 를 그린다 | `Common/Gui/` · `Common/Widgets/` · `Panels/` · `Popups/` |
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

`EditorCommandRegistry::validate` 는 중복 id·중복 조합도 시작할 때 잡습니다(`Test/EditorTest/Common/Commands/TestEditorCommandRegistry.cpp`).
**패널에서 단축키를 따로 처리하지 마십시오** — ImGui 의 `IsKeyPressed` 는 소비되지 않으므로, 전역 처리기와 패널이 같은 조합을 보면
같은 프레임에 두 번 실행됩니다(예: `Ctrl+Z` 가 두 번 되돌림).

**단축키 라벨을 손으로 적지 마십시오.** 툴팁의 `(Ctrl+S)` 도 표의 조합에서 만들어 붙습니다 —
그래야 조합을 바꿀 때 라벨이 거짓말을 하지 않습니다.

## 테마 프리셋을 하나 더하려면

`Common/Gui/EditorThemeUtil.cpp` 의 프리셋 표(`getPresetRows`)에 한 줄을 넣고 열거형에 값을
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

문서 하나가 애셋 경로와 연동되는 도구 패널은 `Common/Gui/EditorDocumentPanel` 을 상속하십시오
(포커스 추적 · Undo 기준선 · 문서 전환 확인 팝업까지 얹어 줍니다). 한 패널이 문서를 둘 이상
들면(`DataTablePanel`) 기반 비트는 "무언가 바뀌었다"만 말하므로, 어느 쪽인지는 패널이 자기
반쪽 비트로 알고 한곳에서 동기화합니다(`syncDocumentDirty`).

계약 자체는 ImGui 없이 컴파일되므로 테스트가 있습니다: `Test/EditorTest/Common/Gui/TestEditorPanelDocument.cpp`.

## 그려진 결과를 검증하는 법

`Test/EditorTest` 가 ImGui 없이 도는 것만 본다는 뜻은, **패널을 비워 놓고도 테스트가 통과한다**는
뜻입니다. 화면을 직접 볼 수 없을 때(CI·자동화·원격)는 `-gv_editorPanelDump=N` 을 씁니다.

```powershell
./App.exe -gv_profileFrames=40 -dx12 -EnableEditor -gv_editorPanelDump=25
```

N 번째 ImGui 프레임에 창 하나당 한 줄(이름 · 크기 · **정점 수** · 활성/접힘/숨김)과 요약을 로그에
남깁니다. **보이는데 정점이 0인 패널**이 곧 빈 패널입니다. 컨테이너(자식이 내용을 든 창)와 순수
오버레이(`NoInputs` — ImGuizmo 의 `gizmo` 가 그렇습니다)는 정상적으로 비므로 빼고 셉니다.

구현과 스위치 선언은 `Common/Gui/EditorPanelDump.*` 에 있습니다 — 모듈의 전역 변수도 모듈을 올릴 때 커맨드라인 값을 받습니다.
기준선과 비교 방법은 [docs/06_Backlog.md](../../docs/06_Backlog.md) 0절에 있습니다.

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
- 시험: `TextureImportStampTest.RepositoryRawTexturesMatchTheirDds`(저장소의 원본과 DDS 가 맞는지), `AppSmokeTest.TextureCheckRunsHeadlessThroughTheEditorModule`.
- 폴더 훑기 · 스탬프 · 어긋남 판정은 모델과 같은 한 벌입니다(`AssetImportStampUtil` + 종류마다 `IRawAssetImporter`).

## 모델도 들일 때 임포트한다

glTF 원본(`.glb` · `.gltf` · `.vrm`)은 `models_raw/` 에 두고 같은 상대 경로의 `models/<이름>.mesh`(스킨드 모델이면 옆 폴더의 스켈레톤 · 부착 메시 · 클립까지)로
임포트합니다(`ModelImporter`, cgltf + meshoptimizer).
런타임은 `.mesh` 만 읽고(`MeshCache`), `MeshComponent::_meshId` 에 그 경로를 적습니다.

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

## UI 스레드가 놓은 GPU 자원

ImGui 텍스처 · 게임 뷰 렌더 타깃은 UI 스레드가 놓지만 그리는 것은 렌더 스레드이고, 렌더 스레드는 UI 가 다음 스냅샷을 내기 전까지 **같은 draw
스냅샷을 여러 프레임에 다시 그립니다.** 그래서 놓은 자원은 바로 지우지 않고 `IImGuiRendererBackend::getDrawReleaseQueue()` 의
`EditorDrawReleaseQueue::enqueue` 에 맡깁니다. 큐는 해제마다 "다음에 낼 스냅샷 번호" 를 찍어 두고, 렌더 스레드가 그 번호 이상의 스냅샷을
기록하는 프레임에서 `IRHIDevice::enqueueGpuRelease` 로 넘깁니다 — 그 프레임의 GPU 완료 뒤에 실제로 풀립니다.
주의: UI 스레드에서 읽은 펜스 값으로 해제하면 뒤에 줄 선 프레임이 놓인 자원을 씁니다.

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
`EditorSelfTest|DONE|<통과>|<실패>` 를 로그와 보고서에 남기고 앱을 닫습니다. 실행 중에는 저장된 레이아웃(`imgui.ini` · `windows.ini`)을 읽지도
쓰지도 않고 기본 가시성 · 기본 도킹 배치로 뜹니다. `AppSmokeTest.EditorSelfTestsPassInsideTheEditor`(hostgpu)가 이렇게 띄워 알려진 시험이 모두
PASS 인지 봅니다 — 시험을 더하면 그 목록에도 한 줄 더합니다.

## Game View 의 개발 편의 기능

- **Play / Simulate**: Play 는 플레이어가 조종하는 세션(게임 모듈 업데이트 · 게임 입력 · 게임 카메라), Simulate 는 **월드만** 돈다 — 씬은 틱하지만
  게임 모듈 업데이트와 게임 입력이 꺼지고 에디터 카메라로 본다(언리얼 Simulate). 도는 중에 서로 바꿀 수 있다. 호스트는 `IEditor::isPlaying`
  (= `EditorPlaySession::isPlayerActive`)으로 게임 모듈을 켜고, 씬 틱은 `isPaused` 가 정한다.
- **Step · Step N**: 한 프레임 / 칸에 적은 프레임 수만큼 진행하고 일시정지한다(`EditorPlaySession::stepFrames`).
- **Cam**(카메라에서 시작): Play 를 에디터 카메라 위치에서 시작한다 — `Player` 태그를 단 오브젝트, 없으면 게임 카메라를 든 오브젝트의 맨 위 조상을
  순간이동한다. 월드 시작 직후와 첫 프레임 뒤 두 번 옮긴다(첫 틱에 스폰 자리로 되돌리는 게임이 있다).
- **시간 배율**(`x1.00` 칸): `gv_timeScale`(`Engine/Utility/GameTimeScale`). 끌어서 바꾸고 오른쪽 클릭으로 1 로 되돌린다. 호스트의 프레임 시간이
  곱해 게임 업데이트 · 씬 틱 · 고정 스텝이 같이 느려지거나 빨라진다. 에디터 UI · 에디터 카메라는 자기 시간으로 돈다.
- **디버그 드로우**: 게임 코드가 `DebugDrawQueue`(엔진 서비스)에 넣은 선 · 구 · 상자 · 화살표 · 글자를 `debug_draw` 시각화(툴바 `Dbg`)가 그린다.
  지속 시간(초)과 카테고리를 받는다. `Dbg Cat` 팝업이 카테고리를 켜고 끈다. 2D 뷰(직교 카메라가 Z 를 본다)에서는 구가 XY 원 하나다.
  오버레이(시각화 · 피킹 · 기즈모)는 호스트가 그리는 것과 같은 카메라로 투영한다 — Play 중에는 게임 카메라다.
- **Auto**(자동 플레이): 게임이 `SW_GAME_AUTOPLAY` 로 등록했으면 툴바에 서고, 누르면 그 게임의 자동 플레이 전역 변수를 켜고 끈다
  (`GameAutoplay::setOn` — 전역 변수 표를 거쳐 써서 패널 · 콘솔과 같은 값이다). 자체 시험 `gameView.autoplayButton`.
- **HUD**(디버그 오버레이): 게임이 `DebugOverlayState` 에 쓴 값을 캔버스 왼쪽 아래에 키 순서로 그린다.
- 시험: `EditorPlaySessionTest`(Simulate · Step N · 카메라에서 시작), `DebugDrawQueueTest`, `DebugOverlayStateTest`, `FixedTimestepTest.TimeScale…`,
  에디터 자체 시험 `gameView.debugDraw` · `gameView.debugOverlay`.

## Output Log · 설정 · 선택 · 레이아웃

- **로그 줄 → IDE**: 줄을 더블 클릭(또는 오른쪽 클릭 `Open in IDE`)하면 그 줄이 가리키는 소스 위치를 IDE 로 연다. 메시지 안의 위치
  (`경로(줄,열)` · `경로:줄:열` — 컴파일러 · 셰이더 오류)가 먼저, 없으면 로그를 쓴 자리다. 명령 틀은 `editortooldefaults.json` 의
  `_ideOpenCommand`(`{file}` · `{line}`), 비우면 VS Code(`code -g`, Windows 는 `cmd /c`)다. 판정은 `Common/Commands/EditorLogCommands`.
- **카테고리 필터**: 툴바 `Tags` 팝업이 로그 카테고리(로그를 쓴 자리 `SW_LOG_CALLER`, 없으면 모듈 태그)마다 보이기를 켜고 끈다(`EditorLogTagFilter`).
- **설정 파일 핫 리로드**: `Common/Workspace/ConfigHotReload` 가 `Config/` 의 `.json` 을 감시한다(에셋과 같은 `FileWatchDispatcher`, 루트만 다름).
  호스트 설정(`ConfigManager` 가 파일에서 읽은 EngineConfig · GameConfig)은 `ConfigManager::reloadConfigFile` 이 **제자리에서** 다시 읽고
  `onConfigReloaded` 로 알린다 — App 은 프레임 시간 정책, EngineLoop 는 게임 설정 활성본 · 선호 수직 동기화(다음 스왑체인부터). 에디터 도구 시드
  (`editortooldefaults.json`)는 에디터가 다시 읽는다. 앱이 다시 쓰는 `EditorConfig.json` 은 다시 읽지 않는다.
- **같은 종류 · 태그 모두 선택**: Hierarchy 오른쪽 클릭 `Select All With` — 그 오브젝트의 컴포넌트 종류(파생 포함) · 태그(아래 계층 포함)마다
  (`EditorSceneCommands::collectObjectsWithComponent` · `collectObjectsWithTag` · `selectObjects`).
- **이름 붙인 레이아웃**: `Panel > Layouts` — 이름을 적고 Save, 목록에서 고르면 불러오고 `x` 로 지운다. 도킹 배치(`<이름>.imgui.ini`)와 패널
  가시성(`<이름>.windows.ini`)이 `Config/Editor/Layouts/` 에 남는다(git 무시). 불러오기는 다음 프레임 `NewFrame` 앞에서 한다
  (`EditorDockLayout::applyPendingNamedLayout`) — 프레임 안에서 ImGui 설정을 읽으면 이미 있는 창 · 도킹 노드에 적용되지 않는다.
- 시험: `EditorLogCommandsTest` · `ConfigHotReloadTest` · `EditorLayoutStoreTest` · `EditorSceneCommandsTest.CollectObjectsByComponentTypeAndTag`
  (EditorTest), `ConfigManagerTest.ReloadConfigFileUpdatesInPlaceAndNotifies`(EngineTest), 에디터 자체 시험 `console.tagFilter` ·
  `hierarchy.selectAllWith` · `layout.namedRoundTrip`.

## 개발 콘솔(Output Log 입력 줄)

Output Log 아래 입력 줄이 개발 콘솔(`Engine/Utility/Console/DevConsole`)입니다 — `help`, `gv_이름 [값]` · `get` · `set`, 개발 명령(`SW_DEV_COMMAND`),
Tab 자동완성(후보가 여럿이면 로그에 줄로 보인다), ↑↓ 기록. 답은 로그(`DevConsole`)로 남아 같은 패널에 보입니다. 에디터가 등록하는 명령은
`Common/Commands/EditorDevCommands.cpp` — `editor <커맨드 id>`(커맨드 팔레트의 id) · `play` · `simulate` · `pause` · `stop` · `step [N]` ·
`select.type <컴포넌트 타입>` · `select.tag <태그>` · `layout.save <이름>` · `layout.load <이름>` · `debugdraw.demo [초]`(뷰포트 카메라 앞에 상자 · 구 · 화살표 · 글자와 HUD 값 하나 — 시각화가 도는지 보는 용도). 엔진 명령(`timescale` · `teleport` ·
`debugdraw.category`)은 `Engine/DevTools/EngineDevCommands.cpp`. 에디터 없이 띄운 게임 창에서는 `~` 오버레이가 같은 콘솔입니다(`Source/App/README.md`).
시험: `DevConsoleTest` · `DevCommandRegistryTest` · `DevConsoleControllerTest`(EngineTest), `DevCommandShippingTest`(AppTest), 자체 시험 `console.devCommands`.

## ⚠️ 핵심 특징 및 규칙
- **Dev 모드 전용**: 이 폴더의 코드는 개발(Dev) 모드에서만 `MODULE DLL`로 빌드되고 동작합니다. 배포(Shipping) 빌드를 할 때는 **코드가 통째로 날아갑니다.**
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
