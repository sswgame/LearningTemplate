# 다음 세션 인계

<!-- next-format: one-page -->

> 질문: 지금 어디까지 왔고, 다음 세션이 처음에 할 일은 무엇인가?
> 독자: 새로 시작하는 세션(에이전트와 사람).
> 결론: 통합 S(브랜치 60여 개)가 main 에 들어가 push 됐다. 아직 병합하지 않은 브랜치 7 개(아래 3절)를 정해진 순서로 병합하는 것이 첫 일이고, 그 전에 작업 구조를 한 에이전트 단계별 진행으로 바꾸는 문서 개정(3절 맨 아래)이 있다. 끝난 통합의 일지는 `git log` 가 정본이라 여기 두지 않는다.

이 문서는 지금 상태 한 장만 둔다. 순서는 [Roadmap](Roadmap.md), 설계는 계획 문서(`py -3 -m Scripts plans`), 결정은 [결정 기록](../09_Decisions.md), 작업 방식은 [11 작업 방식](../11_Workflow.md)이다.

## 1. 마지막 통합 — 통합 S (2026-10-27)

`wt/integ-s` 에 구독 · 잠금 · 오류 · 입력 계층 · 그래픽스 · 결정성 · 에디터 성능 · 네트워크 보안 등 작업자 브랜치를 병합하고, 병합 뒤 결함을 원인별로 고쳐 main 에 fast-forward 했다(내용은 `git log origin/main` 의 최근 커밋; 사용자 영향은 아래 3절 끝).

| 단계 | 결과 |
|---|---|
| `verify-batch` Release 전체(첫 판, 606a0c07a) | 통과 94 · 실패 40 · 건너뜀 67. 실패 40 중 32 개는 `app-scenarios-release` 가 덮는 `covered` 항목 |
| 실패 원인 고침 | 에디터 인스턴스 파괴 뒤 프레임 상태의 뷰 RT 해제(A) · 간접 드로우 테스트의 PSO(C) · GL 간접 디스패치의 컴퓨트 프로그램(E) · 텍스처 리로드 테스트(F) · 무인 실행의 사양 품질 프리셋(`flowterrain` · `terrainbrush`) · 키보드 주인 판정(`netsim`) · UI 미리보기 도구 줄(`uipreview`) · `assetmanage` GL 색인 대기 · 새 에디터 인스턴스 첫 UI 프레임의 병렬 기록 경합 · 서버의 지난 크래시 묶기 경고 · 데스크톱 전역 상태 케이스의 `SuiteKind::Desktop` 분리 |
| `app-scenarios-release`(이제 `ctest -L hostgpu` 샤드 + `*_Desktop`) | 통과, 1300 s(c62f76b26). 시나리오 Warning/Error 0 |
| 건너뛴 것 | `load-baseline-suite` 실패(기준 낡음 의심, 남은 일 1) · `scenario-thresholds-promote` 실패(낡은 인자, 남은 일 2) · `load-auto-render-scale` 은 통과했으나 첫 측정이 예산의 1/10 이라 전환 확인 못 함. 동 일회성 항목 외 Debug 3 개의 결과는 아래 3절 첫 줄 |

검증 도구도 이번에 바뀌었다: `verify-batch --failed-from <결과 폴더> --skip-kind <종류>`(실패 항목과 그 빌드만), 가드 훅이 전경 `verify-batch` · 20 초 넘는 `sleep` 을 막는다(긴 검증은 `run_in_background`, [12 에이전트와 토큰](../12_Agents.md) 2절).

## 2. 남은 일

