# 다음 세션 인계

> 질문: 지금 어디까지 왔고, 다음 세션이 처음에 할 일은 무엇인가?
> 독자: 새로 시작하는 세션(에이전트와 사람).
> 결론: 통합 워크트리 `integD` 에 병합을 끝내고, 아래 표의 A → D 를 한 번씩만 돌린다. 새 병렬 파동은 띄우지 않는다.

이 문서는 지금 상태만 둡니다. 한 일은 `git log` 가 원본이고, 설계는 계획 문서, 순서는 [Roadmap](Roadmap.md)에 있습니다.

| 알고 싶은 것 | 어디 |
|---|---|
| 남은 일 한 건 한 건 | 후보 문서 `Candidates-<영역>.md` — `py -3 -m Scripts plans --area <영역>` |
| 설계와 구현 단위 | 계획 문서 — `py -3 -m Scripts plans`, `--file <이름>` |
| 앞으로 할 일의 순서 | [Roadmap](Roadmap.md) |
| 결정과 근거 | [결정 기록](../09_Decisions.md) |
| 작업 방식 · 검증 | [작업 방식](../11_Workflow.md), [검증과 측정](../08_Verification.md) |
| 에이전트에게 맡길 때 · 토큰 | [에이전트와 토큰](../12_Agents.md), [작업자 규칙](WorkerRules.md) |
| 환경 · 도구 함정 | [함정](../14_Pitfalls.md) |

## 0000. 2026-10-24 통합 중

### 상태

- **작업자(코드만 짜고 린트 후 커밋하고 멈춤, 워크트리 `D:/w/<이름>`, 브랜치 `wt/<이름>`)**: 앞 파동 shaderfmt3 · leakwin · ownership · candproject · candquality1 · candgraphics · measurebatch(검증 · 측정 매니페스트와 id 없는 확인 대기 줄 차단)는 대부분 병합됐고, 새 파동 planengine · plangfxq · plananim · plangameplay · planui · plannet · leftovers · planproject · plangfx2 · docsweep 가 돈다. 보고가 오면 `integD` 에 병합한다.
  `Roadmap.md` · `README.md` 충돌은 새 쪽 줄을 남기고 옛 줄을 지운 뒤 `plans --check` 를 돌리고, 표는 `status-board --write README.md` 로 다시 만든다.
- **통합 워크트리 `D:/w/integD`**(브랜치 `wt/integD`, main `4c7b2085b` 위): 한 번도 빌드하지 않았다. 병합 끝: parseintlist · lambdaeditor · lambdakits · servicelist · lambdagfx · lambdacore · lambdaeng · kitshelpers · verifyleak · scnpixel · singletonget · thumbheadless · plansweep · lambdaser.
  손으로 푼 충돌: `Candidates-engine.md` 의 `FunctorLambdaCommonization` 줄(전부 남김), `Engine.cpp` · `TestHostRuntime.cpp`(servicelist 쪽을 택하고 서브시스템 세 줄을 `EngineSubsystemSet.cpp` 로 옮김, `ServiceSetup::_bAuthorityGhost` 추가), `InputMapSerialization.cpp`(lambdaser 판을 택하고 lambdacore 의 `loadActionNode` 선언을 지움).
- **아직 병합 못 한 브랜치**: shaderfmt3 · leakwin(`wt/verifyleak` 위) · ownership(`sw_new` · `Memory::allocate` 소유 형식) · candengine · candproject · candquality1 · candquality2 · candeditor · candgraphics · candmisc. candnetwork 는 병합했고, 그 "통합에서 실행 대기" 중 WSL 항목은 리눅스 검증을 사용자가 요청할 때만 하므로 건너뛴다.
- **main**: 로컬 main 이 `origin/main` 보다 7 커밋 앞서 있다(훅 주석 · parseintlist 병합 · 규칙 문서 등). 병합이 끝난 `wt/*` 브랜치와 워크트리(49 개)는 통합이 끝난 뒤 `worktree-remove` 로 지운다.
- **판정 중**: 포토모드 HUD · 팝업 잘림 · 단일체 예외 12곳 · plansweep 이 닫은 두 문서 · 소유 형식 게이트 · 훅 점검은 design-reviewer(a066094)가 상용 엔진과 비교해 정하는 중이다. 결과는 `docs/09_Decisions.md` 에 옮긴다.

