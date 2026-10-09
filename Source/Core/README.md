# Core — 토대 라이브러리

## 이것은 무엇이고 왜 있나

`Source/Core` 는 엔진의 가장 아래층입니다. 문자열, 로그, 파일, 메모리, 컨테이너, 동시성, 태스크, 시간, 압축, 네트워크 공통 계층이 여기 있습니다.
언리얼의 `Core` 모듈, Godot의 `core/` 에 해당합니다.

Core는 Engine, GameFramework, 게임, 에디터의 어느 코드도 include하지 않습니다. 리플렉션 코드 생성기(`Tools/ReflectionParser`)가 Engine 없이 Core만 링크하기 때문입니다.
Core가 Engine을 알게 되면 생성기와 Engine.dll 사이에 순환이 생깁니다. 그래서 GPU나 에디터처럼 무거운 의존성도 두지 않습니다.

소스는 `Core_objects` 라는 OBJECT 라이브러리로 **한 번만** 컴파일됩니다. `Core` 정적 라이브러리는 그 결과를 묶은 것이고 ReflectionParser가 링크합니다.
Engine은 같은 OBJECT를 링크해 Dev 구성에서 `Engine.dll` 로 Core의 심볼까지 내보냅니다. App과 에디터는 `SW_IMPORTS` 로 그 심볼을 가져옵니다.

## 머릿속 그림

폴더는 하는 일로 나뉩니다. 자주 여는 것부터 적으면 다음과 같습니다.

| 폴더 | 하는 일 |
|---|---|
| `Common/` | 기본 타입, 매크로, 타깃 매크로, 빌드 정보 |
| `String/` | `hashed_string`, `formatString`, `StringUtil`, `TagID` |
| `Container/` | 표준 컨테이너 별칭, 핸들, 레지스트리 목록 |
| `Memory/` | `sw_new`, 할당기, 메모리 태그, `MemoryProfiler` |
| `Log/` | 로그 매크로와 출력 장치 |
| `GlobalVariable/`, `CommandLine/` | 실행 인자로 바꾸는 전역 변수 |
| `Task/` | 워커 풀과 태스크 그래프([Task](Task/README.md)) |
| `Concurrency/` | 락프리 큐, 잠금, 교착과 경합 검출기 |
| `File/` | 파일 유틸, 비동기 파일 IO, 파일 감시 |
| `Time/` | 단조 시계와 벽시계 |
| `Compression/` | 압축 스트림과 코덱 레지스트리 |
| `Network/` | 네트워크 공통 계층([Network](Network/README.md)) |
| `Module/`, `Process/` | 동적 라이브러리와 자식 프로세스 |
| `Predefined/` | Engine과 ReflectionParser가 함께 읽는 X-macro 목록 |

이 문서에서 기억할 개념은 네 가지입니다.

**`hashed_string`.** 이름을 전역 intern 테이블에 한 번 넣고 번호로 비교하는 문자열입니다. 언리얼의 `FName` 과 같은 규칙입니다.
같음과 해시는 대소문자를 무시하고, `c_str()` 은 처음 적은 철자를 돌려줍니다. 컴포넌트 이름, 액션 이름, 프로퍼티 이름처럼 자주 비교하는 이름에 씁니다.

**메모리 태그.** 할당을 용도(Texture, Mesh, Audio, Physics, UI …)별로 나눠 세는 표시입니다. 언리얼의 LLM과 같은 역할입니다.

**전역 변수.** `gv_` 로 시작하는 변수를 코드에 선언하면 실행 인자(`-gv_이름=값`), 에디터, 개발 콘솔에서 값을 바꿀 수 있습니다. 언리얼의 콘솔 변수(CVar)에 해당합니다.

**타깃 매크로.** 플랫폼, 아키텍처, 컴파일러는 `SW_PLATFORM_WINDOWS`, `SW_X64`, `SW_COMPILER_CLANG` 같은 `SW_*` 매크로로만 묻습니다.

## 따라 해 보기 — 전역 변수 하나로 값을 바꾸며 로그 보기

실행 중에 바꿔 볼 수 있는 이동 속도 값을 하나 만들고, 그 값을 로그로 확인해 보겠습니다.

### 1단계 — 전역 변수 선언

값을 읽는 `.cpp` 파일에 선언합니다. 다른 파일에서 다시 선언하지 않습니다.

```cpp
SW_GLOBAL_VARIABLE( float32, gv_tutorialMoveSpeed, 3.0f, "튜토리얼 이동 속도(m/s)" );
```

매크로의 인자는 타입, 이름, 기본값, 설명입니다. 쓸 수 있는 타입은 `bool`, `int32`, `float32`, `sw::string`, 리플렉션에 등록된 enum입니다.
다른 파일에서 읽어야 한다면 그 파일에서 `SW_EXTERN_GLOBAL_VARIABLE( float32, gv_tutorialMoveSpeed );` 로 참조합니다. 타입이 어긋나면 `CheckGlobalVariableKinds.py` 게이트가 막습니다.

### 2단계 — 값을 읽고 로그로 남기기

```cpp
SW_LOG_CALLER( "Tutorial" );

SW_LOG_INFO( "move speed %#", gv_tutorialMoveSpeed );
```

