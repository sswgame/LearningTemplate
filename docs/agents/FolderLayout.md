# FolderLayout

> 질문: 새 파일 · 옮길 파일을 어느 폴더에 두는가?
> 독자: 이 저장소에서 파일을 만들거나 옮기는 에이전트.
> 결론: 이름 낱말(Util · Registry · Component)이나 처음 쓰는 쪽이 아니라 **하는 일**로 자리를 정한다. 아래 표가 정본이다. 굵게 쓴 폴더는 아직 없는 목표 자리이고, 남은 이동은 [NEXT](../NEXT.md) 에 있다.

- 잡동사니 폴더(`Util` · `Utility` · `Helpers` · `Misc`)를 새로 만들지 않는다 — 하는 일(Encoding · Algorithm · Hash …)로 묶는다. 린트 `folder-layout`(표 `Config/Dev/FolderLayout.json`)가 막는다.
- 층(티어) 규칙이 먼저다: 하위 층이 상위 층을 include 하지 않는다(cmake 층 표 · `docs/agents/Includes.md`).
- 개발 기능은 `Source/DevTools/`(언리얼 Developer 모듈 자리)로. 엔진 쪽 `Dev/` 폴더는 린트가 막고, DevTools 가 볼 수 없는 모듈 안(게임 · App · ModuleManager · GF 내부 · 리플렉션 에셋)만 `Config/Dev/FolderLayout.json` `_devFolderAllowed` 에 까닭과 함께 둔다. 엔진 내부를 봐야 하면 엔진이 확장점(등록 목록 · 인터페이스 · 프레임 훅)을 내고 DevTools 가 붙는다.
- 약어는 파일 · 폴더 · 식별자 모두 대문자(`FourCCUtil` · `HPACKDecoder`) — 린트 `acronyms`(표 `Config/Dev/Acronyms.json`).
- Core 티어 역류는 모든 티어에서 린트(`CoreTiers.py`, 하위 폴더 티어 `[subtier]`)가 막는다.

## 폴더마다 무엇을 두는가

> 한 파일의 자리는 "이 파일이 하는 일"로 정한다 — 이름 낱말(Util · Registry · Component)이나 쓰는 쪽이 아니라. 두 자리 다 맞으면 **낮은 층**에 둔다(위 층이 아래를 include 할 수 있으므로).

### 1 타깃

| 타깃 | 두는 것 | 두지 않는 것 |
| --- | --- | --- |
| `Core` | 엔진 · 게임을 모르는 기반(형식 · 컨테이너 · 글자 · 수학 · 스레드 · 파일 · 로그 · 네트워크 전송) | 씬 · 에셋 · 리플렉션 · 게임 낱말 |
| `Engine` | 런타임 제품 기능(배포 게임이 쓰는 것)과 그 확장점 | 편집기만 부르는 함수, 개발 전용 도구 구현(확장점만 둔다) |
| `DevTools` | 엔진 확장점에 스스로 붙는 개발 도구(배포 구성 미빌드). 클라이언트 전용은 `DevTools/Client` | 게임 · GF · ModuleManager 내부를 아는 것(DevTools 는 그것을 못 본다 — TargetLayers) |
| `ModuleManager` | 모듈 로드 · 매니페스트 · 호스트 공용 기동 열. 개발 전용 핫 리로드는 `Dev/`(`Dev/LiveReload`) — 정적 라이브러리 내부라 DevTools.dll 로 못 간다 | 엔진 기능 |
| `App` · `Server` | 그 실행 파일에만 있는 호스트 배선(창 · OS 서비스 · 에디터 모듈 호스트 · 운영 채널) | 엔진 기능, 개발 도구 구현(호스트에는 "붙이는 줄"만) |
| `Editor` | 편집기 기능 전부 | 런타임이 쓰는 것 |
| `GameFramework` | 장르 공통 게임 틀(Runtime) · 키트(Kits) · 편집기 플러그인(Plugin). 클라이언트 전용은 `Client/` | 엔진 기반에 둬도 되는 범용 수학 · 컨테이너 |
| `Programs` | 따로 빌드하는 도구 실행 파일(언리얼 Source/Programs) | |
| `Games` | 게임 하나 | |

