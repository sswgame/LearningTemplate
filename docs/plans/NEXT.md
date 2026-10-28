# 다음 세션 인계

<!-- next-format: one-page -->

> 질문: 지금 어디까지 왔고, 다음 세션이 처음에 할 일은 무엇인가?
> 독자: 새로 시작하는 세션(에이전트와 사람).
> 결론: 통합 V(작업자 브랜치 14 개)와 뒤이은 설정 소유 변경 · 테스트 수정이 main 에 들어가 push 됐다(ca6208524). 병합된 상태에서 세 구성의 빌드와 일부 검증만 돌렸으므로 첫 일은 CI 의 Linux 잡을 보고 못 돌린 검증을 마치는 것이다(아래 1절). 끝난 통합의 일지는 `git log` 가 정본이라 여기 두지 않는다.

이 문서는 지금 상태 한 장만 둔다. 순서는 [Roadmap](Roadmap.md), 설계는 계획 문서(`py -3 -m Scripts plans`), 결정은 [결정 기록](../09_Decisions.md), 작업 방식은 [11 작업 방식](../11_Workflow.md)이다.

## 1. 지금 상태 — 통합 V (2026-10-28, main 에 push 완료)

1 차(net-load · gfx-baseline · cand-sweep · old-defects · scratch-gate · vtools · keep-assets)와 2 차(wpath · svc-cancel · parse-core · ui-wrap · py-compat · rhi-leftovers · pair-scene)를 병합해 main 에 fast-forward 로 push 했다. CI 수정 커밋 01e3e4e06(Vulkan 확장 개수 · Python 3.12 `read_text` · 없는 sysfs 카드 조사)과 사용자가 지운 `prof.out` 도 들어갔다. push 직전 증거는 세 구성의 빌드 통과와 `full-build` 뿐이고 아래 "못 돌린 검증"은 안 했다.

워크트리는 모두 지웠다(주 작업 트리만 남는다). 병합 안 된 `wt/integ-merge` 의 커밋 12 개를 보존했던 태그는 내용을 확인한 뒤 지웠다 — 5 패치 가운데 `d3338bc16`(모듈이 정의한 설정을 `ConfigManager` 에 등록하지 않고 서비스가 `readConfigFileInto` 로 직접 소유)만 main 에 없어 9d2c3d83f 로 가져왔고 `unloadConfig` 는 지웠다. 나머지는 main 에 같은 수정이 있거나(`releaseConfig` = `unloadConfig`) 낡은 문서다. `ResourceID` 호출처 수정(`f505cecc3`)은 15 개 파일을 하나씩 대조하지 않았고 main 이 세 구성에서 빌드되는 것으로 갈음했다.

그 뒤 Debug `nogpu` 에서 새로 쓰고 돌려 보지 않았던 테스트 둘이 이미 push 된 main 에서 졌다 — `ServiceStoreMemoryTest`(`armFault` 의 `skipCount` 1 이 읽기를 흘려보냄)와 `PairedAnimationReplicationTest`(앞 구간의 클립이 공유 캐시에 남아 뒤 구간 참가자가 이미 준비됨) — ca6208524 가 고쳤다. **작업자가 돌리지 않고 쓴 테스트는 통합에서 처음 돌릴 때 이렇게 가정이 틀린 채 나온다.**