`SW_LOG_CALLER` 는 이 파일의 로그 줄에 붙을 이름을 정합니다. `%#` 은 타입을 가리지 않는 자리표입니다.
`SW_LOG_INFO` 는 Shipping에서 사라지고, `SW_LOG_WARNING` 과 `SW_LOG_ERROR` 는 남습니다. 로그 문자열은 영어로 씁니다.

### 3단계 — 실행 인자로 값 바꾸기

```powershell
cd build/Ninja-Debug/Bin
./App.exe -gv_tutorialMoveSpeed=8
```

로그에 `move speed 8` 이 나옵니다. 에디터를 켜고 실행했다면 전역 변수 목록에서도 값을 바꿀 수 있습니다.
모듈이 아직 로드되지 않아 변수가 없는 동안 들어온 `gv_` 인자는 보관했다가, 모듈이 변수를 등록할 때 적용합니다.
bool이 아닌 변수에 값을 빠뜨리면 경고하고 기본값을 씁니다.

### 4단계 — 설정 참조 문서 갱신

```powershell
py -3 Scripts/generate/GenerateConfigReference.py
```

이 스크립트가 코드를 읽어 `docs/Config/GlobalVariables.md` 를 다시 만듭니다. 새 변수가 표에 한 줄로 들어가므로 같은 커밋에 넣습니다.

전역 변수에는 세 종류가 있습니다. 차이는 `GlobalVariable/GlobalVariableManager.h` 의 매크로 주석에 자세히 있습니다.

- `SW_GLOBAL_VARIABLE` 은 일반 변수입니다. 에디터 목록, 프리셋, 실행 인자에 모두 나옵니다.
- `SW_TEST_GLOBAL_VARIABLE` 은 벤치, 자동화, 진단 스위치입니다. 에디터 목록에서 빠지고, Shipping에서는 등록되지 않아 기본값으로만 읽힙니다.
- `SW_TEST_GLOBAL_VARIABLE_SHIPPED` 는 테스트용이지만 Shipping에도 등록됩니다. 배포 실행 파일을 스크립트가 조종해야 하는 스위치(`gv_profileFrames`, `gv_screenshot`)만 이것으로 둡니다.

## 작동 원리

### 플랫폼 의존 코드를 두는 곳

같은 일을 두 방식으로 하면 플랫폼을 하나 더 지원할 때 한쪽을 빠뜨립니다. 그래서 **플랫폼 분기는 그 분기만 아는 곳에 둡니다.**

- 타입이나 클래스 단위로 다르면 플랫폼 폴더(`File/Windows`, `File/Linux`)에 둡니다. `WindowsFileDialog`, `WindowsFileWatcher` 가 그 예입니다.
- 함수 이름만 다르면 원시 연산 하나를 감싸는 `*Util` 한 곳에 둡니다. `PlatformFileUtil` 의 `openFile`, `seekTo`, `tellPosition`, `replaceFile` 이 그 예입니다. 같은 `#if` 를 다른 파일에 다시 쓰지 않습니다.
- 표준 라이브러리가 이미 플랫폼을 덮어 주면 분기를 만들지 않습니다. 파일 존재, 크기, 시각, 순회, 복사, 삭제는 `std::filesystem` 이 하지만, `File/Std/FileUtilStdFileSystem.cpp` 한 파일 안에서만 씁니다.
  엔진은 `FileUtil` 로만 봅니다. 함수 하나를 플랫폼 API로 바꿀 때는 측정에서 이길 때만 그 정의를 플랫폼 폴더로 옮깁니다.

### 타깃 매크로

코드는 `SW_PLATFORM_WINDOWS`, `SW_PLATFORM_LINUX`, `SW_X64`, `SW_ARM64`, `SW_COMPILER_*` 만 읽습니다. 판정은 `cmake/Modules/` 의 Platform, Architecture, Compiler 폴더가 합니다.
컴파일러 내장 매크로(`_WIN32`, `_MSC_VER`, `__clang__`)를 읽는 곳은 `Common/TargetMacroCheck.h` 하나뿐입니다. 이 헤더는 CMake 판정이 실제 컴파일러와 어긋나면 `#error` 로 빌드를 멈춥니다.
나머지 코드에서 내장 매크로를 읽으면 `CheckTargetMacros.py` 게이트가 막습니다.

빌드 구성과 플랫폼의 **이름** 문자열은 `BuildInfo.h` 의 `sw::build::kConfigName` 과 `kPlatformName` 을 씁니다. `#if` 사슬로 다시 만들지 않습니다.

### 메모리

**맨 `new` 는 쓰지 않습니다**(`Style/RawNew` 린트). CRT의 `new` 로 만든 메모리는 메모리 태그와 누수 검사에 보이지 않기 때문입니다.
객체는 `sw_new T( ... )` 나 `make_unique<T>` 로, 배열은 `sw_new_array<T>( n )` 과 `sw_delete_array( p, n )`, `make_unique<T[]>( n )`, `vector<T>` 로 만듭니다.
정렬 할당을 쓸지는 `kUsesAlignedAllocation<T>` 한 기준을 모든 할당 함수가 같이 씁니다. MSVC에서 `max_align_t` 는 8이라 이 기준이 없으면 힙 계열이 갈립니다.
외부 라이브러리(pugixml, JSON, zlib, zstd, LZ4, stb_image, ImGui)도 공개된 설정 지점으로 할당을 sw 할당기에 보내 태그 보고에 잡히게 합니다.

