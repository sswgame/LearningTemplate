# 엔진 분할 계획

`Engine.dll` 하나를 티어 경계로 나누고, 그 전에 의존 방향의 약한 고리를 푼다. 폴더 의존은 이미 DAG 이고 `CheckEngineLayers.py` 가 지킨다.
막힌 곳은 링크 단위가 하나라는 점과, 컴포넌트 모델(`Object`)이 기능 폴더 다섯에 묶여 있다는 점이다.

## 측정한 현재 상태 (2026-10-10, `Source/Engine` 의 `#include "Engine/…"` 수)

| 방향 | include 수 | 파일 수 | 대표 헤더 |
|---|---|---|---|
| Object → Graphics | 51 | 21 | `Mesh.h` · `GpuLight.h` · `ShaderBindingSlots.h` · `MaterialInstance.h` |
| Object → Physics | 41 | 22 | `IPhysicsScene.h` · `PhysicsTypes.h` · `AABB.h` |
| Object → Animation | 38 | 22 | `Skeleton.h` · `Pose.h` · `AnimClip.h` |
| Object → Audio | 11 | 8 | `AudioSpatial.h` · `AudioEngine.h` |
| Object → Navigation | 8 | 4 | `NavMeshGeometry.h` · `INavMesh.h` |
| Graphics → Animation | 3 | 1 | `Skeleton.h` · `Pose.h` · `AnimClip.h` |
| Graphics → Physics | 1 | 1 | `PhysicsDebugDraw.h` |
| Resource → Animation | 5 | 2 | `AnimClip.h` · `RigAsset.h` |
| Scene → Graphics | 3 | 2 | `MaterialCache.h` · `IRHIDevice.h` |
| Automation → UI / UI → Automation | 2 / 3 | 1 / 1 | `UiSystem.h` / `AutomationRunner.h` — 게이트가 보고하는 유일한 사이클 |

- `Object` 안에서 기능 의존이 몰린 곳은 `Component/{2D, 3D, Audio, Navigation, Physics}`(기능 컴포넌트)와 `GameObject/`(Graphics 15 · Physics 10 · Navigation 5 · Audio 2)다.
  `GameObject` 가 기능 폴더를 직접 아는 것이 진짜 문제다 — 컴포넌트 모델의 코어가 렌더 · 물리 · 내비를 include 한다.
- `Engine/` 은 소스 559개 · 헤더 포함 약 1,160개, Debug `Engine.dll` 은 약 18 MB 다. 재링크 시간은 아직 재지 않았다(아래 0 단계).
- 게이트 티어 표가 실제와 다르다: `Text`(표 5, 실제 3), `DevTools`(표 7, 실제 8).

## 단계

### 0. 기준 측정 (먼저)
- Engine 안에서 `Graphics` 한 파일 · `UI` 한 파일 · `Object` 한 파일을 고쳤을 때 Release 증분 빌드 시간(컴파일 + 재링크)을 3회씩 잰다. 표는 [검증과 측정](../08_Verification.md) 형식.
- 분할 뒤 같은 세 경우와 비교한다. 이득이 재링크 시간에서 나오지 않으면 3 단계(DLL 승격)는 하지 않는다.

### 0-2. Core 층 정리 (분할의 선행 조건)
Core 폴더 13개(`Common` · `Concurrency` · `Container` · `Delegate` · `File` · `Log` · `Math` · `Memory` · `Module` · `Process` · `String` · `Task` · `Time`)가 하나의 순환 묶음이다(폴더 간 `#include "Core/…"` 분석, 2026-10-10).
Engine 에는 티어 게이트가 있는데 Core 에는 `Network` 내부 방향을 보는 `CheckCoreNetworkLayers` 뿐이다.
밑에서 위로 `Common` → `Math` · `Time` · `Memory` → `Container` · `String` → `Concurrency` · `Delegate` · `Log` · `Event` → `File` · `Process` · `Module` · `Task` → `Compression` · `CommandLine` · `GlobalVariable` · `Uuid` → `Network` 순으로 놓으면(제안, 확정 아님) 거꾸로 가는 include 가 48 건 남는다.