| 무엇 | 첫 일 한 줄 | 계획 문서 |
|---|---|---|
| 1. 렌더 기준 낡음(`perf-frame` · `load-baseline-suite` · `load-auto-render-scale`) | DoF · 모션 블러가 렌즈 없는 카메라(조리개 0)에서도 전 화면 한 번 돈다(Empty `GPU.Frame` 468 → 1089 us) — 꺼진 카메라에서 건너뛰는 단위를 내고, GPU 메모리 56.8 → 176.9 MB 를 `RenderTargetRegistry` 로 나눈 뒤 기준을 다시 뜬다(숨기기 위한 재기록 금지). 벤치 골든 8 장은 사양 프리셋 `low` 때문에 잘못 갱신했다가 되돌렸다(862ad03f3) — 같은 일이 다른 골든 · 기준에 있는지 본다 | [Candidates-graphics](Candidates-graphics.md) |
| 2. `scenario-thresholds-promote` 인자 | 인자가 낡은 `{bin}/Saved/Automation` 을 가리킨다 — 시나리오 보고는 `build/<preset>/AppRuns/shard<k>of<n>/<케이스>/Saved/Automation` 아래다. 인자를 `AppRuns` 로 고치고 `requires` 를 단다 | [AutomationSplit](AutomationSplit.md) |
| 3. OnlineLoadBot 접속 부하 | 봇이 "online kit 'matchmaking' could not initialize" 로 멈춘다(봇 프로세스에 엔진 서비스 · 타입 레지스트리가 없다) — `Tools/OnlineLoadBot/Main.cpp` 의 `ToolProcessScope` 를 `ServerApp` 같은 호스트 범위 + `Engine`(DedicatedServer)으로 | [ServerDebug](ServerDebug.md) |
| 4. 명령 없는 검증 항목 | `verify-batch --list` 의 "명령이 없어 돌지 않은 항목" 에 실행 명령을 달거나 사람 눈이 꼭 필요한 것은 사유와 분리하고, `plans --check` 가 "명령 없는 측정대기" 를 막게 구조로 닫는다(`wt/verifycmd` 병합 뒤 이어서) | [GpuVerification](GpuVerification.md) |
| 5. 통과한 일회성 항목의 문서 대기 글 | `Test/QA/VerifyBatch.json` 의 `result_doc` 열을 따라 "측정대기 · 남은 확인" 글을 지우고 상태를 올린다. `result_doc` "docs/plans/NEXT.md 0000절"(232 곳)은 없는 절을 가리키므로 실제 고칠 문서로 일괄 정리하고 `verify-batch --check` 로 본다 | 각 `result_doc` |
| 6. 같은 결함 후보(이번 통합에서 드러남) | ① 기계 사양 · 사용자 설정에 따라 결과가 갈리는 다른 무인 경로(자동 렌더 배율 · 품질 조정기) ② 패널을 그리기 전에 이번 프레임 플래그(hovered 등)를 읽는 패턴 ③ 도구 줄이 넘치는 패널(`EditorChrome::sameLineIfFits` 로) ④ "기다릴 지난 상태가 없는 첫 프레임" — 게임 모듈 리로드 직후 · 미리보기 창이 처음 열리는 프레임 ⑤ 실행 파일 옆 `Saved` 를 공유해 앞선 실행 잔재가 판정을 바꾸는 경로(로그 폴더 · 텔레메트리 스풀; 실행마다 로그 폴더 격리가 근본책) ⑥ 컴퓨트 디스패치의 PSO 계약 통일(DX11 · DX12 · Vulkan 은 PSO 없이 디스패치, GL 은 조용히 건너뜀 — 드로우처럼 PSO 없으면 멈추게 할지 정한다) ⑦ 프레임 상태 핸들 수명 | — |
| 7. 데스크톱 전역 상태 단계 | `ExpectCursorClip` 같은 단계가 `input="mixed"` 없이 쓰이면 단계 검증이 거절하게 한다. 클립보드 등 다른 전역 자원을 쓰는 스위트가 더 있는지 훑는다 | — |
| 8. 도구 | `Scripts syntax-check` 가 `--build-dir` 를 줘도 에디터 계층 스텁 경로로 컴파일해 `EditorInputActions.cpp` 가 거짓 실패한다 | — |
| 9. 조립 문장 Warning 에 id | explog 뒤 id 없는 Warning 은 본문 전체 일치로만 선언된다 — 조립 문장(`%#`)인 Warning/Error 를 소스에서 찾아 `SW_LOG_WARNING_ID` 로 올린다 | [GpuVerification](GpuVerification.md) 4절 |
| 10. 에디터 자체 테스트의 `find` | `Source/Editor/SelfTest` 약 20 곳이 여전히 `findSubsystem` — 컨텍스트가 늘 있는 경로만 `getSubsystem` 으로 | — |
| 11. RHI 등록 단언 승격 | `assertRegistryMutableNow` 가 한 번도 걸리지 않았다 — 다른 백엔드 등록 · 해제 경로를 `requireRegistryMutableNow`(모든 구성 abort)로 올릴지 정한다 | [결정 기록](../09_Decisions.md) 5-5 |
| 12. 샤드 비용표 | 시나리오 케이스 예상 비용을 실제 케이스 시간으로 보정한다 — `ctest -L hostgpu` 한 바퀴의 `AppTest_HostOnly_Shard*` 시간을 재 `Test/TestPolicy.json` 에 적고, 샤드 둘이 겹쳐 시간이 실제로 줄었는지(`hostResourceLock` 이 사라진 뒤) 비교한다 | — |
| 13. 선호 백엔드 목록 케이스 | `TestRHIDevice` 8 · `TestRenderPassGPU` 18 곳은 아직 "처음 서는 백엔드 하나" 로 돈다 — 선호 순서가 필요 없으면 `SW_TEST_CASE_PER_PARAMETER` 한 줄씩 | — |
| 14. 자동화 개선 후보 | 어림값 문턱 승격 도구(provisional 38 개) · 비에디터 게임 시나리오 묶음 실행(HostGpuIsolation H2 선행) · 고정 프레임 대기 계측 · 교착 시나리오를 `VerifyBatch.json` 항목으로 | [AutomationSplit](AutomationSplit.md) · [HostGpuIsolation](HostGpuIsolation.md) |
| 15. 린트 후속 | `GenerateLintTargets` 에 `SKIP_RETURN_CODE 77`, `CheckNameReferences` 의 다른 걷기들이 건너뜀을 통과로 내는지 | — |
| 16. 오래된 결함 후보 | 셰이더 쿠커가 옛 퍼뮤테이션 바이너리를 안 지움 · `--cook-shaders` 실패에도 0 · 손 불리언 파서 · 이름 거절 레지스트리 · `ResourceDataKindRegistry` 단일체 · 누수 탐지 2~4 단계 · 야간 Linux Load 실패 원인 · crowd `CrowdSharedUnits` 가설 · RHI 파일 후보 | [LeakDetection](LeakDetection.md) · [ShooterCharacterIntegration](ShooterCharacterIntegration.md) · `Candidates-*.md` |
| 17. CI 원인 재확인 | push 뒤 `ci-jobs --summary` 한 번 — Wayland 잡 · `CheckSourceGlob` 위반 · `MeshAssetTest.MeshComponentResolvesMeshAssetPath` | — |
| 18. 추적되는 컴파일 셰이더 | `Resource/engine/shaders` 의 `.spv` · `.dxil` · `.dxbc` 약 1만 3천 개(팩 770 MB)를 git 에 두는 근거를 `docs/decisions/` 에서 찾고, 없으면 생성물로 뺄지 정한다(되돌리기 어려운 삭제라 사용자 확인 뒤) | [결정 기록](../09_Decisions.md) |
| 19. 사용자 결정 대기 | 말 모델(WildWestHorseMount W3)을 사용자가 받아 `models_raw/` 에 넣으면 CC0 · 라이선스 확인. Steam/Epic SDK · H-3 운영 문서는 보류 | — |
| 20. 통합 에이전트 모델 | `.claude/agents/integrator.md` 의 `model: opus` 고정을 [에이전트와 토큰](../12_Agents.md) 1절 표에 맞춰 확정한다 | [12 에이전트와 토큰](../12_Agents.md) |

