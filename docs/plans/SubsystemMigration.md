# 서브시스템 이전 계획 — 기동 표를 ISubsystem 으로

이 문서는 작업 계획입니다. 기동 표(`Source/Engine/EngineInitStepList.xxx`)의 줄이 모두 `ISubsystem` 으로 옮겨지면 지웁니다.

## 왜

기동 단계는 X-macro 표의 줄과 호스트마다의 본문 정적 함수(`initialize<단계>` · `shutdown<단계>` · `destroy<단계>`)로 되어 있었습니다.
`EngineLoop.cpp` 에 그런 함수가 57개 있었고 20개는 `loop._owned.destroyX()` 같은 한 줄 위임이었습니다. 이름 규칙에 기대어 오타가 조용히 빠지고,
새 단계마다 함수를 또 써야 하며, `destroy` 단계와 `_owned` 에 소유가 모여 있었습니다.

방향은 "무엇을 처리하려고 함수가 계속 늘어나는 구조가 아니라, 요청한 대로 반응하는 구조" 입니다. 서브시스템이 `ISubsystem` 을 상속해 스스로 이름 · 의존 ·
대상을 선언하고 초기화 · 종료를 구현하면, `SubsystemCollection` 이 의존으로 정렬한 순서로 초기화하고 역순으로 종료 · 소멸합니다.
표 · 이름 매칭 · 본문 정적 함수 · destroy 단계가 모두 필요 없어집니다(Source/Engine/README.md "기동과 종료").

## 지금 상태(1단계)

- 기반: `Engine/Common/ISubsystem.h` · `SubsystemCollection` · `FunctionSubsystem`. `EngineInitSequence` 는 표의 줄을 어댑터로 같은 목록에 넣어 한 순서로 섞어 돌린다.
- 옮긴 것(3): `Fonts`(`FontService`) · `Telemetry`(`TelemetryService` — 크래시 보고 묶기 포함) · `UI`(`UISystem` — Dev 의 디버그 HUD 포함). 본문 함수 8개를 지웠다.
- 불변: 옮기기 전후 App(에디터 · DX12)과 Server 의 초기화 · 종료 · 해제 순서 덤프가 바이트 단위로 같았다(App 72줄, Server 54줄).

## 옮기는 순서를 막는 것

1. ~~표의 의존은 서브시스템 이름을 부르지 못한다.~~ 2단계에서 풀었다 — 표의 의존 칸도 이름 글이고 판정은 `SubsystemCollection::sort` 하나다
   (모르는 이름 · 순환 · 대상 어긋남은 기동 실패, `EngineInitSequenceTest` 가 실제 목록으로 본다). 이제 어느 줄이든 순서와 상관없이 옮길 수 있다.
2. **테스트 하네스도 같은 표를 쓴다**(`Test/TestFramework/TestHostRuntime.cpp` 의 `TestHost`). 하네스의 본문은 앱과 다르다(GameFramework 타입 등록,
   오디오는 종료만, 글꼴 · UI · RHI 없음). 하네스가 쓰는 단계는 서브시스템이 하네스와 앱에서 같은 동작을 하거나, 호스트가 서로 다른
   `FunctionSubsystem` 을 넣게 해야 한다.
3. **EngineLoop 의 멤버를 만지는 본문**(`_rhi` · `_frameRenderer` · `_renderThread` · `_bHeadless` · `_shellActions` · `_configManager`)은 서브시스템이
   생성 인자로 받거나(`EngineLoop&` 가 아니라 필요한 포인터만), 결과를 서브시스템의 getter 로 돌려준다(`RHIInitResult`).

## 남은 단계

난이도: 낮음 = 서비스의 `initialize` · `shutdown` 을 그대로 넘긴다 · 중간 = 엔진 데이터나 호스트 상태를 읽어 조립한다 · 높음 = 다른 단계의 재시작 · 호스트 결과가 걸린다.

