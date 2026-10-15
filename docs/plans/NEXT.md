# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 끝난 일은 여기 적지 않습니다. 한 일은 `git log`, 결정과 근거는 [결정 기록](../09_Decisions.md), 검증 방법과 기준값은
[검증과 측정](../08_Verification.md), 남은 일 전체는 [백로그](../06_Backlog.md)(분류 A~H)에 있습니다. 작업 방식은 [작업 방식](../11_Workflow.md)이고,
병렬 작업자에게 주는 공통 규칙은 [작업자 규칙](WorkerRules.md)입니다.

## 1. 지금 상태 (2026-10-16)

main 은 `origin/main` 과 같습니다(2026-10-16 push 했습니다). 원격 브랜치는 `main` 하나입니다. 한 일은 `git log` 가 정본이고, 여기에는 다음 세션이 알아야 할 것만 적습니다.

- 빌드 속도(2026-10-16): SHARED 타깃의 임포트 라이브러리를 내용이 바뀔 때만 갱신(`.cpp` 하나 고친 증분 13.9 → 5 s), PCH 에서 자주 바뀌는 헤더 여섯을 뺌(핵심 헤더 증분 131 → 14 ~ 77 s, 풀 빌드 +15 %).
  다음 후보는 리플렉션 헤더 팬아웃([빌드 속도 계획](BuildSpeed.md) 12 단계). 리눅스 · PCH 끈 Windows CI 는 아직 이 변경으로 돌지 않았으니 push 뒤 첫 결과를 봅니다.
- PCH 에 없는 `Component.h` · `GameObject.h` · `Scene.h` · `SceneManager.h` · `RHITypes.h` 는 쓰는 .cpp 가 직접 include 합니다. 새 .cpp 가 "incomplete type" 으로 서면 include 를 더합니다.
- 문서 읽는 법(2026-10-16): 백로그는 분류표와 내 항목만, `AGENTS.md` 는 절만 읽습니다([CLAUDE.md](../../CLAUDE.md)). 에이전트 지시문에 읽을 문서와 절을 적습니다.

**남은 워크트리.** `D:\w\perfbase` · `perfbase2` 는 성능 기준선이라 남깁니다. 그 밖의 것은 `git worktree list` 로 보고, 병합이 끝난 것은 `py -3 -m Scripts worktree-remove <이름>` 으로 지웁니다.

## 1-1. 세션 사이에 남은 일 (2026-10-16, 새 세션은 여기부터)

**A. 테스트 시간 줄이기 — `D:\w\testspeed`(에이전트 `aab0622613cb4ec2f`, 미커밋).** 이미 하던 것: `ctest` 항목에 `COST` 를 달아 긴 것부터 띄움, `gpuSlots: 2`, `hostResourceLock: AppBin`, 변경 파일로 테스트를 고르는 `Scripts/dev/SelectChangedTests.py`.
사용자 지시로 더한 것(메시지로 전달, 결과 미확인): Debug 전용 라벨 `race`(스레드 경로를 지나는 최소 묶음 × 네 백엔드, 직렬 10 분 안쪽), 나머지 기능 시나리오는 Shipping `hostgpu` 가 기본, shard · `caseCosts` 재균형, `11_Workflow.md` 3절과 `08_Verification.md` 에 단계 규칙.
새 세션이 처음 할 일: `git -C D:\w\testspeed status` 와 `ListAgents` 로 끝났는지 보고, 끝났으면 최신 main 을 받아 Debug · Shipping `all AllTests` 경고 0, `lint-suite`, `nogpu` 로 확인한 뒤 병합한다. 목표(race 10 분)를 실측으로 확인한다.

- **근거(2026-10-16 `finalverify` 측정):** Debug `AppTest_HostOnly` 한 바퀴 = Shard1 1138 s + Shard2 1840 s(제한에 걸림) + Shard3 456 s ≈ 57 분, 같은 검증이 Shipping 에서는 65 s. 최종 검증이 3 시간 걸렸고 약 2 시간은 검증에서 나온 결함 8 건을 고치고 다시 도는 데 썼다.
- **Shipping 으로 다 옮기지 않는 이유:** `DataRaceDetector` 는 `SW_DEBUG` 에서만 켜진다. 레이스 검사는 Debug 가 맡고, 리눅스 `CI-Debug-TSAN` 은 보조다.
- 같은 PC 에서 빌드 · 테스트를 겹쳐 돌리면 시간 제한에 걸려 다시 돌게 된다. 검증을 돌리는 동안 다른 빌드를 하지 않는다.

