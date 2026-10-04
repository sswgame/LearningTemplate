# Core (코어 유틸리티)

엔진의 가장 밑바닥(Foundation)에 해당하는 정적 라이브러리(STATIC) 모듈입니다.
문자열, 로그, 파일, 델리게이트, 컨테이너 래퍼, 동시성, 메모리 진단이 여기 있습니다.

## 디렉터리
- **Common/**: `Types.h` · `Macros.h` · `Defines.h`(버퍼 크기 상수) · `StdHeaders.h` · `PlatformOsHeaders.h` · `EnumUtil.h` · `VarIntUtil.h`(LEB128 · ZigZag) ·
  `BuildInfo.h`(`sw::build::kConfigName` · `kPlatformName` — 값은 CMake 가 정한다) · `TopologicalSortUtil`(의존 위상 정렬 — 동점은 이름 순, 순환 경로 찾기. 엔진 기동 단계 · 모듈 적재 순서가 함께 쓴다) · `TargetMacroCheck.h`(아래 "타깃 매크로") ·
  `X11MacroUndef.h`(Xlib · GLX 를 포함한 바로 뒤에 다시 include)
- **Predefined/**: 엔진과 ReflectionParser 가 함께 include 하는 X-매크로 표(`*.xxx` — 명령줄 인자 · 고정 이름 · 컨테이너 종류 · 애노테이션 종류)와 `AnnotationMeta.txt`
- **Memory/**: `Memory`(`allocateAligned` · 바이트 유틸) · `sw_new` / `sw_delete` · `sw_new_array` / `sw_delete_array` · `make_unique<T>` / `make_unique<T[]>`(`Memory.h`) ·
  `MemoryTag`(아래 "메모리 태그") · `LinearAllocator` · `FrameArenaAllocator`(+ `FrameDoubleBuffer`) · `PoolAllocator` · `MemoryProfiler`(태그별 통계 · 콜스택 · 누수 검사) ·
  할당 관찰자(`Memory::setAllocationObserver` — 외부 프로파일러가 할당 · 해제를 받는다. 관찰 중에 잡힌 블록의 해제만 알린다, 배포본에는 없다)
- **Concurrency/**: `LockFreeObjectPool`, `LockFreeQueue`, `ConcurrentQueue`, `WorkStealingDeque`, `SpinLock`, `Futex`, `DeadlockDetector`, `DataRaceDetector`,
  `mutex`(데드락 탐지 내장 래퍼) · `atomic`(PROPERTY 로 노출 · 직렬화할 수 있는 래퍼) · `ThreadName`(스레드 진입 함수가 OS 에 이름을 적는다 —
  디버거와 Tracy 가 같은 이름을 읽는다: `GameThread` · `RenderThread` · `Worker N` · `IO` · `Logger` · `Net`)
- **Task/**: `TaskManager` · `TaskHandle` · `TaskFuture` (워커 풀 + DAG 스케줄러, `Task/README.md`)
- **Container/**: 표준 컨테이너 별칭(`vector.h` · `unordered_map.h` …) · `span` · `VectorUtil`(`removeAtSwap` 등) · `sparse_set` · `DynamicBitset` · `PagedArray`(주소가 옮겨지지 않는 청크 배열) ·
  `InlineAllocator`(SBO) · 핸들(`SlotHandle` · `SlotHandleTable` · `GameObjectHandle` · `ComponentHandle`) · `RegistrationList`(등록부의 공통 모양 —
  중복 거절 · 정렬 · 이름 찾기 · 이름 사본)
- **String/**: `StringUtil` · `StringBuilder` · `fixed_string` · `hashed_string` · `formatString` · `string_splitter` · `TagID`
- **Delegate/**: `Delegate`
- **Event/**: `EventDispatcher` · `EventType`(엔진 예약 이벤트 ID — 이벤트 타입은 그 개념이 사는 층에 둔다)
- **Module/**: `IModuleUnloadListener`(`ModuleUnloadListener.h`) — 모듈 이미지를 내리기 전에 그 코드(델리게이트 스텁 · vtable)를 떼야 하는 등록부의 공통 계약과 목록.
  같은 수명 계약의 Engine 쪽은 `Engine/Module`, App 쪽(라이브 리로드)은 `App/Module` 이다.
-  `Process::terminate` 는 다른 스레드가 `readOutputLine` · `waitForExit` 을 도는 중에 불러도 된다(pid 는 원자). 자식은 출력 파이프 하나만 물려받는다(남의 핸들 · 서술자 상속 없음). 기다리지 않는 실행은 `Process::launchDetached`.
- **Compression/**: `ICompressionCodec` · `CompressionCodecRegistry` · `CompressionStream` · `NullCompressionCodec` · `RleCompressionCodec`
  (zlib · zstd · LZ4 코덱은 외부 라이브러리를 쓰므로 `Engine/Compression` 에 있다)
- **Network/**: 네트워크 공통 계층 — 장르를 모른다. 장르별 방식(권위 서버 복제 · 락스텝 · 롤백 · 턴 중계 · MMO 관심 영역)은 GameFramework 의 `GF_Net*` 키트(DLL)로
  얹어, 싱글 게임은 그 키트를 링크하지 않는다.
  - `BitStream`(`BitWriter` · `BitReader` — 범위 정수 · 양자화 실수 · 가변 정수, 넘침 감지. 비트를 바이트 덩어리로 쓰고 읽고, 경계에 맞은 바이트는 `memcpy` —
    선 위 배치는 비트 단위 시절과 같다), `SequenceBuffer`(16 비트 감김 시퀀스 고리), `NetTypes`(`NetAddress` ·
    채널 · 연결 상태 · 메시지 첫 바이트 영역 `NetMessageRange`)
  - `NetConnection` — 연결 하나의 신뢰성: 패킷 시퀀스 · ack + 32 비트 묶음, 채널(신뢰 순서 · 순서만 — 메시지 첫 바이트(종류)마다 가장 새 것 하나, 다른 종류끼리는 서로 지우지 않는다 · 비신뢰), 재전송(RTT × 1.5), RTT · 손실률 · 대역폭.
    메시지 길이 칸 11 비트(0..1024). 패킷 끝 1 비트 "확인 요청" — 확인만 담은 답은 끄므로 한가할 때 답에 답이 꼬리를 물지 않는다(RTT · 손실률은 요청 패킷으로 잰다)
  - `NetHost` — 서버 · 클라이언트 끝점: 요청 → 도전 → 응답 → 수락 핸드셰이크(위조 주소 방지), 프로토콜 id + 체크섬으로 남의 · 깨진 패킷 거르기, 유지 · 타임아웃 · 끊기.
    보낼 것이 없으면 `_sendInterval` 이 아니라 `_keepAliveInterval`(0.25 초)마다만 보낸다. 도전 소금 씨앗은 0 이면 OS 난수(`_saltSeed` 는 시험 재현용).
    주소 → 자리 해시(연결 수에 상관없이 받은 패킷 하나에 O(1)), 패킷 · 쓰기 버퍼는 다시 쓴다.
    **스레드 안전** — 공개 함수는 잠금 하나로 지켜져 아무 스레드에서나 보내고 꺼낸다. `update` 는 소켓 받기 · 보내기를 잠금 밖에서 묶어 하고(한 번에
    최대 512 개) 잠금 안에서는 패킷 처리만 한다. **비동기 연결** `connectAsync` → `TaskFuture<NetConnectResult>`(연결 · 가득 참 · 거절 · 타임아웃 · 끊음,
    `then` 은 잠금 밖에서). 여러 스레드가 쓰는 동안 연결 통계는 `getConnectionStats`(사본)
  - `NetHostThread` — 전용 네트워크 스레드: 소켓을 기다렸다가(`poll` · `WSAPoll`, 최대 2 ms) `update` 를 돌린다. 게임 프레임이 멈춰도 확인 · 유지 · 재전송이
    돌아 끊기지 않고 RTT 에 프레임 길이가 섞이지 않는다. 기다리는 일이라 TaskManager 워커가 아니라 전용 스레드(로그 · 파일 감시와 같은 규칙)
  - `NetParallel` — 서버 키트가 연결(관찰자)마다의 일을 `TaskManager::runParallel` 로 나누는 `NetParallelFor` 와 스레드마다의 작업 자리
    `NetParallelScratch<T>`. 매니저가 없으면 지금 스레드가 돈다(결과는 같다)
  - `NetMessage` — `NetMessageWriter`(종류 바이트 + 몸, 버퍼 재사용), `INetMessageHandler`(영역 하나를 맡는 쪽 — `GF_Net*` 키트의 서버 · 클라이언트),
    `NetMessageRouter`(첫 바이트 위 4 비트로 처리기를 바로 찾아 `pump( host )` 로 나눠 주고, 아무도 안 받은 것은 돌려준다)
  - 전송: `INetTransport`(`send` 는 아무 스레드, `receive` · `waitForReceive` 는 `update` 스레드 하나), 실제 UDP(`UdpNetTransport` — 플랫폼 차이는 `PlatformSocketUtil` 한 곳, 송수신 버퍼 1 MB, Windows 는 ICMP 포트 닿지 않음으로
    `recvfrom` 이 실패하지 않게 `SIO_UDP_CONNRESET` 을 끈다), 한 프로세스 루프백 망(`LoopbackNetwork` — 잠금 하나로 끝점마다 다른 스레드가 돌아도 된다. 지연 · 흔들림 · 손실 ·
    중복 · 깨짐을 씨앗으로 흉내, 시험 · 리슨 서버), 네트워크 흉내(`NetEmulationTransport` — 어느 전송(UDP · 루프백)에나 씌워 보내는 쪽에서 지연 · 흔들림 ·
    손실 · 중복 · 순서 뒤바뀜 · 대역폭 상한(목적지마다 회선 줄 · 큐 넘침 버림)을 건다. 조건은 기본값 + 연결별 덮어쓰기, `-gv_netEmuLatencyMs` · `JitterMs` ·
    `LossPercent` · `DuplicatePercent` · `ReorderPercent` · `BandwidthKilobytesPerSecond` 로 `NetEmulationConditions::makeFromGlobalVariables` —
    언리얼 PktLag · PktLoss · PktDup · PktOrder · 유니티 Network Simulator 의 자리. `NetHost` 는 그냥 전송으로 받는다)
- **Math/**: `VectorMath` · `MatrixMath` · `MathUtil` · `Frustum`
- **Time/**: `MonotonicClock`(아래 "시간") · `GameTimer` · **Uuid/**: `Uuid` · **CommandLine/**: `CommandLineManager` · **GlobalVariable/**: `GlobalVariableManager`(`SW_GLOBAL_VARIABLE_*`)
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

## 비동기 파일 IO
- `AsyncFileIo`(`File/AsyncFileIo.h`)는 읽기 요청(파일 전체 `readFile` · 구간 `readRange` · 연 파일의 구간)을 우선순위 큐에 넣고, 백엔드가 높은 우선순위부터
  꺼내 OS 에 겁니다(UE `IAsyncReadFileHandle` + IoStore 우선순위 큐 · Unity `AsyncReadManager` 자리). 동시에 OS 에 걸린 수는 `_maxInFlightCount` 로 묶여
  뒤에 온 급한 요청이 대량 요청 뒤에 줄 서지 않습니다. 같은 우선순위는 들어온 순서입니다.
- 백엔드: Windows 오버랩드 IO + 완료 포트(`File/Windows/WindowsAsyncFileIoBackend.cpp`), 리눅스 io_uring(`File/Linux/LinuxAsyncFileIoBackend.cpp` — liburing 없이
  시스템 호출 직접, `IORING_OP_READV`), 어디서나 도는 스레드 풀(`AsyncFileIo.cpp`). `Auto` 는 플랫폼 것을 고르고, io_uring 을 쓸 수 없으면(ENOSYS · EPERM —
  옛 커널 · WSL1 · 컨테이너 seccomp) 로그 한 줄과 함께 스레드 풀로 내려갑니다. 명시한 백엔드를 쓸 수 없으면 `initialize` 가 실패합니다(폴백하지 않는다).
- 정책(우선순위 · 상한 · 취소 · 파일 열기와 버퍼 준비 · 완료 전달)은 `AsyncFileIoQueue`(`File/AsyncFileIoBackend.h`) 한 곳이고, 백엔드는 "꺼내 걸고, 끝나면 알린다" 만 합니다.
- 파일 열기 · 크기 확인 · 버퍼 할당은 IO 스레드가 합니다. 버퍼는 **요청한 스레드의 메모리 태그**로 셉니다.
- 완료 콜백은 요청마다 한 번(성공 · 실패 · 취소). `TaskManager` 를 넘기면 그 워커에서(Low · Normal → `TaskPriority::Low`, High · Critical → `Normal` — High 줄은 렌더 · 물리 몫),
  아니면 IO 스레드에서 돕니다. `readFileFuture` 는 결과를 `TaskFuture` 로 돌려줍니다.
- 결과: `Succeeded` · `Canceled` · `FileNotFound` · `OutOfRange`(구간이 파일 끝을 넘으면 짧게 읽지 않고 실패) · `ReadFailed` · `ShutDown`(시작 전 · 내린 뒤 요청).
- 취소: 큐에 있으면 OS 에 넘기지 않고, 이미 걸렸으면 읽은 뒤 결과를 버립니다(진행 중 취소는 "최선" — UE · Unity 와 같다).
- 엔진은 서비스 하나(`engine::getAsyncFileIo()`)를 기동 단계 `FileIo` 에서 세우고 Task · 모듈 이미지보다 먼저 내립니다(걸린 읽기와 완료 태스크를 다 기다린다).
  주의: 완료 콜백이 핫 리로드되는 모듈의 코드를 가리키면, 그 모듈을 내리기 전에 핸들을 취소하고 기다려야 합니다(델리게이트와 같은 규칙).
- `PlatformFileUtil::openNativeFileForRead` · `readNativeFileAt` 은 공유 파일 위치가 없는 위치 지정 읽기입니다 — 여러 스레드가 한 핸들을 잠금 없이 읽습니다. Windows 는
  `FILE_FLAG_OVERLAPPED` 로 열고, 동기 읽기는 낮은 비트를 세운 이벤트로 기다려 완료 포트에 묶인 핸들에서도 패킷이 가지 않게 합니다.

## 메모리
- **맨 `new` 는 쓰지 않는다**(`Style/RawNew` 린트). 객체는 `sw_new T( ... )` · `make_unique<T>`, 배열은 `sw_new_array<T>( n )` /
  `sw_delete_array( p, n )` · `make_unique<T[]>( n )` · `vector<T>`. CRT `new` 는 메모리 태그 · 누수 검사에 보이지 않는다.
- 주의: `sw_new T[n]` 은 원소 소멸자를 부르는 해제 짝이 없다 — 소멸자가 있는 원소는 `sw_new_array` · `vector` 로 담는다
  (`sw_delete_array` 의 static_assert 가 막는다). `make_unique<T[]>` 의 해제자도 원소 소멸자를 부르지 않는다.
- 정렬 할당 여부는 `kUsesAlignedAllocation<T>` 한 기준을 `sw_new` · `sw_delete` · `make_unique` · `Allocator` 가 같이 쓴다.
- 외부 라이브러리(pugixml · JSON · zlib · zstd · LZ4 · stb_image · ImGui)도 할당 훅으로 sw 할당자에 보낸다 — 태그 보고에 잡히게.

## 메모리 태그
- `MemoryTag`(`Memory/MemoryTag.h`)는 할당을 **용도**로 나눈다(UE LLM 의 태그와 같은 역할 — Texture · Mesh · Audio · Animation · Physics · UI · Script …). 스레드 로컬 값 하나를 할당 헤더에 적고
  `MemoryProfiler` 가 태그별로 센다. 해제는 헤더의 태그로 빼므로 어느 스레드에서 풀어도 같은 줄에서 빠진다.
- 거는 자리는 하위 시스템의 **진입점**(기동 단계 · 서비스 생성 · 에셋 종류별 로드 · 씬 로드 · 렌더러 · 모듈 호출)이다 — `SW_MEMORY_SCOPE( Tag )` / `ScopedMemoryTag`.
- 태스크는 만든 쪽의 태그를 노드에 담아 실행 중에 쓰고(`Task/README.md`), 엔진이 띄우는 스레드는 띄운 쪽의 태그를 받아 첫 줄에서 건다.
  새 스레드는 `Unknown` 에서 시작한다 — `Unknown` 줄이 크면 진입점이 빠진 것이다.
- 스코프 · 할당 헤더 · `MemoryProfiler` 는 배포본이 아닌 모든 구성에 있다. 추적은 Debug · 시험 하네스에서 켜져 있고 Release 는 꺼 둔 채 `-gv_memoryTracking=1` 로 켠다 —
  꺼져 있으면 할당마다 분기 하나다(Release 64 B 할당 + 해제 63 ns → 켜면 73 ns, `MemoryTagBenchTest`). 배포본에는 헤더도 프로파일러도 없다.
- 명시 태그: 잡는 곳과 쓰는 하위 시스템이 다른 버퍼는 `Memory::allocate( size, tag )` · `allocateAligned( size, align, tag )` 로 그 자리에서 태그를 준다.
- 태그마다 살아 있는 바이트 · 블록, **최고치**(`_peakAllocatedBytes` · `resetPeaks`), **예산**(`setBudget` · `reportExceededBudgets` — 넘을 때 경고 한 번, 90 % 아래로 내려가면 다시 건다)을 든다.
  예산 데이터 · 프레임 검사 · 표 보고(`-gv_memoryReport=1`) · FrameProfiler 카운터(`Mem.LiveKB` · 예산 있는 태그의 `Mem.<태그>KB`)는 Engine 의 `MemoryBudgetMonitor` 가 한다.
- 누수 보고: `captureMemoryLeakBaseline` 이 태그별 기준선도 찍고, Debug 종료 끝(`EngineBootstrap::shutdown`, 프로파일러만 남은 때)에 기준선보다 늘어난 태그를
  stderr 로 남긴다(`[MemoryLeak] shutdown - tag …`). CRT 검사는 힙 **합계**만 견주므로 서비스가 내려가 합계가 줄면 남은 블록을 보지 못한다 — 태그 보고는 본다.
- 새 태그를 더하면 `MemoryProfiler::getMemoryTagName` 의 이름 표에도 한 줄을 더한다(줄 수는 static_assert 가 본다).

## 시간
- 엔진의 단조 시계는 `Time/MonotonicClock.h` 하나다 — 지금 시각 `MonotonicClock::nowNanoseconds` / `nowMicroseconds`, 경과 시간 `Stopwatch`,
  기한 `Deadline::afterMilliseconds( ms )`(`isExpired` · `getRemainingMilliseconds`). 엔진 코드에서 `std::chrono::steady_clock::now()` 를
  직접 읽지 않는다(시험 코드 포함) — 프로파일러 · 로그 · 기한이 같은 시각을 봐야 한다. `Scripts/lint/gate/CheckClockReads.py` 가 막는다.
- 기다리는 루프는 횟수가 아니라 시간(`Deadline`)으로 묶는다. 횟수 상한은 느린 머신에서 정상을 실패로 만든다.
- 프레임 델타 · 일시정지가 필요하면 `GameTimer`(같은 OS 카운터)를 쓴다.

## 빌드 모델
- 소스는 `Core_objects`(OBJECT)에서 **한 번만** 컴파일됩니다.
- `Core` STATIC = 그 OBJECT 아카이브 → `Tools/ReflectionParser`가 직접 링크합니다.
- `Engine`는 동일 OBJECT를 링크해 Dev에서 `Engine.dll`로 foundation 심볼을 export합니다 (App/Editor는 `SW_IMPORTS`로 dllimport).
- 소스 목록은 `CMakeLists.txt` 에 **경로 문자열로 적혀 있다**(GLOB 이 아니다). 새 `.cpp` 를
  추가하면 거기도 같이 고쳐야 하고, 잊으면 컴파일러가 알려주지 않는다.

## 핵심 규칙
- **독립성 유지**: `Core`는 `Engine`이나 `GameFramework`, `Game` 폴더의 코드에 **절대 의존해서는 안 됩니다.**
- **어디서나 쓰임**: ReflectionParser에도 직접 링크되므로 무거운 GPU/에디터 의존성은 피해야 합니다.