### 2 Core 폴더 (티어 = 숫자 큰 쪽이 위)

| 폴더(티어) | 두는 것 | 대조 |
| --- | --- | --- |
| `Foundation`(0) | 형식 별칭 · 매크로 · OS 헤더 · enum 깃발 · 글자 하나 수준 도구(ASCII · 16진 숫자 — Hash 가 써서 여기) | UE Core/HAL · Templates |
| `Hash`(0) | 해시 · 씨앗 상수 · 비밀 비교 · PRF | UE Core/Hash |
| `Predefined`(0) | `.xxx` 이름 표 | |
| `Concurrency`(1) | 원시 동기화(atomic · mutex · SpinLock · Futex · StopToken)와 경합 검출 훅. 스레드 사이에서 나눠 쓰는 자료 구조(ConcurrentQueue · WorkStealingDeque · SpscQueue · LockedSwapQueue · SharedVectorPool)는 하위 폴더 **`Concurrency/Queue`**(Container 위 티어 — vector 를 쓰므로)에 둔다 | UE HAL/Thread*, Async |
| `Math`(1) | 벡터 · 행렬 · 쿼터니언 · 듀얼 쿼터니언 · 색 · 압축 수치 형식 · 결정적 난수 · 노이즈 | UE Core/Math, Godot core/math |
| `Memory`(2) | 할당기 · 메모리 태그 · 블록 | UE HAL/Memory |
| **`Algorithm`**(2) | 컨테이너 형식에 묶이지 않은 알고리즘(정렬 비교자 · 연속 배열 도우미 · 유니온 파인드) | UE Core/Algo |
| `Container`(3) | 컨테이너 · 핸들 표 · 슬롯 · 링 · 큐(락 · lock-free 모두) · 그래프 자료 구조 | UE Containers, Godot core/templates |
| `String`(4) | 로그 없는 글자 도구: UTF 변환 · 분할 · 포맷 · 빌더 · 이스케이프 · 마크업 스캐너 · enum 이름 표 | UE Core/String · Misc/CString, Godot core/string |
| **`Encoding`**(4) | 바이트 ↔ 글자 인코딩(Base64 · Hex · FourCC) | UE Misc/Base64 · String/BytesToHex, Godot core/crypto(base64) |
| `Serialization`(4) | 바이트 · 비트 직렬화와 그 정수 인코딩(VarInt · LittleEndian) | UE Serialization, Godot core/io/marshalls |
| `Delegate` · `Path` · `UUID` · `Subsystem` · `Spatial`(4) | 이름 그대로 | |
| `Log`(5) | 로그 매크로 · 창구 · 싱크 포트 | |
| **`Name`**(6) | intern 이름(`hashed_string` · `TagID`) — 로그를 쓴다 | UE UObject/NameTypes(FName), Godot StringName |
| **`Registry`**(6) | 레지스트리 공통 구조(`RegistrationList` · `NameRegistry`) | (UE 는 모듈마다 따로 — 공통 틀은 여기 하나) |
| `CommandLine` · `Compression` · `Process` · `Time`(6), `GlobalVariable` · `Task`(7), `File` · `Network`(8), `Module`(9), `Diagnostics` · `Event` · `Testing`(10) | 이름 그대로 | |

### 3 Engine 폴더 (층 = EngineLayers.toml)

