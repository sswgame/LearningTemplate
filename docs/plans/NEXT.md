# 다음 세션 인계

> 질문: 지금 어디까지 왔고, 다음 세션이 처음에 할 일은 무엇인가?
> 독자: 새로 시작하는 세션(에이전트와 사람).
> 결론: 통합 E · F 가 main 에 들어갔다(미푸시). 그 뒤 GPU 확인에서 진 항목을 고친 작업 브랜치 약 17 개가 병합을 기다린다(`## 00000`). 다음 세션은 이 브랜치들을 한 곳에 모아 병합하고 한 번에 검증한 뒤 GPU 확인(Shipping · hostgpu · 네 백엔드)이 초록이면 push 한다.

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

## 00000. 2026-10-25 병합 대기 브랜치 약 17 개 (이것부터 읽는다)

main = `d679388a5`, origin 보다 276 커밋 앞, **push 안 함**. 아래는 전부 `D:/w/<이름>` 워크트리의 `wt/<이름>` 브랜치에만 있고 main 에 없다. **작업자는 빌드 · 테스트를 하지 않았다**(CLAUDE.md 원칙 2) — 모든 항목이 "컴파일 대기"다. 통합 담당이 한 곳(`worktree-make integG --base main`)에 모아 병합하고 Debug `full-build`(경고 0) · `lint-suite` · `ctest -L nogpu` · Release 쿠킹 · GPU 확인을 **한 번만** 돌린다.

**끝난 브랜치(보고 받음)**
| 브랜치 | 내용 |
|---|---|
| `cookfix` | 셰이더 쿠커가 매니페스트 밖 옛 바이너리를 지우고, 조용히 건너뛰던 갈래를 실패로 센다 |
| `cookers2` | 씬 · 프리팹 쿠커의 옛 산출물 정리(`CookStore` 요청 목록), `-Wundef`, `SW_LOG_ERROR_ID` · `countWithID`(본보기 세 곳) |
| `singletons2` | 서비스 접근 게이트에 `instance()` · `shared()` 이름 검사 |
| `plansclean` | 상태 `끝` 계획 문서 5 개 삭제, `plans --check` 가 `끝` 문서를 위반으로 잡음 |
| `registries` | 레지스트리 5 곳 `RegistrationList`/`NameRegistry`, `tryParseBool` 5 곳, `ElementGrid::Cell` |
| `staticselftest` | `Editor/SelfTest` 정적 지역 상태 19 곳 → 서브시스템 · `getTestState<T>()` |
| `staticeditor` | 에디터 정적 지역 상태 38 곳 + 약 10 곳 → `EditorStateStore` · 패널 멤버 · 서브시스템 |
| `gfxfix` | `sky` 무대 해 높이, `vfxgpulights` 캡처 프레임, DX11 `waitIdleInternal` 이 GPU 완료를 기다림, 래그돌 `Writes` 조건, 패드 연결 단계 |
| `editorsc` | 에디터 시나리오 6 개(`editorstartup` 은 `Editor.HasPanel`, `retarget` 접기, `tilemapdatalayers` 임시 폴더 등) |
| `lowlevellog` | `SW_LOW_LEVEL_ERROR` · `WARNING` · `ASSERT( expr, "fmt", … )`(옛 `REPORT` 삭제), 보고자에 파일 · 줄, Error 는 stderr 먼저 동기, `LowLevelReportTest` 6 건, Decisions 한 줄, 후보 `LowLevelReportingGaps` |
| `shipscen` | Shipping 에서 `gv_` 시나리오 `Skipped`(`[ PARTSKIP ]`), `BuildFeature::SourceResources` 로 21 케이스 건너뜀, `GlobalVariableTest` 종료 조건 |