`MemoryTag` 는 스레드 로컬 값 하나를 할당 헤더에 적고, `MemoryProfiler` 가 태그별로 셉니다. 해제는 헤더의 태그로 빼므로 어느 스레드에서 해제해도 같은 줄에서 빠집니다.
태그를 거는 곳은 하위 시스템의 **진입점**입니다. 기동 단계 목록의 태그 열, 서비스 생성의 `kServiceMemoryTag<Type>`, 하위 시스템 진입점의 `SW_MEMORY_SCOPE( Tag )` 입니다.
태스크와 병렬 청크는 만든 쪽의 태그를 이어받고, 엔진이 띄우는 스레드는 띄운 쪽의 태그를 인자로 받아 첫 줄에서 겁니다. 새 스레드는 `Unknown` 에서 시작하므로, `Unknown` 이 크면 진입점이 빠진 것입니다.
할당 위치와 쓰는 하위 시스템이 다른 버퍼는 `Memory::allocate( size, tag )` 로 그 자리에서 태그를 줍니다.

태그 스코프와 할당 헤더와 `MemoryProfiler` 는 Shipping이 아닌 모든 구성에 있습니다. 추적은 Debug와 테스트 하네스에서 켜져 있고, Release에서는 `-gv_memoryTracking=1` 로 켭니다.
꺼져 있으면 할당마다 분기 하나의 비용입니다(Release에서 64B 할당과 해제가 63ns, 켜면 73ns, `MemoryTagBenchTest`).
태그마다 살아 있는 바이트와 블록, 최고치, 예산을 보관합니다. 예산을 넘으면 한 번 경고하고, 90% 아래로 내려가면 다시 경고할 수 있게 됩니다. 예산 데이터와 프레임 검사는 Engine의 `MemoryBudgetMonitor` 가 합니다.
분포는 `-gv_profileFrames` 보고의 "memory by tag" 와 에디터 프로파일러 패널에서 봅니다. GPU 메모리는 대상이 아니고 CPU 힙만 셉니다.

누수 보고는 `captureMemoryLeakBaseline` 이 태그별 기준선을 찍고, Debug 종료 끝(`EngineBootstrap::shutdown`)에 기준선보다 늘어난 태그를 stderr로 남깁니다(`[MemoryLeak] shutdown - tag …`).
CRT 검사는 힙 합계만 비교하므로 서비스가 종료되어 합계가 줄면 남은 블록을 놓치지만, 태그 보고는 놓치지 않습니다.
보고의 "(sw 할당자 밖)"은 CRT 합계에서 태그 합계를 뺀 값이라, 프로파일러보다 먼저 할당된 sw 블록도 들어갑니다. 그래서 `MemoryProfiler` 는 부트스트랩 맨 앞에서 만듭니다.

### 시간

엔진의 단조 시계는 `Time/MonotonicClock.h` 하나입니다. 지금 시각은 `MonotonicClock::nowNanoseconds`, 경과 시간은 `Stopwatch`, 기한은 `Deadline::afterMilliseconds( ms )` 로 다룹니다.
엔진 코드와 테스트 코드는 `std::chrono::steady_clock::now()` 를 직접 읽지 않습니다. 프로파일러, 로그, 기한이 같은 시각을 봐야 하기 때문이고, `CheckClockReads.py` 게이트가 막습니다.
기다리는 루프는 횟수가 아니라 시간(`Deadline`)으로 끊습니다. 횟수 상한은 느린 머신에서 정상적인 대기를 실패로 만듭니다.
프레임 델타와 일시정지가 필요하면 `GameTimer` 를 씁니다.

UTC 시각(서버의 만료, 기록 시각, 기간)은 `Time/WallClock.h` 하나가 다룹니다. `system_clock` 을 읽는 유일한 파일입니다.
NTP 보정으로 거꾸로 갈 수 있으므로 경과 시간에는 쓰지 않습니다. 서비스는 현재 시각을 `nowMs` 매개변수로 받고, 테스트는 가짜 시각을 넣습니다.

### 로그

로그는 두 층으로 나뉩니다. 이 둘을 섞지 않습니다.

- **파사드**(`ILogSink`, `Logger`)는 매크로가 말을 거는 쪽입니다. 포맷, 타임스탬프, 리스너, 비동기 큐, 상세도, 호출자 이름 테이블을 맡습니다. 테스트 프레임워크는 이 인터페이스를 구현해 기존 싱크를 감쌉니다.
- **장치**(`ILogOutput`, `ConsoleLogOutput`, `FileLogOutput`)는 완성된 한 줄이 실제로 나가는 곳입니다. 장치마다 자기 잠금을 가지므로 느린 파일 쓰기가 콘솔 쓰기를 막지 않습니다. 출력을 더하려면 `Logger::addOutput` 을 씁니다.

두 층이 함께 쓰는 값 타입(`LogLevel`, `LogEntry`, `LogRecord`)은 `LogTypes.h` 에 따로 있습니다. 한쪽 헤더에 두면 장치가 파사드를 include하게 되어 방향이 뒤집힙니다.

로그는 워커 스레드에서 비동기로 씁니다. 그래서 크래시 직전의 메시지는 사라질 수 있고, 크래시 경로는 `Logger::flushGlobalForCrash` 로 남깁니다. 잠금을 잡지 못하면 포기합니다.