### 통합 검증 · 측정 (한 묶음, 단계마다 한 번)

빌드 구성별로 모아 아래 순서로 한 번에 끝낸다. 한 단계가 실패하면 그 자리에서 고치거나 해당 커밋을 되돌린 뒤 같은 단계부터 다시 한다. 새 측정 질문은 C 단계에 한 줄로 더한다. 측정 결과는 한 번에 해당 문서(계획 문서 · `08_Verification.md` · 결정 기록)에 옮기고 후보 절을 지운다.

| 단계 | 구성 | 돌릴 것 | 잠금 |
|---|---|---|---|
| A | Debug | 병합 → `cmake --preset Ninja-Debug`(vcpkg 잠금) → `full-build --owner integD` 경고 0 · 오류 0. 컴파일 오류는 `cmake --build ... -- -k 0` 으로 전부 모아 한꺼번에 고친다. 작업자들이 본 실패(AppTest_NoGPU · EditorTest_Shard2 · EngineTest_NoGPU 샤드 · ServerTest_Shard1 · CoreTest · PythonTest_TestAssetValidation은 서버 프로세스가 몰린 때의 환경 영향일 수 있고, MeshModifiers 오브젝트 7개 FAILED 는 원인 미확인)의 원인 조사도 이 단계에 둔다. → `-L lint` · `-L nogpu` · `-L race` → 작업자들이 적은 "통합에서 실행 대기" 중 CPU 만 쓰는 테스트 → 서버 버스 테스트(`ReflectionSerializationTest.*` · `ServerBusTest.*` · `ServerBusMixedBuildTest.*` · `OnlineServiceHostTest.*` · `RemoteConfigTest.*` · `OnlineMultiServerTest.ARevokeNotice*`)와 `MaxCount` 상수 이름 단계(`wt/maxcount`)의 빌드 · 테스트 기록 | 빌드 슬롯 |
| B | Shipping | 빌드 → `-L hostgpu` 전체 → 확인 대기 전부(ModuleRenderPass M2 · 텍스처 R8 · TilemapCellDataLayers T3 · SerializerSilentSkips 씬 로드 · DiskPathVsResourceId `--cook-scenes` · PanelIDLiterals P3 · UiNotificationRemaining N2 · contentthumbnails · scnpixel 의 `differingFraction` 문턱 · 후보 작업자의 시나리오) → `Empty` 렌더 테스트 씬 + 개발 중인 게임(지금 Shooter3D) × 네 백엔드 | GPU 공유 |
| C | Release | 빌드 → 쿠킹 한 번(물 그림자 · 셰이더 2단계) → 골든 다시 뜨기 → 측정을 혼자 이어서: `perf` · `load` · `perf-compare`(InteractionSpatialGrid I3, 누수 창 오버헤드, 소유 형식) · 네트 벤치(`NetUDPBenchTest` · `NetReplicationBenchTest` · `NetCompressionBench`의 Windows 쪽) · `scene-load-bench` · `soak` · 서버 버스 코덱 비용(채팅 짧은 글 · 512자의 A 대 B, 모르는 필드 유무, 서버 둘 초당 1k · 10k 채팅의 틱 p50 · p99, 상한으로 채운 A 바이트 — 코덱 비용이 버스 왕복의 5 % 아래면 최적화하지 않는다) · 후보의 측정 질문(HalfResolutionBloomMeasurement 등) | GPU 독점 |
| D | CI | push 뒤 `ci-jobs --summary`(리눅스 · TSAN 포함). 로컬 WSL 은 사용자가 요청할 때만 | — |