**`staticengine` 끝(보고 받음, 커밋 7 개)**: Core · Engine · GameFramework · App 정적 지역 상태를 소유 구조로 — 새 엔진 서비스 `ScreenshotMailbox`(서버에서는 없으면 오류 로그 후 버림), `RHIBackendSwitcher`·`GlobalVariableManager` 멤버, `ScopeOwned` 약 15 곳(`MeshCache` · `SharedAssetCache<T>` · `ShaderReflection` · `GameDataCache` 등), `LogOnce` 에 `BusMessageRejected`/`PushRelayRejected` 종류, `TickBatchRegistry` 침입 연결 목록, `PrefabThreadChain`. 마지막 빌드(세션 끊기기 전)는 경고 0 · 오류 0, CoreTest 703/706, EngineTest 3902 통과 · 0 실패. **그 뒤 편집은 컴파일 대기**(`RHIRenderResource` 멤버명 `_uniqueSetResource` 개명, `ServerBus` 의 `describeReadResult` 자유 함수, 헤더 주석). 못 옮긴 곳(이유는 09_Decisions): `RHIRenderResource` 표, 링크 때 등록 표(`AssetTypeRegistry` · `DevCommandRegistry` · `DebugHUDRegistry` · `BehaviorTaskRegistry`), `CrashContext`, 카메라 흔들림 델리게이트, 폐쇄 후 폴백 정적(`Render2DSettings` · `ReflectionValidation`). `CheckServiceAccess` 를 `.cpp` 로 넓히면 남은 영역이 실패해 넓히지 않았다 — `staticgate` 가 맡는다.

**`thresholdaudit` 끝(보고 받음, 커밋 3 개 `f51e6fb0a` · `23879be0c` · `65b2e5642`, 컴파일 대기)**: `ExpectImage` 72 개를 전수 분류(목록은 `GpuVerification.md` 3절). 모든 `ExpectImage` 는 `measured="YYYY-MM-DD 백엔드:값 …"` 와 `provisional="이유"` 중 정확히 하나가 있어야 하고 없으면 시나리오 읽기 오류(새 `AutomationThresholdBasis`). 잰 값 17 개는 `measured`, 어림값 52 개는 `provisional`. 어림값 문턱이 맞으면 종료 코드 14(`AutomationResult::Unconfirmed`), 보고 JSON `unconfirmed[]` 와 `[Scenario] UNCONFIRMED` 줄에 남는다(체인은 14 를 실패로 보지 않고 이어 가며 최종 결과를 14 로, `AppScenarioTest` 는 14 를 통과로 넘기되 로그를 남긴다 — CI 케이스는 초록이고 로그로만 구분됨). **병합하면 `sky` 2 개와 `vfxgpulights` 1 개의 `ExpectImage` 는 근거 속성이 없어 읽기 오류가 난다** — `gfxfix` 가 이 시나리오를 고치는 중이라 일부러 옮기지 않았다. 병합 뒤 이 셋에 `measured`(측정 후) 또는 `provisional` 을 달아야 한다. 통합이 돌릴 것: EngineTest `AutomationScenarioTest` · `AutomationScenarioChainTest`(테스트가 `makeTempDirectory` 로 PPM 을 쓰는 부분을 특히). 형식 밖: 코드의 `kMaxDifferingFraction` 같은 테스트 상수와 `Test/TestPolicy.json` 의 `softwareRenderSeconds` 추정값. 주의: 이 작업자가 입력 대기 `py -3 -` 를 `taskkill /IM py.exe` 로 정리해 이 PC 의 다른 `py.exe`(다른 작업자의 스크립트 포함)가 같이 끝났을 수 있다 — 중간에 이상하게 멈춘 작업자가 있으면 이것을 의심한다.

**다시 시작시킨 브랜치(세션이 끊겨 재개, 보고 대기)**: `staticgate`(함수 안 가변 `static` 을 모양으로 막는 게이트 — lint-suite 등록은 패치 파일로만 두고 위반 0 이 된 뒤 켠다), `backendaudit`(`wt/gfxfix` 기준, RHI 완료 계약 표 · 네 백엔드 대조 · 테스트), `poseaudit`(`getPoseAccess` 선언이 실제 쓰기와 어긋난 구현체 전수), `staticleft`(`wt/staticeditor` 기준, `EditorSettingsInstance` · `ScopeOwned` `s_*Owner` · `thread_local` 등). 각 워크트리의 `git log main..` 와 `git status` 로 어디까지 됐는지 본다.

