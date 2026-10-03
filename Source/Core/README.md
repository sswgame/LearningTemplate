# Core (코어 유틸리티)

엔진의 가장 밑바닥(Foundation)에 해당하는 정적 라이브러리(STATIC) 모듈입니다.
문자열, 로그, 파일, 델리게이트, 컨테이너 래퍼, 동시성, 메모리 진단이 여기 있습니다.

## 디렉터리
- **Common/**: `Types.h` · `Macros.h` · `Defines.h`(버퍼 크기 상수) · `StdHeaders.h` · `PlatformOsHeaders.h` · `EnumUtil.h` · `VarIntUtil.h`(LEB128 · ZigZag)
- **Predefined/**: 엔진과 ReflectionParser 가 함께 include 하는 X-매크로 표(`*.xxx` — 명령줄 인자 · 고정 이름 · 컨테이너 종류 · 애노테이션 종류)와 `AnnotationMeta.txt`
- **Memory/**: `allocateAligned`(`Memory.h`), `LinearAllocator`, `FrameArenaAllocator`(+ `FrameDoubleBuffer`), `PoolAllocator`, `MemoryProfiler`(누수 검사 포함)
- **Concurrency/**: `LockFreeObjectPool`, `LockFreeQueue`, `ConcurrentQueue`, `WorkStealingDeque`, `SpinLock`, `Futex`, `DeadlockDetector`, `DataRaceDetector`
- **Task/**: `TaskManager` · `TaskHandle` · `TaskFuture` (워커 풀 + DAG 스케줄러, `Task/README.md`)
- **Container/**: 표준 컨테이너 별칭(`vector.h` · `unordered_map.h` …) · `sparse_set` · `DynamicBitset` · `PagedArray`(주소가 옮겨지지 않는 청크 배열) ·
  `InlineAllocator`(SBO) · 핸들(`SlotHandle` · `SlotHandleTable` · `GameObjectHandle` · `ComponentHandle`) · `RegistrationList`(등록부의 공통 모양 —
  중복 거절 · 정렬 · 이름 찾기 · 이름 사본)
- **String/**: `StringUtil` · `StringBuilder` · `fixed_string` · `hashed_string` · `formatString` · `string_splitter` · `TagID`
- **Delegate/**: `Delegate`
- **Event/**: `EventDispatcher` · `EventType`(엔진 예약 이벤트 ID — 이벤트 타입은 그 개념이 사는 층에 둔다)
- **Module/**: `IModuleCodeHolder` — 모듈 이미지를 내리기 전에 그 코드(델리게이트 스텁 · vtable)를 떼야 하는 등록부의 공통 계약과 목록.
  같은 수명 계약의 Engine 쪽은 `Engine/Module`, App 쪽(라이브 리로드)은 `App/Module` 이다.
- **File/**: `FileUtil` · `PlatformFileUtil` · `IFileWatcher` + 플랫폼 폴더(`Windows/` · `Linux/` · `Mac/` — 파일 다이얼로그 · 워처)
- **Process/**: `Process` · `CallStackCapture` · `CrashContext` · `CrashHandler` + 플랫폼 폴더(`Windows/` · `Posix/`)
- **Compression/**: `ICompressionCodec` · `CompressionCodecRegistry` · `CompressionStream` · `NullCompressionCodec` · `RleCompressionCodec`
- **Network/**: 네트워크 공통 계층 — 장르를 모른다. 장르별 방식(권위 서버 복제 · 락스텝 · 롤백 · 턴 중계 · MMO 관심 영역)은 GameFramework 의 `GF_Net*` 키트(DLL)로
  얹어, 싱글 게임은 그 키트를 링크하지 않는다.
  - `BitStream`(`BitWriter` · `BitReader` — 범위 정수 · 양자화 실수 · 가변 정수, 넘침 감지. 비트를 바이트 덩어리로 쓰고 읽고, 경계에 맞은 바이트는 `memcpy` —
    선 위 배치는 비트 단위 시절과 같다), `SequenceBuffer`(16 비트 감김 시퀀스 고리), `NetTypes`(`NetAddress` ·
    채널 · 연결 상태 · 메시지 첫 바이트 영역 `NetMessageRange`)
  - `NetConnection` — 연결 하나의 신뢰성: 패킷 시퀀스 · ack + 32 비트 묶음, 채널(신뢰 순서 · 순서만 · 비신뢰), 재전송(RTT × 1.5), RTT · 손실률 · 대역폭.
    메시지 길이 칸 11 비트(0..1024). 패킷 끝 1 비트 "확인 요청" — 확인만 담은 답은 끄므로 한가할 때 답에 답이 꼬리를 물지 않는다(RTT · 손실률은 요청 패킷으로 잰다)
  - `NetHost` — 서버 · 클라이언트 끝점: 요청 → 도전 → 응답 → 수락 핸드셰이크(위조 주소 방지), 프로토콜 id + 체크섬으로 남의 · 깨진 패킷 거르기, 유지 · 타임아웃 · 끊기.
    보낼 것이 없으면 `_sendInterval` 이 아니라 `_keepAliveInterval`(0.25 초)마다만 보낸다. 도전 소금 씨앗은 0 이면 OS 난수(`_saltSeed` 는 시험 재현용).
    주소 → 자리 해시(연결 수에 상관없이 받은 패킷 하나에 O(1)), 패킷 · 쓰기 버퍼는 다시 쓴다
  - `NetMessage` — `NetMessageWriter`(종류 바이트 + 몸, 버퍼 재사용), `INetMessageHandler`(영역 하나를 맡는 쪽 — `GF_Net*` 키트의 서버 · 클라이언트),
    `NetMessageRouter`(첫 바이트 위 4 비트로 처리기를 바로 찾아 `pump( host )` 로 나눠 주고, 아무도 안 받은 것은 돌려준다)
  - 전송: `INetTransport`, 실제 UDP(`UdpNetTransport` — 플랫폼 차이는 `PlatformSocketUtil` 한 곳, 송수신 버퍼 1 MB, Windows 는 ICMP 포트 닿지 않음으로
    `recvfrom` 이 실패하지 않게 `SIO_UDP_CONNRESET` 을 끈다), 한 프로세스 루프백 망(`LoopbackNetwork` — 지연 · 흔들림 · 손실 ·
    중복 · 깨짐을 씨앗으로 흉내, 시험 · 리슨 서버)
- **Math/**: `VectorMath` · `MatrixMath` · `MathUtil` · `Frustum`
- **Time/**: `CpuTimer` · **Uuid/**: `Uuid` · **CommandLine/**: `CommandLineManager` · **GlobalVariable/**: `GlobalVariableManager`(`SW_GLOBAL_VARIABLE_*`)
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