로그 문맥(`LogContext`)은 스레드 로컬 요청 추적 id(128비트)와 주체입니다. 문맥이 있는 스레드의 줄에만 `[trace=… acct=…]` 가 붙고, 문맥이 없는 줄은 바이트가 그대로입니다.
비동기 경계에서는 넘기는 쪽이 문맥을 복사해 보관하고, 받는 쪽이 `ScopedLogContext` 로 다시 겁니다(`IServiceStoreWork` 가 그 예). 요청 머리(`NetRequestOptions`)가 클라이언트의 추적 id를 서버까지 전달합니다.

### 비동기 파일 IO

`AsyncFileIo`(`File/AsyncFileIo.h`)는 읽기 요청(파일 전체, 구간, 연 파일의 구간)을 우선순위 큐에 넣고, 백엔드가 높은 우선순위부터 OS에 겁니다.
언리얼의 `IAsyncReadFileHandle` 과 IoStore 우선순위 큐, 유니티의 `AsyncReadManager` 에 해당합니다.
동시에 OS에 걸린 요청 수는 `_maxInFlightCount` 로 제한합니다. 그래야 뒤에 온 급한 요청이 대량 요청 뒤에 줄 서지 않습니다. 같은 우선순위는 들어온 순서입니다.

백엔드는 세 가지입니다. Windows는 오버랩드 IO와 완료 포트, 리눅스는 liburing 없이 시스템 호출로 쓰는 io_uring, 그 밖에는 스레드 풀입니다.
`Auto` 는 플랫폼 백엔드를 고르고, io_uring을 쓸 수 없으면(옛 커널, WSL1, 컨테이너 seccomp) 로그 한 줄과 함께 스레드 풀로 내려갑니다. 명시한 백엔드를 쓸 수 없으면 `initialize` 가 실패합니다.
정책(우선순위, 상한, 취소, 파일 열기, 버퍼 준비, 완료 전달)은 `AsyncFileIoQueue` 한 곳에 있고, 백엔드는 "꺼내서 걸고, 끝나면 알린다"만 합니다.

- 파일 열기와 크기 확인, 버퍼 할당은 IO 스레드가 하고, 버퍼는 **요청한 스레드의 메모리 태그**로 셉니다.
- 완료 콜백은 요청마다 한 번 불립니다. `TaskManager` 를 넘기면 그 워커에서, 아니면 IO 스레드에서 돕니다. `readFileFuture` 는 결과를 `TaskFuture` 로 돌려줍니다.
- 결과는 `Succeeded`, `Canceled`, `FileNotFound`, `OutOfRange`, `ReadFailed`, `ShutDown` 중 하나입니다. 구간이 파일 끝을 넘으면 짧게 읽지 않고 `OutOfRange` 로 실패합니다.
- 큐에 있는 요청을 취소하면 OS에 넘기지 않고, 이미 걸린 요청은 읽은 뒤 결과를 버립니다. 언리얼, 유니티와 같은 "최선" 취소입니다.

엔진은 서비스 하나(`engine::getAsyncFileIo()`)를 기동 단계 `FileIo` 에서 만들고, Task와 모듈 이미지보다 먼저 종료합니다. 걸린 읽기와 완료 태스크를 모두 기다립니다.
`PlatformFileUtil::openNativeFileForRead` 와 `readNativeFileAt` 은 공유 파일 위치가 없는 위치 지정 읽기라 여러 스레드가 한 핸들을 잠금 없이 읽습니다.

### 압축

`Compression/` 에는 코덱 인터페이스(`ICompressionCodec`), 레지스트리, 스트림(`CompressionStream`), 내장 코덱(None, RLE)이 있습니다. zlib, zstd, LZ4는 외부 라이브러리를 쓰므로 `Engine/Compression` 에 있습니다.
스트림은 28바이트 머리로 시작합니다. 'SWCS' 표식, 버전, 코덱, 두 크기, FNV-1a 체크섬이 들어 있고, 지금 버전만 읽습니다.
`CompressionCodecType` 값은 디스크에 저장되므로 새 코덱은 뒤에만 더합니다. 목록에 없는 알고리즘은 `Custom` 입니다.

레지스트리는 엔진 서비스 하나(`engine::getCompressionCodecRegistry()`)이고, `CompressionCodecRegistry::setActive` 로 Core의 슬롯에 연결됩니다.
슬롯이 비어 있으면(Core만 링크하는 도구) 스트림은 내장 코덱만 씁니다. 외부 코덱은 `EngineCompressionCodecUtil::registerAll` 이 한 번에 등록합니다.

## 확장하는 법

### Core에 새 파일 더하기

1. 파일을 하는 일에 맞는 폴더에 둡니다. Engine 헤더를 include해야 한다면 그 파일은 Core가 아니라 Engine에 있어야 합니다.
2. `Source/Core/CMakeLists.txt` 의 소스 목록에 경로를 적습니다. 목록은 GLOB이 아니라 경로 문자열이라, 빠뜨려도 컴파일러가 알려 주지 않습니다.
3. 플랫폼마다 다른 코드는 위 "플랫폼 의존 코드를 두는 곳"의 규칙을 따릅니다.

### 새 메모리 태그 더하기

`Memory/MemoryTag.h` 에 태그를 더하고, `MemoryProfiler::getMemoryTagName` 의 이름 테이블에도 한 줄을 더합니다. 줄 수는 `static_assert` 가 확인합니다.

### 등록 목록 만들기