`D:/w` 바로 아래 작업자 산출물(`e.py` · `kh_*.log` · `vb.bak` 등)은 지워도 된다(사용자 허락).

## 3. 따로 병합할 브랜치와 확인할 것

Debug 항목 `ctest-lint` · `ctest-race` · `app-hang-watch` 는 606a0c07a 에서 졌고 수정 뒤 Debug 빌드로 다시 돌렸다(결과는 `build/verify-batch` 의 가장 최근 폴더 — 이 문서가 push 된 시점에 확인되지 않았으면 먼저 돌린다).

사용자 지시: 아래는 통합 S 에서 병합하지 않는다. 병합 순서 권고: `cand-quality` → `cand-engine` → `cand-gameplay` → `resid`(호출처 대량 수정이라 마지막). 병합 뒤 통합이 `normal-maps-reimport` · `shared-module-unload-tests` · `log-gate-cleanup-tests` 를 돌린다. 작업자(worker)에게는 `SendMessage` 가 없다.

| 브랜치 | 내용 · 병합 뒤 할 일 |
|---|---|
| `wt/cand-quality`(9 커밋) | `CaseLogJudge` 를 모든 스위트로 넓혔다. 확인: `ctest-nogpu` · `log-gate-cleanup-tests`. 넓힐 후보 5 개: `UIAnimationTest.CurveEventsFireWhenValueCrossesThreshold` · `LiveOpsServiceTest.ParameterResolvesRemoteConfigAndKillSwitchHidesAll` · `SessionStateComponentTest.UnknownSectionIsSkippedAndMissingSectionStaysFresh` · `InputManagerTest.ConcurrentMultiThreadEventPostingStress` · `PrefabTest.EngineCooksXMLAndJSONPrefabs`. `LocalizationGatherTest` 가 엔진 프로젝트를 덮는다(`App --gather-text`), `ServiceStoreSQLiteTest.*` 에 "database is locked" 가 없어야 한다 |
| `wt/cand-engine`(2) · `wt/cand-gameplay`(2) | 겹치는 파일: `Test/QA/VerifyBatch.json` · `Scripts/qa/PerfCompare.py` · `docs/decisions/5-2·5-8` · `Candidates-*.md`(끝 추가라 풀기 쉽다) |
| `wt/resid`(6) | 호출처 대량 수정 — 마지막에 병합 |
| `wt/verifycmd`(4) | 측정대기 계획 문서 63 개를 닫고 매니페스트를 201→151 로 줄인다(문서 삭제 · 결정은 5-2~5-11 로 이전). 병합 뒤: `status-board --write`(지운 문서 링크 약 30 곳), 이 문서의 지운 `Vfx.md` 링크 정리, 셰이더 5 개 주석 정리(다음 쿠킹 때), 새 perf-compare 케이스 · `cook-pack-timing-release` · `write-snapshot` 인자를 첫 실행에서 확인. `need_command` 가 0 이면 `plans --check` 가 거절하게 막는 것이 구조적 마무리 |
| `wt/verifyopt`(1) | 검증 시간 최적화(`shards` · `requires` · `functional` · 야간 `--measure`)를 `Scripts/common/VerifyManifest.py` · `RunVerifyBatch.py` · `RunTests.py` · `VerifyBatch.json` · 문서에 담았다. 빌드 · 실행 안 해 봤다. **충돌**: `VerifyBatch.json` 의 `app-scenarios-release`(이 브랜치는 `shards`, 통합 S 는 `ctest -L hostgpu`) — 통합 S 쪽을 기준으로 정하고, `scenario-timeline-measure` · `scenario-thresholds-promote` 에 `requires` · 인자를 `AppRuns` 로, `shooter3d-keyrebind-restart` 에 `functional: true`, 야간은 `--measure`. 병합 뒤 겹침 실행이 실제로 줄이는지 시간을 잰다 |
| `wt/lockfmt` 끝 4 커밋 | f7ae810d7 · 0fca6afe8 · 015883359 · 119664da2 (동결 뒤 커밋이라 통합 S 에 안 들어갔다) |