| 폴더(층) | 두는 것 |
| --- | --- |
| `Foundation`(0) | 아래 층이 앞으로 선언만 하는 핸들 · 엔진 상수 · 서비스 창구 |
| `Reflection`(1) | 형식 정보 · 리플렉션 값(ReflectedValue · ReflectedAny · **InstancedStruct**) |
| `Serialization` · `Config` · `Content` · `Console` · `Observability` · `Profiling`(1) | 이름 그대로. `Profiling` 은 **런타임 계측과 그 포트만**(3 절 7) |
| `Behavior`(1) | 조건식 · 동작 어휘(행동 트리 · 스케줄 · 대화 공용 — 옛 Engine/Gameplay, GF Runtime/Gameplay 와 이름이 겹쳐 바꿨다) |
| `Asset` · `Text` · `Localization`(2) | 에셋 · 글 배치 · 번역. 기동 서브시스템 중 "팩 · 엔진 데이터 읽기" 는 `Asset` |
| `Presentation`(2) | 게임 코드가 UI · 오디오 없이 아는 창구(I*Sink · I*Driver · 뷰모델) — 모든 표현 창구는 여기 |
| `Window` · `Input`(3), `Graphics`(4: RHI · Shader · Material · Texture · Mesh — GPU 자원과 CPU 쪽 그래픽 데이터) | |
| `Object` · `Scene`(5) | 컴포넌트 모델 · 씬 |
| 기능 폴더(6: **`Visual`**(Rendering + 2D) · Physics · Audio · Animation …) | 자기 컴포넌트는 `<기능>/Component`, 씬 서비스 · 시스템은 `<기능>/Scene`, 데이터는 `<기능>/` 바로 아래 |
| `Renderer`(7) | 렌더 스레드 · 프레임 그래프 · GPU 파이프라인 |
| `Module` · `Automation`(8) | 엔진 루프 조립 · 핫 리로드 바인딩 · 자동화 창구(감시 값 · 단계 등록 · 시나리오 형식 · `IAutomationRuntime`). 실행기 · 판정 · 세션 · 녹화는 `DevTools/Client/Automation`(언리얼 Automation 컨트롤러 · Gauntlet 이 Developer 모듈인 자리) |

### 4 그 밖의 규칙

| 규칙 | 까닭 |
| --- | --- |
| `Util` · `Utility` · `Helpers` · `Misc` 폴더 금지(린트 `folder-layout`). `Common` 은 새로 만들지 않는다(옛 `Editor/Common` 만) | 잡동사니 폴더는 하는 일로 찾을 수 없다(옛 GF `Runtime/Foundation/Utility` 를 해체했다) |
| `<기능>/Component` 에는 `*Component` 만 | Navigation · Audio · Physics 의 Component 폴더에 씬 서비스 · 질의 · 검사가 섞였다(4 절) |
| 엔진 쪽 `Dev/` 금지(린트 `folder-layout`) — 개발 기능은 `DevTools`, 예외는 표의 `_devFolderAllowed` 만 | DevTools 는 Engine 을 보지만 GF · 게임 · ModuleManager · App 은 못 본다 — 그 안에 붙어야 하는 것만 예외 |
| 실행 중 바꾸기 · 핫스왑은 개발, 기동 때 고르기는 제품 | 언리얼도 RHI · 모듈 교체는 재시작(10 절) |

## 판정해 그대로 두는 것(다시 보지 않는다)

- `Core/Foundation/BitFlagTrait.h` — 생성 헤더(`*.gen.h` · 강제 include `FlagOps.gen.h`)가 include 하는 잎이라 `<type_traits>` 밖을 끌어오면 모든 TU 의 include 누락을 가린다(헤더 머리말). EnumUtil 에 합치지 않는다.
- `Engine/UI/Screen/UIScreenSwitch` — 개발 화면 스위치의 **등록 틀(확장점)** 자체다. DevTools(`OptionsMenuSwitch`) · 게임(Empty Client/Dev)이 여기에 붙는다.
- `Core/Diagnostics/SubsystemTimingReport` — DevTools 가 아니라 Core 진단이다: 시계 걸기(개발 구성만)와 한 줄 요약뿐이고 Core 형식만 쓴다. 엔진 루프 · 에디터 기동 로그가 부른다(언리얼 `FScopedBootTiming` 도 Core).
- `Engine/Profiling/FrameProfileReport` — `FrameProfiler`(런타임 계측)가 내는 보고 값 형식. 보고를 쓰는 쪽(FrameProfileSession)은 DevTools 다.
- 그 밖의 판정(Process/ThreadCrashStack · Math/SecureRandom · Container/DependencyGraph · Container/string.h · Config/NetEmulationPresetTable · Foundation/AnimationNotifyPhase · Observability ↔ Telemetry · Compression 둘 · 쿠킹 셋 · Online 이름 겹침)은 FolderLayout 정본과 같다.
