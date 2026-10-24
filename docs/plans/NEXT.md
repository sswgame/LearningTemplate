# 다음 세션 인계

> 질문: 지금 어디까지 왔고, 다음 세션이 처음에 할 일은 무엇인가?
> 독자: 새로 시작하는 세션(에이전트와 사람).
> 결론: 통합 G(브랜치 14 개)가 main 에 들어가 push 됐다(Debug 빌드 경고 0 · lint 통과). 테스트(`nogpu` · 바뀐 영역) · Release 쿠킹 · GPU 확인이 남았다(`## 00000`). 다음 묶음은 `staticgate` · `poseaudit` · `staticleft` 다.

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

## 00000. 2026-10-25 통합 G — 브랜치 14 개가 main 에 들어감(push), 테스트 · 쿠킹 · GPU 대기 (이것부터 읽는다)

main 에 `wt/integG` 를 fast-forward 하고 push 했다. 검증은 **Debug `full-build` 경고 0 · 오류 0 과 `lint-suite` 까지만** 돌렸다(사용자가 범위를 줄임). `ctest -L nogpu|lint`, 바뀐 영역 테스트, Release 쿠킹 · `--verify-shaders`, GPU 확인은 아직 안 돌렸다 — 다음 세션이 이것부터 한다.

**들어간 브랜치(14)**: `lowlevellog` · `staticengine` · `registries` · `staticeditor` · `staticselftest` · `singletons2` · `cookfix` · `cookers2` · `plansclean` · `gfxfix`(`backendaudit` 에 포함) · `backendaudit` · `editorsc` · `shipscen` · `thresholdaudit`. 내용은 각 병합 커밋 제목과 브랜치 커밋이 원본이다.

**병합에서 고친 것**: ① `09_Decisions.md` 충돌 셋(정적 지역 상태 결정 둘과 단일체 게이트 줄은 양쪽을 남김, `plansclean` 이 지운 `RuleExceptionAudit` 링크는 `cookers2` 의 `-Wundef` 문장을 살려 뺌), `GpuVerification.md`(shipscen 2.1 절과 thresholdaudit 3 절을 둘 다 두고 절 번호 정리), README 진행 표는 `status-board --write` 로 다시 만듦. ② `sky` 2 개 · `vfxgpulights` 1 개의 `ExpectImage` 에 `provisional` 근거를 달았다(gfxfix 가 고친 무대 기준 어림값). ③ 컴파일 오류 둘 — `editorsc` 의 `noteInputFrame` 이 `staticselftest` 가 지운 `getState()` 를 불렀다(`findState()` 로), `lowlevellog` 의 `TestLowLevelReport` 중첩 구조체 기본 멤버 초기화 오류(이름공간의 `LowLevelReceived` 로 꺼냄).

**CI · 결정 · 미푸시**: ① push(`8767a5e18`)한 main 의 CI Lints 잡이 `CheckDocPaths` 로 졌다 — `docs/09_Decisions.md:250` 이 생성되는 폴더 `Source/Engine/Pch/` 를 경로로 적었다(로컬에는 있고 새 체크아웃에는 없음). 문장을 "새 pch 전용 폴더" 로 고쳐 커밋 · push 했다(이 절을 읽을 때 `py -3 -m Scripts ci-jobs --summary` 로 초록인지 먼저 본다; 계속 지면 `--annotations`). 교훈: 로컬에서만 있는 생성 폴더를 문서 경로로 적지 않는다. ② **사용자 결정 대기 없음**(정적 상태 게이트는 (b) 로 정해졌다). ③ 미푸시 변경 없음(이 절 이후 변경은 이 커밋에 포함). ④ 워크트리: 병합한 14 개는 지웠다. 남은 것은 `staticgate` · `poseaudit` · `staticleft`(다음 묶음) 뿐이다.

