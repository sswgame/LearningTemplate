# Task (TaskManager · 비동기 작업)

> **[🏠 위키 홈으로 돌아가기](../../../../README.md)** | **[📖 서브시스템 목록](../../../../docs/02_EngineSubsystems.md)**
> ---

엔진의 **스레드 풀 + 작업 스케줄러**입니다.  
무거운 일을 워커 스레드에 맡기거나, 여러 일을 의존성(순서) 있게 이어서 실행할 때 사용합니다.

경로: `Source/Engine/Utility/Task/`  
접근: App/Engine 쪽에서는 `engine::getTaskManager()` (Editor는 `editor::getTaskManager()`)

관련: [엔진 개요](../../README.md) · [Object 틱과의 관계](../../Object/README.md)

---

## 한 줄로 이해하기

| 개념 | 역할 |
|------|------|
| **TaskManager** | 워커 스레드 풀을 돌리고, 대기열의 작업을 분배합니다. |
| **TaskHandle** | 만든 작업 하나. `submit` / `precede` / `then` 으로 연결합니다. |
| **TaskStageHandle** | 여러 작업을 한 “단계”로 묶어 `waitStage` 로 끝날 때까지 기다립니다. |
| **Affinity** | `Any`(아무 워커) 또는 `MainThread`(메인만). |
| **Work Helping** | `wait` 할 때 놀지 않고 **다른 대기 작업을 대신 실행**합니다. |

```text
메인 스레드                     워커 스레드들
─────────                     ─────────────
emplaceTask / submit  ──────►  큐에서 꺼내 실행
dispatchMainThreadTasks ◄────  (MainThread 친화 작업만)
waitAll / waitStage   ◄────►  Work Helping 으로 같이 진행
```

---

## 파일

| 파일 | 내용 |
|------|------|
| `TaskManager.h` / `.cpp` | 스레드 풀, 스케줄, wait, 메인 큐 |
| `TaskTypes.h` | `TaskHandle`, `TaskArgs`, 델리게이트, Affinity |

---

## 전체 흐름

```mermaid
flowchart TD
  A[initialize<br/>워커 N개 생성] --> B[emplaceTask / emplaceParallel]
  B --> C[precede / succeed / then<br/>의존성 연결]
  C --> D[submit<br/>스케줄러에 넘김]
  D --> E{Affinity?}
  E -->|Any| F[워커 큐<br/>라운드로빈 / Steal]
  E -->|MainThread| G[메인 전용 큐]
  F --> H[executeTask]
  G --> I[dispatchMainThreadTasks<br/>메인 루프에서 호출]
  I --> H
  H --> J[후속 태스크 카운트다운]
  J --> K{선행 모두 끝?}
  K -->|예| D
  K -->|아니오| L[대기]
```

엔진 루프와의 관계(예: Object 틱):

```mermaid
sequenceDiagram
  participant Main as 메인 스레드
  participant TM as TaskManager
  participant W as 워커들

  Main->>TM: dispatchMainThreadTasks()
  Main->>TM: (컴포넌트 병렬 tick 제출)
  TM->>W: emplaceParallel / 웨이브 실행
  Main->>TM: waitAll() / waitStage()
  Note over Main,W: wait 중에도 Work Helping<br/>메인/워커가 남은 일을 돕습니다
  W-->>Main: 완료
```

---

## 기본 사용법

### 1) 한 번 돌릴 작업

```cpp
TaskManager& tm = engine::getTaskManager();

TaskHandle h = tm.emplaceTask( "LoadStuff", []()
{
    // 워커에서 실행 (기본 Affinity = Any)
} );
h.submit();   // 또는 tm.submit( h );

tm.waitAll(); // 전부 끝날 때까지 (Helping 포함)
```

이름을 생략해도 됩니다: `tm.emplaceTask( [](){ ... } );`

### 2) 인자 가방 (`TaskArgs`)

타입이 다른 값을 몇 개 넘길 때:

```cpp
TaskArgs args = MakeTaskArgs( 42, string( "map01" ) );

TaskHandle h = tm.emplaceTask( "WithArgs",
    []( const TaskArgs& a )
    {
        int32 id = a.get<int32>( 0 );
        string name = a.get<string>( 1 );
        (void)id;
        (void)name;
    },
    args );
h.submit();
```

`TaskValue`는 작은 타입(≤32바이트)은 힙 없이 인라인 저장합니다.

### 3) 병렬 for (`emplaceParallel`)

인덱스 `0 .. count-1` 을 워커들에 나눠 줍니다.

```cpp
TaskHandle h = tm.emplaceParallel( 1000, []( uint32 index )
{
    // index번째 요소 처리
} );
h.submit();
tm.waitAll();
```

범위 덩어리로 나누려면 `emplaceParallelBlock( start, end, []( uint32 begin, uint32 end ){ ... } )`.

### 4) 순서 붙이기 (DAG)

```cpp
TaskHandle load = tm.emplaceTask( "Load", [](){ /* ... */ } );
TaskHandle bake = tm.emplaceTask( "Bake", [](){ /* ... */ } );
TaskHandle upload = tm.emplaceTask( "Upload", [](){ /* ... */ },
                                    TaskThreadAffinity::MainThread );

// load 가 끝난 뒤 bake, bake 가 끝난 뒤 upload
load.precede( bake );
bake.precede( upload );

load.submit();
bake.submit();
upload.submit();
```

같은 뜻의 다른 표현:

```cpp
bake.succeed( load );           // bake 는 load 다음
load.then( [](){ /* bake 역할 */ } );  // 체이닝으로 후속 생성
```

```mermaid
flowchart LR
  L[Load] --> B[Bake] --> U[Upload<br/>MainThread]
```

`whenAll` / `whenAny` 로 “여러 개 다 끝나면” / “하나라도 끝나면” 후속을 만들 수도 있습니다.

### 5) 스테이지로 묶어서 기다리기

```cpp
TaskStageHandle stage = tm.createAnonymousStage( "ComponentWave" );
// 또는 tm.getOrCreateStage( "MyStage" );

TaskHandle a = tm.emplaceTask( [](){} );
TaskHandle b = tm.emplaceTask( [](){} );
stage.addTask( a );
stage.addTask( b );
a.submit();
b.submit();

tm.waitStage( stage ); // 스테이지에 넣은 일이 모두 끝날 때까지
```

Object 쪽 컴포넌트 병렬 tick도 비슷한 패턴으로 웨이브를 기다립니다.

### 6) 메인 스레드 전용 작업

UI, RHI, “메인만 만져야 하는” 상태는 Affinity를 `MainThread` 로:

```cpp
TaskHandle h = tm.emplaceTask( "OnMain", []()
{
    // 메인에서만 실행됨
}, TaskThreadAffinity::MainThread );
h.submit();
```

메인은 **매 프레임** (또는 주기적으로) 아래를 호출해야 큐가 소비됩니다.

```cpp
tm.dispatchMainThreadTasks();
```

`GameObjectManager::tick` 시작 시에도 이를 호출합니다.

---

## Affinity · 스레드 확인

| Affinity | 의미 |
|----------|------|
| `Any` | 아무 워커. 기본값. |
| `MainThread` | `dispatchMainThreadTasks` 가 돌릴 때만 실행. |

헬퍼:

```cpp
tm.isMainThread();
tm.isWorkerThread();
tm.isInsideParallelTask();   // emplaceParallel 본문 안인지

tm.ensureMainThread();       // Debug에서 아니면 assert
tm.ensureWorkerThread();
tm.ensureInsideParallelTask();
```

---

## 내부가 하는 일 (초심자용)

몰라도 쓸 수 있지만, “왜 wait가 빠른지”를 이해할 때 도움이 됩니다.

```mermaid
flowchart TB
  subgraph pool ["워커 풀"]
    W0["Worker0 큐"]
    W1["Worker1 큐"]
    W2["Worker2 큐"]
  end
  Idle["유휴 워커"] -->|Steal| W0
  Idle -->|Steal| W1
  Wait["waitAll / waitStage"] -->|Helping<br/>남의 일도 실행| pool
```

