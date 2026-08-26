# Editor (개발자용 에디터 모듈)

씬을 편집하고 디버깅하는 **에디터 UI(ImGui)** 입니다. Dev에서만 `EditorModule` MODULE로 빌드됩니다.

루트에는 모듈 진입점만 둡니다: `IEditor.h`, `ImGuiEditor.*`

## 디렉터리 구조

### 공통 (`Common/`)

에디터 기능이 공통으로 쓰는 프레임워크입니다. 새 패널/툴을 만들 때 여기부터 찾으면 됩니다.

- **EditorUtil**: 폰트·설정 경로 등 공용 유틸
- **Platform/**: ImGui OS/GPU 백엔드
- **Gui/**: `EditorChrome`, `EditorMenuBar`, `EditorDockLayout`
- **Widgets/**: 검색, 헤더, 툴바 구분선 등 공유 위젯
- **Workspace/**: 세션, 선택, 커맨드 스택, 애셋 드롭, 프리팹 인스턴스
- **Config/**: Host JSON(`EditorConfig`)과 XML 시드(`EditorData`)

### 기능

- **Panels/**: Hierarchy, Inspector, Game View, Content Browser, Console, Profiler
- **Inspector/**: 인스펙터 프로퍼티/컴포넌트 드로어
- **Tools/**: AnimationGraph, Sequencer, SpriteClip, TileMap, DialogueGraph
- **Viewport/**: 뷰포트 클라이언트와 툴바
- **Popups/**: 커맨드 팔레트, 토스트, 본 계층 팝업

## ⚠️ 핵심 특징 및 규칙
- **Dev 모드 전용**: 이 폴더의 코드는 개발(Dev) 모드에서만 `MODULE DLL`로 빌드되고 동작합니다. 배포(Shipping) 빌드를 할 때는 **코드가 통째로 날아갑니다.**
- **게임 로직 분리**: **절대 게임(Game) 로직이 이 폴더의 코드에 의존해서는 안 됩니다.** 게임 코드에서 `#include "Editor/"` 등을 호출하면 Shipping 빌드가 100% 터집니다.
에디터에서만 써야 할 기능이라면 매크로를 신중하게 사용하세요.