| 거꾸로 가는 방향 | 건수 | 예 | 풀이 |
|---|---|---|---|
| Container → Concurrency | 12 (전부 헤더) | `array.h` · `deque.h` · `map.h` 의 `DataRaceDetector.h`, `PagedArray.h` 의 `atomic.h` | 경합 검출 훅을 컨테이너가 의존하지 않는 가벼운 헤더(`DataRaceHook.h`)로 가르고, `atomic.h` · `mutex.h` 같은 원시 래퍼는 한 층 아래(Concurrency/Primitives)로 |
| Memory → Concurrency / Container | 9 / 6 | `LinearAllocator.h` 의 `atomic.h` · `mutex.h`, `MemoryProfiler.h` 의 `vector.h` · `string.h` | 원시 래퍼를 아래로 내리면 앞의 것이 풀린다. `MemoryProfiler` 는 컨테이너를 쓰므로 `Memory` 를 둘로(할당기 / 프로파일러) |
| Common → Container | 3 | `TopologicalSortUtil.h` · `VarIntUtil.h` 의 `vector.h` · `string.h` | 두 도우미를 `Container` 쪽(또는 `Utility`)으로 옮긴다 |
| Concurrency → Process | 3 | 검출기의 `CallStackCapture.h` | 호출 스택 수집을 `Process` 에서 `Common/Diagnostics` 로 내리거나 콜백으로 주입 |
| Log → File / Process / Module | 5 (전부 .cpp) | `FileLogOutput.cpp` 의 `FileUtil.h`, `Logger.cpp` 의 `CrashHandler.h` | 출력 장치(`FileLogOutput`)를 로거 코어와 분리해 `File` 위층에 두고 등록받기 |
| String → Log / Concurrency | 4 (헤더) | `hashed_string.h` · `fixed_string.h` 의 `Logger.h` · `atomic.h` · `mutex.h` | 로그 매크로의 최소 헤더(`LogMacros.h`)를 `Common` 으로 내린다 |
| Event → Module, Time → Log, Memory → String/Log/Process | 1~3씩 | `GameTimer.h` 의 `Logger.h` | 같은 방법 |

- 대부분 헤더의 가벼운 래퍼 몇 개를 아래층으로 내리는 일이다 — 설계 변경이 필요한 곳은 `Memory`(할당기 / 프로파일러 분리)와 `Log`(코어 / 출력 장치 분리) 둘이다.
- 끝에 `CheckCoreLayers` 게이트(티어 표 + 거꾸로 가는 include 실패)를 둔다. Engine 의 `_kEngineTier` 와 같은 구조.
- `Core/Network`(59 파일, Core 의 21%)는 순환에 끼지 않는다. 다른 Core 폴더는 Network 를 include 하지 않는다. 순환을 푼 뒤 별도 정적 라이브러리로 빼서 `ReflectionParser` 가 링크하지 않게 할 수 있다(파서가 Network 를 쓰지 않는지 먼저 확인).

### 0-3. Engine 폴더 재배치 (분할 전, 의존 정리의 일부)
폴더 구조를 다시 본 결과(2026-10-10) 같은 개념이 여러 폴더에 흩어진 곳이 있다. 이동은 `git mv` + include 일괄 치환이고, 헤더가 옮겨지면 reconfigure 로 코드젠을 다시 만든다.

| 순서 | 일 | 근거 |
|---|---|---|
| 1 | 기능 캐시를 자기 폴더로: `AnimationAssetCache` · `SpriteClipCache` → `Animation`, `LocalizationReloadCache` → `Localization` | `Resource → Animation` 5 건이 사라진다 |
| 2 | `Graphics/Renderer` → `Engine/Renderer` | 한 폴더 이름이 티어 5 와 8 에 걸친다(티어 표에 `Graphics(Renderer 제외)` 를 따로 적어야 한다) |
| 3 | 애니메이션 정리: 알림이 `Animation/AnimNotifyPhase` · `Character/AnimNotify` · `Object/Animation/AnimNotifyListener` 세 곳, 포즈가 `Animation/Pose.h` · `Character/Pose` 두 곳 | 데이터는 `Animation`, 컴포넌트·시스템은 `Object/Animation` 에 둔다. 먼저 이름 충돌부터 |
| 4 | `Common/IRenderSurface.h` → `Window`(또는 `Renderer`), `EngineServices` 는 위층으로. `Config/RHIBackendType.h` → `Graphics/RHI`, `ServerConfig` · `ServerSecret` → 서버 쪽 | 티어 0 · 3 폴더에 위층 개념이 들어 있다 |
| 5 | `Utility` 해체: `Xml` · `Json` → `Serialization`, `TileMap` → `Spatial`(`Environment/Placement` 와 겹침 확인), `Profiling` · `Console` → `DevTools` 후보, 루트 10 개는 이름을 보고 | 기능 모음 통 |
| 6 | 이름 정리: `Engine/Network`(보안·OpenSSL 뿐, `Core/Network` 와 이름이 같다) → `Security` 후보, HTTP(`Telemetry/HttpClient` · `Observability/OpsHttpEndpoint`)는 한 곳, `UI/Screen` + `UI/Screens` 병합 | |
| 7 | 큰 평평한 폴더를 하위로: `Physics` 루트 27, `Resource` 34, `Animation` 루트 29, `Input` 루트 23 | 마지막 |