소유하지 않는 포인터를 등록받는 목록은 `Container/RegistrationList<T>` 를 씁니다. 중복과 이름 거절, 순서, 이름으로 찾기, 이름 사본을 처리합니다.
슬롯 인덱스로 O(1) 제거를 하는 목록(프리미티브, 틱, 트랜스폼 계층, 콜라이더)과 구조가 다른 레지스트리(TypeRegistry, 전역 변수, 코덱, RHI 백엔드)는 예외입니다.

## 함정과 주의

### 문자열과 이름

**에셋이나 파일 내용으로 `hashed_string` 을 만들지 마세요.** intern 테이블은 줄어들지 않고 영구히 남습니다. 상한에 닿으면 그 뒤 엔진의 **모든** 새 이름이 None이 됩니다.
있는지만 찾으려면 `hashed_string::findInterned`, 해시만 필요하면 `computeHash( string_view )` 를 씁니다. intern 개수는 `getInternedCount` 로 진단합니다.

**`hashed_string` 은 FName 규칙이지만 세 가지가 다릅니다.** 이 차이를 되돌리지 마세요. 해시는 실행마다 같은 FNV라 저장할 수 있고, 철자는 모든 구성에서 보존하고, 숫자 꼬리가 없습니다.
`operator<` 는 없으므로 `HashedStringLexicalLess` 나 `HashedStringFastLess` 를 씁니다. 대소문자만 바꾸는 이름 변경은 `isEqual( …, NameCase::CaseSensitive )` 로 확인합니다.

**데이터 이름 `None` 은 빈 이름입니다.** `hashed_string( "None" )` 은 언리얼 `FName` 처럼 `empty()` 입니다. 항목 이름은 `Off`, `Bare` 처럼 짓습니다.

**문자열 해시 식을 바꾸지 마세요.** `StringUtil::computeHash64`(FNV-1a)에 쿠킹 산출물, 셰이더 쿠킹 스탬프, 파이썬 쿠커가 의존합니다. `RuntimeStringHash` 는 프로세스 안에서만 씁니다.
`computeHash64( "리터럴", false, seed )` 는 포인터 오버로드가 골라져 키가 상수가 됩니다. `string_view` 로 넘깁니다.

**`formatstring` 에 `string_view` 를 `.data()` 로 풀어 넘기지 마세요.** 뷰 끝을 지나 읽습니다. `string_view` 는 그대로 넘기면 길이로 씁니다(`CheckLogViewArgument.py`).
`%#` 은 순수 자리표이고, 모르는 `%…` 는 글자 그대로 나오고, 인자 수가 맞지 않으면 Debug에서 단언합니다. 자세한 규칙은 `FormatString` 클래스 주석에 있습니다.
로캘은 C 로캘 그대로입니다. 잘못된 UTF-8은 `escapeInvalidUtf8` 로 처리합니다. `fixed_string` 은 넘치면 글자 경계에서 자르고 경고합니다.

**`StringUtil` 은 비-ASCII 바이트를 `uint8` 로 넓혀 다룹니다.** UTF-16 문자열에 같은 치환을 하지 않고, `stristr` 을 바이트 묶어 읽기로 "최적화"하지 않습니다.
Win32 문자열 변환은 `utf8ToUtf16` 을 씁니다. `ImmGetCompositionStringW` 는 글자 수가 아니라 바이트 수를 돌려줍니다. `std::hash` 는 `string`, `wstring`, `fixed_string` 모두 `RuntimeStringHash`(대소문자 구분)입니다.

### 컨테이너와 메모리

**`sw::unordered_map` 과 `sw::map` 안 원소의 포인터를 잠금 밖으로 내주지 마세요.** `unordered_map` 은 밀집 배열, `map` 은 정렬 벡터라 삽입과 삭제가 원소를 옮깁니다. 복사로 주거나 값을 `unique_ptr` 로 보관합니다.
짧은 `string` 의 `c_str()` 도 이동 뒤에는 빈 버퍼를 가리킵니다.

**`sw_new T[n]` 에는 원소 소멸자를 부르는 해제 짝이 없습니다.** 소멸자가 있는 원소는 `sw_new_array` 나 `vector` 로 담습니다. `sw_delete_array` 의 `static_assert` 가 막습니다.
`make_unique<T[]>` 의 해제자도 원소 소멸자를 부르지 않습니다.

**해시 버킷 번호는 `bucketIndexOf` 하나로 구합니다.** 스무 곳이 이 함수를 쓰고, 한 곳만 달라도 원소가 사라집니다. 버킷 수는 2의 거듭제곱입니다.

**`SlotHandleTable` 의 점유 표시와 세대를 둘로 나누지 마세요.** 한 워드(`_state`)에 함께 있어야 arm64에서 원자적으로 맞습니다.
잠금 없이 읽고 주소가 고정되어야 하면 `PagedArray` 를 씁니다. 순서 없는 삭제는 `VectorUtil::removeAtSwap` 이고, 표준에 없는 함수를 `vector.h` 에 붙이지 않습니다.

**잠금을 잡은 getter는 자기 멤버의 뷰나 참조를 돌려주면 안 됩니다.** 값으로 돌려줍니다. 콜백을 부르는 순회는 인덱스로 돌고, 부르기 전에 델리게이트를 복사합니다.
`MulticastDelegate` 는 복사와 이동 연산 네 개를 직접 정의합니다. `Delegate` 의 인라인 람다를 이동하면 원본의 소멸자를 부릅니다.

