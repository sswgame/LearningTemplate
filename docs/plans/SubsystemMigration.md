# 서브시스템 이전 계획 — 기동 표를 ISubsystem 으로

이 문서는 작업 계획입니다. 기동 표(`Source/Engine/EngineInitStepList.xxx`)의 마지막 줄(RHI)이 `ISubsystem` 으로 옮겨지면 지웁니다.

## 왜

기동 단계는 X-macro 표의 줄과 호스트마다의 본문 정적 함수(`initialize<단계>` · `shutdown<단계>` · `destroy<단계>`)로 되어 있었습니다.
`EngineLoop.cpp` 에 그런 함수가 57개 있었고 20개는 `loop._owned.destroyX()` 같은 한 줄 위임이었습니다. 이름 규칙에 기대어 오타가 조용히 빠지고,
새 단계마다 함수를 또 써야 하며, `destroy` 단계와 `_owned` 에 소유가 모여 있었습니다.

방향은 "무엇을 처리하려고 함수가 계속 늘어나는 구조가 아니라, 요청한 대로 반응하는 구조" 입니다. 서브시스템이 이름 · 의존 · 대상을 스스로 선언하고
초기화 · 종료를 구현하면, `SubsystemCollection` 이 의존으로 정렬한 순서로 초기화하고 역순으로 종료 · 소멸합니다(Source/Engine/README.md "기동과 종료").

## 지금 상태(2단계 끝)

- 표에 남은 줄은 **RHI 하나**입니다. 본문은 `EngineLoop::StartupStepBody` 의 정적 함수 셋(`initializeRHI` · `shutdownRHI` · `destroyRHI`)입니다.
- 나머지 23단계는 서브시스템입니다.
  - 서비스 클래스가 스스로 선언: `ShaderCache` · `Physics`(`PhysicsSystem`) · `Fonts` · `Telemetry` · `UI`.
  - `EngineFunctionStep`(`EngineInitSequence.h`)의 선언 + `FunctionSubsystem` 본문: Compression · Reflection · Config · Resource · EngineDefaultAssets ·
    Audio · Scene · Task · ModuleImages · FileIO · Input · ModuleTypes · Headless · UserSettings · FrameRenderer · RenderThread · LiveShader · SceneRHI.
- 소유는 기동 순서가 갖습니다(`addSubsystem( std::move( _owned._pX ) )` · `FunctionSubsystem::takeOwnership`). 손으로 적은 `destroy` 본문은 RHI 의 것뿐입니다.
- 테스트 하네스(`TestHostRuntime`)는 표를 쓰지 않고 `SubsystemCollection` 에 필요한 서브시스템만 같은 선언 · 같은 객체로 넣습니다. 본문 함수가 없습니다.
- 불변: 이전 전후 App(에디터 · DX12)과 Server 의 초기화 · 종료 · 해제 순서 덤프가 같았다(App 72줄, Server 54줄). 실제 소멸 위치가 바뀐 것은
  `PhysicsSystem`(저장소의 끝 정리 → Physics 단계의 소멸 자리)과 enum 이름 풀이 해제(리플렉션의 해제 → 종료)뿐이다.

## 왜 클래스가 아니라 FunctionSubsystem 인가

- **Core 서비스**(TaskManager · AsyncFileIO · CompressionCodecRegistry)는 Core 가 `ISubsystem`(Engine/Common)을 모르므로 스스로 서브시스템이 될 수 없다.
- **`InputManager`** 는 정렬이 큰 타입(락프리 큐)이라 `ISubsystem*` 으로 지울 수 없다(`SubsystemCollection::add` 의 static_assert).
  `takeOwnership` 은 제 타입으로 지운다.
- **호스트마다 본문이 다른 단계**(Compression: 외부 코덱 · Reflection: 하네스의 GF · 테스트 타입 · Config: 주 표 · 다시 읽기 · 메모리 예산 ·
  Audio · EngineDefaultAssets: 하네스는 초기화하지 않는다)와 **호스트의 일**(ModuleImages · ModuleTypes · SceneRHI · Headless · UserSettings —
  결과가 EngineLoop 의 상태다)은 선언만 공유하고 본문을 호스트가 준다.
- 클래스로 옮길 수 있는 후보: `AssetManager`(소멸자가 `shutdown` 을 하게 하면 — 지금은 테스트가 만든 매니저의 소멸 동작이 바뀌어 미뤘다),
  `UserSettingsManager`(셸 맵 경로 한 줄만 호스트 것), `FrameRenderer` + `RenderPacketBuilder`(둘을 한 단계가 든다).

## 남은 일

| 단계 | 소유 객체(지금 있는 곳) | 막는 것 | 메모 |
|---|---|---|---|
| RHI | `RHI`(`EngineLoop::_rhi`) · 창 | 백엔드 교체의 기준점 — `applyPendingBackendChange` 가 `shutdownDependentsOf( EngineInitStep::RHI )` 로 표의 자리를 부른다 | 별도 단위. `RHIInitResult` 를 App 이 읽는다 |

RHI 를 옮기면 함께 지울 것: `EngineInitStepList.xxx` · `EngineInitStep` · `EngineInitNoStepBody` · `EngineInitStepTable` · `EngineLoop::StartupStepBody`.
`EngineInitSequence` 는 `SubsystemCollection` 과 하나가 되거나 이름만 남고, 백엔드 교체는 `shutdownDependentsOf( RHI 서브시스템 )` 이다.
`EngineInitSequenceTest` 의 `StartupRecorder`(표 본문 모음) · `PartialStepBody` · `MissingBodiesAreSkipped` 도 같이 지운다.

## 목록으로 옮기지 않는 것

- `OnlineServerRuntime`(GameFramework 온라인 서버 조립). 정상 종료 순서는 두 패스(`stopSubsystem` → `shutdownSubsystem`)로 그대로 적힌다
  (등록 해제 · 키트 stop → 호스트 → 스케줄러 · 저장소 비우기 → 키트 shutdown → 캐시). 하지만 키트 계약(`IOnlineServerKit::stop` 은 "`start` 에 실패했어도 불린다")이
  "만든 키트는 어느 단계에서 멈췄든 모두 stop · shutdown 을 받는다" 이고, 목록은 **초기화에 성공한 것만** 내린다. 키트 초기화나 호스트 기동이 실패하면
  키트 stop 을 받을 서브시스템이 없고, 실패한 서브시스템이 스스로 되돌리면 키트 shutdown 이 저장소 비우기보다 앞서 남은 완료가 내려간 키트를 부른다.

## 끝난 모습

- `EngineInitStepList.xxx` · `EngineInitNoStepBody` · `EngineInitStepTable` · `EngineLoop::StartupStepBody` 가 없다.
- `EngineServiceCollection` 은 만드는 일만 하고, 수명이 기동 순서에 매인 서비스의 소유는 기동 순서가 갖는다(RHI 를 빼면 이미 그렇다).