**병렬로 띄울 묶음(Roadmap 의 `[병렬 가능]`, 파일 소유가 겹치지 않을 때)**: 위 1~3 의 검증(통합 한 명)이 끝난 뒤에 — A. `staticgate` · `poseaudit` · `staticleft` 병합(통합 한 명, `-k 0` 빌드). B. 로그 id 이전(`countContaining` 약 120 곳) · 레지스트리 중복 로그 약 15 곳(작업자 둘, 파일이 다르다). C. 측정대기 문서 59 개의 판정(GPU 확인 뒤, `끝` 으로 올려 지우기 — `plans --check` 가 막는다). D. `Roadmap.md` 의 `[병렬 가능]` 항목(GpuVerification · Vfx 실행 확인 · 카메라 디렉터 · 미리보기 창 · 오디오 · 애니메이션 · 런타임 UI · 키트 조립 · Wayland 등)은 `py -3 -m Scripts plans` 와 Roadmap 에서 고르되 **작업자 여섯 안팎, 작업자는 빌드하지 않고 커밋만**.

**다음 세션이 처음에 할 일(순서대로)**
1. 통합 워크트리에서 `ctest -L nogpu` · `-L lint` · `-L race`(Debug), 바뀐 영역 테스트: CoreTest `LowLevelReportTest`, EngineTest `AutomationScenarioTest` · `AutomationScenarioChainTest` · `AnimationCrowdTest` · `SceneCookTest` · `LogTest`, EditorTest 전체. 실패는 어느 브랜치 탓인지 찾아 고친다.
2. 새 Release App 으로 `--cook-shaders` 후 `--verify-shaders`(`cookfix` 가 쿠커를 바꿨다 — 매니페스트 밖 옛 바이너리 삭제, 건너뛴 갈래 실패 처리).
3. GPU 확인(`gpu-verifier`, Release `ctest -L hostgpu` · 네 백엔드): 아래 "졌던 것" 전부와 `RHIDeviceTest` 의 새 완료 계약 테스트(`backendaudit`). `sky` · `vfxgpulights` 등 `provisional` 문턱은 `metric` 줄로 `measured` 로 바꾼다(`GpuVerification` 3 절). 어림값이 맞으면 종료 코드 14(`Unconfirmed`)로 끝나 CI 는 초록이고 로그로만 구분된다.

**GPU 확인에서 졌던 것(main d679388a5, 위 브랜치가 고침)**: `sky`, `vfxgpulights`(gl 은 `Vfx.md` 의 "GL R32 실수 RW 텍스처 읽기" 가 후보), `RHIDeviceTest.AsyncBufferReadback…`(DX11), `shooter3d.crowd`, `nilecity.gamepadcursor`, 에디터 시나리오 6 개, Shipping `gv_firstScene` `LOAD ERROR` 18 건, Shipping nogpu 7 건, `ShippingSceneMatchesGoldenImage`(dx12 에서 32565 화소 초과 — 아직 안 봄). 지형 여덟 레이어 · 지오모프 전용 시나리오는 저장소에 없다 — 시나리오로 만든다.

