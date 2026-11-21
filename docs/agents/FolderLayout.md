# FolderLayout

> 결론: 이름 낱말(Util · Registry · Component)이나 처음 쓰는 쪽이 아니라 **하는 일**로 자리를 정한다. 두 자리가 다 맞으면 낮은 층에 둔다. 층 규칙은 빌드가 막고([Includes](Includes.md)), 표에 없는 폴더는 configure 가 멈추므로 새 폴더는 층을 정하고 들어온다(층 표 `Config/Rules/Layers/*.toml`). 남은 이동은 [NEXT](../NEXT.md).

## 폴더마다 무엇을 두는가

### 1 타깃

| 타깃 | 두는 것 | 두지 않는 것 |
| --- | --- | --- |
| `Core` | 엔진 · 게임을 모르는 기반(형식 · 컨테이너 · 글자 · 수학 · 스레드 · 파일 · 로그 · 네트워크 전송) | 씬 · 에셋 · 리플렉션 · 게임 낱말 |
| `Engine` | 런타임 제품 기능과 그 확장점 | 에디터만 부르는 함수, 개발 도구 구현 |
| `DevTools` | 엔진 확장점에 스스로 붙는 개발 도구(배포 제외). 클라이언트 전용은 `DevTools/Client`, 머티리얼 그래프 컴파일러는 `DevTools/Material` | 게임 · GF · ModuleManager 내부를 아는 것 |
| `ModuleManager` | 모듈 로드 · 매니페스트 · 호스트 공용 기동. 개발 전용 핫 리로드는 `Dev/LiveReload` | 엔진 기능 |
| `ModuleAPI` | 모듈 C-ABI 계약 | |
| `App` · `Server` | 그 실행 파일에만 있는 호스트 배선 | 엔진 기능, 개발 도구 구현(붙이는 줄만) |
| `Editor` | 에디터 기능 전부 | 런타임이 쓰는 것 |
| `GameFramework` | 장르 공통 틀(`Runtime`) · 키트(`Kits`) · 에디터 플러그인(`Plugin`). 클라이언트 전용은 `Client/` | 범용 수학 · 컨테이너 |
| `Programs` | 따로 빌드하는 도구 실행 파일 | |
| `Games` | 게임 하나 | |

### 2 Core 폴더 (티어 = `CoreLayers.toml`, 숫자 큰 쪽이 위)

| 폴더(티어) | 두는 것 |
| --- | --- |
| `Foundation`(0) | 형식 별칭 · 매크로 · OS 헤더 · enum 깃발 · 글자 하나 수준 도구 |
| `Hash`(0) | 해시 · 씨앗 상수 · 비밀 비교 · PRF |
| `Predefined`(0) | `.xxx` 이름 표 |
| `Concurrency`(1) | 원시 동기화와 경합 검출 훅. 스레드 사이 자료 구조는 `Concurrency/Queue`(Container 위 티어) |
| `Math`(1) | 벡터 · 행렬 · 쿼터니언 · 색 · 압축 수치 · 결정적 난수 · 노이즈 |
| `Memory`(2) | 할당기 · 메모리 태그 · 블록 |
| `Algorithm`(2) | 컨테이너에 묶이지 않은 알고리즘 |
| `Container`(3) | 컨테이너 · 핸들 표 · 슬롯 · 링 · 큐 · 그래프 자료 구조 |
| `String`(4) | 로그 없는 글자 도구(UTF · 분할 · 포맷 · 빌더 · 이스케이프 · enum 이름 표) |
| `Encoding`(4) | 바이트 ↔ 글자 인코딩(Base64 · Hex · FourCC) |
| `Serialization`(4) | 바이트 · 비트 직렬화와 정수 인코딩 |
| `Delegate` · `Path` · `UUID` · `Subsystem` · `Spatial`(4) | 이름 그대로 |
| `Log`(5) | 로그 매크로 · 창구 · 싱크 포트(구현 `Log/Sink` 는 맨 위) |
| `Name`(6) | intern 이름(`hashed_string` · `TagID`) |
| `Registry`(6) | `RegistrationList` · `NameRegistry` |
| `CommandLine` · `Compression` · `Process` · `Time`(6), `GlobalVariable` · `Task`(7), `File` · `Network`(8), `Module`(9), `Diagnostics` · `Event` · `Testing`(10) | 이름 그대로 |

### 3 Engine 폴더 (층 = `EngineLayers.toml`)

