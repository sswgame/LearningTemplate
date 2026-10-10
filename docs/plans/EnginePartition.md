# 엔진 분할 계획

`Engine.dll` 하나를 티어 경계로 나누고, 그 전에 의존 방향의 약한 고리를 푼다. 폴더 의존은 이미 DAG 이고 `CheckEngineLayers.py` 가 지킨다.
막힌 곳은 링크 단위가 하나라는 점과, 컴포넌트 모델(`Object`)이 기능 폴더 다섯에 묶여 있다는 점이다.

## 측정한 현재 상태 (2026-10-10, `Source/Engine` 의 `#include "Engine/…"` 수)

| 방향 | include 수 | 파일 수 | 대표 헤더 |
|---|---|---|---|
| Object → Graphics | 51 | 21 | `Mesh.h` · `GPULight.h` · `ShaderBindingSlots.h` · `MaterialInstance.h` |
| Object → Physics | 41 | 22 | `IPhysicsScene.h` · `PhysicsTypes.h` · `AABB.h` |
| Object → Animation | 38 | 22 | `Skeleton.h` · `Pose.h` · `AnimClip.h` |
| Object → Audio | 11 | 8 | `AudioSpatial.h` · `AudioEngine.h` |
| Object → Navigation | 8 | 4 | `NavMeshGeometry.h` · `INavMesh.h` |
| Graphics → Animation | 3 | 1 | `Skeleton.h` · `Pose.h` · `AnimClip.h` |
| Graphics → Physics | 1 | 1 | `PhysicsDebugDraw.h` |
| Resource → Animation | 5 | 2 | `AnimClip.h` · `RigAsset.h` |
| Scene → Graphics | 3 | 2 | `MaterialCache.h` · `IRHIDevice.h` |
| Automation → UI / UI → Automation | 2 / 3 | 1 / 1 | `UISystem.h` / `AutomationRunner.h` — 게이트가 보고하는 유일한 사이클 |

- `Object` 안에서 기능 의존이 몰린 곳은 `Component/{2D, 3D, Audio, Navigation, Physics}`(기능 컴포넌트)와 `GameObject/`(Graphics 15 · Physics 10 · Navigation 5 · Audio 2)다.
  `GameObject` 가 기능 폴더를 직접 아는 것이 진짜 문제다 — 컴포넌트 모델의 코어가 렌더 · 물리 · 내비를 include 한다.
- `Engine/` 은 소스 559개 · 헤더 포함 약 1,160개, Debug `Engine.dll` 은 약 18 MB 다. 재링크 시간은 아직 재지 않았다(아래 0 단계).
- 게이트 티어 표가 실제와 다르다: `Text`(표 5, 실제 3).

## 단계

### 0. 기준 측정 (먼저)
- Engine 안에서 `Graphics` 한 파일 · `UI` 한 파일 · `Object` 한 파일을 고쳤을 때 Release 증분 빌드 시간(컴파일 + 재링크)을 3회씩 잰다. 표는 [검증과 측정](../08_Verification.md) 형식.
- 분할 뒤 같은 세 경우와 비교한다. 이득이 재링크 시간에서 나오지 않으면 3 단계(DLL 승격)는 하지 않는다.

### 0-2. Core 층 정리 (분할의 선행 조건)
Core 폴더 13개(`Common` · `Concurrency` · `Container` · `Delegate` · `File` · `Log` · `Math` · `Memory` · `Module` · `Process` · `String` · `Task` · `Time`)가 하나의 순환 묶음이다.
`Scripts/lint/report/RunCoreLayerGraph.py` 가 `.cpp` 를 포함한 파일 단위 간선을 전수로 뽑는다(2026-10-10). 거꾸로 가는 include 가 가장 적은 순서를 찾아도 38 건이 남는다.
건수만 줄이는 순서(`Concurrency` 를 `Memory` · `Container` 아래에 두는 쪽)를 그대로 쓰지 않고, 아래 순서로 확정한다. 남는 간선이 설계 문제 몇 개로 모이기 때문이다.

**확정한 층(밑에서 위로, 같은 줄은 서로 include 하지 않는다):**
`Common` · `Predefined` → `Math` · `Concurrency`(원시 동기화: `atomic` · `mutex` · `SpinLock` · `Futex` · 경합 검출 훅) → `Memory`(할당기와 메모리 태그) → `Container`(+ `StringUtil` · `formatString` · 동시 큐) · `UUID` → `Delegate` → `Log`(로그 파사드와 매크로) → `Time` · `String`(이름 · 고정 문자열) · `Process`(프로세스 · 스레드 크래시 스택) · `Compression` · `CommandLine` → `Task` · `GlobalVariable` → `File` → `Module` → `Diagnostics`(호출 스택 · 크래시 · 메모리 프로파일러 · 교착 · 경합 보고) · `Event` → `LogSink`(기본 싱크와 출력 장치) → `Network`.