각 순서는 독립 커밋이며 끝날 때 `RunEngineLayerGraph.py` 로 티어 표를 다시 맞춘다. 0-2(Core 층 정리) 뒤, 1 단계 앞에 한다.

### 0-4. ModuleHost 를 공통 부분과 App 전용 부분으로 (폴더 재배치 뒤)
`Server` 가 `ModuleHost` 를 쓰는 이유는 정당하다 — 서버 대상 모듈 올리기(`ModuleCatalogLoader`), 게임 API 표를 바인딩해 게임 인스턴스 만들기, Dev 핫 리로드를 App 과 같은 코드로 하지 않으면 게임 쪽을 두 벌 짜야 한다.
어색한 것은 한 클래스에 서버가 쓰지 않는 몫이 같이 있다는 점이다: `ModuleHost.h` 에 "editor" 가 35 번, `ModuleHost.cpp` 에 115 번이고, 에디터 인스턴스 바인딩 · 에디터 리로드 · `updateEditorUi` · `onWindowMessage` · RHI 교체(`reinitializeAfterRhiSwap`)가 서버 실행 파일에도 링크된다.
서버 모드는 `_bDedicatedServer` 플래그와 `initializeDedicatedServer` 로 갈라 두었다.

- 공통 `ModuleHost`: 카탈로그 해석, 이미지 올리기 · 내리기, 게임 API 바인딩 · 게임 업데이트 · 고정 업데이트, 게임 쪽 핫 리로드(`onBeforeGameReload` · `onAfterGameReload` · `onBeforeGameplayDllReload` …), 서버 시작(`initializeDedicatedServer`).
- App 전용(`ModuleHost` 위에 얹는 클래스 — 이름 후보 `EditorModuleHost`): 에디터 인스턴스 · API 바인딩, 에디터 리로드와 오류 처리, `updateEditorUi` · `endEditorFrame` · `onWindowMessage`, RHI 교체 후 재초기화, 원본 임포트(`importAssetsWithEditorModule`).
- 갈라진 뒤 `Server` 는 공통 부분만 링크하고, `_bDedicatedServer` 모드 플래그는 사라진다. 동작은 바뀌지 않는다.
- 확인: 서버 · App 의 모듈 올리기 · 핫 리로드 · 임포트 시나리오(`ModuleHost` 시험, `SmokeTest`), 서버 Shipping 에 에디터 심볼이 없음(링크 맵).
- 위험: 핫 리로드의 모듈 범위(`ModuleScope`)와 에디터 · 게임 이미지 쌍을 함께 다루는 `onBeforeCommitBatch` · `suspendModules` 가 두 클래스에 걸친다. 의존 방향은 App 전용 → 공통 하나뿐이어야 한다.

### 1. 작은 고리 풀기 (낮은 위험)
- 게이트 티어 표를 실제에 맞춘다(`Text` · `DevTools`). `RunEngineLayerGraph.py` 출력이 기준.
- `Automation ↔ UI`: 공유 타입(단계 레지스트리 · 시나리오 인터페이스)을 `Automation` 쪽 인터페이스로 두고 `UI` 가 단계를 등록하게 한다 — `Automation` 은 `UI` 를 include 하지 않는다.
- `Graphics → Animation` 3 건: 스키닝 입력(`Skeleton` · `Pose` · `AnimClip`)을 GPU 쪽 중립 타입으로 받게 한다. `Graphics → Physics` 1 건(`PhysicsDebugDraw`): 디버그 선 입력을 `Common` 의 선 목록 타입으로.
- `Resource → Animation` 5 건: 에셋 종류 등록을 `Animation` 이 `Resource` 에 하게 뒤집는다(레지스트리 방향 반전).