**`sw::vector` 는 `is_bitwise_copyable_v` 타입을 `Memory::copy` 한 번으로 옮깁니다.** ReflectionParser도 이 동작에 의존합니다. 함수 인자로 받는 연속 뷰는 `vector_reference<const T>` 로 씁니다.

**완료를 모으는 줄에 고정 용량 큐(`ConcurrentQueue`)를 쓰지 마세요.** 가득 차면 `enqueue` 가 false를 돌려주고 그 완료는 조용히 사라집니다.
에셋 스트리밍 큐가 그렇게 한 경로의 콜백 중 1024개를 넘는 것을 잃었습니다(`AssetStreamingTest.ManyCallbacksOnOnePathAreAllDelivered`). 상한 없는 잠금과 deque로 둡니다.

**Debug 경합 검출기는 워커의 비-const `sw::vector::operator[]` 를 쓰기로 셉니다.** 읽기만 한다면 `std::as_const(v).data()` 나 const 참조로 읽습니다. `sw::array` 를 락프리 버퍼에 쓰지 않습니다.
교착 검출기를 피해야 하는 곳(실패 기록 같은 곳)은 `std::mutex` 를 씁니다. `compare_exchange_weak` 은 실패 순서도 명시합니다.

**STL 구성(`SW_ENABLE_STL_CONTAINER=ON`, CI의 `CI-Debug-STL`)은 C++17입니다.** std 해시 컨테이너에 이종 조회와 `contains` 가 없어 sw 쪽 얇은 클래스가 메웁니다. 커스텀 컨테이너 전용 테스트는 그 구성에서 건너뜁니다.

**`drainEvents` 는 받는 쪽 목록에 붙이고 원본을 비웁니다.** 바꿔치기(`swap`)하면 받는 쪽 목록의 앞 내용이 지워집니다.
매 프레임 같은 목록을 쓰는 쪽은 먼저 `clear()` 합니다. 그러지 않으면 같은 알림을 매 프레임 다시 받습니다. StarSkirmish의 패배 로그가 두 번 나왔습니다.

**`EventDispatcher` 의 큐(`push`)는 아무 스레드나 쓰지만, 버스(`subscribe`, `publish`)는 `processEvents` 를 부르는 스레드만 씁니다.** 큐에 남은 이벤트는 `destroyQueuedEvents()` 로 지웁니다.

### 플랫폼

**X11 헤더는 X11을 쓰는 `.cpp` 에서만 include하세요**(`Common/X11Headers.h`, `CheckX11Isolation.py`). `PlatformOsHeaders.h` 는 PCH로 모든 파일에 들어가므로 X11을 넣지 않습니다.
X11이 퍼지면 `Convex`, `None` 같은 매크로가 서드파티 헤더를 덮어 Jolt(`EShapeType::Convex`)가 리눅스 빌드만 깨집니다. Windows 빌드로는 원리상 볼 수 없습니다.
유니티 빌드는 X11 `.cpp` 를 include 줄로 찾아 유니티 배치에서 뺍니다(`sw_skipUnityForX11Sources`). Xlib, GLX, XKB 헤더를 더 include한 뒤에는 `X11MacroUndef.h` 를 다시 include합니다.

**플랫폼 스텁도 인터페이스를 따라가야 합니다.** `IWindow` 에 가상 함수를 더하면 `Win32Window` 의 비-Windows `#else` 스텁에도 정의를 둡니다. 빠지면 리눅스 링크만 실패합니다.

**Win32 API는 W 버전을 이름으로 부르세요.** 이 저장소는 UNICODE를 정의하지 않아 `DefWindowProc`, `LoadCursor`, `CreateFile` 같은 일반 이름이 A 버전입니다.
W 클래스로 만든 창의 프로시저가 `DefWindowProcA` 로 끝나 제목이 "S" 한 글자가 된 적이 있습니다(`WindowTest.TitleReachesTheOsAsUtf16`). `IDC_*` 는 `reinterpret_cast<LPCWSTR>` 로 넘깁니다.
전역 UNICODE 정의(언리얼 방식)는 대상마다 정의가 빠지면 조용히 A 버전으로 돌아가므로 택하지 않았습니다.

**A 버전도 부르지 마세요.** 모든 실행 파일은 `WindowsProcess.manifest` 로 ANSI 코드 페이지가 UTF-8이라 지금은 A 버전에 UTF-8 경로를 넘겨도 동작합니다.
하지만 매니페스트가 없는 호스트(다른 프로세스에 로드된 Engine.dll, 1903 이전 Windows)에서는 깨집니다. 그래서 `CheckWin32WideCalls.py` 가 A 버전 호출도 막습니다(예외는 `OutputDebugStringA`).
새 실행 파일에 매니페스트(`sw_embedProcessManifest`)를 빠뜨리면 한글 경로를 읽지 못합니다.

**X11 창 제목은 `_NET_WM_NAME`(UTF8_STRING)까지 적어야 합니다.** `XStoreName` 은 Latin-1이라 한글이 깨집니다. 확인하는 테스트가 없으므로 리눅스에서 `xprop _NET_WM_NAME` 으로 봅니다.
로드된 이미지는 이름이 아니라 주소로 찾습니다(`ModuleBuildId::find( &함수 )._modulePath`). 리눅스에서는 `Lib/libEngine.so` 라 이름으로 찾던 테스트가 늘 건너뛰어졌습니다.