**다음 묶음(끝났다고 보고 받음, 이번에 병합하지 않음 — 모두 컴파일 대기)**: 각 워크트리 `D:/w/<이름>` 의 `git log main..` 와 `git status` 로 본다.
- `staticgate`(커밋 `f0deeebc4` 게이트 + `c8f253333` 래칫 · lint-suite 켬): `CheckMutableStatics` 가 정적 저장 기간의 가변 변수를 모양으로 판정(`MutableStatic/Local` · `/Namespace` · `/Member`). **정책 (b)(사용자 결정, 2026-10-25)**: 신규 위반부터 막고 기존은 `Scripts/lint/rules/CheckMutableStatics.toml` 의 `[pending]` 파일별 위반 총수 상한(292 개 파일, 합 628 — 옛 main 기준)으로 줄여 간다(표에 없는 파일 · 상한 초과 · 줄었는데 표 그대로면 실패, 범주를 옮겨도 막힘). **병합 직후**: 이번에 들어간 `staticeditor` · `staticselftest` · `staticengine`(그리고 `staticleft`) 때문에 줄인 파일마다 "표를 줄여라" 로 실패한다(의도) — `py -3 -m Scripts gate CheckMutableStatics --regen-baseline` 을 한 번 돌려 커밋한다(약 542 건 남을 것). 부수 변경: `CppScope.py` 로 옮긴 `classifyHead` · `isLambdaOpen`, `CodeText.py` 의 원시 문자열 안 `{` 가림 결함 수정(전 게이트 공용 — 병합 뒤 lint-suite). 못 보는 모양: 괄호 초기화 변수 · `#define` 본문 · `struct S{…} g;`.
- `poseaudit`(커밋 7 개): `getPoseAccess` 가 늘 `Writes` 이던 구현체(후처리 리그 · 방향 보정 · 리타깃 · 얼굴 · 체형 비율)가 실제 쓰기와 같은 판정을 쓴다. Debug 가드(`SkeletalMeshComponent::runTasks` 가 `ReadsOnly` 전후 포즈 지문 비교 → `SW_LOG_ASSERT`), 새 테스트 `AnimationPoseAccessTest` 6 건, 매니페스트 `pose-access-test`. 볼 것: 래그돌 · 군중 · 리그 · 리타깃 · 얼굴 테스트에서 새 단언, FacialRig 최소 XML, 지문이 `float3` · `quaternion` 패딩 없음을 가정.
- `staticleft`(`wt/staticeditor` 기준, 커밋 6 개 — `staticeditor` 가 main 에 들어갔으니 그대로 병합된다): `EditorSettingsInstance<T>` → `EditorSettingsRegistrar<T>`(등록 안 된 섹션 읽기는 로그 후 abort), `ScopeOwned` 11 곳, X11 `_s_active` → `ImGuiIO::BackendPlatformUserData`, `EditorChrome` `thread_local` 7 개, `setMeshModifierTaskManager` 삭제. 컴파일 위험: `LifetimeScope.h` include 를 지워 전이 include 가 빠진 파일, `EditorSettingsRegistration` 의 `mutable uint8` 과 집합 초기화, X11 가드 파일.

**아직 시작하지 않은 것**: ① 남은 `countContaining` 약 120 곳과 `HostLogJudge` 의 `KnownLog` 부분 문자열을 로그 id 로. ② 공용 형식을 쓰면서 중복 로그를 손으로 쓰는 레지스트리 약 15 곳. ③ 측정대기 계획 문서 59 개의 판정(GPU 확인이 초록이어야 `끝` 으로 올려 지울 수 있다). ④ `SW_LOG_ASSERT` 가 멈추기 전에 동기로 쓰지 않는 것, `SW_LOG_*` 가 싱크 없을 때 줄을 버리는 것(후보 `LowLevelReportingGaps`). ⑤ (`apptestfeature` 로 끝: 게임 팩 케이스 셋에 `SourceResources` 를 달고, `AppTestUtil` 이 `-EnableEditor` · `-game=` 인자와 `collectBuiltGamePacks` 를 선언 없이 쓰면 실패하게 했다. 에디터 케이스는 `#if !SW_SHIPPING` 로 컴파일에서 빠져 있어 표식이 필요 없다. `BuildFeature` 에 `ShippingBuild` · `WindowsPlatform` · `LinuxPlatform` · `ClientCode` · `AudioAssets` · `FatalSignalHandling` · `SwContainerTags` 를 더해 `#if`/`#else` 바로 뒤의 구성·플랫폼 `SW_TEST_SKIP` 22 곳을 `SW_TEST_REQUIRES_FEATURE` 로 바꿨다. 남은 것: `SW_TEST_SKIP` 을 구성 차이에 못 쓰게 막는 형식은 못 만들었다(런타임 조건과 모양이 같다) — 후보는 `#if`/`#else` 바로 뒤 `SW_TEST_SKIP` 을 잡는 린트 게이트. 런타임 판단으로 구성이 갈리는 `SW_TEST_SKIP`(`MemoryTagBench` 의 프로파일러 없음, `TestVfx` 의 `gv_vfxSimulation`, `TestModuleCatalog` 의 Bin/Modules 등)은 손대지 않았다.) ⑥ `CheckServiceAccess` 를 `.cpp` 로 넓히기(남은 영역을 옮긴 뒤 — `staticgate` 몫).