| 단계 | 결과 |
|---|---|
| 1 차 병합 뒤 재검증(`verify-batch --only …`) | Debug `ctest-hostgpu-debug` 통과(3677 s) · Shipping `ctest-hostgpu-shipping` 통과 · Release `app-scenarios-release` 11 중 10 통과 |
| crowd 기록 불일치(해결) | `WaitUntil` 이 시계를 붙잡는 동안 실제 프레임이 흘러 전투가 진행되므로 풀린 뒤의 군중 수가 백엔드마다 달랐다. 3a8dd4e51 · 5d3613207 이 흔들리는 `<Record>` 를 모두 지웠다. `RecordsMatchAcrossBackends` 단독 159 s 통과, crowd 시나리오 네 백엔드 통과 |
| `shooter3d-keyrebind-restart`(Release) | 병합된 상태에서 직접 돌리면 통과(두 시나리오 2.4 s · 1.7 s). 검증 도구 안에서 20 분 시간 초과였던 원인은 재현 못 했다(원인 불명 — 다시 나오면 앱 로그부터) |
| 2 차 병합 뒤(Debug · Release) | 두 구성 빌드 통과. Debug 린트 35/35(`StatusBoard` 재생성) · 파이썬 71/71(`pairedanimnet.scene.xml` 을 엔진 서식으로) 통과. Release 공식 샤드 1 통과(236 s), 샤드 0 · 1 · 5 는 비용표 없이 직접 돌려 통과(57 · 56 · 61 개). Shipping 빌드 통과. ca6208524 기준 Debug `nogpu` 90/90 · `race` 5/5 · 파이썬 71/71 통과, 린트 35/35(`CheckSourceGlob` 은 병렬에서 15 s 제한에 걸렸다가 단독 1 s 통과). **Debug `hostgpu` 재실행, Shipping 테스트, Release 샤드 2 · 3 · 5(공식 분배)와 `AppTest_Desktop` 은 아직 안 했다**. 컴파일 확인이 못 본 것: `PosixProcess.cpp`(Linux 전용) |

**첫 일(순서)**: ① `py -3 -m Scripts ci-jobs --summary` 로 Linux 잡(특히 `PosixProcess.cpp` · 쓰기 게이트 · `NetHost` 경고 · 야간 Load)을 보고 원인별로 묶는다(`ci-triage` 스킬) ② main 기준으로 새 워크트리를 만들어(`worktree-make <이름> --base main`) **못 돌린 검증**을 `run_in_background` 로 돌린다 — Debug `hostgpu`, Shipping 테스트, Release 공식 샤드 2 · 3 · 5 와 `AppTest_Desktop` ③ 아래 확인 목록 ④ 고칠 것이 나오면 작업자 브랜치로 나눈다(남은 일 표와 3절이 병렬로 나눌 묶음).

**돌릴 때 알아 둘 것(이번 세션에서 시간을 크게 썼다)**
- `verify-batch` 는 `--owner <이름>` 이 필수이고 전경에서는 훅이 막는다. `--failed-from` 은 폴더가 아니라 요약 파일을 받는다. 호출 하나는 10 분이 한도라 긴 검증은 `run_in_background`.
- 시험 실행 파일(`TestBin/AppTest.exe`)을 손으로 돌리려면 `Bin` 을 `PATH` 에 넣어야 `Engine.dll` 을 찾는다. `--test_shard=k/5` 를 비용표(`--test_costs`) 없이 돌리면 분배가 달라 한 샤드에 에디터 체인(200~240 s)이 몰린다 — 공식 분배는 `ctest -R AppTest_HostOnly_Shard<n>`.
- 샤드 로그의 `exit 143` 은 바깥 `timeout` 이 앱을 끊은 것이라 진짜 실패가 아니다. `GL_CONTEXT_LOST` 로 시작하는 GL 실행은 일시적인 GPU 사건이었고(단독 재실행 통과) 원인은 못 찾았다.

**병합 뒤 확인 목록**
- 에디터 줄바꿈 변경(`ui-wrap`, `cand-sweep`)이 마크 클릭을 깨지 않는지 — `panels.toolWindowsOpenAtAUsableSize` 와 Console · Sequencer · AnimationGraph · ContentBrowser · MapCheck · Profiler · RenderTarget · UIPreview · Multiplayer · InputMap · Packaging · DataTable · GlobalVariables · GameView · GimmickGraph · MeshModifiers · CharacterEditor 시나리오. 시나리오 11 개가 `CaptureWindow` 대신 `CaptureEditorUI` 로 바뀌었으니 PNG 가 결과 창 · 팝업을 담는지, `toastreveal` · `lightprobebakecancel` 이 `requireForeground` 없이 통과하는지.
- 쓰기 게이트(`FileUtil::isWriteAllowed`)가 켜진 채 원본에 쓰는 시나리오가 없는지(있으면 오류로 드러난다 — scratch 사본으로).
- 컴퓨트 디스패치가 PSO 없이 불리면 `SW_FATAL` 이다 — 컴퓨트를 쓰는 GPU 테스트(Compute* · 컬링 · 모프 · 스킨 · 안개 · VFX)가 네 백엔드에서 도는지. Release 의 `requireRegistryMutableNow` 승격이 가린 위반을 드러내지 않는지.
- 종료 때 서비스 경고가 나는지(`ServerRuntimeOpensTheSQLiteStore…` 의 방어 범위 표시를 지웠다 — 코드 경로만 근거).
- `RHIStagingRingGPUTest.DirectX11TransferLargerThanTheBudgetIsAcceptedAlone`(새 DX11 케이스) · `UploadBudget*` 세 케이스.
- `py -3 -m Scripts paired-net-pair --preset Ninja-Release` 첫 실행 — `ClipLengthMismatch` · 헤드리스 서버의 SkeletalMesh · 파괴 러너가 새로 보는 `[Error]` 판정의 오탐. 클라이언트의 `ParticipantNotReady` 는 보관 뒤 300 프레임까지 재시도한다.
- 지운 일회성 시나리오 7 묶음이 `app-scenarios-release` 에서 실제로 통과하는지(`GpuVerification.md` 6절).
- 골든 16 개(`quality: high`)를 다시 기록했다 — 로그에 `Graphics preset 'low' picked` 가 없는지. perf 기준 `Test/QA/Perf/Empty.json` · Load 기준선 `Test/Load/Baselines.json` 은 `quality` 가 없어 다시 뜰 때까지 거절된다(의도).