**작업 구조 변경(사용자 결정 2026-10-27)**: 앞으로는 병렬 작업자 + 통합 대기를 쓰지 않고, 한 에이전트가 일을 몰아서 단계별로 나눠 진행한다(단계마다 코드 → 린트 → 문법 검사 → 커밋, 빌드 · 검증은 새 일마다 하지 않고 맡은 묶음이 다 끝난 뒤 같은 에이전트가 한 번에 하고 병합 · push 까지 한다). 다음 세션 첫 일: CLAUDE.md 원칙 2 · `WorkerRules.md` · `11_Workflow.md` · `12_Agents.md` 를 이 구조로 고친다(긴 검증은 `run_in_background`, 에이전트가 5 분 안에 답이 없으면 직접 한다는 규칙은 유지).

사용자 결정(2026-10-27): 장르 키트 21 개(232 파일)는 유지. 말 모델은 BlendSwap "Horse Rigged All Gaits"(CC0 표기)를 사용자가 받아 `models_raw/` 에 넣는다. 평문 UDP 정책(`wt/netsec`)은 병합 승인됨 — 이번 통합에 들어갔다.

**사용자 영향(통합 S)**: Shortcuts.json 초기화 · `<bind modifier>` 형식 · 레이아웃 dockspace v7→v8 · 설정 `foliageDensity` / `reduceFlashing` 제거 · Shipping 에서 평문 LAN 파티 불가 · 소문자 열거값과 옛 태그 철자는 읽기 실패 · 모듈 ABI v9(모듈 다시 빌드 필요) · `SW_EDITOR_INPUT_BAN` 다시 구성 필요 · 무적 피격은 효과음 · 큐를 내지 않음 · 화살 머리가 속도 방향으로 정렬 · 월드별 검증 결과 · 법선맵 DDS 는 다시 가져오기 전까지 낡음.