**교훈**: 작업자가 빌드하지 않는 대신, 서로 다른 브랜치가 같은 함수를 하나는 지우고 하나는 새로 부르는 "의미 충돌" 은 텍스트 병합이 잡지 못하고 통합 빌드에서만 드러난다(이번 `getState()`). 통합은 `-k 0` 빌드로 오류를 한 번에 모은 뒤 고친다. 세션이 갑자기 끊기면 브랜치 참조 파일이 NUL 로 깨질 수 있다(`git fsck`, `.git/logs/refs/heads/wt/<이름>` 의 마지막 커밋으로 복구).

## 0000. 2026-10-25 통합 E · F — main 에 들어감, GPU 확인 대기

### 상태

- **main 에 들어감(미푸시)**: 통합 E(`wt/integD` 의 34 브랜치 + `wt/plangfx2`)와 통합 F(`wt/plangfx2` 의 지형 레이어 여덟 · 지오모프 d9e050414, `wt/shaderfmt3` 의 셰이더 필드 `_camelCase` 6c1416635)를 합쳐 main 을 fast-forward 했다. **push 는 하지 않았다.**
- **검증(Debug)**: `full-build` 경고 0 · 오류 0(증거 남음), `lint-suite` 46/46, `ctest -L nogpu|lint` 통과(CheckTextureFolders 는 동시 부하에서만 시한을 넘고 혼자 통과). 요청받은 GPU 장치 테스트 — `RenderPassGPUTest` · `MaterialTest` · `ShaderBindingValidatorTest` · `QaGoldenImages` · `AppTest_Race` 통과. 셰이더는 새로 빌드한 **Release App** 으로 구웠고 다시 구워도 커밋과 같다(`--verify-shaders` 통과). **GPU 확인 대기** — Shipping · `-L hostgpu` 전체 · 네 백엔드 × 대표 셋(Empty · 에디터 · Shooter3D · NileCity) · 새 시나리오(fog · sky · realtimegi · vfxgpulights · modulepass · 지형 여덟 레이어)는 돌리지 않았다.
- **통합이 고친 것**(커밋 메시지가 정본): E — 리플렉션 · 컴파일 · 링크(프로파일 매크로 창구, `make_unique<T>.createWith`, `basic_string::substr`), 셰이더 패치 01~04 적용 · 쿠킹, 린트, 자식 프로세스 상속 핸들 수명, 뗀 뒤 새 누름 소비, 고도 0 하늘, PassCB 768 넘침(슬롯 1024 · 넘치면 오류). F — shaderfmt3 규칙을 새 셰이더 · 지형(패치 05 포함) · 머티리얼 · C++ 이름에 입힘, `App -help`(표에서 만든 인자 목록을 찍고 끝냄 + AppHelpTest), Vulkan 구조 버퍼의 TRANSFER_SRC(차폐 컬링 되읽기), avatarsample_d 임포트 스탬프.
- **병합하지 않은 것**: `plangfx2` 워크트리에 남은 커밋하지 않은 변경(셰이더 원본 — 이미 패치로 들어간 것의 사본)은 버려도 된다. `lambdaser` · `singletonget` 의 커밋하지 않은 변경은 공유 stash(`stash@{0}` 망가진 정규식, `stash@{1}` 중복 줄)에 있다.