**B. `wt/finalverify` — 에이전트는 사용자가 멈췄고 커밋 8 개가 main 에 없다.** `8b01e350f`(현지화 `--check-text` 대상을 `stagedCultures` 로), `992ad77fa` · `a24d36587`(배포본 `<Load>` 테스트 · 설정 바탕 층 테스트를 개발 빌드 전용으로),
`b81efafb0`(배포본이 캡처 인자를 버리면 골든 비교를 건너뜀), `614dc8459` · `e0a6934e9`(호스트 로그 게이트에 걸린 `RenderPassGPUTest` · DX12 장치 제거 `RHIDeviceTest` 의 의도한 경고), `f183207a4`(모듈 타입 등록 · 해제 전에 비동기 씬 로드를 기다림, A-5), `e420fa989`(`CheckResourceCasing` 15 초 시한).
워크트리에 커밋하지 않은 `docs/06_Backlog.md` · `docs/plans/NEXT.md` 수정이 있다 — 이 파일의 1 절을 다시 썼으므로 **충돌한다.** 그 수정에서 백로그 갱신(B-3 · B-13 · C-25 · E-11 항목)만 골라 새 main 위에서 다시 적용하고 `NEXT.md` 수정은 버린다.
A 의 새 규칙이 병합된 뒤 Shipping `hostgpu` + Debug `-L race` + Debug `nogpu` 로 검증해서 병합한다(전체 Debug `hostgpu` 는 마지막에 한 번). 병합이 끝나면 `py -3 -m Scripts worktree-remove finalverify`.

**C. 문서 역할 정리(사용자 방향, 미착수).** `NEXT.md` 는 지금 상태 · 다음 할 일 3~5 개(백로그 번호) · 함정 · 시작 절차만 두고 약 2 KB 로 줄인다. 이력 절(4 ~ 5-4)은 백로그 · 결정 기록 · `git log` 와 대조해 없는 것만 옮기고 지운다.
사용자는 남은 일을 영역별 문서로 나눠 중앙 백로그를 없애는 쪽을 보고 있다. 백로그는 120 개 항목 · 116 KB 이고 `D. 기능 로드맵`이 약 50 개로 가장 크다. 이 파일을 직접 읽는 것은 린트 `CheckDocPaths` · `RunDocStyle` 둘이고,
링크하는 파일 약 12 개 · 본문 항목 번호 참조 15 곳이다. 항목 번호를 유지한 채 나누고 `lint-suite` 로 링크를 확인한다. 합의가 필요한 설계 선택이다 — 항목의 새 자리(영역 README 의 `남은 일` 절 / `plans/*.md`)와 전체 목록을 스크립트로 뽑을지를 사용자와 정한다.

**D. 이번 세션에 이미 들어간 것(main).** SHARED 임포트 라이브러리 갱신 · PCH 에서 자주 바뀌는 헤더 제거(빌드 속도 10 · 11 단계), `CLAUDE.md` 읽기 지시(백로그는 분류표 + 내 항목, `AGENTS.md` 는 절만, 에이전트 지시문에 읽을 절을 적는다).
개인 설정: `.claude/settings.local.json` 의 graphify 훅은 `Grep` 에만 남기고 낡은 허용 목록을 지웠다(추적 안 함). 사용자 메모리는 121 개에서 29 개로 줄였고 나머지는 `memory_archive_2026-10-16/` 에 있다.

## 2. 검증 상태와 남은 검증

2026-10-15 `verify` 묶음이 병합된 전체(terrain2 · modgpu · fixnogpu · fixlint · cond · folders · structure 포함)를 검증했습니다. Debug · Shipping `all AllTests` 경고 0 · 오류 0,
`lint` 68/68, Debug `nogpu` 49/49, Shipping `nogpu` 48/48, Debug · Shipping `hostgpu` 7/7(에디터 체인 네 백엔드 · 게임 팩 시나리오 × 네 백엔드 · 스모크)입니다.
Shipping 키트는 STATIC 이고 내보내기 매크로가 빈 것을 산출물로 확인했습니다(키트 TU 246 개에 내보내기 정의 0, 정적 `GF_*.lib` 47 개의 `/EXPORT` 지시문 0).
`rhiswap` 시나리오 셋(`swapbackends` · `terrainedits` · `removedevice`)은 AppScenarioTest 밖이라 손으로 돌렸고 모두 통과했습니다.

