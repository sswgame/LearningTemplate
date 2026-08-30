# App (진입점 프로그램)

엔진이 켜질 때 **가장 먼저 실행되는 순수한 실행 파일(.exe)** 진입점입니다.

## 동작 흐름
1. `App`이 실행되면서 창(Window)을 만들고 렌더러(RHI)를 초기화합니다.
2. 개발 모드(Dev)라면 백그라운드에서 핫리로드를 관장하는 `LiveReloadManager`를 가동합니다.
3. 이후 `EditorModule` DLL과 `SWGame` DLL을 불러와 함수 포인터(`exportGameAPI`)를 연결하고 게임 루프를 시작합니다.

윈도우 메시지는 `NativeWindowEvent`로만 받고, 키/마우스 해석은 `InputManager`가 합니다.
App은 GameFramework를 링크하지 않으므로 셸 전용 `ActionMap`(`_shellActions`)을 둡니다. Debug 레이어(`alwaysOn`)로 ReloadShaders=F8, ReloadEditor=F6, ReloadGame=F7을 조회합니다. 퀵세이브/로드(F5/F9)는 게임플레이 `gameActions()` 쪽입니다.

## 디렉터리 구조
- **App.cpp / App.h**: 앱 생명주기 및 윈도우/엔진 부트스트랩
- **Module/**: 동적 모듈 로드, 라이프사이클 핫리로드 관리, 직렬화를 통한 상태 보존 및 태스크 펜싱을 수행하는 `ModuleHost`

기존에 존재하던 `AppBootstrap.cpp`, `AppModuleBinding.cpp`, `AppRhiHotSwap.cpp` 등의 파편화된 로직은 런처의 경량화(Thin Launcher) 원칙에 따라 모두 `App.cpp` 내부와 `EngineLoop`, `ModuleHost` 로 통폐합되었습니다.

## ⚠️ 핵심 규칙
- `App` 폴더 내부는 게임 루프의 시작점일 뿐, 복잡한 로직을 담는 곳이 아닙니다. 새로운 시스템을 추가해야 한다면 `App`이 아니라 `Engine` 폴더를 고려하세요.