## 2. 남은 일

| 무엇 | 첫 일 한 줄 | 계획 문서 |
|---|---|---|
| 1. 렌더 기준 재측정 | DoF · 모션 블러 건너뛰기는 이미 코드에 있다(`kSkipsWithoutPostProcess` 등). 통합 빌드로 `perf-frame` · `load-baseline-suite` · `load-auto-render-scale` 를 `quality` 를 적어 다시 뜨고 Empty `GPU.Frame` 468 → 1089 us 와 GPU 메모리 56.8 → 176.9 MB 가 줄었는지 숫자로 본다(숨기기 위한 재기록 금지) | [Candidates-graphics](Candidates-graphics.md) |
| 2. 누수 탐지 L2~L4 | L2 는 Release 측정, L3 는 실행 테스트, L4 는 L2 결과로 값을 정한다 — L2 부터 | [LeakDetection](LeakDetection.md) |
| 3. 비에디터 게임 시나리오 묶음 실행 | `AppTestUtil::runScenario` 가 시나리오마다 App 을 새로 띄우고 App 은 `-scenario` 하나만 받는다 — 다중 시나리오(씬 · 모듈 상태 초기화 경계) 설계가 첫 단계, C++ 변경 | [AutomationSplit](AutomationSplit.md) |
| 4. 야간 Linux Load 실패 | 4d3249e 야간은 `availableExtCount` 컴파일 오류로 졌고 01e3e4e06 이 고쳤다. 그 전 실행의 connect × Mid `solo:2x1` 거절은 `parseCompactMode` 가 이유를 돌려주게 했으니 다음 야간 문구로 원인을 본다 | `Candidates-*.md` |
| 5. 샤드 비용표 실측 | `py -3 -m Scripts test-costs` 가 준비됐다 — `ctest -L hostgpu` 한 바퀴 뒤 `--apply` 로 `Test/TestPolicy.json` 을 고치고 샤드 겹침이 줄었는지 본다 | [AutomationSplit](AutomationSplit.md) 7절 |
| 6. 선호 백엔드 목록 케이스 | 이미 `SW_TEST_CASE_PER_PARAMETER` 안에 있다. 남은 일반 케이스 7 개는 백엔드끼리 견주거나 고정한 것이라 취지 밖 | — |
| 7. 사용자 몫 | 말 모델(WildWestHorseMount W3): BlendSwap "Horse Rigged All Gaits"(CC0 표기)를 `Resource/game/wildwest/models_raw/` 에 넣으면 `credits.md` · `ModelImportConfig.json` 규칙으로 가져온다. Steam/Epic SDK · H-3 운영 문서는 보류. 컴파일 셰이더 git 보관은 유지로 확정(2026-10-28, `docs/decisions/5-12_FeatureQueue.md`) | [WildWestHorseMount](WildWestHorseMount.md) |
| 8. 통합 에이전트 모델 | `.claude/agents/integrator.md` 의 `model: opus` 고정을 [에이전트와 토큰](../12_Agents.md) 1절 표에 맞춰 확정한다(아직 확인 안 함) | [12 에이전트와 토큰](../12_Agents.md) |
| 9. 작업 구조 문서 | 한 에이전트 단계별 진행(11 머리에 반영)에 `/integrate` · `/worker-brief` 스킬과 11 의 "작업자" 절을 맞춘다 | [11 작업 방식](../11_Workflow.md) |

