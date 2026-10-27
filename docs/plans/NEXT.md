# 다음 세션 인계

<!-- next-format: one-page -->

> 질문: 지금 어디까지 왔고, 다음 세션이 처음에 할 일은 무엇인가?
> 독자: 새로 시작하는 세션(에이전트와 사람).
> 결론: 통합 Q(브랜치 22 개)와 그 뒤 문서 요약 병합이 main 에 들어가 push 됐다. 첫 일은 아래 "남은 일" 15(CI) · 1~3 이다. 끝난 통합의 일지는 `git log` 가 정본이라 여기 두지 않는다.

이 문서는 지금 상태 한 장만 둔다. 순서는 [Roadmap](Roadmap.md), 설계는 계획 문서(`py -3 -m Scripts plans`), 결정은 [결정 기록](../09_Decisions.md), 작업 방식은 [11 작업 방식](../11_Workflow.md)이다.

## 1. 마지막 통합 — 통합 Q (2026-10-26 ~ 27)

`wt/integ-q` 에 끝 브랜치 14 개(체인 포함 22 개: skipgate · editctx · linecap · scntitle2 · autorun · scnrun · scnvalid · scnsplit · explog · wlwin2 · rhilocks · camframe2 · cfgref2 · jobhandle)를 병합하고, 병합 뒤 컴파일 · 테스트 결함 18 건을 원인별로 고쳐 main 에 fast-forward 하고 push 했다(내용은 `git log 2503baaf2..main` 의 `통합 Q -` 커밋).

| 단계 | 결과 | 소요 |
|---|---|---|
| 병합(충돌 21 파일, 예고대로 main 쪽 수정을 취함) · 구성 세 프리셋 | 통과 | 약 40 분 · 38 s |
| Debug `integ-gate`(`-k 0` + `nogpu\|lint`) | 112/112 | 140 s |
| `verify-batch` Debug | 전 항목 통과(`ctest-race` 포함) | 약 10 분 |
| `verify-batch` Shipping(빌드 · `hostgpu`) | 통과 | 약 8 분 |
| `verify-batch` Release(`app-scenarios-release` 포함) | 통과 78 · 실패 3 · 막힘 0 · 건너뜀 69 | 약 65 분(`app-scenarios-release` 1223 s) |

못 통과한 셋(`perf-frame` · `load-baseline-suite` · `load-auto-render-scale`)은 통합 P 와 같은 원인이다 — 아래 남은 일 1 · 2. 시나리오 Warning/Error 는 `app-scenarios-release` 에서 0 건(처리하지 않은 줄 15 없음).

## 2. 남은 일