남은 검증은 다음과 같습니다.

1. Release 네 백엔드 스크린샷(`-gv_benchMeshes=8 -gv_benchAnimate=0 -gv_screenshotFrame=30`): main 은 포워드 · 디퍼드 모두 네 백엔드가 바이트까지 같습니다. perfbase 와 포워드는 같고, 디퍼드 차이는 `ee6fff232`(반해상도 블룸)의 의도된 변경입니다(결정 기록 5-5). 디퍼드 비교 기준은 그 커밋 이후입니다.
2. Linux 전용 파일은 Windows 에서 컴파일되지 않아 기계적 치환만 하고 짓지 못했습니다(`PosixCrashHandler.cpp` · `EpollStreamTransport.cpp` · `FileUtil.cpp` 의 `st_mtim` 분기 · `HardwareProbe.cpp`). 사용자가 요청할 때만 WSL 로 확인합니다.
3. 열린 결함(A)은 없습니다. 관찰은 E-11(netconditions DX11 1 회) · E-12(uipreview Vulkan 레이블 없음 1 회 — 이제 레이블을 기다린 프레임이 로그에 남는다) · E-13(실제 TDR 뒤 행 — DX12 · Vulkan 흉내로는 행 없음)입니다. Debug DX12 는 디버그 레이어 메시지를 `D3D12 debug layer` 로그로 바로 냅니다.

## 3. 계획한 큰 일 (순서대로)

1. **서브틱 활용 조사.** `PostUpdate` 로 옮긴 컴포넌트 41 개를 전수 분류합니다(5 절).
2. **그 밖.** 지형 후속(식생 · 내비 재계산, 콜라이더 부분 갱신 측정), 다중 월드 잔여(프리팹 창, 창별 선택과 전역 선택 연결, 스카이), 캐릭터 편집 (a) 미리보기 씬 이관과 (c) 슬라이더, 서버 디버그 잔여(클라이언트 해시, 어긋난 오브젝트 짚기), `TaskNode` · PCH · 에디터 기동 시간 측정.

## 4. 구조 리팩토링과 데이터 주도 (2026-10-15 묶음 둘)

`wt/structure` 와 `wt/structure2` 를 main 에 병합했습니다. C-26 은 모두 끝났습니다(마지막 ⑤ `FrameRenderer` 분할은 `wt/framerenderer`). 결정은 [결정 기록](../09_Decisions.md) 4 절(옛 이름) · 5-4 · 5-5 · 5-6 · 5-9 에 있습니다.

- 새로 생긴 규칙: 동사 `add` / `register` / `append` 쓰임표, `Config`(서비스 · 호스트 · 전송 · 저장소의 `initialize` 설정) · `Def`(카탈로그 하위 구조체) 기준(AGENTS.md).
  키트 서버 조립은 옵션 표(`OnlineServerKitEntry::readOptions`) 한 번과 메서드 표(`OnlineMethodTable`)를 씁니다. 확장자 → 에디터 종류의 정본은 `EditorAssetType.cpp` 이고 엔진 포맷은 `kExtension` 을 듭니다.
- `FrameRenderer` 의 이미지 꺼내기는 `getCapture()`(`FrameCapture`), 진단 덮어쓰기는 `setDebugOverrides( FrameDebugOverrides )`(배포본에 없음), 캔버스 렌더 텍스처 대상은 `CanvasRenderer` 입니다.

## 4-1. 게임 로직 자동화 (2026-10-15 묶음 `wt/automation`)

시나리오가 입력 맵 액션(`Action` · `ActionAxis`), 게임 시간(`AdvanceGameTime`), NPC 일정(`Schedule.*`), AI 상태 · 블랙보드(`AI.*`, `ExpectVisited`), 빙의 · 탑승(`Pawn.*`, `Mount`)을 데이터로 다룹니다.
형식과 함정은 `Source/Engine/Automation/README.md`, 결정은 [결정 기록](../09_Decisions.md) 5-6 끝의 다섯 줄입니다. HarvestValley 에 마을 사람(일정 시스템의 첫 사용처)이 생겼습니다.