- **Work-Stealing**: 내 큐가 비면 다른 워커 큐에서 일을 가져옵니다.  
- **DAG**: 선행이 끝나면 후속 `_unfinishedPredecessors` 가 줄어들고, 0이 되면 자동 스케줄.  
- **Work Helping**: `wait` 중 슬립만 하지 않고 대기열 작업을 직접 돕습니다 → 데드락·코어 낭비 완화.  
- **Adaptive Spin**: 일이 자주 오면 `cpuPause` 스핀 후 잠자기 → 컨텍스트 스위치 비용 감소.

---

## 수명 · 초기화 및 큐 비우기 (Clear)

```cpp
tm.initialize( 0 );  // 0 = 논리 코어 수에 맞춤
// ... 엔진 가동 ...
tm.waitAll();
tm.clear();          // 대기 중인 모든 작업을 안전하게 취소 (Shutdown 직전)
tm.shutdown();
```

- **안전한 취소 (Thread-safe Clear)**: `TaskManager::clear()`는 큐를 비울 때 반복자(iterator) 무효화나 락 경합(lock contention)을 방지하기 위해 `std::erase_if`를 사용하지 않습니다. 대신 **`Queue::steal()`을 통해 작업을 원자적으로 가져와(steal) 안전하게 메모리를 해제**합니다. 엔진 종료 시나 씬 전환 시 락 경합 없이 잔여 작업을 빠르게 비울 수 있습니다.

- App이 `EngineLoop` 초기화 때 TaskManager를 만들고 `engine::bindEngineServices` 로 붙입니다.  
- 게임 모듈은 보통 **직접 TaskManager를 만들지 않고**, 이미 돌아가는 엔진 서비스를 쓰거나 Object씬/리소스 API** 뒤에 숨은 비동기를 사용합니다.

Games에서 `EngineServices` 를 include 하지 않는 규칙은 [Object README](../../Object/README.md) / lint와 같습니다.  
게임 쪽에서 비동기가 필요하면 엔진이 제공하는 고수준 API(씬 비동기 로드 등)를 우선하세요.

---

## 자주 하는 실수

| 실수 | 결과 | 올바른 방법 |
|------|------|-------------|
| `emplace` 후 `submit` 안 함 | 영원히 Pending | `handle.submit()` 또는 `tm.submit(handle)` |
| `MainThread` 작업만 넣고 `dispatchMainThreadTasks` 안 함 | 큐에 쌓인 채 미실행 | 메인 루프마다 dispatch |
| 워커에서 메인 전용 자원(RHI/UI) 직접 사용 | 크래시·레이스 | Affinity `MainThread` 또는 메인으로 다시 넘기기 |
| `waitAll` 없이 종료 | 미완료 작업 / 종료 레이스 | `shutdown` 전 `waitAll` |
| parallel 본문에서 또 무거운 동기 wait | 스레드 고갈 위험 | 의존성은 DAG/`precede`로, 깊은 wait 중첩 피하기 |
| Games에서 `engine::getTaskManager` 직접 남용 | 레이어 경계 흐려짐 | 엔진/씬 API 경유, 또는 팀 규칙에 맞는 서비스 |

---

## 엔진 안에서 이미 쓰는 곳 (참고)

- **Object / ECS**: 컴포넌트 tick 웨이브를 `emplaceParallel` + stage wait  
- **SceneManager**: 씬 비동기 로드 태스크  
- **RenderPass / Pipeline**: 에셋 비동기 로드  
- **LiveReload / ModuleHost**: 언로드 전 `waitAll`

---

## 더 볼 곳

- `TaskManager.h` — API 주석  
- `TaskTypes.h` — `TaskHandle::precede` / `then` / `submit`  
- [Object/README.md](../../Object/README.md) — 병렬 tick과 `waitAll` 타이밍  
- [ARCHITECTURE.md](../../../../ARCHITECTURE.md) — 병렬 tick Gotcha
