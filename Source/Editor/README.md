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
  그리고 커맨드 정의를 담는 `EditorCommandRegistry`.
  패널은 UI 만, 실제 동작은 여기입니다 (그래서 테스트가 붙습니다)
- **Asset/**: 원본 임포트 — 텍스처(`TextureImporter`, `TextureImportConfig`, `ImageUtil`) · 모델(`ModelImporter`)과 둘이 쓰는 스탬프 절차
  (`AssetImportStamp`), 헤드리스 임포트 진입점(`AssetImportEntry.cpp` — 아래 "텍스처는 들일 때 임포트한다" · "모델도 들일 때 임포트한다"). 감시는 `Common/Workspace/AssetHotReload` 하나뿐이다
- **Config/**: Host JSON(`EditorConfig`)과 XML 시드(`EditorToolDefaults`)

### 기능

- **Panels/**: Hierarchy, Inspector, Game View, Content Browser, Console, Profiler,
  Sequencer, Animation Graph, Dialogue Graph, Prefab Editor, Tile Map, Sprite Clip
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

`EditorCommandRegistry::validate` 는 중복 id·중복 조합도 시작할 때 잡습니다(`Test/EditorTest/TestEditorCommandRegistry.cpp`).
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
- 주의: `.hdr` 는 임포트하지 않고 보고합니다 — 디코더(stb_image)가 8비트라 값이 잘립니다.
- 시험: `TextureImportStampTest.RepositoryRawTexturesMatchTheirDds`(저장소의 원본과 DDS 가 맞는지), `AppSmokeTest.TextureCheckRunsHeadlessThroughTheEditorModule`.
- 폴더 훑기 · 스탬프 · 어긋남 판정은 모델과 같은 한 벌입니다(`AssetImportStampUtil` + 종류마다 `IRawAssetImporter`).

## 모델도 들일 때 임포트한다

glTF 원본(`.glb` · `.gltf`)은 `models_raw/` 에 두고 같은 상대 경로의 `models/<이름>.mesh` 로 임포트합니다(`ModelImporter`, cgltf + meshoptimizer).
런타임은 `.mesh` 만 읽고(`MeshCache`), `MeshComponent::_meshId` 에 그 경로를 적거나 `PrimitiveStage::createModelObject` 로 세웁니다.

- **변환**: 기본 씬의 노드 계층을 월드 변환째 한 메시로 합칩니다. glTF(오른손 · +Y 위 · 앞 +Z)를 엔진(왼손 · +Y 위 · 앞 +Z, 앞면 = 시계 방향)으로
  옮기려고 **X 를 뒤집고 삼각형마다 감김을 뒤집습니다**(노드가 거울상이면 한 번 더). 노멀이 없으면 면 노멀, 색은 baseColorFactor × COLOR_0.
  삼각형이 아닌 프리미티브는 경고하고 건너뜁니다. 텍스처는 로그로만 알리고, 머티리얼은 게임이 `PrimitiveLook` 으로 고릅니다.
- **헤드리스 · 스탬프**: `App --import-models` · `--check-models`. `models_raw/import.stamp` 는 텍스처와 같은 형식이고 원본 해시에 임포터 버전 ·
  `.mesh` 형식 버전 · `.gltf` 의 외부 버퍼가 섞입니다. 임포트 동작을 바꾸면 `ModelImporterInternal::kImporterVersion` 을 올립니다.
- **핫 리로드**: `models_raw/` 원본이 바뀌면 일괄 임포트하고, 쓰인 `.mesh` 를 메시 캐시가 같은 `Mesh` 에 제자리로 다시 읽습니다.
- 시험: `ModelImporterTest`(좌표계 · 감김 · 노드 변환 · 색 · 스탬프), 엔진 쪽은 `MeshAssetTest`.

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

## ⚠️ 핵심 특징 및 규칙
- **Dev 모드 전용**: 이 폴더의 코드는 개발(Dev) 모드에서만 `MODULE DLL`로 빌드되고 동작합니다. 배포(Shipping) 빌드를 할 때는 **코드가 통째로 날아갑니다.**
- **게임 로직 분리**: **절대 게임(Game) 로직이 이 폴더의 코드에 의존해서는 안 됩니다.** 게임 코드에서 `#include "Editor/"` 등을 호출하면 Shipping 빌드가 100% 터집니다.
에디터에서만 써야 할 기능이라면 매크로를 신중하게 사용하세요.