**병합 순서와 충돌 후보**: 기준 브랜치가 있는 것은 그 뒤에 — `backendaudit` 은 `gfxfix` 위, `staticleft` 는 `staticeditor` 위. `Macros.cpp` 는 `lowlevellog` 와 `staticengine`(`getAssertDialogState`), `LogOnce.cpp` 는 `staticengine`, 셰이더 쿠커는 `cookfix` 와 `cookers2`(파일은 다르다고 보고), `Test/EngineTest` 는 `staticeditor` · `registries`, README 진행 표는 `lowlevellog` · `plansclean` 이 `status-board --write` 로 다시 만들었으니 충돌하면 생성기로 다시 만든다, `thresholdaudit` 의 시나리오 형식 변경은 `editorsc` · `gfxfix` 가 고친 시나리오와 겹친다(옛 형식 리더는 두지 않는 방침이라 데이터를 다시 쓴다).

**GPU 확인에서 졌던 것(main d679388a5, 위 브랜치가 고침 — 병합 뒤 Release `ctest -L hostgpu` 로 확인)**: `sky`(무대 해가 노을이었음 → 피치 0.4, 문턱 −0.02 는 측정 전 어림값이라 확정 필요), `vfxgpulights`(문턱 0.02 어림값, gl 은 `Vfx.md` 의 "GL R32 실수 RW 텍스처 읽기"가 후보), `RHIDeviceTest.AsyncBufferReadback…`(DX11), `shooter3d.crowd`, `nilecity.gamepadcursor`, 에디터 시나리오 6 개, Shipping `gv_firstScene` `LOAD ERROR` 18 건, Shipping nogpu 7 건, `ShippingSceneMatchesGoldenImage`(dx12 에서 32565 화소가 허용 오차 초과 — 골든을 다시 뜨거나 원인을 본다, 아직 안 봄). 지형 여덟 레이어 · 지오모프 전용 시나리오는 저장소에 없다(`envshowcase` 만 봤고 이음매는 못 봄) — 시나리오로 만든다.

**아직 시작하지 않은 것**: ① 남은 `countContaining` 약 120 곳과 `HostLogJudge` 의 `KnownLog` 부분 문자열을 로그 id 로(`cookers2` 위에서). ② 공용 형식을 쓰면서 중복 로그를 손으로 쓰는 레지스트리 약 15 곳(`registries` 위에서). ③ 측정대기 계획 문서 59 개의 판정(GPU 확인이 초록이어야 `끝` 으로 올려 지울 수 있다 — `plans --check` 가 이제 `끝` 문서를 막는다). ④ `staticgate` 의 lint-suite 등록(위반 0 일 때). ⑤ 로거 구조 결정은 끝났다(아래 `lowlevellog`) — 이 문서 `## 0000` 의 "결정 대기" 4 번은 지운다. ⑥ `SW_LOG_ASSERT` 가 멈추기 전에 동기로 쓰지 않는 것, `SW_LOG_*` 가 싱크 없을 때 줄을 버리는 것(후보 `LowLevelReportingGaps`). ⑦ `AppTestSupport` 가 Shipping 에서 안 도는 게임 케이스에 `SW_TEST_REQUIRES_FEATURE( SourceResources )` 가 빠졌는지.

**정리**: 병합이 끝난 워크트리는 `py -3 -m Scripts worktree-remove <이름>`(다른 세션이 주인으로 잡혀 있으면 `--even-if-owned`, 사용자가 허용함). stash 두 개(`lambdaser` 중복 줄, `singletonget` 망가진 정규식)는 버려도 된다. 입력 대기로 남은 파이썬 프로세스가 있으면 끈다.

**교훈**: 작업자에게 "끝에 빌드 한 번" 이라고 시켰다가 작업자 여덟이 빌드 칸을 다투었다 — 작업자는 빌드하지 않고 통합이 한 번에 돌린다(CLAUDE.md 원칙 2). 세션이 갑자기 끊기면 브랜치 참조 파일이 NUL 로 깨질 수 있다(`git fsck`, `.git/logs/refs/heads/wt/<이름>` 의 마지막 커밋으로 복구).

