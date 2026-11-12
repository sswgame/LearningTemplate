# FolderLayout

> 질문: 새 파일 · 옮길 파일을 어느 폴더에 두는가?
> 독자: 이 저장소에서 파일을 만들거나 옮기는 에이전트.
> 결론: 이름 낱말(Util · Registry · Component)이나 처음 쓰는 쪽이 아니라 **하는 일**로 자리를 정한다. 두 자리가 다 맞으면 낮은 층에 둔다(위 층이 아래를 include 할 수 있으므로). 아래 표가 정본이고, 남은 이동은 [NEXT](../NEXT.md) 에 있다.

층(티어) 규칙이 먼저다 — 하위 층은 상위 층을 include 하지 못하고 빌드가 막는다(층 표 `Config/Rules/Layers/*.toml`, 규칙은 [Includes](Includes.md)). 표에 없는 파일 · 폴더는 configure 가 멈추므로 새 폴더는 층을 정하고 들어온다.

## 폴더마다 무엇을 두는가

### 1 타깃

| 타깃 | 두는 것 | 두지 않는 것 |
| --- | --- | --- |
| `Core` | 엔진 · 게임을 모르는 기반(형식 · 컨테이너 · 글자 · 수학 · 스레드 · 파일 · 로그 · 네트워크 전송) | 씬 · 에셋 · 리플렉션 · 게임 낱말 |
| `Engine` | 런타임 제품 기능(배포 게임이 쓰는 것)과 그 확장점 | 에디터만 부르는 함수, 개발 도구 구현(확장점만 둔다) |
| `DevTools` | 엔진 확장점에 스스로 붙는 개발 도구(배포 구성은 짓지 않는다 — 언리얼 Developer 모듈). 클라이언트 전용은 `DevTools/Client` | 게임 · GF · ModuleManager 내부를 아는 것(DevTools 는 그것을 못 본다 — `TargetLayers.toml`) |
| `ModuleManager` | 모듈 로드 · 매니페스트 · 호스트 공용 기동 열. 개발 전용 핫 리로드는 `Dev/LiveReload`(정적 라이브러리 내부라 DevTools 로 못 간다) | 엔진 기능 |
| `ModuleAPI` | 모듈 C-ABI 계약 | |
| `App` · `Server` | 그 실행 파일에만 있는 호스트 배선(창 · OS 서비스 · 에디터 모듈 호스트 · 운영 채널) | 엔진 기능, 개발 도구 구현(호스트에는 "붙이는 줄"만) |
| `Editor` | 에디터 기능 전부 | 런타임이 쓰는 것 |
| `GameFramework` | 장르 공통 게임 틀(`Runtime`) · 키트(`Kits`) · 에디터 플러그인(`Plugin`). 클라이언트 전용은 `Client/` | 엔진 기반에 둬도 되는 범용 수학 · 컨테이너 |
| `Programs` | 따로 빌드하는 도구 실행 파일(언리얼 Source/Programs) | |
| `Games` | 게임 하나 | |

### 2 Core 폴더 (티어 = `CoreLayers.toml`, 숫자 큰 쪽이 위)