컴파일 확인이 필요한 곳(작업자 보고): Graphics `RHIReleaseQueue.h` ↔ `RHIMemoryLedger.h` include · `GPUSkinPaletteCollector` · `hashed_string.h` `makeInterned`(전체 재빌드) · `InputMap.h` friend `InputMapActionLoader` · `SW_DELEGATE_FUNCTION` 템플릿 · `GridOpenEntry` · `AutomationRunner.cpp` `validateExpectImage`.

통합 순서: ① 남은 브랜치 병합 → ② A → ③ B → ④ C → ⑤ main 병합 · push → D → ⑥ `worktree-list` 로 병합 끝난 워크트리 `worktree-remove`. 병합 뒤 `README.md` 표는 `status-board --write README.md` 로 다시 만든다.

### 남은 것

끝난 일은 `git log` 에 있고, 여기에는 안 끝난 결함 · 확인만 한 줄씩 둔다.

- **싱글턴 · 람다**: `ResourceDataKindRegistry` 단일체(정적 등록자, 후보 `SingletonGetToServices`), 싱글턴 `get()` 게이트가 `instance()` · `shared()` 이름을 못 봄, 이름 붙은 지역 람다 정당 약 75곳(그대로).
- **누수 탐지**: 설계 검토의 2 ~ 4단계([LeakDetection](LeakDetection.md)).
- **같은 결함이 남았을 곳**: 이름을 손으로 거절하는 레지스트리(`LocalStoreFactory` · `SQLDriverRegistry` · `LoginService` 플랫폼 제공자 · `ModuleRenderPassRegistry` · `EditorAssetTypeActions`)는 핫 리로드에서 같은 모양으로 질 수 있어 `RegistrationList` 로 옮길 후보다. 손 불리언 파서(`AutomationScenario.cpp` · `OnlineServerConfig.cpp` 등)는 true/false 만 받으므로 `StringUtil::tryParseBool` 로 합칠 후보다. `ElementGrid.cpp` 의 `cellBytes` 는 읽는 쪽에서 크기를 손으로 계산한다(기믹 상태에서 고친 것과 같은 구조).
- **셰이더 확인**: 툰이 프로브(간접 조명)를 받지 않는 것이 의도인지. Shipping `MaterialTest.GameDomainShaderMaterialMatchesItsStruct` 는 셰이더를 쿠킹하지 않은 폴더에서 진다(환경).
- **층 게이트**: GameFramework 표에 "표 = 계산값" 검사를 붙일지, Bootstrap 13(계산값 9)이 의도인지(코드 소유자가 확인), Vfx 11 · Core Spatial 4 를 계산값으로 맞춘 것이 의도와 다르면 `[tierReason]` 으로 되돌린다.
- **확인 항목**: 클라이언트 근접 피해 소실(meleeunify) · 활의 가는 줄과 투사체 방향 · `PoseModifierComponent::setSocketTable` 호출처 없음([ShooterCharacterIntegration](ShooterCharacterIntegration.md)).
- **GPU 골든 겹침**: `EngineTest_HostOnly` 렌더 패스 골든 · `AppTest_HostOnly` 시나리오 · `QaGoldenImages` 가 같은 셰이더 출력을 서로 다른 장면으로 세 번 확인하는지 장면이 겹치는지 비교한다.
- **CI**: 야간 `Load (Linux | Release, nightly)` 실패의 근본 원인을 모른다. 근본 원인 후보(미해결)는 `DiskPathVsResourceId` 다.
- **시나리오**: 게임 시나리오(`game/*/automation`)가 `Resource/` 에 쓰는지 점검하지 않았다. 클릭한 순간의 방향으로 사격이 실제로 나가는지는 시나리오로 보지 못했다.
