# App (진입점 프로그램)

엔진이 켜질 때 **가장 먼저 실행되는 순수한 실행 파일(.exe)** 진입점입니다.

## 동작 흐름
1. `App::initialize` 가 `EngineLoop::initialize` 를 부릅니다. 엔진 기동은 표(`Engine/EngineInitStepList.xxx`) 순서로 돌고, 창 · RHI 디바이스도
   그 표의 `RHI` 단계가 만듭니다. App 은 그 뒤 활성 창의 소유권을 넘겨받습니다(`acquireMainWindow`).
2. **모듈 이미지는 기동 단계 `ModuleTypes` 에서 올립니다.** App 이 `EngineLoop::setModuleTypeLoader` 로 건 `App::loadModuleImages` 가
   (Dev) `LiveReloadManager` 를 만들고 `ModuleHost::loadModuleImages` 로 GameFramework → 키트 → `SWGame` 이미지를 올려 타입만 등록합니다
   (인스턴스는 아직 없음). 씬은 이 단계 뒤에만 읽힙니다. Shipping 은 정적 링크라 올릴 이미지가 없습니다.
3. 기동이 끝나면 `ModuleHost::initialize` 가 **게임 인스턴스 → 에디터 인스턴스** 순으로 만듭니다. 씬 매니저는 마지막 요청을 남기므로
   에디터의 시작 씬(`-gv_editorStartupScene`)이 게임의 첫 씬 요청보다 이깁니다. 에디터는 `-EnableEditor` 일 때만 올라옵니다.
4. 게임 루프(`App::run`)를 돌립니다.

윈도우 메시지는 `NativeWindowEvent`로만 받고, 키/마우스 해석은 `InputManager`가 합니다.
셸 단축키는 `EngineLoop` 가 든 셸 전용 `InputMap` 의 `Debug` 레이어(`alwaysOn`, `Resource/engine/input/default.input.xml`)로 묻습니다 —
ReloadShaders=Ctrl+F8(엔진이 처리), ReloadEditor=Ctrl+F6, ReloadGame=Ctrl+F7(`App::pollReloadHotkeys` 가 `EngineLoop::wasDebugActionTriggered` 로 묻는다).
이 리로드 단축키는 **Dev 전용**입니다 — Shipping 에는 리로드할 모듈이 없어 `App::pollReloadHotkeys` 가 통째로 비어 있습니다.

## 헤드리스 실행

창 없이 일만 하고 끝나는 실행입니다. 기동 표의 `Headless` 단계가 명령줄을 보고 뒤 단계(창 · RHI · 렌더러)를 건너뛰며,
일이 실패하면 `App::initialize` 가 false 를 돌려 **종료 코드가 0 이 아닙니다**(`Scripts/generate/CookAssets.py` 가 그것으로 실패를 압니다).

| 인자 | 하는 일 | 누가 |
|---|---|---|
| `--cook-shaders` | 셰이더 쿠킹(`ShaderCookDriver::cookAllShaders`) | 엔진(`Headless` 단계) |
| `--cook-scenes --cooked-dir=<폴더>` | 씬 · 프리팹 · GUID 레지스트리 쿠킹. 입력은 소스 트리(`ContentSource::SourceTree` — 팩을 마운트하지 않는다). 모르는 타입의 컴포넌트(`MissingComponent`)가 든 씬은 실패로 센다 | 엔진(`Headless` 단계) — 모든 타입 공급자가 오른 `ModuleTypes` 뒤라야 쿠킹한다 |
| `--import-textures` / `--check-textures` | 텍스처 임포트 / 원본 · DDS 스탬프 대조만 | App 이 `ModuleHost::importAssetsWithEditorModule( EditorImportKind::Texture, … )` 로 에디터 모듈을 인스턴스 없이 올려 부른다. Dev 전용(Shipping 은 에디터가 없어 실패) |
| `--import-models` / `--check-models` | glTF 모델 임포트 / 원본 · `.mesh` 스탬프 대조만 | 위와 같은 길(`EditorImportKind::Model`) |

## 디렉터리 구조
- **main.cpp**: `App` 을 만들고 `initialize` → `run` → `shutdown`. 초기화가 실패해도 `shutdown` 을 불러 일부만 선 서브시스템을 정해진 순서로 내린다.
- **App.cpp / App.h**: 앱 생명주기. App 이 직접 아는 것은 **모듈 로더 배선·창 소유·프레임 순서·콜백 배선** 네 가지뿐입니다(엔진 기동 · 종료 순서는 `EngineLoop` 의 기동 표).
  같은 파일에 `RHIBackendSwitcher` — `gv_rhiBackend` 변경을 받아 프레임 경계에서 백엔드를 교체합니다(App 만 쓴다).