### 다음 세션이 처음에 할 일

1. **GPU 확인**: Shipping 빌드 → `-L hostgpu` → `verify-batch --owner <이름>` 의 Shipping 항목(새 시나리오 fog · sky · vfxgpulights · modulepass · realtimegi, `Empty` 렌더 테스트 씬 + Shooter3D × 네 백엔드). 실패하면 그 단위를 고치거나 되돌린다. 특히 이번에 처음 GPU 에서 도는 것: 볼륨 안개 컴퓨트, 대기 산란 하늘, 프로브 갱신 컴퓨트(`probeupdate.hlsl` — FXC 때문에 루프 모양을 바꿨다), GPU 파티클 라이트, 모듈 패스.
2. **push**: 통합 G 와 함께 GPU 확인 전에 push 됐다(사용자 결정) — GPU 확인에서 지는 단위는 고치거나 되돌린다.
3. **정리**: 공유 stash, 셰이더 주석의 옛 이름 `MaterialShaderHooks`.

### 남은 것

끝난 일은 `git log` 에 있고, 여기에는 안 끝난 결함 · 확인만 한 줄씩 둔다.

- **같은 모양이 남았을 곳(이번 통합에서 본 것)**: ① 셰이더 쿠킹이 새 퍼뮤테이션 해시로 다시 구울 때 옛 바이너리를 지우지 않아 `--verify-shaders` 가 진다(통합이 손으로 지웠다) — `ShaderCooker` 가 매니페스트에 없는 바이너리를 스스로 지우게 할 후보. ② `App --cook-shaders` 가 쿠킹 실패가 있어도 종료 코드 0 이다(폴백). ③ 정의되지 않은 매크로를 `#if` 로 보는 include(AuthorityGhost)는 `-Wundef` 가 잡는다 — 켤지 정한다. ④ 로그 문장을 맞추는 테스트(`countContaining("…")`)는 로그 문구를 바꾸면 진다 — 로그 id 로 맞출 후보. ⑤ `TestResourceDataSchema` 의 종류 표는 엔진 등록표(`SW_RESOURCE_DATA_KIND`)의 사본이다(게임 파이프라인 XML 을 몰랐다).
- **싱글턴 · 람다**: `ResourceDataKindRegistry` 단일체(후보 `SingletonGetToServices`), 싱글턴 `get()` 게이트가 `instance()` · `shared()` 이름을 못 봄.
- **누수 탐지**: 설계 검토의 2 ~ 4단계([LeakDetection](LeakDetection.md)).
- **레지스트리 · 파서**: 이름을 손으로 거절하는 레지스트리(`LocalStoreFactory` · `SQLDriverRegistry` · `LoginService` · `ModuleRenderPassRegistry` · `EditorAssetTypeActions`)는 `RegistrationList` 후보, 손 불리언 파서는 `StringUtil::tryParseBool` 후보, `ElementGrid.cpp` 의 `cellBytes` 손 계산.
- **셰이더 확인**: 툰이 프로브(간접 조명)를 받지 않는 것이 의도인지.
- **층 게이트**: GameFramework 표에 "표 = 계산값" 검사를 붙일지, Bootstrap 13(계산값 9)이 의도인지.
- **확인 항목**: 클라이언트 근접 피해 소실(meleeunify) · 활의 가는 줄과 투사체 방향 · `PoseModifierComponent::setSocketTable` 호출처 없음([ShooterCharacterIntegration](ShooterCharacterIntegration.md)).
- **CI**: 야간 `Load (Linux | Release, nightly)` 실패의 근본 원인을 모른다(후보 `DiskPathVsResourceId`).
- **시나리오**: 게임 시나리오가 `Resource/` 에 쓰는지 점검하지 않았다. 클릭한 순간의 방향으로 사격이 나가는지는 시나리오로 보지 못했다.