**MSVC 확장을 쓸 수 있는지는 `SW_PLATFORM_WINDOWS` 로 묻습니다.** clang-cl은 `SW_COMPILER_CLANG` 이라, `SW_COMPILER_MSVC` 로 물으면 clang-cl이 다른 분기로 갑니다.
아키텍처는 `CMAKE_CXX_COMPILER_ARCHITECTURE_ID` 로 판정합니다. 교차 컴파일에서 `CMAKE_SYSTEM_PROCESSOR` 는 틀립니다. ReflectionParser는 CMake를 거치지 않으므로 `ParserConfig::load` 가 대상 매크로를 넘깁니다.
리눅스 arm64와 macOS 분기는 실제로 빌드해 본 적이 없습니다.

**리눅스에서만 빌드한 코드를 들일 때는 세 가지를 먼저 보세요.** 첫째는 Windows 매크로 `near`, `far`, `small` 과 겹치는 이름입니다.
둘째는 DLL이 내보내지 않은 타입입니다. 리눅스 .so는 모두 내보내므로 거기서는 드러나지 않습니다. 셋째는 같은 이름의 타입이 두 모듈에 있는 ODR 위반입니다(`CheckDuplicateTypeNames.py`).

### 파일과 프로세스

**파일 쓰기는 원자적으로 합니다**(`writeAtomically`). 같은 폴더의 임시 파일에 쓰고 `replaceFile` 로 바꿉니다. Windows 읽기는 Win32 API로 합니다. `fopen_s` 는 ANSI라 한글 경로가 깨집니다.

**`std::filesystem` 을 직접 쓰지 마세요**(`CheckStdFilesystemIsolation.py`). 순회는 `forEachDirectoryEntry`, 테스트의 파일 시각 조작은 `setFileWriteTime` 을 씁니다.
`FileUtilStdFileSystem.cpp` 안에서도 `path` 는 넓은 문자로 만듭니다. 좁은 문자 생성자는 잘못된 UTF-8에서 예외를 던집니다. 오류는 `error_code` 버전으로만 받습니다.

**파일 감시기가 알림을 잃으면 빈 파일 이름의 `Modified` 를 보냅니다.** 다시 훑으라는 신호이고, `expandRescanEvents` 가 풉니다.
`getFileTimestamp` 는 초 단위입니다. 셰이더 소스 캐시처럼 더 세밀한 판단이 필요하면 크기와 시각을 함께 보는 `getFileStamp` 를 씁니다.

**자식 프로세스는 출력 파이프 하나만 물려받습니다.** Windows는 `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`, POSIX는 CLOEXEC와 `close_range` 를 씁니다. 그러지 않으면 동시에 띄운 자식이 서로의 파이프를 잡습니다.
`Process::terminate` 는 다른 스레드가 `readOutputLine` 이나 `waitForExit` 을 도는 중에 불러도 됩니다. 기다리지 않을 실행은 `Process::launchDetached` 를 씁니다. `execute` 는 부른 스레드를 멈춥니다.

**모듈 이미지를 언로드하기 전에 그 코드를 가리키는 등록을 떼야 합니다.** `IModuleUnloadListener`(`Module/ModuleUnloadListener.h`)가 델리게이트 스텁이나 vtable을 보관하는 레지스트리의 공통 계약입니다.
같은 수명 계약의 Engine 쪽은 `Engine/Module`, 호스트 쪽은 `ModuleHost` 입니다. 완료 콜백이 핫 리로드되는 모듈 코드를 가리키는 비동기 파일 IO도, 모듈을 언로드하기 전에 취소하고 기다립니다.

### 비동기와 크래시

**비동기 IO의 완료 콜백은 태스크 워커에서 돕니다**(엔진 설정). 팩 압축 해제와 CRC 검사가 IO 스레드를 막지 않게 하기 위해서입니다.
그래서 `AsyncFileIo` 종료는 Task보다 먼저입니다. 콜백 안에서 자기 큐의 잠금을 쥔 채 IO를 걸지 마세요. 종료 뒤의 요청은 그 스레드에서 바로 완료되어 교착합니다.

**`SW_ASSERT` 는 Release와 Shipping에서 사라지고, `SW_LOG_ASSERT` 는 Debug에서 `SW_DEBUG_BREAK` 까지 합니다.** 디버거가 없는 CI에서는 프로세스가 죽으므로, 방어 경로 테스트는 Release와 Shipping에서 합니다.
배포 구성의 `SW_LOG_ASSERT` 는 진행하므로, 뒤 코드가 그 전제에 의존한다면 하드 단언을 씁니다.

**엔진이 만드는 스레드는 시작할 때 `CrashHandler::initializeCurrentThread()` 를 부릅니다.** 빠뜨리면 스택 오버플로 덤프가 0바이트가 됩니다.
Windows 덤프는 보고 스레드가 `PssCaptureSnapshot` 으로 씁니다. 살아 있는 자기 프로세스를 `MiniDumpWriteDump` 하면 로더 잠금에서 멈춥니다.
보고 시한은 `setReportDeadline`(20초)이고, POSIX는 `alarm` 과 SIGALRM을 씁니다. 실물 확인은 `-gv_crashTest=1..5` 로 합니다.
보고 프로세스는 `setReporterExecutable` 을 정한 호스트(App)만 띄웁니다. 경로를 `getExecutablePath` 로 잡으면 테스트 실행 파일이 자기를 끝없이 다시 띄웁니다.
Debug 기동은 CRT 누수 보고도 stderr로 냅니다(`MemoryTagTest.DiagnosticBootstrapEnablesPlatformLeakChecks`).

