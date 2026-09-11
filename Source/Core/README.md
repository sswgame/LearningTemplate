# Core (코어 유틸리티)

엔진의 가장 밑바닥(Foundation)에 해당하는 정적 라이브러리(STATIC) 모듈입니다.
문자열, 로그, 파일, 델리게이트, 컨테이너 래퍼, 동시성, 메모리 진단이 여기 있습니다. `Handle/`은 없습니다.

## 디렉터리
- **Memory/**: `alignedAlloc`, `LinearAllocator`, `FrameArenaAllocator`(+ `FrameDoubleBuffer`), `MemoryProfiler`(누수 검사 포함), `CallStackCapture`
- **Concurrency/**: `LockFreeObjectPool`, `LockFreeQueue`, `ConcurrentQueue`, `WorkStealingDeque`, `DeadlockDetector`, `DataRaceDetector`
- **Task/**: `TaskManager` · `TaskHandle` · `TaskFuture` (워커 풀 + DAG 스케줄러)
- **Container/**: `DynamicBitset` · **String/** · **File/** · **Event/** · **Delegate/**
- **Log/**: 층이 둘이다 — **파사드**와 **장치**를 섞지 않는다.
  - `ILogSink` / `Logger` — 매크로가 말을 거는 파사드. 포맷 · 타임스탬프 · 리스너 · 비동기 큐 · 상세도 ·
    Caller 표를 맡는다. 테스트 프레임워크는 이 인터페이스를 구현해 기존 싱크를 **감싼다**(로그 가로채기).
  - `ILogOutput` / `ConsoleLogOutput` / `FileLogOutput` — 완성된 한 줄이 실제로 나가는 장치.
    **장치마다 제 락을 갖는다.** 예전엔 뮤텍스 하나가 콘솔·파일 쓰기를 함께 잠가, 느린 파일 I/O 가
    콘솔까지 멈춰 세웠다. 출력을 더 붙이려면 `Logger::addOutput` 을 쓴다 — `Logger` 를 고칠 일은 없다.
  - 값 타입(`LogLevel` · `LogEntry` · `LogRecord`)은 `LogTypes.h` 에 있다. 두 층이 함께 쓰므로
    한쪽 헤더에 두면 장치가 파사드를 include 하게 되어 방향이 뒤집힌다.

## 플랫폼 의존 코드는 어디에 두는가

같은 일을 두 방식으로 하고 있으면 플랫폼을 하나 더 지원할 때 한쪽을 빠뜨린다. 규칙은 하나다 —
**플랫폼 분기는 그 분기만 아는 자리에 둔다.**

| 표면 크기 | 두는 곳 | 예 |
|---|---|---|
| 타입·클래스 단위로 다르다 | `File/Windows` · `File/Linux` · `File/Mac` 처럼 **플랫폼 폴더** | `WindowsFileDialog`, `WindowsFileWatcher` |
| 함수 이름만 다르다 | 원시 연산 하나를 감싸는 **`*Util` 한 곳** | `PlatformFileUtil::openFile` / `seekTo` / `tellPosition` |

- `PlatformFileUtil` 은 `fopen_s`↔`fopen`, `_fseeki64`↔`fseeko`, `_ftelli64`↔`ftello` 만 담는다.
  예전에는 이 `#if` 가 `FileUtil` 에 5벌, `Logger` 에 1벌, Engine 의 `ResourcePackReader` 에
  5벌 있었다.
- 표준 라이브러리가 이미 플랫폼을 덮어 주면 **분기를 만들지 않는다.** 파일 크기·시각·복사·삭제는
  `std::filesystem` 이 한다 (`FileUtil::getFileSize` 가 그 예 — 예전엔 파일을 열고 끝까지
  탐색했다).

## 빌드 모델
- 소스는 `Core_objects`(OBJECT)에서 **한 번만** 컴파일됩니다.
- `Core` STATIC = 그 OBJECT 아카이브 → `Tools/ReflectionParser`가 직접 링크합니다.
- `Engine`는 동일 OBJECT를 링크해 Dev에서 `Engine.dll`로 foundation 심볼을 export합니다 (App/Editor는 `SW_IMPORTS`로 dllimport).
- 소스 목록은 `CMakeLists.txt` 에 **경로 문자열로 적혀 있다**(GLOB 이 아니다). 새 `.cpp` 를
  추가하면 거기도 같이 고쳐야 하고, 잊으면 컴파일러가 알려주지 않는다.

## 핵심 규칙
- **독립성 유지**: `Core`는 `Engine`이나 `GameFramework`, `Game` 폴더의 코드에 **절대 의존해서는 안 됩니다.**
- **어디서나 쓰임**: ReflectionParser에도 직접 링크되므로 무거운 GPU/에디터 의존성은 피해야 합니다.