| 거꾸로 가는 방향 | 건수 | 예 | 풀이 |
|---|---|---|---|
| Common → Container | 3 | `TopologicalSortUtil.h` · `VarIntUtil.h` 의 `vector.h` | 두 도우미를 `Container` 로 옮긴다 |
| Concurrency → Container · Memory · String · Log · Process | 14 | `mutex.h` → `DeadlockDetector.h`, `DataRaceDetector.cpp` → `CallStackCapture.h`, `ConcurrentQueue.h` → `vector.h` | `mutex` 는 관찰자 인터페이스(`ILockObserver`)만 알고 `DeadlockDetector` 는 `Diagnostics` 로. 경합 보고는 함수 포인터로 받는다(`DataRaceReporter`). `ConcurrentQueue` · `WorkStealingDeque` 는 `Container`, `LockFreeObjectPool` 은 `Task` 로 |
| Memory → Container · String · Log · Process | 10 | `Memory.cpp` → `MemoryProfiler.h`, `FrameArenaAllocator.h` → `vector.h` | 스레드의 현재 태그와 `ScopedMemoryTag` 는 `Memory` 가 갖고, 프로파일러는 할당 기록기 인터페이스(`IAllocationTracker`)로 걸린다. `MemoryProfiler` 는 `Diagnostics`, `FrameArenaAllocator` 는 `Container` 로 |
| String → Log | 2 | `hashed_string.h` · `fixed_string.h` → `Logger.h` | `Log` 가 쓰는 `StringUtil` · `formatString` 을 `Container`(문자열 타입 옆)로 옮겨 `String` 을 `Log` 위에 둔다 |
| Log → File · Process · Module | 5 | `FileLogOutput.cpp` → `FileUtil.h`, `Logger.cpp` → `CrashHandler.h` | `Logger` 는 정적 파사드(전역 싱크 · 상세도 · 호출자 표)만 남고, 기본 싱크(`AsyncLogSink`)와 출력 장치는 `LogSink` 로 |
| Process → File · Module | 3 | `WindowsCallStackCapture.cpp` → `FileUtil.h` · `ModuleImageUtil.h`, `ModuleBuildId.cpp` → `FileUtil.h` | 작업 스레드가 부르는 크래시 스택 준비(`ThreadCrashStack`)만 `Process` 에 남기고 호출 스택 · 크래시 보고는 `Diagnostics` 로, `ModuleBuildId` 는 `Module` 로 |
| Time → Log | 1 | `GameTimer.h` → `Logger.h` | `Time` 을 `Log` 위에 둔다(`Log` 파사드는 시계를 쓰지 않는다) |
| 폴더 → 루트 모음 헤더(38 건 밖) | 3 | `LinearAllocator.cpp` 등의 `CoreMinimal.h` | 쓰는 헤더를 직접 include 한다 |

- 경합 보고기는 `Diagnostics/DataRaceReporter.cpp` 의 정적 등록으로 걸린다. `Core` STATIC 을 링크하는 `ReflectionParser` 는 그 목적 파일이 빠질 수 있어 진입점에서 `DataRaceReporter::install()` 을 부른다.
- 끝에 `CheckCoreLayers` 게이트(티어 표 + 거꾸로 가는 include 실패)를 둔다. Engine 의 `_kEngineTier` 와 같은 구조.
- `Core/Network`(59 파일, Core 의 21%)는 순환에 끼지 않는다. 다른 Core 폴더는 Network 를 include 하지 않는다. 순환을 푼 뒤 별도 정적 라이브러리로 빼서 `ReflectionParser` 가 링크하지 않게 할 수 있다(파서가 Network 를 쓰지 않는지 먼저 확인).

### 0-3. Engine 폴더 재배치 (분할 전, 의존 정리의 일부)
1 ~ 7 은 끝났다(2026-10-10). 이동 표와 include · 문서 치환은 `Scripts/dev/MoveEngineFolders.py`(단계별, 다시 돌릴 수 있다)이고,
`--sync-tier` 가 게이트 표와 `Source/Engine/README.md` 티어 표를 include 그래프 계산값으로 맞춘다. 옛 경로는 [결정 기록](../09_Decisions.md) 4 절,
옮기지 않기로 한 것(`EngineServices` · `RHIBackendType`)은 같은 문서 2 절에 있다. 남은 판단:

- `Utility` 루트 10 파일(`CommandStack` · `DebugOverlayState` · `GameAutoplay` · `GameTimeScale` · `KeyValueFile`)은 이름만 보고 두었다. 후보는 `CommandStack` → `Scene`(에디터 실행 취소),
  `KeyValueFile` → `Serialization/Format`, `GameTimeScale` · `GameAutoplay` → `Config`, `DebugOverlayState` → `Profiling` 이다.
- `Character/AnimNotify`(처리기 · 표 · 컴포넌트)는 `CharacterHit` · `SocketSetComponent` 를 써서 `Object/Animation` 으로 내릴 수 없다 — 알림 계약만 `Animation/Notify` 로 모았다.
- `Test/EngineTest/Graphics` 의 렌더러 시험(`TestGpuScene` · `TestRenderGraph` …)은 아직 `Graphics` 폴더에 있다(`Test/EngineTest/Renderer` 로 옮길 후보).

**Engine 에 남는 이유가 있는 폴더(2026-10-10 사용 현황 조사):** 에디터는 `GameFramework` 를 include 할 수 없다(`CheckEngineLayers`: Editor 금지 목록) — 그래서 에디터가 쓰는 폴더(`Dialogue` · `Sequencer` · `Destruction` · `Environment` · `Automation` · `UI` · `Animation` · `Scene` …)는 Engine 이 아니면 둘 곳이 없다. 서버 실행 파일도 `GameFramework` 를 모르므로 `Observability`(서버가 `MetricRegistry` · `OpsHTTPEndpoint` 를 쓴다)도 남는다.
`Character` 는 `Destruction`(에디터가 씀)과 `Resource/AssetManager` 가 쓰고 `Spatial` 은 `Character/Fit` 이 쓰므로 남는다 — `Resource` 의 기능 캐시를 자기 폴더로 보낸 뒤에도 `Destruction` 이 `Character` 를 쓴다. `Telemetry` 는 `EngineLoop` · App · 게임이, `Compression` 은 Core 인터페이스의 코덱 공급자라 Core 가 서드파티를 몰라야 하는 규칙 때문에 남는다.

각 순서는 독립 커밋이며 끝날 때 `RunEngineLayerGraph.py` 로 티어 표를 다시 맞춘다. 0-2(Core 층 정리) 뒤, 1 단계 앞에 한다.

### 1. 작은 고리 풀기 (낮은 위험)
- 게이트 티어 표를 실제에 맞춘다(`Text`). `RunEngineLayerGraph.py` 출력이 기준.
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
- 먼저 `OBJECT` 라이브러리로 나눈다(DLL 은 하나, 위험 낮음). 층: Foundation(티어 0~2) · Data(3~4) · Render(5, Renderer, Environment) · World(6~7 코어) · Features(기능 컴포넌트) · Tools(UI · Telemetry · Automation · Module) · 루트(EngineLoop).
- 0 단계 측정으로 이득이 확인되면 Dev 만 층마다 DLL 로 올린다. 내보내기 매크로는 층별로 늘리지 말고 `SW_API` 하나를 유지하되 각 DLL 의 `SW_EXPORTS` 를 층 전체에 건다.
- 리플렉션 등록기 보존: `sw_linkWholeArchive` 를 층 라이브러리마다 건다(Shipping 정적 링크는 지금과 같다). 서버 타깃은 Render 층을 링크하지 않는다.

### 4. 월드를 값으로
- `World` 가 씬 · 물리 월드 · 내비 · GPUScene 빌더 · 틱 설정을 소유하고 `EngineLoop` 는 월드 목록을 돈다. 프리팹 격리 · 머티리얼 미리보기 · 에디터 툴 창 · 서버의 방이 이 위에 선다(백로그 "다중 월드").
- 2 단계 뒤에 한다 — 월드가 코어 위의 소유자가 되려면 코어가 기능을 모르는 상태여야 한다.

### 5. 모듈 확장 창구와 상태 규칙
- 모듈이 서비스 · 렌더 패스 · 레지스트리를 등록하는 공식 창구. 리로드되는 모듈의 변경 가능한 정적 변수를 막는 게이트(허용 목록 + 이유)와, `GameDataCache` · `SQLDriverRegistry` · `LocalStoreFactory` 의 등록 표를 서비스로 올리는 일.

## 건드리지 않는 것
- 키트 DLL 병합(핫 리로드 단위와 키트별 켜기 · 끄기가 깨진다). 산출물 폴더 정리는 별도 진행.
- 임포터의 위치 — 에디터 모듈의 일이다(`EditorModuleHost::importAssetsWithEditorModule`).

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