- 결정적 재생: 시나리오 끝의 `<Record>` 를 `AppScenarioTest` 가 네 백엔드끼리 비교하고, `RecordedPlayReplaysOnEveryBackend` 가 녹화(`-scenario-record`)를 다시 돌려 검사점을 봅니다.
- 남은 것은 백로그 `D5-4`(탑승 단계 실물 확인, 시나리오 없는 게임 네 개, 같은 이름 오브젝트 고르기, 링크 앵커 정리)와 `D5-3`(마을 사람의 길 찾기 · 세이브)입니다.

## 4-2. 에디터 흐름 시나리오 (2026-10-15 묶음 `wt/editorscn`)

에디터 기능을 잇는 흐름 시나리오 다섯(`flowboundaries` · `flowprefab` · `flowterrain` · `flowedit` · `flowselection`)과 경계 전후 견주기(`Remember` · `sameAs`,
`SwapBackend other/start`, 편집 상태 탐침)를 더했습니다. 기능 × 시나리오 표와 못 한 것은 [에디터 시나리오 표](EditorScenarios.md), 결정은 결정 기록 5-11 첫 네 줄입니다.
흐름 시나리오가 드러낸 결함 여섯(백엔드 교체 · 에디터 리로드 뒤 되돌리기 · 선택 소실, 프리팹 적용 미전파, 에디터 문구 표 미적재, 지형 편집의 플레이 소실, 메시 끌어 놓기 무시,
에디터 모듈 리로드가 게임을 내려 저장을 막음)은 고쳤습니다.

- 남은 것은 백로그 `D3-11`(에셋 임포트 · 캐릭터 편집 → 씬 · 스케줄 창은 시나리오로 못 돌린다, 중첩 프리팹 부품 전파).
- 새 흐름 시나리오는 체인(`AppScenarioTest.EditorScenariosPassOn<백엔드>`)에서 돈다. 백엔드 교체를 넣은 둘(`flowboundaries` · `flowterrain`)은 DX11 · DX12 가 있어야 한다.

## 5. 서브틱 활용 조사

서브틱(`registerSubTick`)은 `GameDirectorComponent.cpp` 한 곳에서만 쓰이고, `TickPhase` 의 `Early` · `Late` · `Finalize` 는 정의 밖 사용이 0 입니다. 반면 `setTickGroup` 은 48 개 파일, `PostUpdate` 는 41 개 파일입니다.
주석을 읽은 14 개는 "디렉터(`PrePhysics`)가 이 프레임의 ○○ 를 끝낸 뒤에 읽는다" 였고(HarvestValley · NileCity · StarSkirmish 의 컴포넌트, 카메라 계열), 선행 조건(`getTickHandle()` 로 다른 오브젝트 컴포넌트에도 건다)이 이 의도를 정확히 표현합니다.

- 할 일: `PostUpdate` 41 개를 (렌더 직전이 필요한 것 / 특정 컴포넌트 뒤에 읽는 것 / 이유를 모르는 것) 로 분류합니다. 후보 몇 개를 실제로 바꿔 프레임 시간과 동작을 비교합니다. 이득이 없으면 서브틱 · `TickPhase` 를 삭제 후보로 구조 리팩토링 지도에 올립니다.
- 선행 조건이 있는 서브틱은 별도 스테이지로 가므로 지금 방식보다 느릴 수도 있습니다. 측정하지 않았습니다.

## 5-1. 테스트 틀 개편 (2026-10-15, wt/testfw)

테스트 프레임워크를 책임별 파일(단언 · 등록 · 옵션 · 선택 · 실행 · 보고 · 판정)로 나누고, 라벨 · 샤드 · 제한 시간을 `Test/TestPolicy.json` 한 곳으로 옮겼습니다. 쓰는 법은 `Test/README.md`, 근거는 결정 기록 5-2 끝입니다.

