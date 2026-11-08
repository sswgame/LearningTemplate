# FolderLayout

> 질문: 새 파일 · 옮길 파일을 어느 폴더에 두는가?
> 독자: 이 저장소에서 파일을 만들거나 옮기는 에이전트.
> 결론: 이름 낱말(Util · Registry · Component)이나 처음 쓰는 쪽이 아니라 **하는 일**로 자리를 정한다. 아래 표가 정본이다. 굵게 쓴 폴더는 아직 없는 목표 자리이고, 남은 이동은 [FolderMoves](../FolderMoves.md) 에 있다.

- 층(티어) 규칙이 먼저다: 하위 층이 상위 층을 include 하지 않는다([Includes](Includes.md)). 두 자리 다 맞으면 **낮은 층**에 둔다.
- 개발 기능은 `Source/DevTools/` 로, 그 모듈 안에 붙어야만 하는 것만 기능 폴더의 `Dev/` 로.

## 폴더마다 무엇을 두는가

### 1 타깃

| 타깃 | 두는 것 | 두지 않는 것 |
| --- | --- | --- |
| `Core` | 엔진 · 게임을 모르는 기반(형식 · 컨테이너 · 글자 · 수학 · 스레드 · 파일 · 로그 · 네트워크 전송) | 씬 · 에셋 · 리플렉션 · 게임 낱말 |
| `Engine` | 런타임 제품 기능(배포 게임이 쓰는 것)과 그 확장점 | 편집기만 부르는 함수, 개발 전용 도구 구현(확장점만 둔다) |
| `DevTools` | 엔진 확장점에 스스로 붙는 개발 도구(배포 구성 미빌드). 클라이언트 전용은 `DevTools/Client` | 게임 · GF · ModuleManager 내부를 아는 것(DevTools 는 그것을 못 본다 — TargetLayers) |
| `ModuleManager` | 모듈 로드 · 매니페스트 · 호스트 공용 기동 열. 개발 전용 핫 리로드는 `Dev/` 아래 | 엔진 기능 |
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
| `Serialization` · `Config` · `Content` · `Console` · `Observability` · `Profiling`(1) | 이름 그대로. `Profiling` 은 런타임 계측과 그 포트만(보고 · 내보내기는 `DevTools`) |
| `Gameplay`(1) | 조건식 · 동작 어휘(행동 트리 · 스케줄 · 대화 공용) |
| `Asset` · `Text` · `Localization`(2) | 에셋 · 글 배치 · 번역. 기동 서브시스템 중 "팩 · 엔진 데이터 읽기" 는 `Asset` |
| `Presentation`(2) | 게임 코드가 UI · 오디오 없이 아는 창구(I*Sink · I*Driver · 뷰모델) — 모든 표현 창구는 여기 |
| `Window` · `Input`(3), `Graphics`(4: RHI · Shader · Material · Texture · Mesh — GPU 자원과 CPU 쪽 그래픽 데이터) | |
| `Object` · `Scene`(5) | 컴포넌트 모델 · 씬 |
| 기능 폴더(6: **`Visual`**(Rendering + 2D) · Physics · Audio · Animation …) | 자기 컴포넌트는 `<기능>/Component`, 씬 서비스 · 시스템은 `<기능>/Scene`, 데이터는 `<기능>/` 바로 아래 |
| `Renderer`(7) | 렌더 스레드 · 프레임 그래프 · GPU 파이프라인 |
| `Module` · `Automation`(8) | 엔진 루프 조립 · 핫 리로드 바인딩 · 자동화 실행기 |

### 4 그 밖의 규칙

| 규칙 | 까닭 |
| --- | --- |
| `Util` · `Utility` · `Common` · `Misc` 폴더를 새로 만들지 않는다 | 잡동사니 폴더는 하는 일로 찾을 수 없다(GF `Runtime/Foundation/Utility` 가 그 예) |
| `<기능>/Component` 에는 `*Component` 만 | 컴포넌트를 찾는 자리에 씬 서비스 · 질의 · 검사가 섞이지 않게 |
| `Dev/` 는 "그 모듈 내부에 붙어야 하는 개발 전용" 만. 엔진 공개 헤더 · 확장점만 쓰면 `DevTools` 로 | DevTools 는 Engine 을 보지만 GF · 게임 · ModuleManager 는 못 본다 |
| 실행 중 바꾸기 · 핫스왑은 개발, 기동 때 고르기는 제품 | 언리얼도 RHI · 모듈 교체는 재시작 |
