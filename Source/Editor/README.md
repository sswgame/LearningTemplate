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
  `EditorActionMenuManager`(우클릭 메뉴), 인터페이스 `IEditorPanel` / `IEditorPopup`
- **Widgets/**: 검색, 헤더, 툴바 구분선, 노드 그래프 캔버스(`EditorNodeGraph`), 뷰포트 입력 오버레이
- **Workspace/**: ImGui 없는 **상태** — 컨텍스트·선택·트랜잭션(Undo)·서비스 로케이터·애셋 종류 ·
  플레이(PIE) 세션(`EditorPlaySession`, `EditorSessionPolicy`)
- **Commands/**: 패널이 쓰는 **ImGui 없는 로직** — 애셋/씬/트랜스폼/데이터테이블 변이와 파일 IO.
  패널은 UI 만, 실제 동작은 여기입니다 (그래서 테스트가 붙습니다)
- **Asset/**: 텍스처 임포트·베이크·감시 (`TextureBaker`, `TextureWatcher`, `ImageUtil`)
- **Config/**: Host JSON(`EditorConfig`)과 XML 시드(`EditorData`)

### 기능

- **Panels/**: Hierarchy, Inspector, Game View, Content Browser, Console, Profiler,
  Sequencer, Animation Graph, Dialogue Graph, Prefab Editor, Tile Map, Sprite Clip
  - `Panels/Inspector/`: 프로퍼티·컴포넌트 인스펙터 확장
- **Viewport/**: 뷰포트 클라이언트, 툴바, 에디터 카메라(`EditorCamera`)
- **Popups/**: 커맨드 팔레트, 퀵 런처, 본 계층 팝업

### 어디에 두나

| 새로 쓰는 것 | 자리 |
|---|---|
| ImGui 를 그린다 | `Common/Gui/` · `Common/Widgets/` · `Panels/` · `Popups/` |
| ImGui 없이 상태만 든다 | `Common/Workspace/` |
| ImGui 없이 무언가를 바꾸거나 읽고 쓴다 | `Common/Commands/` |

경계가 흐려지면 테스트가 먼저 막힙니다 — `Test/EditorTest` 는 ImGui 없이 도는 것만 검증합니다.

## ⚠️ 핵심 특징 및 규칙
- **Dev 모드 전용**: 이 폴더의 코드는 개발(Dev) 모드에서만 `MODULE DLL`로 빌드되고 동작합니다. 배포(Shipping) 빌드를 할 때는 **코드가 통째로 날아갑니다.**
- **게임 로직 분리**: **절대 게임(Game) 로직이 이 폴더의 코드에 의존해서는 안 됩니다.** 게임 코드에서 `#include "Editor/"` 등을 호출하면 Shipping 빌드가 100% 터집니다.
에디터에서만 써야 할 기능이라면 매크로를 신중하게 사용하세요.
