# AppHost (모듈 호스트)

## 이것은 무엇이고 왜 있나

`AppHost` 는 플레이어 실행 파일 `App` 과 전용 서버 실행 파일 `Server` 가 같이 링크하는 정적 라이브러리입니다.
모듈 매니페스트를 해석하고, 게임 · 키트 · 에디터 모듈을 올리고 내리고, 개발 빌드에서 모듈을 핫 리로드하고, 프레임 시간을 고정 스텝으로 나눕니다.
두 실행 파일이 같은 코드로 모듈을 다뤄야 하므로 실행 파일 폴더 밖에 따로 둡니다. AppHost 는 `Engine` 과 `RuntimeAPI` 만 알고, `App` 과 `Server` 를 모릅니다.

App은 이번 빌드에 포함된 모든 타깃의 모듈을 로드하고(`ModuleCatalog::getBuildTargetMask`), Server는 `Server` 타깃의 모듈만 로드합니다.
언리얼에 비유하면 `ModuleHost` 는 `FModuleManager`, `LiveReloadManager` 는 Live Coding 에 해당합니다.

## 머릿속 그림

**모듈 이미지와 인스턴스.** 모듈 이미지는 메모리에 로드한 DLL이고, 인스턴스는 그 DLL이 만든 게임 객체나 에디터 객체입니다.
엔진이 시작될 때 `ModuleTypes` 단계에서 이미지를 먼저 로드해 리플렉션 타입만 등록하고, 인스턴스는 엔진 시작이 끝난 뒤에 만듭니다.
씬은 `ModuleTypes` 단계 뒤에만 읽으므로, 씬 파일에 적힌 게임 컴포넌트 타입을 항상 찾을 수 있습니다.

**ModuleHost** 는 게임과 에디터 인스턴스를 만들고, 정리하고, 그 API 테이블을 받습니다. 게임과 에디터는 같은 템플릿 코드로 다룹니다.
상태를 직렬화해 보존하는 일과, 모듈을 언로드하기 전에 태스크와 렌더 워커를 비우는 일(`drainRenderWorkers`)도 ModuleHost가 합니다.

**LiveReloadManager** 는 개발 빌드의 핫 리로드를 맡습니다. Shipping 빌드에서는 파일째 빠집니다.
섀도 복사본의 바이트를 고치는 `ModuleImagePatch`, 새 모듈 코드 호출을 감싸는 `ModuleCallGuard`, 복사본 이름을 짓는 `ShadowCopyName` 은 각자 파일에 있고 함께 빠집니다.

**FixedTimestep** 은 실제 경과 시간을 가변 델타와 고정 스텝 수로 나눕니다. `AppTest` 가 이 파일만 따로 컴파일해서 테스트합니다.

## 따라 해 보기 — 게임 모듈을 핫 리로드하기

```powershell
cd build/Ninja-Debug/Bin
./App.exe -gv_reloadGameAtFrame=60 -gv_profileFrames=120
```

60번째 프레임에 게임 모듈(`SWGame`) 리로드를 한 번 요청하고, 120 프레임 뒤에 스스로 끝납니다.
로그의 `[ReloadProbe]` 줄 앞뒤로 리로드 전후의 상태가 남습니다. 리로드 단축키(Ctrl+F7)와 같은 경로를 탑니다.

## 작동 원리

### 시간 정책

고정 스텝 길이와 프레임당 상한은 `EngineConfig` 의 `_fixedDeltaTime`, `_maxFixedStepPerFrame`, `_maxFrameDeltaTime` 이 정합니다.
상한은 꼭 필요합니다. 상한이 없으면 느린 프레임이 고정 스텝을 더 많이 부르고, 그래서 다음 프레임이 더 느려지는 악순환에 빠집니다.
상한을 넘은 시간은 버립니다. 실시간을 따라잡는 대신 시뮬레이션이 실시간보다 느려지는 쪽을 택한 것입니다.

게임 시간 배율(`gv_timeScale`)은 최대 델타로 자른 뒤에 곱합니다(`FixedTimestep::advance`). 그래서 고정 스텝 수도 배율을 따라 늘고 줄며, 상한은 그대로입니다. 배율이 0이면 게임 시간이 멈춥니다.
App이 넘기는 `-gv_fixedFrameDelta=<초>` 를 주면 벽시계 대신 매 프레임 그 시간만큼 흐릅니다. 자동화 시나리오나 픽셀 비교처럼 결과가 매번 같아야 하는 실행에 씁니다.

### 모듈을 언로드하는 경로는 하나입니다

종료, 핫 리로드, 그래픽 API 교체는 모두 `ModuleHost::suspendModules( ModuleScope, bReleaseApiTable )` 하나를 거칩니다.
이 함수는 워커를 비우고, 상태를 보존하고, 인스턴스를 파괴하는 순서를 지킵니다. 이 순서를 이유마다 따로 조립하면 한 곳만 고치고 나머지를 잊게 됩니다.