**싱글턴으로 둘 수밖에 없는 것이 있습니다.** `CrashContextStore` 는 시그널 핸들러가 읽고, 등록자 헤드와 `TestRegistry` 는 main 이전에 스스로 등록하며, `TagID` 와 `hashed_string` intern은 프로세스 전역이어야 뜻이 있습니다.
그 밖에 Core에 인스턴스가 필요하면 Logger 구조를 따릅니다. 인스턴스는 `EngineLoop` 이 가지고, Core에는 포인터 슬롯만 둡니다.

### 데이터 형식

**명령줄 철자는 인자마다 하나입니다.** `Predefined/ArgumentList.xxx` 의 줄에 적은 철자만 키이고, 열거자 이름(`WIDTH`, `COOK_SHADERS`)은 키가 아닙니다(`CommandLineTest.EnumeratorNameIsNotACommandLineKey`).
예외로 RHI 백엔드 인자만 쿠킹 계약(`CookContract.json`)의 별칭 여러 개를 받습니다.

**X-macro 목록 `.xxx` 의 원본은 `Predefined/` 입니다.** 사본이 남으면 `CheckDataFileReferences.py` 가 막습니다.
`PredefinedNameType.xxx` 의 줄 순서가 곧 intern 인덱스라 중간에 끼워 넣지 않고, 대소문자만 다른 이름도 넣지 않습니다. 게이트의 제외 폴더는 게이트마다 따로 두지 않고 `kNotOurDirNames` 를 씁니다.

**4글자 표식은 `FourCcUtil::make( "...." )` 하나로 만듭니다.** 파일 바이트 순서 그대로입니다. 16진수로 손으로 적으면 바이트 순서가 갈립니다.
형식을 바꾸면 커밋된 데이터(`Test/AppTest/Golden/*.ppm.z` 압축 스트림 등)의 앞 네 바이트도 함께 고칩니다.

**팩의 압축 enum과 스트림의 압축 enum을 `static_cast` 로 오가지 마세요.** `PackCompressionType` 과 `CompressionCodecType` 은 서로 다른 디스크 형식입니다. 변환은 `PackCompressionUtil::kArrCodecMapping` 한 곳에서 합니다.
모듈이 코덱을 등록했다면 그 모듈의 shutdown에서 `unregisterCodec` 합니다. 레지스트리는 `Engine.dll` 에 있어 모듈보다 오래 삽니다. zlib은 Windows에서 4GB, LZ4는 2GB가 한계입니다.

### 수학

**`quaternion::inverse()` 와 `conjugate()` 는 const가 아닌 값에서 제자리 버전(void)이 골라집니다.** 식 안에서는 const 참조로 받아서 부릅니다(`RigIkSolver::makeInverse`).
**`quaternion::fromToRotation` 은 코사인 차 1e-6(약 0.08°) 안쪽을 단위 회전으로 버립니다.** 반복 IK의 마지막 몇 mm가 그 범위에 들어 CCD가 멈춥니다(`RigIkSolver::makeFromToRotation`).

### 서비스 비동기 API

**여러 서비스가 함께 쓰는 비동기 API는 결과를 "꺼내 가기(poll)"로 두지 마세요.** 소비자가 둘이면 서로의 결과를 가져갑니다.
맡길 때 델리게이트를 받아 그 요청에만 알립니다(`EphemeralStoreRouter`, `IAccountPresence`). 요청 id와 델리게이트의 대응은 보내기 전에 등록합니다. `sendRequest` 가 그 자리에서 실패를 알릴 수 있기 때문입니다(`ServiceClientCallTable::send`).
라우터보다 먼저 종료되는 델리게이트 주인은 기다리던 요청을 `cancel` 합니다.

**지표 라벨에는 추적 id나 계정 id를 넣지 마세요.** 시리즈가 끝없이 늘어납니다. 운영 지표와 상태와 HTTP 엔드포인트는 Engine(`Engine/Observability`)에 둡니다. 전용 서버 실행 파일은 GameFramework DLL을 링크하지 않기 때문입니다.

네트워크 계층의 함정은 [Network](Network/README.md)에 있습니다.

## 더 볼 곳

- [Task](Task/README.md) — 워커 풀과 태스크 그래프
- [Network](Network/README.md) — 네트워크 공통 계층
- [Engine](../Engine/README.md) — Core 위의 엔진 본체와 기동 순서
- [07 설정](../../docs/07_Configuration.md) — 실행 인자와 전역 변수 참조 문서

| 파일 | 내용 |
|---|---|
| `GlobalVariable/GlobalVariableManager.h` | 전역 변수 매크로와 종류 |
| `Log/Logger.h` | 로그 매크로와 상세도 |
| `Memory/Memory.h`, `Memory/MemoryTag.h` | 할당 함수와 메모리 태그 |
| `String/hashed_string.h` | 이름 문자열 |
| `File/AsyncFileIo.h` | 비동기 파일 읽기 |
| `Time/MonotonicClock.h` | 시계, 스톱워치, 기한 |
| `Common/TargetMacroCheck.h` | 컴파일러 매크로와 CMake 판정 대조 |