| 단계 | 소유 객체(지금 있는 곳) | 생성 인자 · 읽는 것 | 하네스 | 난이도 | 메모 |
|---|---|---|---|---|---|
| Compression | `CompressionCodecRegistry`(`_owned`) | 없음 — Core 슬롯 연결 · 외부 코덱 등록 | 씀(외부 코덱 없음) | 낮음 | 해제가 Core 슬롯을 끊는다 |
| Reflection | `TypeRegistry`(`_owned`) | 없음 — 등록기 모으기 · enum 이름 묶기 | 씀(GameFramework · 테스트 타입) | 낮음 | `FunctionSubsystem` 후보, 호스트별 본문 |
| Config | `ConfigManager`(`EngineLoop::_configManager`) | 프로젝트 루트 · 리로드 콜백(`EngineLoop::onConfigReloaded`) · 메모리 예산 파일 | 씀 | 중간 | `_pEngineConfig` 를 읽는 단계(Resource · RHI)가 getter 로 받는다 |
| Resource | `AssetManager`(`_owned`) | `EngineConfig::_listResourcePriority` · `--cook-scenes` | 씀 | 중간 | 해제가 `AssetManager::shutdown`(에셋을 드는 단계가 모두 뒤) |
| EngineDefaultAssets | `EngineDefaultAssets`(`_owned`) | 리소스 경로 · 로컬라이제이션 · `--language` | 해제만 | 중간 | |
| ShaderCache | `ShaderCache`(`_owned`) | 없음 | 씀 | 낮음 | |
| Task | `TaskManager`(`_owned`) | 없음 | 씀 | 낮음 | 소멸자가 태스크를 기다리는 객체(스트리밍 큐)는 Scene 이 먼저 해제 |
| ModuleImages | 없음 | 종료 훅 `EngineLoop::_onScenesReleased` | 안 씀 | 낮음 | `FunctionSubsystem`(종료 델리게이트만) |
| Audio | `IAudioService`(`EngineLoop`, 역할별 팩토리) | 호스트 역할(서버는 Null 백엔드) | 종료 · 해제만 | 중간 | HostCreated 서비스 |
| FileIO | `AsyncFileIO`(`_owned`) | `TaskManager` | 씀 | 낮음 | |
| Input | `InputManager`(`_owned`) | 없음 — 해제 전에 `ShellInputActions` 를 놓는다 | 씀 | 낮음 | 셸 맵(`EngineLoop::_shellActions`)이 입력 매니저를 가리킨다 |
| ModuleTypes | 없음 | 호스트의 타입 공급자 로더(`setModuleTypeLoader`) | 씀(로더 없음) | 낮음 | `FunctionSubsystem` |
| Physics | `PhysicsSystem`(`_owned`) | 없음 | 씀 | 낮음 | |
| Scene | `SceneManager` · `AssetStreamingQueue`(`_owned`), `CommandStack`(`EngineLoop`) | 묶인 서비스(`SceneEngineServices`) · 되돌리기 기록 구독 | 씀 | 중간 | 해제가 스트리밍 큐 · 되돌리기 기록도 놓는다 |
| Headless | 없음(결과 `_bHeadless` · `_bHeadlessTaskFailed`) | 명령줄 · 호스트 역할 | 안 씀 | 중간 | 본문이 크다(쿠킹 · 굽기 · 임포트) — 결과를 getter 로 돌려주는 `ISubsystem` 하나 |
| UserSettings | `UserSettingsManager`(`_owned`) | 전역 변수 · 명령줄 · 입력 맵 · 오디오 · 로컬라이제이션 · 셸 맵 경로 | 안 씀 | 중간 | |
| RHI | `RHI`(`EngineLoop::_rhi`) · 창 | 엔진 설정 · 사용자 화면 설정 · 명령줄 · RenderDoc | 안 씀 | 높음 | 백엔드 교체의 기준점(`shutdownDependentsOf`) — `RHIInitResult` 를 App 이 읽는다 |
| FrameRenderer | `FrameRenderer`(`EngineLoop`, HostCreated) · `RenderPacketBuilder` | 디바이스 · `TaskManager` | 안 씀 | 중간 | 백엔드 교체로 다시 선다 |
| RenderThread | `RenderThread`(`EngineLoop`) | 디바이스 · 렌더러 | 안 씀 | 중간 | 다시 설 때 같은 객체에 붙는다(훅 · 포인터 유지) |
| LiveShader | `ShaderRecompiler`(`EngineLoop`, Debug) | 렌더 스레드 · 렌더러(재컴파일 콜백) | 안 씀 | 중간 | 지금 옮길 수 있다(아무도 기대지 않는다) |
| SceneRHI | 없음 | `SceneManager` · 디바이스 | 안 씀 | 낮음 | 지금 옮길 수 있다 — `FunctionSubsystem` |

## 끝난 모습

- `EngineInitStepList.xxx` · `EngineInitNoStepBody` · `EngineInitStepTable` · `EngineLoop::StartupStepBody` · 하네스의 `TestHost` 본문이 없다.
- `EngineInitSequence` 는 `SubsystemCollection` 과 하나가 되거나 이름만 남는다. 백엔드 교체는 `shutdownDependentsOf( RHI 서브시스템 )` 이다.
- `EngineServiceCollection` 은 만드는 일만 하고, 수명이 기동 순서에 매인 서비스의 소유는 기동 순서가 갖는다.