`D:/w` 에 작업자 워크트리와 산출물이 남아 있다. 통합이 main 에 들어간 뒤 `Scripts/dev` 정리 스크립트로 지운다(워크트리 기준 4 개).

## 3. 남은 후보 (이번 통합에서 드러남, 아직 안 한 것)

- 쓰기 게이트는 `Resource` 루트 아래만 지킨다 — `docs/` · `Config/` 같은 루트 밖 추적 파일을 자동화가 쓰는 길과 `fopen` · `ofstream` 직접 쓰기는 못 막는다. `FileUtil::writeTextFile` 은 빈 글도 쓴다(저장 입구마다 거절).
- `RemoteConfig` 밖 서비스는 `ServiceStoreCompletion` 으로 취소를 가르지만 저장소 스레드의 `run` 안 로그는 린트 밖이다(`ServiceRetentionJob` 한 곳은 손으로).
- 이유가 사라지는 파서 조사를 못 한 곳: `Source/Server` · `Source/App` · `Source/ModuleHost` · `Tools`(`ReflectionParser` 등), 상태 아카이브 읽기 40 여 곳, `(void)load…` 40 여 곳(안쪽 로그가 이유를 남긴다고 주석).
- `ArrowButton` · 체크박스 앞 `SameLine` 은 게이트가 보게 넓혔으나 `InspectorPanel` 의 "Active" · `TileMapPanel` 의 "Erase" 는 `{ }` 가 끼어 게이트도 못 본다. 콤보 · 입력 칸 앞은 폭을 따로 정해 안 봤다.
- `uavBarrier` · `copyBufferToReadback` 선행 상태의 백엔드별 매핑 대조(표는 `transitionBuffer` 만 있다). Vulkan 프레임 밖 펜스 대기는 `RhiAsyncUploads` 계획이 맡는다(M1 측정에서 `RT.Upload.waitIdle` 가 보이면).
- `PairedAnimationSystem::startMatchingSession` · `InteractorComponent::startPairedAnimation` 의 `ParticipantNotReady` 처리. DX11 굶음 방지 두 스레드 GPU 케이스(방법만 `Candidates-graphics.md` 에 있다).
- `runApp` 을 우회하는 `subprocess` 직접 실행은 정규식 근사로 막는다(이름을 바꿔 피하면 리뷰 몫). 시나리오 XML 을 읽는 `qa/ScenarioWaitCost` · `qa/ScenarioThresholds` · `qa/Load` 는 공통 입구를 안 쓴다. `Scripts` 의 Python 이 사용자 폴더를 직접 읽는 곳은 안 훑었다.
- crowd 기록의 뒤쪽(+240 · +300 프레임) 두 기록도 `WaitUntil` 이 붙잡은 길이에 따라 상태가 갈릴 수 있어 같은 종류의 흔들림이 남았다.
- `CheckPythonMinimumVersion` 의 새 기능 표는 표에 있는 이름만 잡는다. 저장소 기준 최소 버전은 3.10(`Constants.py`) 이고 CI 의 리눅스 러너는 3.12 쯤이다.

사용자 결정(2026-10-27 · 28): 장르 키트 21 개(232 파일) 유지, 컴파일 셰이더 git 보관 유지, 평문 UDP 정책 병합 승인.

**사용자 영향(통합 V)**: 자동화 실행 중 `Resource` 원본 쓰기는 오류(`<Scratch>` 선언 또는 `scratch/` 아래만) · `asset.import` 의 임시 폴더가 `scratch/import` · 컴퓨트 디스패치는 PSO 없으면 멈춤 · 배포본이 `SW_USER_DATA_ROOT` 로 사용자 데이터 위치를 돌릴 수 있음 · 무인 골든 · 기준은 `quality` 를 기록하고 다르면 거절 · `Ninja-Release` 가 모든 게임을 지음.
