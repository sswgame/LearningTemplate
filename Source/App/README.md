# App (진입점 프로그램)

엔진이 켜질 때 **가장 먼저 실행되는 순수한 실행 파일(.exe)** 진입점입니다.

## 동작 흐름
1. `App`이 실행되면서 창(Window)을 만들고 렌더러(RHI)를 초기화합니다.
2. 개발 모드(Dev)라면 백그라운드에서 핫리로드를 관장하는 `LiveReloadManager`를 가동합니다.
3. 이후 `EditorModule` DLL과 `SWGame` DLL을 불러와 함수 포인터(`exportGameAPI`)를 연결하고 게임 루프를 시작합니다.

윈도우 메시지는 `NativeWindowEvent`로만 받고, 키/마우스 해석은 `InputManager`가 합니다.
App은 GameFramework를 링크하지 않으므로 셸 전용 `ActionMap`(`_mapDebugAction`)을 둡니다. Debug 레이어(`alwaysOn`)로 ReloadShaders=Ctrl+F8, ReloadEditor=Ctrl+F6, ReloadGame=Ctrl+F7 조합 키를 조회하여 게임플레이 단독 스킬 단축키와의 충돌을 차단합니다.
이 셸 단축키는 **Dev 전용**입니다 — Shipping 에는 리로드할 모듈이 없어 `App::pollReloadHotkeys` 가 통째로 비어 있습니다.

## 디렉터리 구조
- **App.cpp / App.h**: 앱 생명주기 및 윈도우/엔진 부트스트랩. App 이 직접 아는 것은 **부팅 순서·창·프레임 순서·콜백 배선** 네 가지뿐입니다.
- **Frame/**: `FrameTimeline` — 실시간 경과를 가변 델타와 고정 스텝 수로 나눕니다.
- **Module/**: 동적 모듈 로드, 라이프사이클 핫리로드 관리, 직렬화를 통한 상태 보존 및 태스크 펜싱을 수행하는 `ModuleHost`
- **Rhi/**: `BackendSwapController` — `gv_rhiBackend` 변경을 받아 프레임 경계에서 백엔드를 교체합니다.

기존에 존재하던 `AppBootstrap.cpp`, `AppModuleBinding.cpp`, `AppRhiHotSwap.cpp` 등의 파편화된 로직은 런처의 경량화(Thin Launcher) 원칙에 따라 모두 `App.cpp` 내부와 `EngineLoop`, `ModuleHost` 로 통폐합되었습니다.

## 프레임 순서와 그 이유

`App::run` 의 한 프레임은 아래 순서를 지킵니다. 순서를 바꾸면 조용히 깨지는 자리가 있습니다.

| 단계 | 왜 그 자리인가 |
|---|---|
| `FrameTimeline::advance` | 가변 델타를 잘라내고 이번 프레임의 고정 스텝 수를 확정한다. |
| `ModuleHost::beginFrame` | 에디터 Play 상태를 한 번 래치한다. 고정 스텝이 6번 돌아도 DLL 경계를 다시 넘지 않는다. |
| `fixedUpdateGame` × N → `updateGame` | 래치된 상태를 읽으므로 모든 스텝이 같은 답을 본다. |
| `ModuleHost::updateEditorUI` | 에디터가 이번 프레임 입력을 처리한 **뒤** 게임 뷰포트 RT 와 씬 틱 여부를 확정한다. 이 질의를 앞으로 옮기면 **Step 한 칸이 틱 없이 소비**된다. |
| `EngineLoop::tick` | 래치된 프레임 상태(`ModuleFrameState`)를 그대로 넘긴다. 뷰 카메라는 tick 내부에서 지연 조회한다 — 미리 잡으면 씬 전환/핫리로드가 파괴한 객체를 역참조한다. |
| `BackendSwapController::applyIfPending` | 교체는 프레임 경계에서만 한다. |

## 시간 정책

고정 스텝 길이와 프레임당 상한은 `EngineConfig` 가 들고 있습니다 (`_fixedDeltaTime`,
`_maxFixedStepPerFrame`, `_maxFrameDeltaTime`). 상한이 **반드시** 필요합니다 — 없으면 느린
프레임이 더 많은 스텝을 부르고 그래서 더 느려지는 되먹임(고정 스텝 스파이럴)에 빠집니다.
상한을 넘긴 잔여 시간은 버립니다: 시뮬레이션이 실시간보다 느려지는 쪽을 택합니다.

## 모듈을 내리는 경로는 하나다

종료·핫리로드·RHI 핫스왑은 모두 `ModuleHost::suspendModules( ModuleScope, bReleaseApiTable )`
하나를 지나갑니다. "워커 배수 → 상태 보존 → 파괴" 순서를 사유마다 따로 조립하면 한 곳만
고쳐 놓고 나머지를 잊습니다. 새 재생성 사유(디바이스 상실, 어댑터 변경 등)가 생기면
`ModuleScope` 와 호출 한 줄만 늘립니다.

## ⚠️ 핵심 규칙
- `App` 폴더 내부는 게임 루프의 시작점일 뿐, 복잡한 로직을 담는 곳이 아닙니다. 새로운 시스템을 추가해야 한다면 `App`이 아니라 `Engine` 폴더를 고려하세요.
- 프레임 중에 에디터 상태를 **다시 묻지 마세요.** `ModuleHost::getFrameState()` 가 이번 프레임의 답입니다.