## 확장하는 법

**모듈을 다시 만들어야 하는 새 이유가 생길 때**(디바이스 상실, 어댑터 변경 등)

1. `ModuleScope` 에 값을 하나 더합니다.
2. 그 이유가 생기는 곳에서 `ModuleHost::suspendModules` 를 한 번 부릅니다. 워커를 비우고 상태를 보존하는 순서를 직접 조립하지 않습니다.

## 함정과 주의

**AppHost 는 실행 파일을 모릅니다.** `App/` · `Server/` 를 include 하지 않고, 실행 파일 쪽 정책은 콜백이나 인자로 받습니다(`CheckEngineLayers`).

**프레임 중간에 에디터 상태를 다시 묻지 않습니다.** 이번 프레임의 답은 `ModuleHost::getFrameState()` 입니다. 중간에 다시 물으면 고정 스텝마다 다른 답을 볼 수 있습니다.

**리로드를 거절할 이유는 옛 이미지를 언로드하기 전에 확인합니다.** `LiveReloadManager::setOnValidateImage` 는 ABI와 API 테이블을 검사합니다.
배치 콜백(`OnBeforeCommitBatchDelegate`)이 false를 반환하면 아무것도 언로드하지 않습니다. 게임 상태 저장이 실패한 경우도 여기에 포함됩니다.
이렇게 적용 전에 실패하면 옛 모듈이 계속 돕니다. 적용한 뒤의 결함만 `markGraphBroken` 으로 처리합니다. 언리얼의 Live Coding도 같은 방식입니다.

**모듈 리로드는 호스트가 맡습니다.** Engine에는 `IModuleHandleProvider` 인터페이스만 두고, 공개 헤더에 리로드 콜백을 두지 않습니다.
`LiveReloadManager` 와 그 도우미 셋은 Shipping 빌드에서 빠집니다(`Source/AppHost/CMakeLists.txt` 의 제외 목록). Shipping은 모듈을 언로드하지 않습니다. 이 동작은 SmokeTest가 검증합니다.

**핫 리로드는 원본 DLL이 아니라 섀도 복사본을 로드합니다.** 원본 파일을 잠그지 않아야 빌드가 그 파일을 덮어쓸 수 있기 때문입니다.
Windows에서는 지연 로드 훅(`DelayLoadNotifyHook.cpp`)이 `GameFramework.dll` 의 import를 현재 복사본으로 돌립니다. 이 훅을 빼면 원본이 한 번 더 로드되어 정적 상태가 두 벌이 됩니다.
Linux에서는 SONAME을 같은 길이의 이름으로 바꿔 씁니다(`ModuleImagePatch`). 모듈이 올바른 이미지에 연결됐는지는 `verifyModuleBindings` 가 확인합니다.

**옛 이미지는 바로 언로드하지 않습니다.** `deferImageUnload` 가 최근 4번의 리로드 배치를 남겨 두고, 배치 안에서는 의존하는 쪽부터 언로드합니다.
다른 코드가 구독 중인 채널을 만든 이미지는 프로세스가 끝날 때까지 남겨 둡니다. 언리얼도 같은 방식이고, 되돌리면 구독자가 언로드된 코드를 부르게 됩니다.

**`ModuleCallGuard` 는 `onAfterReload` 호출 하나만 보호합니다.** Windows SEH로 잡는 예외는 접근 위반, 잘못된 명령, 0으로 나누기뿐입니다.
예외를 잡은 뒤의 프로세스는 온전하지 않습니다. 이 장치는 작업을 저장하고 재시작할 시간을 벌어 주는 것이 목적입니다.

## 더 볼 곳

- [App](../App/README.md): 이 라이브러리를 쓰는 플레이어 실행 파일과 한 프레임의 순서
- [Server](../Server/README.md): 같은 라이브러리를 쓰는 전용 서버
- [핫 리로드와 C-ABI](../../docs/03_LiveReload_and_ABI.md): 리로드가 도는 순서와 지켜야 할 규칙
- [Module](../Engine/Module/README.md): 모듈 매니페스트와 타입 등록

| 파일 | 내용 |
|---|---|
| `ModuleHost.cpp` | 모듈 인스턴스 관리 |
| `ModuleCatalogLoader.cpp` | 모듈 매니페스트 읽기와 해석 |
| `LiveReloadManager.cpp` | 핫 리로드 |
| `ModuleImagePatch.cpp` | 섀도 복사본의 ABI 도장 · SONAME 바이트 |
| `ModuleCallGuard.cpp` | 새 모듈 코드 호출의 하드웨어 예외 가드 |
| `ShadowCopyName.cpp` | 섀도 복사본 이름과 남은 복사본 정리 |
| `ModuleCompiler.cpp` | 에디터가 부르는 백그라운드 CMake 빌드 |
| `FixedTimestep.cpp` | 고정 스텝 계산 |