- 다음 단계(부하 검증 · Automation 보강)가 얹을 자리: `SW_TEST_LOAD_SUITE` + `SW_TEST_TIME_BUDGET` + 정책 `loadSeconds` → `<타깃>_Load`(라벨 `load`), 판정기 `ITestCaseJudge`, 보고 `ITestRunListener`. 할 일 목록은 백로그 `C-28`.
- 스위트가 크게 느려지면 정책의 `seconds` · `caseCosts` 를 다시 잽니다(실행 파일 요약의 `[       OK ]` 시간). 제한 시간은 그 값에서 계산되므로 숫자를 직접 올리지 않습니다.
- `-L hostgpu` 는 이번 묶음에서 돌리지 않았습니다. AppTest 의 App 실행에 시한(600 초)이 새로 붙었고 호스트 조각 시간이 정책 계산값으로 바뀌었으니(EngineTest 440 초, AppTest 1840 초) 다음 GPU 검증에서 확인합니다.

## 5-2. 부하 검증 시스템 (2026-10-15, wt/loadtest)

`py -3 -m Scripts load`(시나리오 × 하드웨어 프로필, Release 판정, 예산 · 기준선 · 병목 보고 · 추세)와 엔진 쪽 자리(`<Load>` 시나리오, 친화도를 따르는 워커 수, 디스크 · VRAM · 회선 흉내, 히치 꼬리 줄)가 들어갔습니다.
쓰는 법은 [검증과 측정](../08_Verification.md) "부하 검증" 절, 근거는 결정 기록 5-2 끝, 남은 일은 백로그 `C-29` 입니다.

- 첫 기준선은 이 PC(`SW`, Ryzen 7 6800H)의 `Test/Load/Baselines.json` 에 있습니다. 다른 PC 는 `py -3 -m Scripts load suite baseline --runs 3 --record` 로 자기 기준선을 뜹니다.
- 야간 잡 `.github/workflows/load.yml` 은 아직 한 번도 돌지 않았습니다(push 뒤 첫 실행을 봅니다 — `CI-Release` 프리셋 · 리눅스 봇 빌드 · 러너 속도 대비 시간 예산).
- 부하 스위트 시간 예산은 Release 케이스 최대의 네 배입니다. CI 러너가 그보다 느리면 그 러너 값으로 다시 잽니다.
- 첫 측정의 병목 셋은 `wt/loadopt` 에서 닫았습니다(전후 표는 검증과 측정 "부하 검증" 절). 기동 6.4 s 는 디스크 흉내의 타이머 반올림 착시였고(지금 Mid 1.1 s · LowEnd 3.5 s),
  플레이 중 히치는 StarSkirmish 가 처음 짓는 유닛 모델을 게임 스레드에서 읽던 것(`MeshPreloadSet`), 할당은 워커 수가 아니라 `RTSWorld::findAutoTarget` 의 지역 목록이었습니다.
  남은 기동 열기(개발 배치에만 있는 DLL 섀도 복사 · 매니페스트 · 셰이더 원본 해시)와 렌더 벤치의 13 ~ 20 ms 한 프레임은 백로그 `C-29` ① ② 입니다.
- 진단: `py -3 -m Scripts load run <시나리오> --app-arg=-gv_ioLogReads=1`(파일 열기마다 경로 · 스레드), `--app-arg=-gv_profileAllocSites=12`(Release 로도 콜스택).

## 5-3. 유지보수성을 위한 데이터화 (2026-10-15, wt/datadriven)

기준(사용자 결정)과 정한 모양은 결정 기록 5-4 "유지보수성을 위한 데이터화", 남은 일은 백로그 `C-30` 이다.

- 들어간 것: 액션 룸 내장 몬스터 · 배치 삭제(카탈로그 · `ActionRoomLayout` 필수), `EngineConfig.json` 바탕 층(헤더는 형식만), 회선 프리셋 표(`NetEmulationPresetTable`), 그림자 맵 한 변(스키마 `targetValue`),
  UI 미리보기 해상도 표, 부하 봇 필수 키, `PROPERTY( Required )` + `GameDataXML::readFields`(첫 적용 `ParkLayout`), 게임 튜닝값 PROPERTY(여덟 게임) · 벤치 설정(`benchscene.xml`), 린트 `CheckDataDrivenLiterals`.

## 5-4. 로그 Warning 정리와 로그 게이트 (2026-10-15, wt/logwarn → main `27d62df08`)

