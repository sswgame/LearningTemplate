# Editor (개발자용 에디터 모듈)

씬을 편집하고 디버깅하는 **에디터 UI(ImGui)** 입니다. Dev에서만 `EditorModule` MODULE로 빌드됩니다.

## 디렉터리 구조
- **Backend/**: ImGui 플랫폼/렌더러 백엔드 (`ImGuiEditor`)
- **Windows/**: Hierarchy, Inspector, Game View, Content Browser, Console, Profiler
- **Tools/**: AnimationGraph, Sequencer, SpriteClip, TileMap
- **Workspace/**: 도킹 레이아웃, 선택, 커맨드 스택, 애셋 드롭
- **Widgets/** · **Shell/** (트랜스포트 바) · **Overlay/** (본 계층 팝업)

## ⚠️ 핵심 특징 및 규칙
- **Dev 모드 전용**: 이 폴더의 코드는 개발(Dev) 모드에서만 `MODULE DLL`로 빌드되고 동작합니다. 배포(Shipping) 빌드를 할 때는 **코드가 통째로 날아갑니다.**
- **게임 로직 분리**: **절대 게임(Game) 로직이 이 폴더의 코드에 의존해서는 안 됩니다.** 게임 코드에서 `#include "Editor/"` 등을 호출하면 Shipping 빌드가 100% 터집니다.
에디터에서만 써야 할 기능이라면 매크로를 신중하게 사용하세요.