| 폴더(티어) | 두는 것 | 대조 |
| --- | --- | --- |
| `Foundation`(0) | 형식 별칭 · 매크로 · OS 헤더 · enum 깃발 · 글자 하나 수준 도구(ASCII · 16진 숫자 — Hash 가 쓴다) | UE Core/HAL · Templates |
| `Hash`(0) | 해시 · 씨앗 상수 · 비밀 비교 · PRF | UE Core/Hash |
| `Predefined`(0) | `.xxx` 이름 표 | |
| `Concurrency`(1) | 원시 동기화(atomic · mutex · SpinLock · Futex · StopToken)와 경합 검출 훅. 스레드 사이 자료 구조(ConcurrentQueue · WorkStealingDeque · SpscQueue …)는 `Concurrency/Queue`(vector 를 써서 Container 위 티어) | UE HAL/Thread*, Async |
| `Math`(1) | 벡터 · 행렬 · 쿼터니언 · 색 · 압축 수치 형식 · 결정적 난수 · 노이즈 | UE Core/Math, Godot core/math |
| `Memory`(2) | 할당기 · 메모리 태그 · 블록 | UE HAL/Memory |
| `Algorithm`(2) | 컨테이너 형식에 묶이지 않은 알고리즘(정렬 비교자 · 연속 배열 도우미 · 유니온 파인드) | UE Core/Algo |
| `Container`(3) | 컨테이너 · 핸들 표 · 슬롯 · 링 · 큐 · 그래프 자료 구조 | UE Containers, Godot core/templates |
| `String`(4) | 로그 없는 글자 도구: UTF 변환 · 분할 · 포맷 · 빌더 · 이스케이프 · 마크업 스캐너 · enum 이름 표 | UE Core/String, Godot core/string |
| `Encoding`(4) | 바이트 ↔ 글자 인코딩(Base64 · Hex · FourCC) | UE Misc/Base64 |
| `Serialization`(4) | 바이트 · 비트 직렬화와 정수 인코딩(VarInt · LittleEndian) | UE Serialization, Godot core/io/marshalls |
| `Delegate` · `Path` · `UUID` · `Subsystem` · `Spatial`(4) | 이름 그대로 | |
| `Log`(5) | 로그 매크로 · 창구 · 싱크 포트(구현 `Log/Sink` 는 맨 위) | |
| `Name`(6) | intern 이름(`hashed_string` · `TagID`) — 로그를 쓴다 | UE FName, Godot StringName |
| `Registry`(6) | 레지스트리 공통 구조(`RegistrationList` · `NameRegistry`) | |
| `CommandLine` · `Compression` · `Process` · `Time`(6), `GlobalVariable` · `Task`(7), `File` · `Network`(8), `Module`(9), `Diagnostics` · `Event` · `Testing`(10) | 이름 그대로 | |

### 3 Engine 폴더 (층 = `EngineLayers.toml`)

| 폴더(층) | 두는 것 |
| --- | --- |
| `Foundation` · `Compression`(0) | 아래 층이 앞으로 선언만 하는 핸들 · 엔진 상수 · 서비스 창구, 압축 코덱 |
| `Reflection`(1) | 형식 정보 · 리플렉션 값(ReflectedValue · ReflectedAny · InstancedStruct) |
| `Serialization` · `Config` · `Content` · `Console` · `Observability` · `Curve` · `CoreServices`(1) | 이름 그대로 |
| `Profiling`(1) | **런타임 계측과 그 포트만** — 보고서를 쓰는 쪽은 DevTools |
| `Behavior`(1) | 조건식 · 동작 어휘(행동 트리 · 스케줄 · 대화 공용) |
| `Asset` · `Text` · `Localization`(2) | 에셋 · 글 배치 · 번역. 기동 때 "팩 · 엔진 데이터 읽기" 는 `Asset` |
| `Presentation`(2) | 게임 코드가 UI · 오디오 없이 아는 표현 창구(I*Sink · I*Driver · 뷰모델) — 표현 창구는 모두 여기 |
| `Window` · `Input`(3), `Graphics`(4: RHI · Shader · Material · Texture · Mesh) | |
| `Object` · `Scene`(5) | 컴포넌트 모델 · 씬 |
| 기능 폴더(6~9) | 자기 컴포넌트는 `<기능>/Component`, 씬 서비스 · 시스템은 `<기능>/Scene`, 데이터는 `<기능>/` 바로 아래. 바닥(6: `Visual` · Physics · Audio · TileMap · Net · UserSettings · Server, `Animation/Sprite` · `AnimationPlayback`) → 애니메이션(7: Animation · `Physics/Cloth` · `Visual/2D/Shadow`) → 합성(8: Character · Navigation · Environment · Destruction · VFX · Sequencer · Dialogue · UI · Telemetry) → 알림 디스패치(9: `Animation/Notify`). 같은 층 폴더끼리 서로 include 하면 린트 `layer-cycles` 가 멈춘다 |
| `Renderer`(10) | 렌더 스레드 · 프레임 그래프 · GPU 파이프라인 |
| `Module` · `Automation`(11) | 엔진 루프 조립 · 핫 리로드 바인딩 · 자동화 창구(감시 값 · 단계 등록 · 시나리오 형식 · `IAutomationRuntime`). 실행기 · 판정 · 세션 · 녹화는 `DevTools/Client/Automation`(언리얼 Automation 컨트롤러 · Gauntlet 자리) |