- 사용자가 본 "계속 나오는 Warning" 은 에디터 실행마다 남던 `Missing localized text for key 'Ed…'`(약 80 줄, 9 게임 × 4 백엔드 × 에디터 켬 전부, 로케일 무관)였다 — 원인은 editorscn `68525f070`(에디터 문구 표 미적재).
- 이제 시나리오는 선언하지 않은 Warning · Error · 빠진 현지화 키 하나로 진다(`AutomationLogPolicy`, 허용 목록 `Resource/engine/automation/allowedlogs.json` 은 비어 있다). 새 경고를 넣는 작업자는 시나리오가 깨질 수 있음을 안다.
- `--check-text`(CI 에서는 `LocalizationGatherTest.RepositoryProjectsAreUpToDate`)는 번역 테이블이 있는 문화권의 빠진 · 낡은 번역과 수집 경고도 실패로 본다 — 원문을 바꾸면 ko 번역도 같은 커밋에서.
- 병합 전 확인: Debug 경고 0, lint 71/71, nogpu 44/44(병합 전 트리), hostgpu 는 게임 팩 시나리오 × 4 백엔드 · 에디터 시나리오 DX12 만(사용자 지시로 전체는 통합 검증에서). 남은 일은 백로그 `C-33` · `A-5`.
- 배포본 테스트용 변수: `SW_TEST_GLOBAL_VARIABLE_SHIPPED` 는 `CheckGlobalVariableKinds.toml` `[shipped]` 목록(이유 포함)에 있는 것만 둔다. 벤치 · 크래시 · 측정 스위치는 내렸다(Release 는 등록된다).
- **에디터 모듈의 리플렉션 헤더는 `Source/Editor/CMakeLists.txt` 의 `REFLECTION_HEADERS` 목록이다** — 새 REFLECT 헤더를 넣지 않으면 링크 오류(`StaticType` 미정의)로 드러난다(EditorTest 는 에디터 소스를 따로 컴파일해 그 코드젠을 받지 못하므로, 둘이 같이 쓰는 데이터 타입은 Engine 에 둔다 — `UIResolutionPresetTable`).

## 6. 확정된 방침

- 상용 엔진과 비교해 고릅니다. 근거는 결정 기록에 "~로 알고 있다" 로 적어 기억 기반임을 밝힙니다.
- 구조는 규모가 커도 장기 이득이면 과감히 바꿉니다. 래퍼 · 별칭 · 옛 이름 호환은 두지 않고 데이터도 다시 씁니다.
- ponytail 과 graphify 를 항상 켭니다(Headroom 은 삭제). 구조 개편을 시킬 때는 요청에 그 뜻을 분명히 적습니다. ponytail 의 "기존 구조 유지" 와 부딪히기 때문입니다.
- graphify 그래프는 작업 묶음을 띄우기 직전에 갱신합니다(`graphify update Source`, 약 4분, 증분 아님). 산출물을 저장소 루트 `graphify-out/` 으로 옮깁니다.
- 에이전트는 push 하지 않습니다. 에이전트를 띄울 때 종류와 모델을 답변에 적습니다.
- 검증 단계: Debug `all AllTests` 경고 0, `ctest -L "lint|nogpu"`, Shipping `all AllTests` 경고 0, Shipping `nogpu`, 해당하면 앱 실행과 모듈 리로드.
- 동시 빌드는 최대 둘입니다. 워크트리는 `py -3 -m Scripts worktree-make <이름> --base main` 으로 만들고, 병합이 맞는지 확인한 뒤 `py -3 -m Scripts worktree-remove <이름>` 으로 지웁니다.

## 7. 도구와 환경

