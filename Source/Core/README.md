# Core (코어 유틸리티)

엔진의 가장 밑바닥(Foundation)에 해당하는 정적 라이브러리(STATIC) 모듈입니다.
문자열, 로그, 파일, 델리게이트, 컨테이너 래퍼, 동시성, 메모리 진단이 여기 있습니다.

## 디렉터리
- **Common/**: `Types.h` · `Macros.h` · `Defines.h`(버퍼 크기 상수) · `StdHeaders.h` · `PlatformOsHeaders.h` · `EnumUtil.h` · `VarIntUtil.h`(LEB128 · ZigZag) ·
  `BuildInfo.h`(`sw::build::kConfigName` · `kPlatformName` — 값은 CMake 가 정한다) · `TargetMacroCheck.h`(아래 "타깃 매크로") ·
  `X11MacroUndef.h`(Xlib · GLX 를 포함한 바로 뒤에 다시 include)
- **Predefined/**: 엔진과 ReflectionParser 가 함께 include 하는 X-매크로 표(`*.xxx` — 명령줄 인자 · 고정 이름 · 컨테이너 종류 · 애노테이션 종류)와 `AnnotationMeta.txt`
- **Memory/**: `Memory`(`allocateAligned` · 바이트 유틸) · `sw_new` / `sw_delete` · `sw_new_array` / `sw_delete_array` · `make_unique<T>` / `make_unique<T[]>`(`Memory.h`) ·
  `MemoryTag`(아래 "메모리 태그") · `LinearAllocator` · `FrameArenaAllocator`(+ `FrameDoubleBuffer`) · `PoolAllocator` · `MemoryProfiler`(태그별 통계 · 콜스택 · 누수 검사)
- **Concurrency/**: `LockFreeObjectPool`, `LockFreeQueue`, `ConcurrentQueue`, `WorkStealingDeque`, `SpinLock`, `Futex`, `DeadlockDetector`, `DataRaceDetector`,
  `mutex`(데드락 탐지 내장 래퍼) · `atomic`(PROPERTY 로 노출 · 직렬화할 수 있는 래퍼)
- **Task/**: `TaskManager` · `TaskHandle` · `TaskFuture` (워커 풀 + DAG 스케줄러, `Task/README.md`)
- **Container/**: 표준 컨테이너 별칭(`vector.h` · `unordered_map.h` …) · `span` · `VectorUtil`(`removeAtSwap` 등) · `sparse_set` · `DynamicBitset` · `PagedArray`(주소가 옮겨지지 않는 청크 배열) ·
  `InlineAllocator`(SBO) · 핸들(`SlotHandle` · `SlotHandleTable` · `GameObjectHandle` · `ComponentHandle`) · `RegistrationList`(등록부의 공통 모양 —
  중복 거절 · 정렬 · 이름 찾기 · 이름 사본)
- **String/**: `StringUtil` · `StringBuilder` · `fixed_string` · `hashed_string` · `formatString` · `string_splitter` · `TagID`
- **Delegate/**: `Delegate`
- **Event/**: `EventDispatcher` · `EventType`(엔진 예약 이벤트 ID — 이벤트 타입은 그 개념이 사는 층에 둔다)
- **Module/**: `IModuleCodeHolder`(`ModuleCodeHolder.h`) — 모듈 이미지를 내리기 전에 그 코드(델리게이트 스텁 · vtable)를 떼야 하는 등록부의 공통 계약과 목록.
  같은 수명 계약의 Engine 쪽은 `Engine/Module`, App 쪽(라이브 리로드)은 `App/Module` 이다.
- **File/**: `FileUtil` · `PlatformFileUtil` · `IFileWatcher` + 플랫폼 폴더(`Windows/` · `Linux/` — 파일 다이얼로그 · 워처)
- **Process/**: `Process` · `CallStackCapture` · `CrashContext` · `CrashHandler` + 플랫폼 폴더(`Windows/` · `Posix/`).
  `Process::terminate` 는 다른 스레드가 `readOutputLine` · `waitForExit` 을 도는 중에 불러도 된다(pid 는 원자). 자식은 출력 파이프 하나만 물려받는다(남의 핸들 · 서술자 상속 없음). 기다리지 않는 실행은 `Process::launchDetached`.
- **Compression/**: `ICompressionCodec` · `CompressionCodecRegistry` · `CompressionStream` · `NullCompressionCodec` · `RleCompressionCodec`
  (zlib · zstd · LZ4 코덱은 외부 라이브러리를 쓰므로 `Engine/Compression` 에 있다)
- **Math/**: `VectorMath` · `MatrixMath` · `MathUtil` · `Frustum`
- **Time/**: `CpuClock`(아래 "시간") · `CpuTimer` · **Uuid/**: `Uuid` · **CommandLine/**: `CommandLineManager` · **GlobalVariable/**: `GlobalVariableManager`(`SW_GLOBAL_VARIABLE_*`)
- **Log/**: 층이 둘이다 — **파사드**와 **장치**를 섞지 않는다.
  - `ILogSink` / `Logger` — 매크로가 말을 거는 파사드. 포맷 · 타임스탬프 · 리스너 · 비동기 큐 · 상세도 ·
    Caller 표를 맡는다. 테스트 프레임워크는 이 인터페이스를 구현해 기존 싱크를 **감싼다**(로그 가로채기).
  - `ILogOutput` / `ConsoleLogOutput` / `FileLogOutput` — 완성된 한 줄이 실제로 나가는 장치.
    **장치마다 제 락을 갖는다** — 느린 파일 I/O 가 콘솔 쓰기를 막지 않는다.
    출력을 더 붙이려면 `Logger::addOutput` 을 쓴다 — `Logger` 를 고칠 일은 없다.
  - 값 타입(`LogLevel` · `LogEntry` · `LogRecord`)은 `LogTypes.h` 에 있다. 두 층이 함께 쓰므로
    한쪽 헤더에 두면 장치가 파사드를 include 하게 되어 방향이 뒤집힌다.

## 플랫폼 의존 코드는 어디에 두는가

같은 일을 두 방식으로 하고 있으면 플랫폼을 하나 더 지원할 때 한쪽을 빠뜨린다. 규칙은 하나다 —
**플랫폼 분기는 그 분기만 아는 자리에 둔다.**

| 표면 크기 | 두는 곳 | 예 |
|---|---|---|
| 타입·클래스 단위로 다르다 | `File/Windows` · `File/Linux` 처럼 **플랫폼 폴더** | `WindowsFileDialog`, `WindowsFileWatcher` |
| 함수 이름만 다르다 | 원시 연산 하나를 감싸는 **`*Util` 한 곳** | `PlatformFileUtil::openFile` / `seekTo` / `tellPosition` |

- `PlatformFileUtil` 은 이름이 다른 원시 연산만 담는다 — `openFile`(Windows 는 UTF-16 경로 · 공유 열기 `_wfsopen`) ·
  `seekTo`(`_fseeki64`↔`fseeko`) · `tellPosition`(`_ftelli64`↔`ftello`) · `replaceFile`(원자적 바꿔치기) · `getOpenFileSizeAndRewind`.
  같은 `#if` 를 `FileUtil` · `Logger` · `ResourcePackReader` 에 다시 쓰지 않는다.
- 표준 라이브러리가 이미 플랫폼을 덮어 주면 **분기를 만들지 않는다.** 파일 크기·시각·복사·삭제는
  `std::filesystem` 이 한다(`FileUtil::getFileSize`).
- 플랫폼 · 아키텍처 · 컴파일러는 아래 "타깃 매크로" 의 `SW_*` 매크로로만 묻는다.

## 타깃 매크로
- 코드는 `SW_PLATFORM_WINDOWS` / `_LINUX` · `SW_X64` / `SW_ARM64` · `SW_COMPILER_*` 만 읽는다. 판정은
  `cmake/Modules/{Platform,Architecture,Compiler}/` 가 한다.
- 컴파일러 내장 매크로(`_WIN32` · `_MSC_VER` · `__clang__` · `__x86_64__` …)를 읽는 곳은 `Common/TargetMacroCheck.h` 하나뿐이다
  (`Scripts/lint/gate/CheckTargetMacros.py` 게이트). 이 헤더는 `Macros.h` 맨 위에서 포함되어 CMake 판정이 실제 컴파일러와 어긋나면 빌드를 세운다.
- 주의: clang-cl 은 `SW_COMPILER_CLANG` 이다. MSVC 확장(`__forceinline` · `__declspec` …)을 쓸 수 있는지는 `SW_COMPILER_MSVC` 가 아니라
  `SW_PLATFORM_WINDOWS` 로 묻는다(Windows 는 MS ABI 툴체인으로만 짓는다).
- 빌드 구성 · 플랫폼 **이름** 문자열은 `BuildInfo.h` 의 `sw::build::kConfigName` · `kPlatformName` 을 쓴다. `#if` 사슬로 다시 만들지 않는다.

## 메모리
- **맨 `new` 는 쓰지 않는다**(`Style/RawNew` 린트). 객체는 `sw_new T( ... )` · `make_unique<T>`, 배열은 `sw_new_array<T>( n )` /
  `sw_delete_array( p, n )` · `make_unique<T[]>( n )` · `vector<T>`. CRT `new` 는 메모리 태그 · 누수 검사에 보이지 않는다.
- 주의: `sw_new T[n]` 은 원소 소멸자를 부르는 해제 짝이 없다 — 소멸자가 있는 원소는 `sw_new_array` · `vector` 로 담는다
  (`sw_delete_array` 의 static_assert 가 막는다). `make_unique<T[]>` 의 해제자도 원소 소멸자를 부르지 않는다.
- 정렬 할당 여부는 `kUsesAlignedAllocation<T>` 한 기준을 `sw_new` · `sw_delete` · `make_unique` · `Allocator` 가 같이 쓴다.
- 외부 라이브러리(pugixml · JSON · zlib · zstd · LZ4 · stb_image · ImGui)도 할당 훅으로 sw 할당자에 보낸다 — 태그 보고에 잡히게.

## 메모리 태그
- `MemoryTag`(`Memory/MemoryTag.h`)는 할당을 **용도**로 나눈다(UE LLM 의 태그와 같은 역할). 스레드 로컬 값 하나를 할당 헤더에 적고
  `MemoryProfiler` 가 태그별로 센다. 해제는 헤더의 태그로 빼므로 어느 스레드에서 풀어도 같은 줄에서 빠진다.
- 거는 자리는 하위 시스템의 **진입점**(기동 단계 · 서비스 생성 · 에셋 종류별 로드 · 씬 로드 · 렌더러 · 모듈 호출)이다 — `SW_MEMORY_SCOPE( Tag )` / `ScopedMemoryTag`.
- 태스크는 만든 쪽의 태그를 노드에 담아 실행 중에 쓰고(`Task/README.md`), 엔진이 띄우는 스레드는 띄운 쪽의 태그를 받아 첫 줄에서 건다.
  새 스레드는 `Unknown` 에서 시작한다 — `Unknown` 줄이 크면 진입점이 빠진 것이다.
- 태그를 읽는 것은 `SW_DEBUG` 구성의 `MemoryProfiler` 뿐이라 다른 구성에서는 스코프 비용이 0 이다.
- 새 태그를 더하면 `MemoryProfiler::getMemoryTagName` 의 이름 표에도 한 줄을 더한다(줄 수는 static_assert 가 본다).

## 시간
- 엔진의 단조 시계는 `Time/CpuClock.h` 하나다 — 지금 시각 `CpuClock::nowNanoseconds` / `nowMicroseconds`, 경과 시간 `CpuStopwatch`,
  기한 `CpuDeadline::afterMilliseconds( ms )`(`isExpired` · `getRemainingMilliseconds`). 엔진 코드에서 `std::chrono::steady_clock::now()` 를
  직접 읽지 않는다 — 프로파일러 · 로그 · 기한이 같은 시각을 봐야 한다.
- 기다리는 루프는 횟수가 아니라 시간(`CpuDeadline`)으로 묶는다. 횟수 상한은 느린 머신에서 정상을 실패로 만든다.
- 프레임 델타 · 일시정지가 필요하면 `CpuTimer`(같은 OS 카운터)를 쓴다.

## 빌드 모델
- 소스는 `Core_objects`(OBJECT)에서 **한 번만** 컴파일됩니다.
- `Core` STATIC = 그 OBJECT 아카이브 → `Tools/ReflectionParser`가 직접 링크합니다.
- `Engine`는 동일 OBJECT를 링크해 Dev에서 `Engine.dll`로 foundation 심볼을 export합니다 (App/Editor는 `SW_IMPORTS`로 dllimport).
- 소스 목록은 `CMakeLists.txt` 에 **경로 문자열로 적혀 있다**(GLOB 이 아니다). 새 `.cpp` 를
  추가하면 거기도 같이 고쳐야 하고, 잊으면 컴파일러가 알려주지 않는다.

## 핵심 규칙
- **독립성 유지**: `Core`는 `Engine`이나 `GameFramework`, `Game` 폴더의 코드에 **절대 의존해서는 안 됩니다.**
- **어디서나 쓰임**: ReflectionParser에도 직접 링크되므로 무거운 GPU/에디터 의존성은 피해야 합니다.