| 폴더(층) | 두는 것 |
| --- | --- |
| `Foundation` · `Compression`(0) | 아래 층이 앞으로 선언만 하는 핸들 · 엔진 상수 · 서비스 창구, 압축 코덱 |
| `Reflection`(1) | 형식 정보 · 리플렉션 값 |
| `Serialization` · `Config` · `Content` · `Console` · `Observability` · `Curve` · `CoreServices`(1) | 이름 그대로 |
| `Profiling`(1) | 런타임 계측과 그 포트만 — 보고서를 쓰는 쪽은 DevTools |
| `Behavior`(1) | 조건식 · 동작 어휘 |
| `Asset` · `Text` · `Localization`(2) | 에셋 · 글 배치 · 번역 |
| `Presentation`(2) | 게임 코드가 UI · 오디오 없이 아는 표현 창구(I*Sink · I*Driver · 뷰모델) |
| `Window` · `Input`(3), `Graphics`(4) | |
| `Object` · `Scene`(5) | 컴포넌트 모델 · 씬 |
| 기능 폴더(6~9) | 컴포넌트는 `<기능>/Component`, 씬 서비스 · 시스템은 `<기능>/Scene`, 데이터는 `<기능>/` 바로 아래. 층: 바닥(6) → 애니메이션(7) → 합성(8) → 알림 디스패치(9). 같은 층끼리 include 하면 린트 `layer-cycles` 가 멈춘다 |
| `Renderer`(10) | 렌더 스레드 · 프레임 그래프 · GPU 파이프라인 |
| `Module` · `Automation`(11) | 엔진 루프 조립 · 핫 리로드 바인딩 · 자동화 창구. 실행기 · 판정 · 녹화는 `DevTools/Client/Automation` |

### 3-1 GameFramework 런타임 폴더 (층 = `GameFrameworkLayers.toml`)

| 폴더(층) | 두는 것 |
| --- | --- |
| `Foundation`(0) | 게임 서비스 · 저장 · 게임 데이터 · 격자 · 난수 · 조건 어휘 |
| `Online`(1) | 세션 · HTTP · 보안 제공자 |
| `World`(2) | 땅 · 환경 값 · 월드 질의 |
| `Character`(3) | 조종 · 이동 · AI · 전투 · 카메라 · 길찾기 |
| `Gameplay`(4) | 어빌리티 · 외형 · 인벤토리 · 퀘스트 · 판 상태 · 연출 · 존, 게임 인스턴스 조립 |

### 3-2 CMake (`cmake/` — 폴더 하나에 파일 종류 하나)

| 폴더 | 두는 것 |
| --- | --- |
| `cmake/Modules/<Build·Environment·Config>/` | `include()` 만 하는 모듈 |
| `cmake/Toolchain/` | 플래그 · 툴체인 프로필 |
| `cmake/Scripts/` | `cmake -P` 로만 도는 스크립트(쓰는 함수는 모듈에 둔다) |
| `cmake/Templates/` | `configure_file` · 소스 입력 |
| `Config/Build/` | 빌드 정책 데이터 |
| `Test/<영역>/Run*.cmake` | 테스트 전용 `cmake -P` 드라이버 |

이름 규칙은 [NamingScripts](NamingScripts.md) CMake 절. 옮기기는 `py -3 -m Scripts move-source --base . <옛>=<새>`.

### 4 그 밖의 규칙

| 규칙 | 까닭 |
| --- | --- |
| `Util` · `Utility` · `Helpers` · `Misc` 폴더 금지, `Common` 도 새로 만들지 않는다(`Editor/Common` 만). 린트 `folder-layout` 강제(예외 `Config/Rules/Lint/FolderLayout.json`) | 잡동사니 폴더는 하는 일로 찾을 수 없다 |
| `<기능>/Component` 에는 `*Component` 만(린트 `folder-layout`) | 씬 서비스 · 질의는 `<기능>/Scene` |
| 키트 폴더는 평평하게: 장르 키트는 카탈로그 · 규칙 파일을 키트 바로 아래, 하위 폴더는 `Component/` · `Editor/` · `Tests/` 와 하위 게임만. 온라인 키트는 `Runtime/` · `Server/` 바로 아래(뼈대 `new-online-kit`) | 키트는 파일 10 ~ 20 개 |
| 개발 기능은 `Source/DevTools/`. `Dev/` 폴더는 `Config/Rules/Layers/DevFolders.json` 에 까닭과 함께 적은 자리만(configure 강제) | DevTools 는 GF · 게임 · ModuleManager · App 을 못 본다 — 엔진이 확장점을 내고 DevTools 가 붙는다 |
| 실행 중 바꾸기 · 핫스왑은 개발, 기동 때 고르기는 제품 | 언리얼도 RHI · 모듈 교체는 재시작 |
| 약어는 파일 · 폴더 · 식별자 모두 대문자 — 린트 `naming-words` | [NamingCpp](NamingCpp.md) Acronyms |

## 옮기지 않는 것

`Core/Foundation/BitFlagTrait.h`(생성 헤더가 include 하는 잎 — EnumUtil 에 합치지 않는다) · `Engine/UI/Screen/UIScreenSwitch`(확장점 자체) · `Core/Diagnostics/SubsystemTimingReport` · `Engine/Profiling/FrameProfileReport`(보고 값 형식, 쓰는 쪽은 DevTools).