### 2. 컴포넌트 모델을 코어와 기능으로 가르기 (중간 위험, 가장 큰 효과)
- `Object` 를 둘로: **코어**(`GameObject` · `Prefab` · `Component` 기반 클래스 · 리플렉션 훅)와 **기능 컴포넌트**(`Component/{2D,3D,Audio,Navigation,Physics}` · `Object/Animation`).
- 코어는 `Graphics` · `Physics` · `Navigation` · `Audio` 를 include 하지 않는다. `GameObject` 의 기능 의존 32 건(위 표)은 기능 컴포넌트가 코어에 등록하는 방향으로 뒤집는다:
  Transform 변경 알림 · 바운드 질의 · 씬 등록 모두 `GameObject` 가 인터페이스(`ISceneListener` 꼴)로 듣는다.
- 기능 컴포넌트는 코어 위, 각자의 기능 폴더 아래에 둔다. 폴더 이동은 `git mv` 후 코드젠을 다시 만든다(헤더 이동은 .gen.cpp 를 썩힌다 — 메모리 `moved-header-stale-codegen`).
- 확인: `RunEngineLayerGraph.py` 에서 코어 폴더가 기능 폴더를 가리키는 화살표가 0.

### 3. 링크 단위로 승격
- 먼저 `OBJECT` 라이브러리로 나눈다(DLL 은 하나, 위험 낮음). 층: Foundation(티어 0~2) · Data(3~4) · Render(5, Renderer, Environment) · World(6~7 코어) · Features(기능 컴포넌트) · Tools(UI · Telemetry · DevTools · Automation · Module) · 루트(EngineLoop).
- 0 단계 측정으로 이득이 확인되면 Dev 만 층마다 DLL 로 올린다. 내보내기 매크로는 층별로 늘리지 말고 `SW_API` 하나를 유지하되 각 DLL 의 `SW_EXPORTS` 를 층 전체에 건다.
- 리플렉션 등록기 보존: `sw_linkWholeArchive` 를 층 라이브러리마다 건다(Shipping 정적 링크는 지금과 같다). 서버 타깃은 Render 층을 링크하지 않는다.

### 4. 월드를 값으로
- `World` 가 씬 · 물리 월드 · 내비 · GpuScene 빌더 · 틱 설정을 소유하고 `EngineLoop` 는 월드 목록을 돈다. 프리팹 격리 · 머티리얼 미리보기 · 에디터 툴 창 · 서버의 방이 이 위에 선다(백로그 "다중 월드").
- 2 단계 뒤에 한다 — 월드가 코어 위의 소유자가 되려면 코어가 기능을 모르는 상태여야 한다.

### 5. 모듈 확장 창구와 상태 규칙
- 모듈이 서비스 · 렌더 패스 · 레지스트리를 등록하는 공식 창구. 리로드되는 모듈의 변경 가능한 정적 변수를 막는 게이트(허용 목록 + 이유)와, `GameDataCache` · `SqlDriverRegistry` · `LocalStoreFactory` 의 등록 표를 서비스로 올리는 일.

## 건드리지 않는 것
- 키트 DLL 병합(핫 리로드 단위와 키트별 켜기 · 끄기가 깨진다). 산출물 폴더 정리는 별도 진행.
- 임포터의 위치 — 에디터 모듈의 일이다(`ModuleHost::importAssetsWithEditorModule`).

## 순서와 위험

| 단계 | 규모 | 위험 | 확인 |
|---|---|---|---|
| 0 측정 | 작음 | 없음 | 증분 시간 표 |
| 1 작은 고리 | 작음 | 낮음 | `RunEngineLayerGraph` 사이클 0 · 티어 표 일치 |
| 2 코어 / 기능 가르기 | 중간 | 중간 | 코어 → 기능 화살표 0 · EngineTest · 씬 로드 · 에디터 시나리오 |
| 3 링크 분할 | 중간 | 낮음(OBJECT) / 중간(DLL) | 네 백엔드 × Dev · Shipping · Server 빌드 |
| 4 월드 | 큼 | 높음 | 프리팹 격리 · 서버 방 시나리오 |
| 5 모듈 창구 | 중간 | 낮음 | 게이트 + 핫 리로드 시험 |

각 단계는 독립 커밋 묶음이고, 단계가 끝날 때마다 전 프리셋 컴파일과 `ctest -L lint · nogpu` 를 돈다(검증 보류 지시가 풀린 뒤).