## 0000. 2026-10-25 통합 E · F — main 에 들어감, GPU 확인 대기

### 상태

- **main 에 들어감(미푸시)**: 통합 E(`wt/integD` 의 34 브랜치 + `wt/plangfx2`)와 통합 F(`wt/plangfx2` 의 지형 레이어 여덟 · 지오모프 d9e050414, `wt/shaderfmt3` 의 셰이더 필드 `_camelCase` 6c1416635)를 합쳐 main 을 fast-forward 했다. **push 는 하지 않았다.**
- **검증(Debug)**: `full-build` 경고 0 · 오류 0(증거 남음), `lint-suite` 46/46, `ctest -L nogpu|lint` 통과(CheckTextureFolders 는 동시 부하에서만 시한을 넘고 혼자 통과). 요청받은 GPU 장치 테스트 — `RenderPassGPUTest` · `MaterialTest` · `ShaderBindingValidatorTest` · `QaGoldenImages` · `AppTest_Race` 통과. 셰이더는 새로 빌드한 **Release App** 으로 구웠고 다시 구워도 커밋과 같다(`--verify-shaders` 통과). **GPU 확인 대기** — Shipping · `-L hostgpu` 전체 · 네 백엔드 × 대표 셋(Empty · 에디터 · Shooter3D · NileCity) · 새 시나리오(fog · sky · realtimegi · vfxgpulights · modulepass · 지형 여덟 레이어)는 돌리지 않았다.
- **통합이 고친 것**(커밋 메시지가 정본): E — 리플렉션 · 컴파일 · 링크(프로파일 매크로 창구, `make_unique<T>.createWith`, `basic_string::substr`), 셰이더 패치 01~04 적용 · 쿠킹, 린트, 자식 프로세스 상속 핸들 수명, 뗀 뒤 새 누름 소비, 고도 0 하늘, PassCB 768 넘침(슬롯 1024 · 넘치면 오류). F — shaderfmt3 규칙을 새 셰이더 · 지형(패치 05 포함) · 머티리얼 · C++ 이름에 입힘, `App -help`(표에서 만든 인자 목록을 찍고 끝냄 + AppHelpTest), Vulkan 구조 버퍼의 TRANSFER_SRC(차폐 컬링 되읽기), avatarsample_d 임포트 스탬프.
- **병합하지 않은 것**: `plangfx2` 워크트리에 남은 커밋하지 않은 변경(셰이더 원본 — 이미 패치로 들어간 것의 사본)은 버려도 된다. `lambdaser` · `singletonget` 의 커밋하지 않은 변경은 공유 stash(`stash@{0}` 망가진 정규식, `stash@{1}` 중복 줄)에 있다.

### 다음 세션이 처음에 할 일

1. **GPU 확인**: Shipping 빌드 → `-L hostgpu` → `verify-batch --owner <이름>` 의 Shipping 항목(새 시나리오 fog · sky · vfxgpulights · modulepass · realtimegi, `Empty` 렌더 테스트 씬 + Shooter3D × 네 백엔드). 실패하면 그 단위를 고치거나 되돌린다. 특히 이번에 처음 GPU 에서 도는 것: 볼륨 안개 컴퓨트, 대기 산란 하늘, 프로브 갱신 컴퓨트(`probeupdate.hlsl` — FXC 때문에 루프 모양을 바꿨다), GPU 파티클 라이트, 모듈 패스.
2. **push**: GPU 확인이 초록이면 main 을 push 하고 `ci-jobs --summary`.
3. **정리**: 병합이 끝난 워크트리 `worktree-remove`(통합 세션이 지우지 못한 것 — 주인 프로세스가 살아 있는 앞 세션 소유), 공유 stash, 셰이더 주석의 옛 이름 `MaterialShaderHooks`.
4. **결정 대기**: 로거 구조(`SW_LOW_LEVEL_REPORT` 냐 Logger 를 Container · Memory 에서 떼느냐) — design-reviewer 로 정하고 `09_Decisions.md` 에 적는다.

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