### 3-1 GameFramework 런타임 폴더 (층 = `GameFrameworkLayers.toml`, `Source/GameFramework/Runtime` 만)

| 폴더(층) | 두는 것 |
| --- | --- |
| `Foundation`(0) | 게임 서비스 · 저장 · 게임 데이터 · 격자 · 난수, 조건 · 깃발 어휘(`Foundation/Condition`) |
| `Online`(1) | 세션 · HTTP · 보안 제공자 — Foundation 만 본다 |
| `World`(2) | 땅 · 환경 값(시계 · 날씨) · 월드 질의 |
| `Character`(3) | 조종 · 이동 · AI · 전투 · 카메라 · 길찾기 |
| `Gameplay`(4) | 어빌리티 · 외형 · 인벤토리 · 퀘스트 · 판 상태와 그것을 읽는 연출 · 기믹 · 존, 게임 인스턴스 조립(`GameInstance.cpp`) |

### 4 그 밖의 규칙

| 규칙 | 까닭 |
| --- | --- |
| `Util` · `Utility` · `Helpers` · `Misc` 폴더 금지 — 하는 일(Encoding · Algorithm · Hash …)로 묶는다. `Common` 도 새로 만들지 않는다(`Editor/Common` 만). 린트 `folder-layout`, 예외 표 `Config/Rules/Lint/FolderLayout.json` | 잡동사니 폴더는 하는 일로 찾을 수 없다 |
| `<기능>/Component` 에는 `*Component` 만 | 씬 서비스 · 질의 · 검사는 `<기능>/Scene` 이나 `<기능>/` |
| 개발 기능은 `Source/DevTools/`. `Dev/` 폴더는 `Config/Rules/Layers/DevFolders.json` 에 까닭과 함께 적은 자리만(DevTools 가 볼 수 없는 게임 · App · ModuleManager · GF 내부 · 리플렉션 에셋) — 표 밖 `Dev/` 는 configure 가 멈춘다(배포가 빼는 폴더도 이 표, `SW_DEV_ONLY_SOURCE_PATTERNS`) | DevTools 는 Engine 을 보지만 GF · 게임 · ModuleManager · App 은 못 본다. 엔진 내부를 봐야 하면 엔진이 확장점(등록 목록 · 인터페이스 · 프레임 훅)을 내고 DevTools 가 붙는다 |
| 실행 중 바꾸기 · 핫스왑은 개발, 기동 때 고르기는 제품 | 언리얼도 RHI · 모듈 교체는 재시작 |
| 약어는 파일 · 폴더 · 식별자 모두 대문자(`FourCCUtil` · `HPACKDecoder`) — 린트 `acronyms` | [NamingCpp](NamingCpp.md) Acronyms |

## 판정해 그대로 두는 것

- `Core/Foundation/BitFlagTrait.h` — 생성 헤더(`*.gen.h` · 강제 include `FlagOps.gen.h`)가 include 하는 잎이라 `<type_traits>` 밖을 끌어오면 모든 TU 의 include 누락을 가린다. EnumUtil 에 합치지 않는다.
- `Engine/UI/Screen/UIScreenSwitch` — 개발 화면 스위치의 등록 틀(확장점) 자체다. DevTools · 게임이 여기에 붙는다.
- `Core/Diagnostics/SubsystemTimingReport` — Core 형식만 쓰는 Core 진단(언리얼 `FScopedBootTiming` 도 Core). 엔진 루프 · 에디터 기동 로그가 부른다.
- `Engine/Profiling/FrameProfileReport` — 런타임 계측(`FrameProfiler`)이 내는 보고 값 형식. 보고를 쓰는 쪽(`FrameProfileSession`)은 DevTools 다.