| 무엇 | 첫 일 한 줄 | 계획 문서 |
|---|---|---|
| 1. 렌더 기준 낡음(`perf-frame` · `load-*` 렌더) | DoF · 모션 블러가 렌즈 없는 카메라(조리개 0)에서도 전 화면 한 번 돈다(Empty `GPU.Frame` 468 → 1089 us) — 꺼진 카메라에서 건너뛰는 단위를 내고, GPU 메모리 56.8 → 176.9 MB 를 `RenderTargetRegistry` 로 나눈 뒤 기준을 다시 뜬다(숨기기 위한 재기록 금지) | [Candidates-graphics](Candidates-graphics.md) |
| 2. OnlineLoadBot 접속 부하 | 봇이 "online kit 'matchmaking' could not initialize" 로 멈춘다(봇 프로세스에 엔진 서비스 · 타입 레지스트리가 없다) — `Tools/OnlineLoadBot/Main.cpp` 의 `ToolProcessScope` 를 `ServerApp` 같은 호스트 범위 + `Engine`(DedicatedServer)으로 | [ServerDebug](ServerDebug.md) |
| 3. 명령 없는 검증 항목 약 70 개 | `verify-batch --list` 의 "명령이 없어 돌지 않은 항목" 에 실행 명령을 달거나 사람 눈이 꼭 필요한 것은 사유와 분리하고, `plans --check` 가 "명령 없는 측정대기" 를 막게 구조로 닫는다(영역별로 `VerifyBatch.json` id 소유를 나눈다) | [GpuVerification](GpuVerification.md) |
| 4. 통과한 일회성 항목의 문서 대기 글 | `Test/QA/VerifyBatch.json` 의 `result_doc` 열을 따라 "측정대기 · 남은 확인" 글을 지우고 상태를 올린다(`끝` 이 된 계획 문서는 지우고 링크를 고친다) | 각 `result_doc` |
| 5. 문서 정정 | GL 깊이 충돌 원인(행 순서)으로 GpuVerification 3.1 의 "첫 0.5 초는 빛이 난다" 논거와 Vfx 의 GL R32 RW 읽기 문단을 고친다 | [GpuVerification](GpuVerification.md) · [Vfx](Vfx.md) |
| 6. 조립 문장 Warning 에 id | explog 뒤 id 없는 Warning 은 본문 전체 일치로만 선언된다 — 조립 문장(`%#`)인 Warning/Error 를 소스에서 찾아 `SW_LOG_WARNING_ID` 로 올린다(이번에 `ObjectValidation.Issue` 하나를 올렸다; `CheckExpectLogPhrases` 는 조립 문장을 못 본다) | [GpuVerification](GpuVerification.md) 4절 |
| 7. 에디터 자체 테스트의 `find` | `Source/Editor/SelfTest` 약 20 곳이 여전히 `findSubsystem` — 컨텍스트가 늘 있는 경로만 `getSubsystem` 으로(`EditorContext.h` 계약: 컨텍스트 없이 도는 테스트가 부르는 함수는 `find`) | — |
| 8. RHI 등록 단언 승격 | Debug 검증(`ctest-race` · `hostgpu`)에서 `assertRegistryMutableNow` 가 한 번도 걸리지 않았다 — 위반 경로가 없으니 다른 백엔드 등록 · 해제 경로를 `requireRegistryMutableNow`(모든 구성 abort)로 올릴지 정한다 | [결정 기록](../09_Decisions.md) 5-5 |
| 9. 샤드 비용표 | scnsplit 의 시나리오 케이스 예상 비용(옛 98 s · 286 s 를 케이스 수로 나눔)을 실제 케이스 시간으로 보정한다 — Debug `ctest -L hostgpu` 한 바퀴의 `AppTest_HostOnly_Shard*` 시간을 재 `Test/TestPolicy.json` 에 적는다(Shipping 은 시나리오 케이스가 없어 샤드 1~4 가 빈다) | — |
| 10. 선호 백엔드 목록 케이스 | `TestRHIDevice` 8 · `TestRenderPassGPU` 18 곳은 아직 "처음 서는 백엔드 하나" 로 돈다 — 선호 순서가 필요 없음을 확인하면 `SW_TEST_CASE_PER_PARAMETER` 한 줄씩 | — |
| 11. 자동화 개선 후보 | 어림값 문턱 승격 도구(provisional 38 개) · 비에디터 게임 시나리오 묶음 실행(HostGpuIsolation H2 선행) · 고정 프레임 대기 계측. 행 감시의 교착 시나리오(`wallTimeoutSeconds="3"` + 게임 스레드 멈춤 → 종료 12 · `"hang": true`)를 `VerifyBatch.json` 항목으로 | [AutomationSplit](AutomationSplit.md) · [HostGpuIsolation](HostGpuIsolation.md) |
| 12. 린트 후속 | `GenerateLintTargets` 에 `SKIP_RETURN_CODE 77`, `CheckNameReferences` 의 다른 걷기들이 건너뜀을 통과로 내는지 | — |
| 13. 오래된 결함 후보 | 셰이더 쿠커가 옛 퍼뮤테이션 바이너리를 안 지움 · `--cook-shaders` 실패에도 0 · 손 불리언 파서(`StringUtil::tryParseBool`) · 이름 거절 레지스트리(`RegistrationList`) · `ResourceDataKindRegistry` 단일체 · 누수 탐지 2~4 단계 · 야간 Linux Load 실패 원인 · crowd `CrowdSharedUnits` 가설 셋(임시 로그 실험) · RHI 파일 후보(DX11 staging 합치기 · Vulkan 프레임 밖 펜스 대기 · DX11 Present 실패 로그 반복) | [LeakDetection](LeakDetection.md) · [ShooterCharacterIntegration](ShooterCharacterIntegration.md) · `Candidates-*.md` |
| 15. CI 원인 재확인 | `MeshAssetTest.MeshComponentResolvesMeshAssetPath`(`TestMeshAsset.cpp:342`, `countAssetWaiters()==0` 기대에서 1)는 통합 Q 의 ab2f0cc71(컴포넌트 쪽 블록을 기다림)이 고쳤을 수 있다 — `ci-jobs --summary` 로 재실행 결과부터 본다. `TestSyntaxCheck.testMsvcAndGccFormats` 경로 구분자는 247216e87 이 고쳤다(CI 재실행으로 확인) | — |
| 16. 추적되는 컴파일 셰이더 | `Resource/engine/shaders` 의 `.spv` · `.dxil` · `.dxbc` 약 1만 3천 개(팩 770 MB)를 git 에 두는 근거를 `docs/decisions/` 에서 찾고, 없으면 생성물로 뺄지 정한다(되돌리기 어려운 삭제라 사용자 확인 뒤) | [결정 기록](../09_Decisions.md) |
| 17. 남은 작업자 변경 | `wt/onlinekits`(`HpackHuffmanTable.h`) · `wt/wlserver`(`CheckX11Isolation.py`)의 커밋 안 한 변경을 보고 버릴지 살릴지 정한다(브랜치는 main 에 병합됨, 삭제는 사용자 확인 뒤) | — |
| 18. 사용자 결정 | 말 모델(WildWestHorseMount W3)의 CC0 에셋 · 라이선스 사용 여부 | — |
| 19. `result_doc` 낡은 가리킴 | `Test/QA/VerifyBatch.json` 의 `result_doc` "docs/plans/NEXT.md 0000절"(232 곳)은 NEXT 가 한 장이 되어 없는 절을 가리킨다 — 실제로 고칠 문서 · 문구로 일괄 정리하고 `verify-batch --check` 로 본다 | — |
| 20. 통합 에이전트 모델 | `.claude/agents/integrator.md` 의 `model: opus` 고정을 [에이전트와 토큰](../12_Agents.md) 1절 표(통합 실행 sonnet · medium, 실패 원인 수정은 worker opus)에 맞춰 확정한다 | [12 에이전트와 토큰](../12_Agents.md) |