- **AppConfig.h**: 부팅 때 읽는 설정(올릴 게임플레이 키트). 리플렉션 대상이라 따로 둡니다.
- **FixedTimestep.cpp / .h**: 실시간 경과를 가변 델타와 고정 스텝 수로 나눕니다. AppTest 가 이 파일만 따로 컴파일합니다.
- **Module/**: 모듈의 수명과 빌드.
  - `ModuleHost` — 모듈 이미지 로드(`loadModuleImages`), 에디터 · 게임 인스턴스의 만들기 · 내리기 · API 표 받기(두 모듈이 같은 템플릿 한 벌),
    직렬화를 통한 상태 보존, 태스크 · 렌더 워커 비우기(`drainRenderWorkers`).
  - `ModuleCompiler` — 에디터가 부르는 백그라운드 CMake 빌드(RuntimeAPI `IModuleCompiler`).
  - `LiveReloadManager` — 핫 리로드(Dev 전용, Shipping 에서 파일째 빠진다). 그것만 쓰는 도우미 `ModuleImagePatch`(섀도 복사본 바이트) ·
    `ModuleCallGuard`(새 모듈 코드 호출 가드)도 같은 파일에 있습니다.

쓰는 곳이 하나뿐인 도우미는 그 사용처와 한 파일에 둡니다(핫 리로드 도우미 → `LiveReloadManager`, `RHIBackendSwitcher` → `App`).

## 프레임 순서와 그 이유

`App::run` 의 한 프레임은 아래 순서를 지킵니다. 순서를 바꾸면 조용히 깨지는 자리가 있습니다.

| 단계 | 왜 그 자리인가 |
|---|---|
| `FixedTimestep::advance` | 가변 델타를 잘라내고 이번 프레임의 고정 스텝 수를 확정한다. |
| `EngineLoop::beginFrame` → `ModuleHost::beginFrame` | 에디터 Play 상태를 한 번 래치한다. 고정 스텝이 6번 돌아도 DLL 경계를 다시 넘지 않는다. |
| `pollReloadHotkeys` | (Dev) 셸 액션을 갱신하고 리로드 단축키를 받는다. |
| `fixedUpdateGame` × N → `updateGame` | 래치된 상태를 읽으므로 모든 스텝이 같은 답을 본다. |
| `ModuleHost::updateEditorUi` | 에디터가 이번 프레임 입력을 처리한 **뒤** 게임 뷰포트 RT 와 씬 틱 여부를 확정한다. 이 질의를 앞으로 옮기면 **Step 한 칸이 틱 없이 소비**된다. |
| `LiveReloadManager::update` | (Dev) 모듈 교체는 **틱 직전**에 한다 — 여기서 DLL 이 바뀌고 인스턴스가 새로 만들어진다. |
| `EngineLoop::tick` → `ModuleHost::endEditorFrame` | 래치된 프레임 상태(`ModuleFrameState`)를 그대로 넘긴다. 뷰 카메라는 tick 내부에서 지연 조회한다 — 미리 잡으면 씬 전환/핫리로드가 파괴한 객체를 역참조한다. |
| `RHIBackendSwitcher::applyIfPending` → `EngineLoop::endFrame` | 백엔드 교체는 프레임 경계에서만 한다. |

## 시간 정책

고정 스텝 길이와 프레임당 상한은 `EngineConfig` 가 들고 있습니다 (`_fixedDeltaTime`,
`_maxFixedStepPerFrame`, `_maxFrameDeltaTime`). 상한이 **반드시** 필요합니다 — 없으면 느린
프레임이 더 많은 스텝을 부르고 그래서 더 느려지는 되먹임(고정 스텝 스파이럴)에 빠집니다.
상한을 넘긴 잔여 시간은 버립니다: 시뮬레이션이 실시간보다 느려지는 쪽을 택합니다.

게임 시간 배율(`GameTimeScale` — `gv_timeScale`)은 최대 델타로 자른 **뒤에** 곱합니다(`FixedTimestep::advance( timeScale )`). 그래서 고정
스텝 수도 따라 늘고 줄며(상한은 그대로), 0 이면 게임 시간이 멈춥니다.

## 게임 창 개발 콘솔(Dev 전용)

에디터 없이 띄우면 `~` 로 게임 창 위의 개발 콘솔(`DevConsoleOverlay`)을 엽니다 — 명령 · `gv_이름 [값]` 을 치고 Enter, Tab 자동완성, ↑↓ 기록,
Esc · `~` 로 닫습니다. 열려 있는 동안 키보드는 게임 입력으로 넘기지 않습니다(`App::onWindowMessage` 가 먼저 묻는다). 에디터가 있으면 Output Log 의
입력 줄이 같은 콘솔입니다. `-gv_devConsoleExec="timescale 0.5;gv_viewMode 2"` 는 시작 씬이 열린 뒤 명령을 돌리고(에디터가 있어도),
`-gv_devConsoleOpen=1` 은 연 채로 시작합니다. Shipping 에는 없습니다.

## 모듈을 내리는 경로는 하나다

종료·핫리로드·RHI 핫스왑은 모두 `ModuleHost::suspendModules( ModuleScope, bReleaseApiTable )`
하나를 지나갑니다. "워커 배수 → 상태 보존 → 파괴" 순서를 사유마다 따로 조립하면 한 곳만
고쳐 놓고 나머지를 잊습니다. 새 재생성 사유(디바이스 상실, 어댑터 변경 등)가 생기면
`ModuleScope` 와 호출 한 줄만 늘립니다.

## ⚠️ 핵심 규칙
- `App` 폴더 내부는 게임 루프의 시작점일 뿐, 복잡한 로직을 담는 곳이 아닙니다. 새로운 시스템을 추가해야 한다면 `App`이 아니라 `Engine` 폴더를 고려하세요.
- 프레임 중에 에디터 상태를 **다시 묻지 마세요.** `ModuleHost::getFrameState()` 가 이번 프레임의 답입니다.
