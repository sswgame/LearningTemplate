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

- **병합 끝(`integD`)**: parseintlist · lambda* 여섯 · kitshelpers · servicelist · verifyleak · scnpixel · singletonget · thumbheadless · plansweep · leakwin · ownership · cand* 전부 · measurebatch · judgeapply · planproject · plannet(세 묶음) · leftovers · docsweep · planengine · plangameplay · planui · plananim · plangfxq. 한 번도 빌드하지 않았다.
- **아직 병합 못 한 것**: ① shaderfmt3 는 커밋되지 않았다(145 파일이 `D:/w/shaderfmt3` 에 staged). 셰이더 커밋 훅이 빌드된 App 의 `--cook-shaders` · `--verify-shaders` 를 요구하므로, A 단계에서 App 을 지은 뒤 그 워크트리에서 한국어 메시지로 커밋하고 병합한다. FXC 확인 곳: `ViewMode::applyToSurface` 의 `inout` 인자, `MaterialSurface::setAlphaCutoff`, 정의 앞 호출 `SurfaceOutput::pack` · `MaterialTexture::sampleIndexTexture`. ② plangfx2(돌고 있음): 보고하면 `integD` 에 병합한다. 확인 대기 줄은 `매니페스트 id` 로 적어야 한다(`plans --check` 가 막음). ③ 통합 단계에서 할 코드: 셰이더 패치 `ShaderPatches/02-ModuleRenderPass-M2.patch` 적용 · 쿠킹, PreloadAndLod P9, CharacterAppearanceIntegration E6 표면 셰이더.
- **병합 충돌 요령**: `README.md` 는 `git checkout --ours README.md` 후 `status-board --write README.md`, `Roadmap.md` 는 새 쪽 줄을 남기고 옛 줄을 지운 뒤 `plans --check`.
- **main**: 로컬 main 이 `origin/main` 보다 7 커밋 앞서 있다(미푸시). 병합이 끝난 `wt/*` 브랜치와 워크트리(약 50 개)는 통합이 끝난 뒤 `worktree-remove` 로 지운다.
- **판정 결과**(design-reviewer, 2026-10-24, `09_Decisions.md` 에 반영 끝): 포토 모드는 UI 층을 숨긴다(구현은 judgeapply) · 팝업 잘림은 유지 · 단일체 예외는 종류 키 5개 · 두 계획 문서는 측정대기로 되돌림 · `sw_new` 삭제 · n/n 점검을 커밋 훅에 걸었다. 컴파일 확인: `Memory.h`(`make_unique`, `FixedCountArray`), `UILayer.h`(`ENUM(Flags)` 를 `PROPERTY` 로 쓴 선례 없음, 리플렉션 파서 확인).

### 새 세션이 처음에 할 일

1. **첫 일**: `D:/w/integD`(브랜치 `wt/integD`)에서 plangfx2 가 보고했는지 `git log wt/plangfx2` 로 보고, 보고가 왔으면 병합한다. 안 왔으면 기다리지 말고 있는 것으로 A 단계부터 시작한다(plangfx2 는 나중에 병합).
2. **A 단계**: `cmake --preset Ninja-Debug` → `full-build --owner integD`, 컴파일 오류는 `-k 0` 으로 모아 한꺼번에 고친다. 아래 "컴파일 확인이 필요한 곳"과 작업자 보고의 파일 목록이 의심 지점이다. 이어서 `py -3 -m Scripts verify-batch --owner integD`.
3. **결정 대기**: 로거 구조 — `SW_LOW_LEVEL_REPORT`(Core/Common 의 낮은 층 보고기)로 갈지, Logger 를 Container · Memory 에 기대지 않게 만들어 `SW_LOG_*` 를 어디서나 쓰게 할지. design-reviewer 로 언리얼 · 유니티 · 고도와 견주어 정하고, 정한 뒤에는 `09_Decisions.md` 에 적는다(사용자에게 되묻지 않는다).
4. **병합 · 푸시 못 한 변경**: 로컬 main 은 `origin/main` 보다 앞서 있고(미푸시), `integD` 의 모든 병합도 main 에 들어가지 않았다. 통합 검증이 끝난 뒤 main 병합 · push, `ci-jobs --summary`, `worktree-remove`(약 50 개).
5. **정리할 것**: 공유 stash 에 남은 작업자 항목(stash@{0..2} 는 plangfx2, stash@{3} 은 `wt/gpB`), 셰이더 주석에 남은 옛 이름 `MaterialShaderHooks`(forwardlit.hlsl · cctvmonitor.hlsl), `08_Verification.md` · `09_Decisions.md` 구조 정리, NEXT 의 A-D 표를 `verify-batch` 설명 한 줄로 줄이기.
6. **새 병렬 파동**: 통합이 끝나기 전에는 띄우지 않는다. 끝난 뒤 후보: plannet 의 남은 단위(채팅 · 친구 화면, GM 도구 조립 실행 파일, APNs H2-2~5, ServerDebug S6~S8), 로드맵의 `[병렬 가능]` 표시.

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