`D:/w` 바로 아래 작업자 산출물(`e.py` · `kh_*.log` · `vb.bak` 등)은 지워도 된다(사용자 허락).

## 3. 확인할 것(다음 통합의 `verify-batch` 에서)

- `app-scenarios-release`(에디터 체인 201 단계 · 게임 팩 `AppScenarioTest.<게임>.*`) — 통합 Q 에서 통과. 다음 통합도 Warning/Error 0 을 본다.
- `perf-frame` · `load-baseline-suite` · `load-auto-render-scale` — 남은 일 1 · 2 가 들어간 뒤.
- `rhi-native-context-guard` · `rhi-transfer-dx12-u5`(DX12 Debug 디버그 레이어), `host-write-snapshot` — 명령이 생기면.
- CI: push 뒤 `py -3 -m Scripts ci-jobs --summary` 한 번 — Wayland 잡(`wayland-ci-job`, wlwin2 의 `PosixProcessEnvironment.cpp` · 커서 모양 · 소수 배율 첫 컴파일)과 `CheckSourceGlob`(skipgate) 위반 여부.

### 끊긴 작업(2026-10-27 새벽, 사용량 한도로 작업자 전원 중단 — 7시 이후 재개)

main 은 통합 R 그대로다. 아래는 워크트리 브랜치에만 있고 병합되지 않았다. 중단된 작업자는 `SendMessage` 로 이어 시킬 수 있다(작업은 저장돼 있다).

