# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 끝난 일은 여기 적지 않습니다. 한 일은 `git log`, 결정과 근거는 [결정 기록](../09_Decisions.md), 검증 방법과 기준값은
[검증과 측정](../08_Verification.md), 남은 일 전체는 [백로그](../06_Backlog.md)(분류 A~H)에 있습니다. 작업 방식은 [작업 방식](../11_Workflow.md)이고,
병렬 작업자에게 주는 공통 규칙은 [작업자 규칙](WorkerRules.md)입니다.

## 1. 지금 상태 (2026-10-15)

main 은 `origin/main` 보다 184 커밋 앞서 있고 push 하지 않았습니다. 원격 브랜치는 `main` 하나입니다. 이번 세션에 병합한 것은 다음과 같습니다.

- 서브시스템 정리의 마지막 단계: `EngineLoop` 를 `Engine` 으로 개명, 호스트 범위(모듈 이미지 창구, 자동화 구동기), C-27 판정 마감.
- 스레드: `sw::thread` 래퍼와 `CheckStdThread` 린트, 에디터 백그라운드 작업의 결과 · 대기 판정 통합(C-24).
- 내보내기: 키트 DLL 이 `GameFramework.dll` 과 겹쳐 내보내던 심볼 제거(`SW_KIT_API` · `SW_KIT_SERVER_API`), `CheckExportMacros` 린트(C-3).
- 상수: 시간 · 바이트 단위 공용 상수와 치환 106 개 파일, `OnlineLimit.h`, `CheckWellKnownConstants` 확장. 조건 세 부분 이상은 이름을 붙이라는 `CheckConditionParts` 린트(새 줄만 검사).
- 이름: 사용자 설정의 `Applier` 를 `Handler` 로 바꿨고 한국어 "적용기" 를 없앴습니다. 정적 등록 표를 `RegistrationList` 로 모았습니다(`EditorAssetTypeActions` 포함).
- 에디터: 서버 디버그 채널 · 상태 해시 · 권한 고스트(S3 · S6 · S7), 다중 월드 단계 2~5(미리보기 씬마다 렌더러, `EditorPreviewPanel`, 모듈 ABI v6), 지형 쓰기 API(CPU), 캐릭터 편집 창(스켈레톤 · 장비 · 피팅 표시 · 프리셋 파일), 메시 모디파이어 보강, UI 혼합 배율 `MatchWidthOrHeight`.
- 결함: E-11(리로드 중복 등록), GL 체인 포커스 상실 입력 처리, 커서 클립 건너뜀 처리, 에디터 창 5 건(`visual`).
- 도구: 가드 훅 `GuardCommands.py`, graphify MCP(`.mcp.json`), `gpu-verifier` · `code-reviewer` 에이전트.

**남은 워크트리.**

- `terrain2`(지형 쓰기)와 `modgpu`(메시 모디파이어 컴퓨트 미리보기)는 병합했습니다. 지형 남은 일은 백로그 `D2-5` (7)입니다.
- `D:\w\visual`: 병합은 끝났습니다. 에디터 기능 캡처(`captures/`)와 미추적 시나리오(`Resource/engine/automation/vis/`)가 남아 있으니 필요한 것만 챙기고 `py -3 -m Scripts worktree-remove visual --force` 로 지웁니다.
- `D:\w\perfbase` · `perfbase2` 는 성능 기준선이라 남깁니다.

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
2. `git worktree list` 로 남은 워크트리를 확인하고 1 절의 `terrain2` · `modgpu` 상태를 봅니다.
3. 2 절의 검증을 먼저 합니다.
4. 3 절의 큰 일을 사용자와 순서를 확인한 뒤 시작합니다. 시작 전에 graphify 그래프를 갱신합니다.