- 병렬 작업자 규칙은 [WorkerRules.md](WorkerRules.md) 에 있습니다. 병합할 때 문서 충돌은 양쪽을 남기는 방식으로 풀었고, 생성 문서(`docs/Config/*`)는 `GenerateConfigReference.py` 로 다시 만듭니다.
- 가드 훅 `.claude/hooks/GuardCommands.py` 가 `git commit --no-verify` 와 링크가 든 폴더의 재귀 삭제를 막습니다. 새 에이전트부터 확실히 적용됩니다.
- `.mcp.json` 이 graphify MCP 서버를 띄웁니다(`graphifyy[mcp]` 필요, `graphify-out/` 이 있는 main 체크아웃에서). 승인 대기 상태일 수 있습니다. context7 은 사용자 전역 설정에 등록했고 Node 는 설치했습니다(새 터미널에서 PATH 에 잡힙니다).
- graphify 와 ponytail 스킬은 `.claude/skills/` 에 있고 추적하지 않습니다. 새 워크트리에는 복사해야 합니다.
- 이 PC 의 사용자 Scripts 폴더(`C:\Users\ssw\AppData\Roaming\Python\Python314\Scripts`)는 PATH 에 없습니다. `graphify` 를 부르려면 더합니다.
- WSL VM(Ubuntu-26.04)이 `wsl --shutdown` 후에도 1 초 안에 다시 뜹니다. VS Code 의 WSL 연결이나 다른 도구가 깨우는 것으로 보이며, 원인을 특정하지 못했습니다.

## 8. 알려진 함정

- **빌드 로그를 bash `grep` 으로 세지 않습니다.** PowerShell `Start-Process` 의 `*>` 리다이렉션은 로그를 UTF-16 으로 써서 `grep` 이 항상 0 을 돌려줍니다. 이 세션에서 그 방식으로 "경고 0" 이라 잘못 보고한 일이 있습니다. 로그를 `Out-File -Encoding utf8` 로 쓰고 `Select-String | Measure-Object` 로 셉니다.
- **`worktree-remove` 는 브랜치도 지웁니다.** 병합이 실패했는데 먼저 지우면 브랜치를 잃습니다(커밋 객체는 남으니 `git branch <이름> <해시>` 로 복구). 병합 뒤 `git merge-base --is-ancestor` 로 확인하고 지웁니다.
- **워크트리 지우기**: 링크(정션)를 링크로서만 먼저 끊습니다. 변수가 든 `rmdir` 는 안전장치가 막으니 `worktree-remove` 를 씁니다.
- **앱 창을 전경으로 올려 캡처하는 스크립트를 쓰지 않습니다.** `SetForegroundWindow` 와 `CopyFromScreen` 방식은 사용자 화면에 창이 튀어나오고 작업표시줄 미리보기가 섞입니다. 메모장을 띄워 포커스를 흉내 내는 일도 같습니다. 게임 뷰는 `-gv_screenshot`, 에디터 UI 는 `-gv_screenshotEditor` · `<CaptureEditorUI>` 로 찍습니다([에디터 UX 기반](EditorUX.md)).
- Edit 도구와 `sed -i` 가 CRLF 파일을 LF 로 바꾼 적이 있습니다. 수정 뒤 `git diff --stat` 로 파일 전체가 바뀌지 않았는지 봅니다.
- 정렬 할당 객체를 기반 포인터로 지우면 배포본 힙이 깨집니다(Debug 는 조용). 엔진 정리를 정적 소멸에 맡기지 않습니다. 모듈 안 정적 객체의 소멸자에서 스레드를 join 하지 않습니다.
- 무효 bindless 인덱스(`0xFFFFFFFF`)는 DX12 페이지 폴트, Vulkan DEVICE_LOST 입니다. 셰이더를 바꾼 뒤에는 `App.exe --cook-shaders` 로 다시 굽습니다.
- `castTo<Component>( Component*, 타입 )` 은 항상 성공합니다. 종류 거르기는 `getTypeInfo()->isDerivedFrom( 타입 )` 입니다.
- 커밋 훅은 우회하지 않습니다. 스크립트로 파일을 고칠 때는 heredoc 대신 Write 도구로 scratchpad 파일을 만듭니다.
- 비결정적 실패는 한 번의 재현으로 확정하지 않고 3~5 회 다시 돌려 재현율을 적습니다.

## 9. 새 세션 시작 절차

1. 이 문서, [작업 방식](../11_Workflow.md), [작업자 규칙](WorkerRules.md), [백로그](../06_Backlog.md) 순서로 읽습니다. C++ 을 쓰기 전에 [AGENTS.md](../../AGENTS.md).
2. `git worktree list` 로 남은 워크트리를 확인하고 병합이 끝난 것을 지웁니다.
3. 2 절의 검증을 먼저 합니다.
4. 3 절의 큰 일을 사용자와 순서를 확인한 뒤 시작합니다. 시작 전에 graphify 그래프를 갱신합니다.