| 묶음 | 브랜치 | 상태 |
|---|---|---|
| 통합 S | `wt/integ-s` | 끝 브랜치 병합 중 중단. 이어서 병합 → 빌드 → `verify-batch` Debug/Shipping/Release → push. subs 소비자(UserSettingsManager · Telemetry · CrashReport · UIBindingSet · TestUserSettings · ReflectionInvoke)를 `Subscription` 에 맞춰 고친다. `HeaderSelfContained.py` 중복 수정은 loadbot 것을 취한다 |
| 구독 · 잠금 · 오류 | `wt/subs` `wt/lockfmt` `wt/errh` `wt/hostlife` `wt/onlinelife` `wt/corebase` `wt/corelib` `wt/jsonwriter` `wt/procargs` `wt/clk` | 대부분 끝, 후속(등록 형태 · ScopedLock/GUARDED_BY · 가능한 실패 동사) 중단 |
| 입력 계층 | `wt/inputlayer` `wt/rclick` `wt/reuse` | I0–I2 끝, I3a–g · I4 중단 |
| 그래픽스 | `wt/gfxc` `wt/taabypass` `wt/gfprobe` | gfxc ea6559434 끝. 셰이더 소스 패치(scratchpad `gfxc-shader-cook-batch.patch`)는 통합이 적용 → `App --cook-shaders` → `--verify-shaders`. 후속: DDS 비트마스크 표를 `DdsFormats.json` 으로, 블렌드 모드 대소문자 별칭 읽기 제거 |
| 중단 | `wt/finitefloat` `wt/determ` `wt/edperf` `wt/unixloop` `wt/netsec` `wt/candnet` | 이어 시킨다. `wt/netsec` 212995371(평문 UDP 정책)은 사용자가 병합을 승인함(아래 결정) |
| **따로 병합할 준비 브랜치(사용자 지시: 통합 S 에서 병합하지 않는다)** | `wt/resid` `wt/cand-engine` `wt/cand-gameplay` `wt/cand-quality` | 모두 기준 `wt/integ-s` 5ec9eabbe. 각 브랜치 보고(후보별 결과 · 충돌 가능 파일 · 통합 확인)는 작업자 보고에 있음. 겹치는 파일은 `Test/QA/VerifyBatch.json` · `Scripts/qa/PerfCompare.py` · `docs/decisions/5-2·5-8` · `Candidates-*.md`(끝 추가라 풀기 쉽다). 병합 순서 권고: cand-quality → cand-engine → cand-gameplay → resid(호출처 대량 수정이라 마지막). 병합 뒤 통합이 `normal-maps-reimport` · `shared-module-unload-tests` · `log-gate-cleanup-tests` 를 돌린다. 작업자(worker)에게는 `SendMessage` 가 없어 직접 소통은 안 된다 |
| **작업 구조 변경(사용자 결정 2026-10-27)** | — | 앞으로는 병렬 작업자 + 통합 대기를 쓰지 않고, 한 에이전트가 일을 몰아서 단계별로 나눠 진행한다(단계마다 코드 → 린트 → 문법 검사 → 커밋, 빌드 · 검증은 새 일마다 하지 않고 맡은 묶음이 다 끝난 뒤 같은 에이전트가 한 번에 하고 병합 · push 까지 한다). 다음 세션 첫 일: CLAUDE.md 원칙 2 · `WorkerRules.md` · `11_Workflow.md` · `12_Agents.md` 를 이 구조로 고친다. 지금 도는 작업자 · 준비 브랜치는 그대로 마무리 |
| 마지막으로 이어 시킨 것 | `wt/verifycmd`(cand-gameplay 작업자) | 측정대기 63개 중 20개만 닫음 → 나머지 약 36개를 닫거나 manual 분류 |

사용자 결정(2026-10-27): 장르 키트 21개(232 파일)는 앞으로 쓰므로 유지, Steam/Epic SDK 와 H-3 운영 문서는 보류. 말 모델은 BlendSwap "Horse Rigged All Gaits"(CC0 표기, 라이선스·클립은 받아서 확인)를 사용자가 받아 `models_raw/` 에 넣는다. 평문 UDP 정책(wt/netsec 212995371)은 병합 승인됨 — 통합 S 가 병합한다(Shipping 에서 평문 LAN 파티 불가). 사용자 영향: Shortcuts.json 초기화, `<bind modifier>` 형식, 레이아웃 dockspace v7→v8, foliageDensity/reduceFlashing 설정 제거.
