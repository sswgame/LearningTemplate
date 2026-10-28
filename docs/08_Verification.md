# 검증과 측정

> 질문: 무엇을 돌려야 일을 끝낸 것이고, 측정값은 어떻게 읽는가?
> 독자: 변경을 검증하거나 성능을 재는 사람.
> 결론: 손대기 전에 1절의 검증 규칙을 따르고, 성능 주장은 2절의 방법으로 Release 에서 잰 값으로만 한다. 3절은 검증할 때의 함정, 4절은 CI 실패 읽는 법이다.

한 모듈에만 해당하는 함정은 그 모듈 코드의 헤더 주석에, 테스트를 쓰는 규칙은 [Test README](../Test/README.md) 에 있습니다. 언제 어디까지 돌리는지는 [작업 방식](11_Workflow.md) 3절입니다.

## 1. 손대기 전에 — 검증 규칙

숫자 기준선(테스트 수 · 창 수 · 시간)은 적어 두지 않는다. **비교가 필요하면 바꾸기 전과 후를 그 자리에서 측정한다.**
통과 기준은 "전부 통과, 스킵은 Dev 전용 케이스 · 그 플랫폼에 없는 타깃뿐" 이다. 시나리오(`-scenario`)는 종료 코드 0 만으로 통과가 아니다 — 아래 "시나리오 통과 기준" 이다.

개발 기본 빌드(`cmake --build --preset Ninja-Debug`)는 테스트 실행 파일을 짓지 않는다. 테스트까지 지으려면 `--target all AllTests` 를 붙인다.
`ctest` 는 테스트 앞에 셋업 항목 `BuildTestBinaries` 를 스스로 돌려 둘을 먼저 짓는다(빌드 없이 돌리려면 `-FS SWTestBinaries`). `py -3 -m Scripts test` 도 먼저 짓는다(`--no-build` 로 건너뛴다).

```powershell
cmake --build --preset Ninja-Debug            # 경고 0 이어야 한다(테스트까지는 --target all AllTests)
cmake --build --preset Ninja-Shipping         # Debug 가 숨기는 결함이 여기서 드러난다(테스트까지)
ctest --preset Ninja-Debug-lint               # 컨벤션 · include 순서 · 린트 자기 테스트
# 커밋 훅 · full-build · Scripts test 의 출력은 요약이다(실패만 상세, 전부는 --verbose)
py -3 Scripts/lint/gate/CheckDataDrivenStructure.py --summary  # 데이터 주도 구조 위반 영역별 표(기준선 0 으로 수렴)
# -j 는 값 없이 준다(CMake 3.29+). 직렬은 빌드 셋업 · Load · QA 셋, GPU 항목은 GPU 슬롯(gpuSlots)만큼. AppTest 의 hostgpu 조각 · Race · QaGoldenImages 는 잠금 없이 GPU 슬롯만큼 겹친다 — App 은 케이스마다의 작업 폴더 build/<프리셋>/AppRuns/ 에서 뜬다(docs/decisions/5-2_Build.md).
ctest --test-dir build/Ninja-Debug -L nogpu -j --output-on-failure
ctest --test-dir build/Ninja-Shipping -L nogpu -j --output-on-failure

# CI 가 못 도는 부분(CI 러너는 nogpu 만) — GPU 있는 PC 에서 일을 끝내기 전에 돌린다. 단계별 내용은 11_Workflow.md 3절 표.
ctest --test-dir build/Ninja-Shipping -L hostgpu -j --output-on-failure   # 기능 시나리오 — Shipping 이 기본(AppSmokeTest 가 네 백엔드 + 에디터를 띄워 종료 코드 0 · [Error] 0건을 본다)
ctest --test-dir build/Ninja-Debug -L race -j --output-on-failure         # 레이스 묶음(Debug 전용 — DataRaceDetector 는 SW_DEBUG 에서만)
ctest --test-dir build/Ninja-Debug -L hostgpu -j --output-on-failure      # Debug 전체 — 큰 단계 끝에 한 번

cmake --preset Ninja-Debug-ASAN; cmake --build --preset Ninja-Debug-ASAN; ctest --test-dir build/Ninja-Debug-ASAN -L nogpu -j   # ASan (Windows)
py -3 Scripts/lint/report/RunClangTidy.py [--filter Core]   # 정적 분석 — 게이트가 아니라 판단 자료

# 눈으로 봐야 할 때 — 종료 코드 0, 로그에 [Error] 0건
cd build/Ninja-Debug/Bin; ./App.exe -gv_profileFrames=40 -dx12 -EnableEditor      # -dx11 / -vk / -gl 도 같게
```

**시나리오 통과 기준.** 종료 코드 0 이고 처리하지 않은 `[Warning]` · `[Error]` 줄이 없을 때만 통과다. 남은 줄이 있으면 실행기가 15(`UnhandledLog`)로 끝나며 미확정(14)보다 앞선다.
줄이 처리됐다는 것은 ① 그 시나리오의 `<ExpectLog id="…">`(id 있는 줄) 또는 `<ExpectLog contains="…">`(id 없는 줄, 본문 전체와 같아야 하고 `count="0"` 은 선언이 아니다)가 선언한 줄이거나 ② 로그 id(`SW_LOG_WARNING_ID` · `SW_LOG_ERROR_ID`)가 붙은 줄이 `Resource/engine/automation/allowedlogs.json` 의 항목(id · 시나리오 하나 · 사유 · 처리 계획 문서)에 있다는 뜻이다. **id 없는 줄은 목록으로 가릴 수 없다** — 소스에 id 를 붙이거나 고친다. 목록의 칸이 비었거나 `TODO` 이면 모든 시나리오가 읽기 오류(11)로 선다. 호스트 테스트 스위트(`hostgpu`)는 같은 목록의 `suite` 항목을 쓴다.
보고에는 허용 목록에 없는 Warning · Error 가 하나라도 있으면 "통과" 가 아니라 "실패(UnhandledLog)" 라고 쓴다. 이미 진 실행(10 · 12 …)의 결과 숫자는 바뀌지 않는다 — 15 는 통과했을 때만 된다.
돌린 뒤에는 `py -3 -m Scripts scenario-logs --check` 로 목록에 있으나 더 나오지 않는 항목(지운다)과 15 로 끝난 보고를 센다(`--regen` 은 초안 생성). 절차와 이유는 [GPU 검증](plans/GpuVerification.md) 4절이다.
**시한과 종료 코드 12.** 프레임 시한(`timeoutFrames`)은 시계를 붙잡은 프레임을 세지 않으므로 실행기가 시나리오 한 편의 벽시계 상한(`wallTimeoutSeconds`, 기본 150 초)을 따로 보고, 넘으면 12(`TimedOut`)로 끝난다. 프로세스 시한(180 초)이 먼저 죽이면 보고가 없으므로 이 상한이 더 짧아야 한다(`AutomationTimeLimits.h` 의 `static_assert`). 부하 시나리오(`<Load>`)는 이 상한을 보지 않는다. 멈춘 행은 감시 스레드가 상한 + 유예 5 초 뒤에 `"hang": true` 보고를 쓰고 12 로 끝낸다.
**보고 필드.** `name` · `result` · `exitCode` · `frames` · `reason` · `failures[]` · `unconfirmed[]` · `unhandledLogs[]` · `allowedLogs[]` · `metrics[]` · `records[]` 와 실행 환경 칸(`backend` · `game` · `startedAt` · `wallMilliseconds` · `lastStep` · `reproduce` 등). 필드는 더하기만 하고 `records[]` 는 끝에 둔다. `reproduce`(같은 실행을 다시 띄우는 명령줄)는 token · secret · password 류 인자의 값을 `REDACTED` 로 가린다.
**린트 게이트 결과**는 `통과 | 건너뜀(사유) | 불성립` 이고, 통합(`lint-suite` · CTest)에서 불성립(종료 5)은 실패, `RunLintSuite --strict` 는 건너뜀도 실패다(`Scripts/lint/GateVerdict.py`).

**단위마다 이 순서로 한다.** 테스트 먼저(이전 코드에서 지는 것을 확인) → 고침 → 변이 검사(고침을 되돌리면 테스트가 지는가) → Debug(`nogpu` · `race`) · Shipping(`hostgpu`) → 커밋. 변이로 죽지 않는 테스트는 아무것도 지키지 않는다.

**에디터 패널에는 단위 테스트가 없다.** 컴파일이 통과해도 화면이 비어 있을 수 있으니 패널 · 위젯을 건드렸으면 실행해서 본다.
화면을 볼 수 없으면 `-gv_editorPanelDump=N` — N 번째 ImGui 프레임에 창 하나당 한 줄(이름 · 크기 · 정점 수 · 상태)을 남긴다. **보이는데 정점이 0 인 패널**이 빈 패널이다(창 수는 패널이 늘면 바뀌니 "내용 없는 패널 0개" 를 신호로 본다).
도구 패널까지 보려면 `-gv_editorOpenPanel=all` 을 같이 준다(저장된 도킹을 쓰지 않고 그 실행의 레이아웃은 저장하지 않는다). 고친 뒤 같은 명령으로 로그를 떠서 비교한다.

**기본 실기동은 테스트 씬을 연다.** Empty 팩 `data/gamesettings.xml` 의 `startMap` 이 `game/empty/maps/editortest.scene.xml` 이다. 그 씬의 메시는 `_meshID` 가 비어 **화면에 기하가 없다** — 픽셀 비교에는 벤치 큐브(`-gv_benchMeshes=N`)를 쓴다.
다른 씬은 `"-gv_editorStartupScene=<경로>"` 로 연다(**PowerShell 은 점이 든 인자를 쪼갠다 — 따옴표로 감쌀 것**). 씬 · 프리팹 에셋은 **엔진 직렬화기로 만든다**(손으로 쓴 XML 은 깨진다).

**렌더 결과는 픽셀로 측정한다.** `-gv_screenshot=out.ppm`(`-gv_screenshotFrame=N`)은 톤맵까지 든 최종 화면이다. 눈으로 보지 말고 배경과 다른 픽셀 수 · 채널 평균을 센다. `-gv_viewMode=<이름>`(`RenderViewMode` 의 열거자 이름, 숫자는 오류)은 에디터 없이도 고른다.
**벤치는 `-gv_benchAnimate=0` 이어야 결정적이다**(네 백엔드 0 바이트 차이). 애니메이션을 켠 채 차이를 주장하려면 같은 조건 두 판으로 잡음 바닥을 먼저 측정한다.
**사진만 찍고 끝낼 때는 `-gv_screenshotQuit=1`.** `-gv_screenshot` · `-gv_screenshotEditor` 는 사진을 쓰고도 앱을 끝내지 않는다(안 쓰면 시한 124 로 죽는다). 요청한 사진을 모두 쓴 프레임에 코드 0 으로 끝나고, 파일을 쓰지 못했으면 14 다. `-gv_profileFrames` 와 같이 쓰는 측정은 사진 뒤 일찍 끝나 프로파일이 잘리므로 기본은 끄기다.

**함정**

- 백엔드는 `-dx11 / -dx12 / -vk / -gl` 로 고른다. 명시한 백엔드를 쓸 수 없으면 폴백하지 않고 에러로 선다(`-gv_rhiBackend=Vulkan` 도 같은 판정). 실제로 뜬 백엔드는 로그 `Initializing RHI with backend:` 로 확인한다.
- 에디터는 `-EnableEditor` 를 줘야 뜬다. 없이 돌린 검증은 에디터 OFF 검증이다.
- 테스트 바이너리는 `build/<프리셋>/TestBin/`, 작업 디렉터리는 `Bin/` 이다. 모듈 DLL 은 `Bin/Modules/`, Dev PDB 는 `Bin/Symbols/`. 옛 자리 산출물은 configure 가 지운다(실행 중이라 못 지운 것은 경고).
- 셰이더 소스(.hlsl/.hlsli)를 고쳤으면 다시 쿠킹한다 — 빌드는 HLSL 을 다시 쿠킹하지 않으므로 `App.exe --cook-shaders` 뒤에 재고 비교한다. `.hlsli` 를 고치면 그 헤더를 (전이로라도) 연 셰이더만 다시 구워진다.
  Shipping 쿠킹은 `cook.stamp` 의 내용 해시로 검증하고 어긋나면 빌드를 세운다(`App --cook-all --verify-shaders`). 쿠킹은 병렬이어도 산출물이 직렬과 바이트까지 같고, 끝줄의 `compile` · `reflection` 시간은 작업별 합이라 `total` 보다 길 수 있다.
- GPU 확인은 이미 통과가 증명된 항목을 다시 돌리지 않는다. 통과 증거(`verify-batch` 결과 · 증거 파일 · `NEXT.md` 의 통과 기록)가 있고 그 증거 커밋 이후 항목이 쓰는 코드 · 셰이더 · 에셋 · 시나리오가 안 바뀌었음(`git diff <증거커밋>..HEAD -- <관련 경로>` 가 빔)을 둘 다 보였을 때만 건너뛴다. 모호하거나 고쳐진 항목이거나 GPU 에서 처음 도는 항목이면 돌린다. 보고에는 건너뜀 표(항목 · 증거 커밋 · 바뀐 경로 없음)를 따로 둔다.
- 비결정적 실패는 한 번 재현으로 "확정" 이라 부르지 않는다. 3~5 회 다시 돌려 재현율을 보고, 출력 전체를 파일로 남긴다.
- 직접 실행으로 통과해도 CTest 에서 질 수 있다(병렬 부하 · 시한). 끝내기 전 검증은 CTest 로 한다. 멈추면 `LastTest.log` 의 마지막 `[ RUN ]` 을 본다.
- 테스트 실행 파일은 `AllTests` 를 지어야 최신이다. 옛 exe 는 모듈 ABI 불일치로 진다.

## 2. 측정 · 프로파일

- **성능은 Release 로 측정한다.** Debug 는 레이스 검출기 · 이터레이터 프록시로 컨테이너 코드를 과장한다(668 vs 87 us). 이전 · 이후 바이너리를 같은 스크립트로 **번갈아** 2~3 회 측정한다(`git stash -u` → 빌드 → 복사 → `stash pop` → 빌드). 아침 기준선과 오후 결과를 비교하면 기계 상태가 결과로 읽힌다.
- **에디터 모듈의 계측은 `SW_EDITOR_PROFILE_SCOPE`**(`Editor/Common/EditorProfile.h`) — 엔진 `SW_PROFILE_SCOPE` 는 엔진 창구를 불러 에디터 모듈에서 쓸 수 없다. 패널별 시간은 `-gv_editorPanelTimes=N`, 하위 구간은 `GT.Editor.updateUI` 아래 `frameSteps` → `GT.Editor.step.<id>`.
- **AppSmokeTest 의 Unknown 태그 상한(8 KB)은 파일 수에도 걸린다** — 태그 없는 호출자가 공유 캐시(`ResourcePaths` 경로 캐시)의 재해시를 일으키면 그 버킷 배열이 Unknown 으로 센다. 공유 캐시는 넣는 자리에서 자기 태그를 건다.
- **측정 명령**(Release · VSync 꺼짐, 측정하는 동안 다른 빌드를 막는다):
  - 묶어서 한 번에 — `py -3 -m Scripts verify-batch --owner <이름> --measure [--config Release] [--only <id>…]`(`--list` · `--check` · `--dry-run` · `--jobs N` 기본 2 · `--fail-fast`). `--measure` 가 없으면 측정 항목은 돌지 않는다(기본 실행은 정합성 검증뿐, 통과 · 실패를 판정하는 `functional` 측정만 기본에 든다) — 측정만 재려면 `--measure-only`(그 구성의 빌드 · 쿠킹만 앞에 붙는다). `Test/QA/VerifyBatch.json` 의 항목(종류 · 구성 · GPU 잠금 · 한계 · 결과를 옮길 문서)을 구성별로 이어 돌려 `build/verify-batch/<시각>/result.md` 에 표 하나(항목 · 값 · 한계 · 통과/실패 · 측정 환경 · 날짜)를 남긴다. 새 측정은 그 파일에 항목을 더한다(사람이 해야 하는 확인은 `manual`, 사람이 필요 없는데 명령이 아직 없으면 `need_command` — 일회성 대기는 명령이 있거나 `manual` 이어야 한다).
    빌드 · 쿠킹 · 골든 · 측정 · `ctest` · GPU 독점 항목은 언제나 혼자 돌고 잠금 없는 `test` 항목과 GPU 공유 항목은 겹친다(이유는 [결정 기록](09_Decisions.md) 검증 묶음 행 · 5-2 의 검증 묶음 구조 줄). 시나리오 전부(`app-scenarios-release`)는 `ctest -L hostgpu` 의 샤드로 돌고, `test` 항목의 `shards` 는 조각끼리 GPU 칸 수만큼 겹쳐 돈다. 겹친 항목이 CPU 경합으로 시간 제한에 걸린 것 같으면 `--jobs 1 --only <id>` 로 다시 돈다.
  - 전 · 후 비교 — `py -3 -m Scripts perf-compare --app <후>/App.exe [--app-before <전>/App.exe] [--cases <이름…>] [--runs N]`(케이스마다 A · B 를 번갈아 재 중앙값 표. `--app-before` 가 없으면 같은 바이너리의 `-gv_` 스위치 A/B).
  - 이 기계의 기준과 견주기 — `py -3 -m Scripts perf`(프레임 p50 · p99).
  - 쓰기 위치 — `py -3 -m Scripts write-snapshot --app <App> [--label <이름>] -- <App 인자>`(App 한 번의 전후로 쓰기 후보 폴더의 파일 목록을 견줘 `Test/AppTest/HostWriteLocations.json` 에 없는 쓰기가 나오면 진다).
  - 씬 로드 — `py -3 -m Scripts scene-load-bench --app <Release>/App.exe [--count 8000] [--runs 5] [--asset-report]`.
  - 프로파일러가 없으면 `Ninja-Release`(PDB 있음) + `py -3 -m Scripts stacks <pid>`(DbgHelp 샘플러). ICF 로 함수가 남의 이름으로 보인다.
- **표 읽기.** 열은 avg · p50 · p99 · min · max · per_frame 이고 카운터 값은 per_frame 열에 있다(시간 열 0 을 "죽은 경로" 로 읽지 말 것). 평균이 히치를 가린다 — p50 · p99 · 최악 프레임을 본다. 백분위는 옥타브 × 8 칸 히스토그램의 아래 끝(±9 %)이다. 구간 표는 스레드마다 **일한 시간**이고, 프레임이 빨라졌는지는 `[Profile] wall N frames … us/frame` 와 `startup N ms` 로 본다. 중첩 합이 바깥보다 크면 표부터 의심한다.
- **캐시 적중률은 측정 창 끝의 `[CacheStats]` 줄로 본다**(`-gv_profileFrames`). 캐시마다 한 줄 — 이름 · `items` · `hit` · `miss` · `rate` · `clear`(통째로 비운 횟수)이고 hit / miss / clear 는 워밍업 뒤 측정 창 시작부터의 값이다. `no counters (<사유>)` 는 조회가 없어 세지 않는 캐시다. 캐시를 늘리거나 상한을 정할 때 이 숫자를 근거로 쓴다(규칙: [결정 기록](09_Decisions.md) "캐시는 `CacheStats` 레지스트리에…").
- **`RT.Frame` = `RT.BeginFrame`(펜스 대기 = GPU 백프레셔) + `RT.ExecutePacket` + `RT.Present`.** `GT.Packet.submit` 이 크면 GT 가 RT 를 기다린다. `GT.Frame` 은 `Engine::update` 만 재고 게임 모듈은 `GT.Game.update` · `GT.Game.fixedUpdate` · `GT.Editor.updateUI` 다.
- **대기 지점 스코프**(`<스레드>.<영역>.<동작>`; 이 값이 크면 그 대기를 비동기로 바꾸는 근거): `GT.Material.waitIdle`(새 머티리얼 첫 사용 때 GT 가 RT + GPU 를 기다림) · `RT.Upload.waitIdle`(Vulkan 업로드 · 리드백의 `vkQueueWaitIdle`) · `RT.Upload.flush`(DX12 갱신 복사 제출) · `RT.PSO.create`(PSO 동기 생성, 셰이더 컴파일 포함) · `GT.Load.sync`(동기 에셋 로드) · `GT.Settings.save`(사용자 설정 쓰기).
  시동은 로그 한 줄 `Startup phases: hostScope · engine · window · modules · finalize+show · total | subsystems: 이름=ms …`(서브시스템은 1 ms 이상만)로 본다. `Test/Load/Baselines.json` 의 `startup_ms` 와는 별개다.
- **병목은 씬 크기에 따라 뒤집힌다.** 큐브 2000 은 GPU 대기, 8000 은 게임 스레드다. 어느 쪽을 깎을지는 재고 나서 정한다. GT 가 병목이면 RT 구간이 늘어 보여도 경합일 뿐이다.
- **타임라인은 Tracy 로 본다**(`-gv_tracy=1` + 같은 판 0.14.1 뷰어). 표는 구간마다 접은 숫자라 "어느 스레드가 무엇을 기다렸나" 는 Tracy 스레드 타임라인으로 본다. `SW_PROFILE_SCOPE` 하나가 둘 다에 남긴다. DX11 · Vulkan 은 GPU 시계를 컨텍스트를 열 때 한 번만 맞춰 긴 실행에서 GPU 줄이 조금씩 밀린다.
- **GPU 비용은 `GPU.<패스>` 타임스탬프로 나눈다.** 패스를 지워서 나누면 타깃 사슬이 바뀌어 답이 뒤집힌다(추정 38 us, 실측 123 us). `RT.BeginFrame` 은 GPU 시간의 대리값이 아니다.
- **재기 전에 VSync 가 꺼졌는지 본다** — 1/RT.Frame 이 주사율과 같으면 VSync 다. DXGI 는 스왑체인 생성과 `ResizeBuffers` **둘 다**에 `ALLOW_TEARING` + `Present( 0, DXGI_PRESENT_ALLOW_TEARING )`(짝이 안 맞으면 `INVALID_CALL`). Vulkan 은 present 모드, CLI 는 `-vsync`.
- **셰이더를 고쳤으면 재기 전에 `App.exe --cook-shaders`.**
- **벤치 스위치**(`Source/Games/Empty/BenchScene.cpp`): `-gv_benchMeshes=N` · `-gv_benchLights=N` · `-gv_benchGround=1` · `-gv_benchMovePercent=%` · `-gv_benchInstanced=1` · `-gv_benchTickMovers=N`(틱 **안** 세터 — 실제 게임플레이 경로) · `-gv_benchSpawnChurn=N` · `-gv_benchMeshVariants` · `-gv_benchMeshShapes=N` · `-gv_benchMaterialChurn*` · `-gv_benchAnimate=0`(컴퓨트 회전과 `update` 사인파를 **둘 다** 멈춘다), `-gv_deferred=1`, `-gv_useRenderThread=0`.
  틱 경로는 `-gv_tickBatch=0`(타입별 일괄 틱을 끈다 — 같은 바이너리 A/B)과 표의 `GT.Scene.tick.objectRange` · `pendingSlotApply` · 카운터 `objectEntries` · `batchElements` · `pendingSlots` · `queuedWrites` 로 가른다. "무버 8000" 은 `-gv_benchMeshes=8000 -gv_benchTickMovers=1`.
- **`-gv_profileFrames=N` 은 프레임 수다** — VSync 가 꺼진 가벼운 장면은 1500 fps 라 90000 프레임이 1 분에 끝난다. 시간으로 재려면 `-gv_profileSeconds=S`(먼저 닿는 쪽이 끝낸다). 프레임당 힙 할당은 보고의 `alloc/frame`, 콜스택은 `-gv_profileAllocSites=N`(Debug App — 횟수는 최적화와 무관, 시간은 같이 재지 말 것).
- **씬 로드 측정**: `Scripts/dev/MakeStressScene.py` 로 큰 씬 → `-gv_firstScene=<씬>` → `[SceneLoad]` 줄. Dev 는 `Cooked/` 를 마운트하지 않으므로 쿠킹 효과는 `--cooked-dir=<repo>/Resource` 로 쿠킹하고 재고 지운다. 쿠킹본을 읽었는지는 `[SceneDocument] Loaded … from binary` 줄이나 하니스 표의 source 줄(`binary …scene.bin`)로 본다(`[SceneLoad]` 줄은 늘 `.scene.xml` 을 찍는다).
- **벤치가 상태를 공유하면 단계 순서를 측정한다.** 손대지 않은 대조군이 움직이면 하니스를 의심한다. 벤치 메시가 공유라 배치 결함을 가린 적이 있다 — 씬에서 온 메시로도 본다.
- **GPU 업로드 비용은 호출당이다**(DX12 ~3.3 us) — 쪼개면 느려진다. **워커가 쓴 데이터를 다른 코어가 읽으면** 캐시 이동이 항목당 일(~40 ns)보다 비싸다. **워커는 공유 카운터 · 비트필드에 쓰지 않는다**(`fetch_add` 한 줄이 병렬 플러시를 직렬보다 느리게 했다).
- **측정 라운드마다 매니저 · 풀 · 레지스트리를 새로 만든다.** best-of-N 반복은 자라는 컨테이너가 만든 오염을 거르지 못한다. 같은 값이 연달아 세 번 나올 때까지 수치를 믿지 않는다.
- **한 번만 일어나는 이벤트(백엔드 교체 프레임의 최대값 등)는 3 회 이상 되풀이해 이상치를 거른다.** 프레임 1 에만 일어나는 일(업로드 · PSO 생성)은 워밍업(`FrameProfileSession::kWarmupFrames`) 뒤의 측정 창 밖이다. 창 안에서 전량 재생성을 일으키려면 시나리오 단계 `<SwapBackend backend="other"/>` 를 워밍업보다 뒤에 둔다. 히치 최적화는 최악 프레임으로 판정한다.
- **성능 워크로드는 움직여야 한다.** 정지한 씬은 "바뀐 것 없음" 빠른 경로만 잰다. 워크로드가 지나지 않은 경로는 "검증됨" 이 아니라 "실행된 적 없음" 으로 보고, 최적화 대상은 프로파일 표에서 고르고 전후 숫자를 커밋 메시지에 남긴다. 새 프로파일 구간을 넣을 때는 상위 구간도 함께 넣는다(하위 합과 전체의 차가 대개 진짜 병목).
- **렌더 성능은 디스크에서 읽은 씬으로 배치 수를 먼저 센다.** 인스턴스 수와 배치 수가 비슷하면 인스턴싱이 꺼진 것이다.
- **측정 표에는 측정한 PC 이름과 CPU 를 적는다.** 다른 빌드가 도는 동안 잰 값은 오염되므로 측정하는 동안은 다른 빌드를 막는다(아래 "빌드 속도" 의 슬롯 잠금).

### 영역별 측정값 (계획 문서 · 후보의 기준선)

**결과는 `verify-batch` 보고(`build/verify-batch/<시각>/result.md`)와 `git log` 가 정본이다.** 이 절에는 다시 잴 명령과 대표값만 두고 날짜 · 전후 표는 쌓지 않는다. 다시 잴 때는 같은 명령과 구성으로 잰다.

- **런타임 UI 벤치**(Release · DX12 · 600 프레임 × 세 번): `-gv_benchUiWidgets=10000` · `-gv_benchUiChurn=M`(프레임마다 글 M 칸) · `-gv_benchUiMarkers=K`. 글 10 칸/프레임에서 `GT.UI.Paint` p50 약 0.3 ms, `GT.UI.Layout` 0.06 ms, `RT.Canvas.Upload` 0.04 ms. 구간 `GT.UI.*` · `RT.Canvas.*`, 카운터 `UI.LayoutWidgets` · `UI.PaintWidgets` · `UI.CanvasQuads`.
- **디버그 HUD**: 꺼진 `GT.DebugHUD` 는 1 us 미만에 할당 0, 켜진 `GT.DebugHUD.Sections` 는 17~36 us(4 Hz).
- **텍스처 임포트 압축**(`App --import-textures`): 띠 병렬 압축으로 BC7 512² 밉 10 단 약 62 s → 8 s, 결과 DDS 는 바이트가 같다. 재는 법: 그 원본의 스탬프 줄만 지우고 실행 — 로그 `Compressed WxH (… bands on N workers): T ms`.
  일괄 임포트(`AssetImportStampUtil::importAll`)는 원본 파일도 워커와 나눠 임포트하고(폴더마다 사슬을 원자 카운터로 가져가는 동적 분배, 띠 압축은 그 안에서 중첩), 요약과 `import.stamp` 는 파일 순서대로 합쳐 직렬과 같다(`AssetImportParallelTest.ParallelImportMatchesSerialImport`). 병렬 전후 전체 시간은 아직 재지 않았다(스탬프를 비우고 `--import-textures` · `--import-models`).
  에디터 핫 리로드는 **백그라운드 잡에서 바뀐 원본만** 임포트한다(결정 기록 5-11). 시나리오가 `Resource/` 원본을 쓴 뒤 결과를 읽으려면 탐침 `Editor.AssetImportBusy`(0 까지) · `Editor.AssetImportFinishedCount`(시작 값보다 커질 때까지)를 `WaitUntil` 로 기다린다.
- **틱 디스패치 벤치**(`GameObjectBenchTest`, Release, `Bin` 에서 `--load_suites=only`): 작은 그룹은 직렬이 병렬보다 빨라 손익분기가 약 1000 개(항목당 ~25 ns)이고 `TaskManager::runParallelByCost` 가 앞 항목을 직접 돌아 재고 고른다.
- **타입별 컴포넌트 질의 벤치**(`TypedComponentBenchTest`, Release, `Bin` 에서 `../TestBin/EngineTest.exe --test_filter=TypedComponentBenchTest.* --load_suites=only --host_suites=all`): 오브젝트 1만 · 10만 × 정확한 타입 수 50 · 200 · 500 에서 버킷 순회 · `ComponentRegistry::getAll` · 오브젝트 훑기를 견준다. 버킷 순회는 옛 훑기보다 말단에서 70 ~ 2000 배, 기반에서 4 ~ 15 배 빠르고, 검사 없는 등록형 목록보다는 말단이 2.0 배 느리다(K 50). 오브젝트 생성 + 삭제 p50 433 ns, `resolveComponent<T>` 타입 확인 약 4 ns. K 가 수백인 씬은 조회 타입별 버킷 목록 캐시가 먼저 필요하며 측정으로 필요가 확인될 때만 넣는다.
- **증분 쿠킹 단계별 시간**(Release App, Empty, 단위 초): 깨끗한 실행 = `Saved`(기록 · DDC), 쿠킹 · 팩 폴더, 셰이더 `bin` 을 지움, 바뀐 것 없는 실행 = 바로 이어 같은 명령. 명령은 `App --cook-shaders` · `App --cook-scenes --cooked-dir=<폴더>` · `-cook-all`. 셰이더 6180 개 약 252 → 2.3(컴파일 캐시만 켜면 약 5.4), 씬 · 프리팹 등 1.4 → 0.9, 팩 13 개 약 12 → 1.7.
  두 실행의 산출물 SHA-256 은 전부 같다(줄바꿈은 LF 로 맞춰 견줌). 팩은 항목별 압축 · CRC 를 병렬로 돌리고(결정 기록 5-2) 요약 로그 `Pack cook: … compress W ms wall / C ms cpu, … hash …` 로 잰다. 병렬 뒤 바이트 동일은 `PackLayoutTest.ParallelCompressionGivesTheSerialBytes` 가 지킨다.
- **지형**: LOD 교체(`TerrainBenchTest.LODSweepWorstFrame`)는 최악 4~5 ms(기준 GT 2 ms), 높이 쓰기(Debug)는 한 바디 1416 ms vs 청크마다 바디 17 ms(`ColliderRebuildAfterBrushWrite`)이고 쓰기 프레임은 청크 메시를 넷까지만 다시 만든다.
- **애니메이션 갱신 — 본 수 × 캐릭터 수**(`SyntheticRigBenchTest.EvaluateByBonesCharactersAndMode`, Release, 5 회 중앙값): `EditorTest.exe --load_suites=only --test_filter=SyntheticRigBenchTest.*` 를 `Bin` 에서. `AnimationSystem::evaluate` 한 프레임(워커 병렬) — x1000 own 은 biped 20 본 551 us, biped 150 본 3223 us, multileg 300 본 8066 us(본당 약 130 ns, 선형), crowd(군중 포즈 공유)는 본 · 캐릭터 수와 거의 무관하게 약 200~260 us. GPU 스키닝은 재지 않았다.
- **파괴 첫 프레임**(`FractureBenchTest.ShowcaseBeginPlay`): 잎 셰이프를 틱마다 약 0.57 ms 예산으로 나눠 지어 p50 19.4 → 7.7 ms 로 줄였고 대신 벽이 깨지는 프레임이 2.4 → 3.2 ms 로 늘었다(맞바꾼 값으로 유지).
- **UDP 소켓 계층**(`NetUDPBenchTest`, 호스트 스위트, 한 PC, Release): 클라이언트 2000 에서 받기 42k · 보내기 42k pkt/s, 서버 스레드 54 %, 패킷당 약 6.5 us, 커널 버림 0. RTT 는 대부분 같은 PC 의 클라이언트 드라이버 지연이다. 온라인 부하 봇(Debug `--local-server`)의 값은 참고용이다.
- **`DynamicBitset` · 기믹 회로 상태 벤치**: 사본을 돌려주는 비트셋 연산은 작은 크기에서 힙 할당(약 45 ns)이 전부이고, 가장 큰 기믹 회로는 244 바이트(한 조각), `saveState` 60 ns 이하 · 할당 0 이다(Release 만 의미 있음).
- **Vulkan VSync 프레임 놓침**: 창 모드 Vulkan 은 가끔 한 프레임이 12 ~ 24 ms 였고 DXGI 는 한 주기 이하였다(이미지가 한 주기 늦게 돌아오거나 합성기 경로에서 펜스가 13 ms 뒤에 신호됨). 평소 `vkAcquireNextImageKHR` 대기 약 5.2 ms.

### 빌드 속도

- **빌드 시간 기준선은 `Scripts/dev/RunBuildBaseline.py`** — 풀 빌드(별도 폴더) · 헤더 하나 수정 · `.cpp` 하나 수정 · 워크트리 콜드를 각 3 회 재 중앙값 표를 낸다. 헤더별 누적 파싱 시간 상위는 `Scripts/lint/report/RunIncludeCost.py --preset Ninja-Debug --jobs 8`(PCH 없이 — 다른 빌드와 겹치면 순위만 믿는다)이다.
  표는 같은 PC 의 전후만 견준다. 측정하는 동안은 빌드 슬롯을 독점으로 잡는다(`py -3 -m Scripts shared-lock acquire build --owner <이름>`). `cmake --preset` · `--clean-first` 는 공유 vcpkg 설치 폴더를 다시 깔 수 있으니 `shared-lock acquire vcpkg` 도 쥔다. 남은 단계는 [빌드 속도 계획](plans/BuildSpeed.md), 최근 기준선은 [ArchitectureSeams](plans/ArchitectureSeams.md) 2 절.
- **대표값**(Ryzen 7 6800H 16 스레드, `Ninja-Debug` `all`): 풀 빌드 약 131 s, Core 기본 헤더 하나 수정 약 132 s(사실상 전 TU), `.cpp` 하나 약 7 s. 헤더 파싱이 컴파일 시간의 절반을 넘는다(`-ftime-trace` 의 `Total Source`).
- **PCH 없이 헤더 비용 재는 법**: `compile_commands.json` 의 명령에서 `/Yu` · `/Fp` · PCH 강제 포함 · `/Fo` 를 정규식으로 빼고 `-fsyntax-only -include Source/Engine/pch.h` 를 붙여 5 회 돈다(문자열을 다시 쪼개면 `-DSW_LOG_TAG=\"…\"` 따옴표가 깨진다). `-ftime-trace` 는 구문 검사만으로는 파일을 쓰지 않으니 `/Fo<임시>.obj` 로 실제 컴파일하고 `/clang:-ftime-trace` 는 `-c --` 앞에 넣는다.
- **함정**: 킷 PCH 공유(`sw_useSharedPch`)는 `REUSE_PRECOMPILE_HEADERS_FROM` 이 무시되면 킷이 PCH 없이 지어져 잘못 빠르게 읽힌다(같은 폴더에서 `--clean-first` 교차 3 쌍으로 잰다). 에이전트 빌드가 겹친 측정은 편차가 ±40 s 라 쓸 수 없고(`busy_before` 로 겹침을 센다) `.ninja_log` 는 `--clean-first` 뒤에도 이전 항목이 남아 CPU 합을 그대로 더하면 틀린다.
  임포트 라이브러리는 바뀔 때만 갱신한다(`Engine.lib` 이 바이트 같은데 시각만 바뀌면 DLL 84 개가 다시 링크된다). PCH 에서 자주 바뀌는 헤더(`Component.h` · `GameObject.h` · `Scene.h` · `RHITypes.h` 등)는 뺀다 — 그 헤더를 건드린 증분이 131 s → 14~77 s 가 된다.

## 2-1. 부하 검증

부하 시나리오 × 하드웨어 프로필을 Release 로 여러 번 돌려 p50 · p99 · max 를 **예산**(제품 목표, `Test/Load/Budgets.json`)과 **기준선**(이 기계의 회귀, `Test/Load/Baselines.json` — 현재 값의 정본)에 견주고, 진 조합은 어디(CPU 게임 스레드 · 렌더 스레드 · GPU · 메모리 · IO · 서버)가 예산을 깼는지 적습니다. 결정과 근거는 [결정 기록](09_Decisions.md) 5-2 끝입니다.

```powershell
py -3 -m Scripts load list                                       # 시나리오 · 프로필 · 묶음(Test/Load/Suites.json)
py -3 -m Scripts load run renderbench --profile Mid LowEnd --runs 3
py -3 -m Scripts load suite baseline --runs 3 --record           # 이 기계의 기준선을 새로 뜬다
py -3 -m Scripts load scale renderbench --profile LowEnd         # 예산을 지키는 가장 높은 품질 단계
py -3 -m Scripts load trend renderbench --metric GT.Frame.p99_us # <빌드>/LoadResults/trend.jsonl
ctest --test-dir build/Ninja-Release -L load                     # 부하 스위트(벤치 · 클러스터) — 시간 예산은 Release 에서만 판정
```

- **시나리오.** 자동화 시나리오에 `<Load clients rampUpSeconds durationSeconds network meshes movers/>` 한 줄을 더한 것이다(`Resource/**/automation/load/` — `AppScenarioTest` 가 줍지 않는 폴더). 엔진이 시작 프레임에 측정 창을 열고 창이 닫히면 보고와 함께 끝난다(`<Pass/>` 를 두지 않는다). 서버 쪽은 `Test/Load/Scenarios/*.load.xml` 의 `<LoadScenario driver="loadbot|cluster">` 가 같은 `<Load>` 를 쓴다 — `loadbot` 은 OnlineLoadBot(`--local-server`), `cluster` 는 `NetSimLoadTest`(루프백 클러스터, 끝에 모든 클라이언트의 상태 해시가 서버와 같은지).
- **하드웨어 프로필**(`Test/Load/Profiles.json`): CPU 코어 · 메모리는 Job Object, 엔진 워커 수는 친화도를 따르고(로그 `Initialized with N worker threads`), VRAM · 디스크 · 회선 · 해상도 · 품질은 스위치다. 소프트웨어 제한이라 결과에 `emulated`(표의 `*`)가 붙고 기준선 키가 실제 기기(`Native`)와 갈린다. 클럭 · 캐시 · GPU 성능은 흉내 내지 않는다(코어가 적은 프로필의 프레임이 오히려 짧을 수 있다).
- **지표.** 프로파일 표(`GT.Frame` · `RT.Frame` · `GPU.Frame` · `RT.BeginFrame` · `GT.Packet.submit` …의 p50 · p99 · max), 히치 꼬리(`[Profile] hitch` — 최악 프레임 다섯과 16 · 33 · 100 ms 를 넘은 수), `alloc.per_frame`, `gpu_memory_mb`, `startup_ms`, `private_mb.max` · `handles.max` 와 소크 기울기, 봇의 동작별 p99 · `error_percent`, 클러스터의 `tick_p99_us` · `converged`.
- **판정.** 여러 판(기본 3)의 가운데 값으로만 견주고 최소..최대를 같이 적는다. 기준선은 p50 15 % · p99 35 % + 바닥(150 us · 32 MB)을 넘으면 회귀이고, max · 히치 수는 예산으로만 본다. Release 가 아니면 숫자만 적고 판정하지 않는다(`--allow-debug`). 판마다의 출력은 `<빌드>/LoadResults/logs/`, 결과는 `<빌드>/LoadResults/<시각>.json`.
- **병목 갈래**는 하위 구간 비율로 가른다 — `RT.BeginFrame` 이 `RT.Frame` 의 절반을 넘으면 GPU(펜스 대기), `GT.Packet.submit` 이 `GT.Frame` 의 절반을 넘으면 게임 스레드가 렌더 스레드를 기다린 것, 아니면 가장 무거운 `GT.*` 구간.
- **측정하기 전에** `py -3 -m Scripts shared-lock acquire gpu --owner <이름>`(독점)을 쥐고 다른 빌드 · GPU 실행이 없는지 본다. VSync 는 기본으로 꺼져 있다(`display.vsync`).
- **디스크 흉내는 장치 하나 · 큐 깊이 1 이다**(고해상도 마감 대기 — `sleep_for` 는 Windows 타이머 주기 15.6 ms 로 반올림돼 쓸 수 없다). 기동은 대략 `Native 기동 + 열기 수 × 지연` 이고 열기 수는 결과의 `startup_file_opens` 다. 어디서 읽는지는 `--app-arg=-gv_ioLogReads=1` 로 돌려 판 로그의 `[IO] open <경로> bytes=… thread=…` 를 본다(`--app-arg` 는 `-gv_profileAllocSites=12` 같은 진단 스위치도 넘기며, 진단을 켠 판은 `--record` 하지 않는다).
- **aiskirmish 의 `alloc.per_frame` 은 측정 창의 프레임 수를 따라간다**(고정 델타라 프레임이 많을수록 유닛이 늘어난다) — 할당 자리를 견줄 때는 `wall.frames` 도 본다. aiskirmish 는 StarSkirmish 가 든 빌드가 필요하다(`cmake --preset Ninja-Release -DSW_GAMES=StarSkirmish`, 없으면 건너뜀 77). OnlineLoadBot 은 Windows 에서 키트 DLL 사본과 함께 `ToolBin` 에 있고 작업 폴더는 `Bin` 이다.
- **CI 야간 잡**(`.github/workflows/load.yml`)이 `ctest -L load` 와 묶음 `nightly`(봇 · 클러스터)를 돌리고 진 조합을 주석으로 올린다(`CiFailureReport.py load`). 러너는 측정 기계가 아니라 기준선은 알림뿐이고, 묶음은 `--report-time-budgets` 로 돌아 시간 예산(`_us` · `_ms` · `hitch.*`)을 넘어도 지지 않는다 — 넘은 줄은 결과 JSON 의 `time_budget_overruns` 에 남고 주석은 `::notice` 로 올라온다. 정확성(수렴 · 오류율) · 메모리 예산과 실행 실패는 러너에서도 판정한다. 시간 회귀는 이 PC 의 기준선이 본다. "the load run did not finish" 로 지면 주석의 `exit` 와 오류 줄로 봇이 왜 끝났는지 읽는다. 부하 스위트의 시간 예산(`SW_TEST_TIME_BUDGET`)은 Release 세 번 중 최대 케이스 시간의 네 배다.

## 3. 검증할 때의 함정

테스트를 쓸 때의 규칙은 [Test README](../Test/README.md) "단언과 도우미" · "함정과 주의" 절이 원본이다. 이 절은 변경을 확인하고 결함을 좁힐 때의 함정만 다룬다.

- **`-gv_fixedFrameDelta=<초>` 는 프레임마다 그 시간만 흘린다**(벽시계 무시) — 시나리오 · 픽셀 비교 · 벤치 재현이 기계와 상관없이 같은 게임 시간을 본다. 종료 코드는 `Engine::requestQuit( code )` 가 정한다(0 성공, 시나리오 10 실패 · 11 읽기 오류 · 12 시간 초과 · 13 건너뜀).
- **시나리오는 `App.exe` 로 직접 조립하지 말고 `py -3 -m Scripts scenario <이름 조각 또는 경로> [--backend dx12|dx11|vulkan|gl|all] [--config Release|Shipping|Debug] [--app <App.exe>] [--screenshot-dir <폴더>]` 로 돌린다.** `<Launch>` 의 기동 변수(`-gv_firstScene` 등)를 그대로 붙이고(손으로 조립하다 빠뜨리면 읽기 오류 11), 백엔드마다 GPU 칸을 잡아 한 판씩 돌려 종료 코드를 뜻으로 풀고 `failures[]` · `unconfirmed[]` · `unhandledLogs[]` 를 표로 보인다. 하나라도 통과가 아니면 종료 코드 1, 이름이 모호하거나 선언을 인자로 풀 수 없으면(`<Load>` · 배포 빌드의 개발 변수) 2. `--dry-run` 은 조립한 명령만 찍고 `--app-arg=<인자>` 는 변형 인자를 맨 뒤에 더한다(조립이 정하는 인자와 겹치면 오류).
- **검증 중 본 화면이 어느 실행인지는 창 제목과 `timeline.log` 로 읽는다.** 제목은 `<게임 제목> — <시나리오> · <백엔드> · step <번호> <단계> · f<프레임>`(벤치는 `bench <인자> · <백엔드> · f<프레임>`)이고 로그의 `[Scenario] <이름> step … frame <F>` 가 같은 정보를 남긴다. 검증 폴더의 `timeline.log`(`HH:MM:SS 시작|끝|앱시작|앱끝 <항목 id> …`; 단독 실행은 `build/timeline/<시각>.log`, 시작 칸이 `—(안 돎)` 이면 건너뜀 · 막힘)에서 시각에 맞춰 본다.
- **`AppScenarioTest` 는 시나리오 × 백엔드마다 독립 케이스 `AppScenarioTest.<시나리오>.<백엔드>`(`dx11` · `dx12` · `vk` · `gl`, 배포본 `default`)다.** 폴더를 훑어 만들므로 파일을 두면 케이스가 생긴다(`SW_TEST_CASE_GENERATOR`). 한 시나리오는 `--test_filter=AppScenarioTest.vfxgpulights.*`, 한 백엔드는 `--test_filter=AppScenarioTest.*.gl`. 같은 이름의 시나리오가 둘이면 폴더까지 넣은 이름(`game_empty_automation_foo`)을 쓴다. 빌드가 지은 다른 게임 팩의 시나리오는 `AppScenarioTest.<게임>.<시나리오>.<백엔드>`(Shipping 에는 없다). 백엔드끼리 기록 비교는 `AppScenarioTest.RecordsMatchAcrossBackends`, 백엔드 루프가 있던 스모크 · 골든 · GPU 디바이스 케이스도 같은 매크로(`SW_TEST_CASE_PER_PARAMETER`)로 `<케이스>.<백엔드>` 로 갈렸다. 백엔드끼리 결과를 견주는 케이스(`…MatchesAcrossBackends` 등)는 한 케이스다. 에디터 시나리오(`EditorScenariosPassOn<백엔드>`)는 백엔드당 체인 하나이고 실패 메시지의 `chain sub-results:` 가 시나리오마다 결과를 나열한다.
- **실기동 확인은 시나리오로 남긴다** — `Resource/<영역>/automation/*.scenario.xml` 을 두면 `ctest -L hostgpu`(`AppScenarioTest`)가 백엔드마다 돈다. 손 확인 목록을 후보 절에 적지 말고 시나리오를 쓴다(못 쓰는 것만 이유와 함께 손 확인으로). 값은 `Expect probe`(게임 .cpp 의 `SW_AUTOMATION_PROBE` 한 줄), 그림은 `Screenshot` + `ExpectImage`로 본다.
  "다르다 · 같다" 는 기준 캡처를 같은 시나리오에서 찍어 `differingFraction`(화소 비율)으로 쓴다 — `differentFrom`(평균 차)은 `near` 만 받는다. 문서를 여는 시나리오는 `WaitUntil` 이 기본 문서와 **구별되는** 값을 기다려야 한다(같은 값이면 파일이 뒤늦게 열려 클릭이 어긋난다). 키 `Tap` 은 사람의 누름처럼 여러 프레임(`hold="6"`)을 눌러야 한다(한 프레임짜리는 UI 층이 켜지기 전에 떼어져 결함을 가린다).
- **게임 로직은 기반 탐침으로 단언하고 결정성은 백엔드끼리 비교한다** — 입력은 `Action` · `ActionAxis`, NPC 일정은 `AdvanceGameTime` + `Schedule.*`, AI 는 `AI.State` + `ExpectVisited`, "N 초 안에" 는 `WaitUntil timeoutSeconds`. 시나리오 끝에 `<Record probe="Scene.TransformHash"/>` 를 두면 네 백엔드의 `records` 를 비교한다. 녹화(`-scenario-record`)의 재생 일치는 `AppScenarioTest.RecordedPlayReplaysOnEveryBackend`. 형식은 `Source/Engine/Automation/README.md`.
  일부러 내는 로그 줄은 그 시나리오에 `<ExpectLog id="…">`(id 없는 줄만 `contains="…"`; `CheckExpectLogPhrases` 가 소스와 맞는지 본다), 고칠 수 없는 줄만 `allowedlogs.json` 에 이유와 함께 둔다. 걸린 줄은 실패 메시지의 `unexpected log:` 와 `Saved/Automation/<이름>_<백엔드>.log` 에 있다.
- **에디터 시나리오 백엔드 사례는 한 체크아웃에서 하나씩 돌린다** — `EditorScenariosPassOn<백엔드>` 는 리소스 트리(`assetmanage/probe.material` · `scratch/import/`)를 고치므로 같은 워크트리에서 두 백엔드를 동시에 돌리면 서로의 파일을 지워 assetmanage · chainreloadgame · flowterrain · flowimport 가 함께 진다. GPU 공유 칸의 병렬은 워크트리가 다를 때만 쓰고, 진 뒤에는 `git status` 로 리소스 트리를 되돌린다.
- **결과를 로그로 늦게 내는 백그라운드 작업은 시나리오 시계를 잡는다.** 저장 검증(`EditorAssetValidation`)처럼 파일을 늦게 읽는 작업은 시계가 앞서 가면 뒤 단계(임시 폴더 지우기)가 바뀐 트리를 읽혀 그 결과가 다음 시나리오 창에 떨어져 로그 게이트로 진다. `SW_AUTOMATION_CLOCK_HOLDER` 로 등록하면 참인 동안 실행기가 시계를 멈추고 다음 시나리오를 시작하지 않는다(`[Scenario] <이름> held the clock for N frame(s)`). 답을 내는 백그라운드 색인도 같다(콘텐츠 브라우저의 참조 역색인은 `ContentBrowserReferenceIndex` 가 시계를 잡고, 탐침 `Editor.ReferenceIndexReady` · `Editor.ReferenceCount` 와 이름 바꾸기는 따라온 색인만 쓴다).
- **ThreadSanitizer 는 이 리눅스 환경의 clang 18 에 런타임이 없다** — clang 으로 `-fsanitize=thread` 컴파일하고 링크만 gcc 의 `/usr/lib/x86_64-linux-gnu/libtsan.so.2` 를 직접 붙이면 돈다(`setarch -R` 으로 ASLR 을 끈다). 잠금 · 스레드를 바꾼 뒤 `NetworkThreadTest` 만 이렇게 돌린다.
- **에디터 확장의 순서는 등록 순서가 아니라 `SW_EDITOR_*` 의 order 키다.** 순서가 바뀐 것은 `AppSmokeTest.EditorRegistriesKeepTheirOrder`(hostgpu)만 잡으므로 패널 · 메뉴를 고치면 hostgpu 를 돌린다.
- **워크트리와 vcpkg 스탬프:** 스탬프 해시는 트립릿 파일(`cmake/Modules/Toolchain/Vcpkg/*-{windows,linux,osx}.cmake`)의 **바이트**로 계산한다. 작업 사본의 줄끝이 체크아웃과 다르면 main 과 워크트리가 서로의 스탬프를 "매니페스트 변경" 으로 보고 configure 마다 vcpkg install 이 돈다. 워크트리를 만들기 전에 `git diff --quiet` 로 같은지 보고, 다르면 그 파일을 지우고 `git checkout` 으로 다시 받는다.
- **배포 구성(Shipping · Shipping-Server) 빌드 폴더의 `Bin/Packs` 는 활성 게임(empty) 팩만 굽는다** — 오래 쓴 폴더에 남은 다른 게임의 낡은 팩으로 통과하던 테스트가 새 워크트리에서 진다. 그런 케이스는 맨 앞에 `SW_TEST_REQUIRES_FEATURE( SourceResources )` 를 `SW_TEST_SKIP` 보다 **먼저** 둔다(`CheckTestSuites` 규칙 10 이 막는다).
- **통합 검증에서 에디터를 한 번은 실제로 띄운다** — Release `-EnableEditor` 시나리오 하나 × 네 백엔드와 Debug 시나리오 하나(dx12). Shipping `hostgpu` 에는 에디터가 없고 Debug `nogpu` 는 ImGui 렌더러 백엔드를 띄우지 않아 기동하자마자 멈추는 결함이 초록 뒤에 숨는다.
- **Debug App 이 크래시로 끝나면 `Bin/Saved` 에 크래시 묶음이 남아 다음 `ServerTest` 샤드들이 같은 폴더를 동시에 묶다가 `manifest.json` 을 못 찾는 Error 로 진다**(`ServerBootTest.EveryBuiltGameStartsAsADedicatedServer`, 다시 돌리면 통과). 크래시를 낸 실행 뒤의 첫 실패는 이것부터 의심한다([결정 기록 5-2](decisions/5-2_Build.md)).
- **렌더 변경은 스크린샷까지 봐야 검증이다.** 로그 · 테스트가 다 초록인데 화면만 찢어진 적이 있다. 정적 씬은 바이트까지 결정적이라 4 백엔드 전후 sha 로 비교한다. 4 백엔드 대조는 Debug 로(Shipping 은 `SW_SHIPPING_RHI_BACKEND` 하나만 링크하고 `-vk` 를 거절한다).
- **픽셀 지표는 튼튼하게.** "특정 색 픽셀 수" 는 클리어 색 · 톤맵에 무너진다 — 모서리 기준 배경 제거 + 평균(R−B) 대소, 또는 "고유 색 ≥ 2". sin(시간) 구동 지표는 "달라진 픽셀 수" 로. define 이 GPU 에 닿았는지는 PSO 디스크립터가 아니라 픽셀로 본다. 막히면 PPM 을 덤프한다.
- **D3D12 디버그 레이어 경고는 Debug DX12 로그의 `D3D12 debug layer (severity S, id N)` 줄이다**(앞 16 개만 적고 나머지는 센다). 렌더 변경 뒤 이 줄이 0 인지 본다. 테스트는 그리기 전후의 수를 비교해 단언한다. 검증 레이어를 켜는 `-gv_` 스위치는 없다 — 레이어는 Debug 빌드에서만 켜지므로 레이어 오류를 보려면 `build/Ninja-Debug` 로 돌린다. 오류는 `[Warning]` · `[Error]` 줄이라 시나리오 로그 게이트가 지고, 손으로는 `Saved/Automation/<이름>_<백엔드>.log` 에서 찾는다.
- **`-gv_screenshot` 은 PrintWindow 가 아니다** — PPM 이 원본이고 `-gv_screenshotFrame=N` 으로 프레임을, `-gv_screenshotAttachment=<이름>` 으로 첨부를 고른다. 게임 화면은 `-gv_screenshotSettledFrames=N`(로딩 화면 · 페이드 · 에셋 읽기가 끝난 뒤 N 프레임, 120 초 안에 안 잡히면 오류 14)으로 찍는다 — 로딩 화면 최소 표시와 페이드는 실제 시간이라 빠른 기계에서 고정 프레임이 로딩 화면을 찍는다.
- **에디터 UI 는 `-gv_screenshotEditor=<경로>.png`**(`-gv_screenshotEditorFrame=N`) 나 시나리오 단계 `<CaptureEditorUI file="….png" maxMagentaFraction="0"/>` 로 찍는다(ImGui 패널 · 플로팅 창까지 든 화면, 창 전경 · 가림과 무관 — [에디터 UX 기반](plans/EditorUX.md) 6 절). "가끔 튄다" 는 `-gv_screenshotCount=30 -gv_screenshotInterval=2` 로 연속으로 찍어 나란히 본다.
- **백엔드 교체는 시나리오로 재현한다**: `py -3 -m Scripts scenario swapother`(한 번 건너기). 투명 큐브가 카나리아다. 텍스처 경로는 `-gv_defaultMaterial=engine/materials/benchtextured.material`. 전역 변수는 `GlobalVariableInfo::setValueAsInt` 로 넣는다 — **C++ 대입은 변경 콜백을 부르지 않는다.**
  `engine/automation/rhiswap/` 시나리오는 네 백엔드가 모두 있는 PC 에서만 통과해 AppScenarioTest 가 줍지 않는다 — `swapbackends`(네 백엔드 건너기), `terrainedits`(저장하지 않은 지형 편집 · 씬 뷰 카메라가 교체를 넘는지), `removedevice`(`rhi.removeDevice` 로 DX12 장치를 제거하거나 Vulkan 장치를 손실 상태로 둔 뒤 프레임 · 종료가 멈추지 않는지 — `-dx12` · `-vk`). 교체 전후 그림을 견줄 때는 한 번 먼저 건너 도킹 배치를 안정시킨다(imgui.ini 가 노드 크기를 정수로 저장해 첫 리로드에서 씬 뷰 너비가 1 픽셀 바뀔 수 있다). `editor/previewreopen` · `editor/previewswap` 은 GPU 오류를 로그로 본다.
- **"인자가 적혔는가" 는 `CommandLineManager::isArgumentProvided` 로 본다**(`getArgument` 는 기본값이 있으면 안 적어도 true). **리사이즈는 창을 실제로 흔들어야 검증된다.** 블로킹을 걷을 때는 그것이 무엇을 가리던지 먼저 찾는다(에디터 모드가 DX12 DEVICE_HUNG 을 가렸다).
- **레이스 테스트는 "돌았다" 가 아니라 결과를 대조한다**(직렬 1 판 · 병렬 3 판 평균 ±1 %, 검출률 ~7/8). hostgpu 라 CI 가 못 돌므로 병렬 기록을 건드리면 로컬 hostgpu 를 여러 번 돌린다. 레이스는 deferred 파이프라인으로 돌려야 드러난다. 겨루는 테스트는 상대가 **돌기 시작한 뒤** 겨루고 "겹친 횟수 ≥ N" 을 단언한다. 레이스가 의심되면 추론하지 말고 `-gv_deferred=1` Debug 실행의 `Concurrent … access detected` 건수를 고치기 전후로 센다("도달 불가" 는 진입 횟수 계측으로 확인).
- **변이 검사 함정** — 조건을 지우는 변이는 방향에 따라 우연히 통과한다. 백업을 `Copy-Item` 으로 되돌리면 mtime 이 과거라 ninja 가 안 짓는다(touch). 변이한 코드를 태우는 스위트가 어느 실행 파일에 있는지 먼저 보고, "깨끗하게 실패했는가"(부정 단언 메시지는 `SW_EXPECT_FALSE_MSG`)와 성공 쪽이 조용한지도 본다. 저장소 밖 `mutlib` 은 기준선을 돌리지 않으니 테스트가 먼저 통과하는지 확인하고, 변이 뒤에는 전체를 다시 빌드한다(부분 빌드는 모듈 DLL ABI 가 섞인다).
- **새 테스트만 걸러 돌리고 커밋하지 말 것.** 빌드가 깨진 채 돈 ctest 는 **옛 바이너리**로 통과한다. 빌드가 진 프리셋(DLL 잠김 — `Scripts/setup/AddDefenderExclusions.py`)에서 테스트를 돌리지 말 것. 빌드 출력을 `| head` 로 자르지 말 것(파이프가 닫히면 빌드가 죽고 낡은 바이너리가 남는다 — 파일로 받은 뒤 본다).
- **간헐 실패를 다룰 때** — `ConsoleLogOutput` 은 Error 만 flush 해서 ctest `--output-on-failure` 의 마지막 `[ RUN ]` 은 크래시 위치가 아니다. 같은 명령을 직접 돌려 파일로 받는다. 단독으로 통과하고 스위트에서만 지면 전역 상태 오염부터 본다(VFS 는 `GlobalVfsScope`).
- **측정은 혼자, 기능 확인은 둘까지.** 빌드 · 스모크 · 벤치 · GPU 스위트를 겹치면 가짜 실패(파일 잠김 · CMake 프로브 레이스 `LNK1107`)와 수치 오염이 난다. 결정적 설정으로 기능만 보는 시나리오 · `hostgpu` 는 GPU 공유 잠금(`--shared`, 두 프로세스)으로 백엔드를 나눠 함께 돌려도 되고, 성능 측정 · 부하 기준선 · 장치 제거 실험은 독점이다. 같은 빌드 폴더(`Bin`)의 에디터 체인 둘은 함께 돌리지 않는다(산출물과 리소스를 고치는 시나리오가 겹친다) — 짝은 빌드 폴더가 다른 둘이나 산출물이 안 겹치는 실행으로 짓는다.
  샤드를 동시에 돌려도(`ctest -j3`) 시간 제한이 가짜로 지니 의심되면 그 샤드 하나만 다시 돌린다. 백그라운드 셸은 `taskkill /T` 로 트리째 내린다. 창을 만드는 GPU 테스트를 Bash 로 돌리면 타임아웃처럼 보인다 — PowerShell `Start-Process`.
- **Shipping 에서만 나는 것** — Info 로그 · `SW_ASSERT` 가 사라지고(반응이 로그 한 줄뿐인 기능은 배포본에서 없는 것과 같다), 리소스는 팩에만 있고 `.hlsl` 원본이 없으며, 쿠킹한 바이너리 씬만 읽는다(씬 · 프리팹 테스트는 `SceneDocument::saveBinary` 로 `.bin` 도 쓴다). 풀 블록을 힙으로 반납하는 손상은 Shipping 에서만 터지는데, 풀 자유 목록이 LIFO 인 것을 써서 "다음 할당이 같은 주소" 로 검사하면 Debug 에서도 잡힌다.
- **변경 전후 그림이 다르면 어느 쪽이 맞는지는 네 백엔드의 일치도로 가린다.** 변경 전 그림이 백엔드끼리 이미 어긋나 있었다면 변경 전은 기준이 아니다. 네 백엔드가 같은 증상이면 공유 호출부를, 한 백엔드만 다르면 그 백엔드의 산출물과 계약을 먼저 본다(한 백엔드만 깨지면 배리어 누락 레이스를 의심).
- **여러 번 띄우는 하니스는 프로세스를 이름(`taskkill /IM App.exe`)이 아니라 PID 로 내린다.** 로그의 `[Error]` 를 세는 실행은 강제 종료하지 않는다 — 비동기 로거의 뒷부분이 사라져 "오류 0" 이 거짓이 되므로 `-gv_profileFrames` 로 스스로 끝나게 한다.
- **셰이더 결과가 화면에 안 보이면 이 순서로 좁힌다.** ① 게이트 값(PSO · CB · UAV 핸들, 인스턴스 수)을 찍는다 ② 셰이더가 무조건 눈에 띄는 일을 하게 한다 ③ CB 멤버를 하나씩 조건으로 건다 ④ 임계값을 쓰기 전에 그 값의 실제 범위를 확인한다. 시간에 따라 움직이는 기능은 `-gv_screenshotFrame` 을 다르게 준 두 장으로 캡처 시점이 다른지 먼저 본다.
- **디바이스 손실(Vulkan `VK_ERROR_DEVICE_LOST`, DX12 `DEVICE_HUNG` · `DEVICE_REMOVED`)이 나면 Debug 로 이 세 단계를 먼저 한다.** ① 결함 주소를 읽는다 — Vulkan 은 `last pass put in the submission order: '<패스>'` 다음 줄의 `fault address 0: read-invalid at 0x…`(VK_EXT_device_fault), DX12 는 DRED 의 `PageFault VA=0x…` 와 `PageFault VA is inside live/recently freed allocation …`. 실행마다 같은 주소이고 서술자 힙 · 배열 시작 + 약 4 GB(0xFFFFFFFF × 서술자 크기)이면 무효 bindless 번호로 서술자를 읽은 것이다(`binding.hlsli` 의 "bindless 입문" 절).
  ② 같은 명령을 `-gv_validateBindlessIndex=2` 로 다시 돌려 `[BindlessIndex]` 줄을 센다. `is not a live` 오류가 있으면 그 패스 · 필드가 1순위 후보이고, 없으면 `= absent` 목록에서 손실 패스의 필드를 고른다. ③ 후보 하나만 셰이더에서 바꿔(첨자를 배열 안으로 자르기, 값을 상수로) 3 회 이상 돌려 재현율을 견준다. 셰이더는 개발 빌드에서 라이브 컴파일되어 다시 빌드할 필요가 없고, 끝나면 `App --cook-shaders` 로 다시 굽는다.

## 4. CI

push 뒤 결과를 기다리지 않는 것은 [작업 방식](11_Workflow.md) 4절의 규칙입니다. 실패를 알게 되면 공개 API 로 실행, 잡, 실패 주석을 읽습니다.

**CI 단계 분류.** GPU 가 필요한 것은 어느 CI 도 돌리지 않습니다(러너에 GPU · 창이 없음) — 로컬 `hostgpu` 가 맡습니다.

| 단계 | 언제 | 무엇 | 파일 | 예산 |
|---|---|---|---|---|
| 린트 | 모든 push · PR (문서만 바뀌어도) | 게이트 + 셀프테스트 전부(훅이 건너뛰는 트리 전체 게이트 포함), 지면 non-main 은 같은 실행의 빌드를 취소 | `ci.yml` `lint` | 약 1 분 |
| 빌드 · `nogpu` | push · PR(문서만 바뀌면 `changes` 잡이 건너뜀) | 구성 열 개 — Windows Debug · STL · Shipping · 전용 서버 Shipping, Linux Debug · ASan · TSan · Shipping · 전용 서버 Shipping | `ci.yml` `build` | 15 ~ 30 분 |
| 매일 컴파일 검사 | 03:00 KST · 수동 | 헤더 단독 컴파일, `SW_ENABLE_DEADLOCK_DETECTION` 구성 컴파일 | `header-self-contained.yml` | 약 30 분 |
| 매일 퍼징 | 03:00 KST · 수동 | 로더 퍼저(ASan) | `fuzz.yml` | 최대 2 시간 |
| 야간 부하 | 04:00 KST · 수동 | Release 부하 스위트 · 봇 · 클러스터 | `load.yml` | 최대 2.5 시간 |

판단 기준: push 마다 돌려도 잡 시간을 늘리지 않는 것만 `ci.yml` 에 둔다. 전 트리를 다시 훑는 검사(14 분 이상)나 평소 어느 프리셋도 켜지 않는 구성은 매일 잡으로 보낸다. 새 검사는 위 표에서 줄을 고른다.

**커밋 훅과 린트 잡의 나눔.** 커밋 훅은 staged 파일로 볼 수 있는 게이트만 돈다. 트리 전체를 읽어야 하는 무거운 게이트(`CheckNameReferences` · `CheckKitNamespaces`)는 훅에서 빼고(`preCommitSkipReason`) 린트 잡이 push 마다 본다. 린트 잡(`RunLintSuite`)과 `ctest -L lint` 는 `LintCatalog` 의 목록 전부를 돌린다.
한계: 이 둘의 위반은 push 뒤 CI 에서 처음 보이므로 이름 · 상태 표를 바꾸는 커밋은 push 전에 `py -3 -m Scripts gate CheckNameReferences` 를 손으로 돌린다.

```powershell
py -3 -m Scripts ci-jobs --summary                 # main 의 가장 새로 끝난 실행 한 줄(실패한 잡과 연속 실패 수) — 세션 가드가 부른다
py -3 -m Scripts ci-jobs --runs                    # main 의 최근 실행
py -3 -m Scripts ci-jobs <실행 id> --annotations   # 실패한 잡과 실패 주석
```

- **CI 가 끝까지 돌게 하는 세 가지**(`ci.yml`). ① main 은 `cancel-in-progress: false`(push 가 실행 시간보다 잦으면 끝나는 실행이 0 건이 된다). ② vcpkg 바이너리 캐시는 구성 직후 `actions/cache/save` 로 저장한다(post 저장은 잡이 성공할 때만 돈다). ③ 모든 잡이 PCH 를 켠 채 짓는다 — 가려진 include 누락은 헤더 단독 컴파일 잡이 잡는다.
  작업 로그 · 아티팩트는 API 로 403 이라 진 테스트는 주석으로 올린다 — 실행 목록 · 잡 단계 · 주석은 로그인 없이 읽힌다(시간당 60 회 한도를 여럿이 나눠 쓴다).
- **리눅스 CI 는 ubuntu-24.04 의 `libclang-dev`(18)다.** 파서에 새 libclang API 를 쓰면 리눅스 잡만 선다 — `CINDEX_VERSION` 으로 가른다. 16 미만은 한정 없이 적은 타입도 FQN 으로 철자하므로 `TypeNameMap::normalize` 가 잎 이름으로 맞춘다. 파이썬 최소 지원이 3.10(`kMinimumPythonVersion`)이라 f-string 식 안의 백슬래시 · 여러 줄 식이 configure 를 죽인다(`CheckPythonMinimumVersion.py`). GH Windows 러너는 cp1252 라 한글을 print 하는 스크립트가 `UnicodeEncodeError` 로 빌드째 죽는다 — 진입점은 `Scripts/common` 을 **모듈 수준에서** import 한다(`CheckScriptEntryPoints`). 재현은 `PYTHONIOENCODING=cp1252`.
- **CI 실패는 실패한 잡과 같은 프리셋으로 재현한다.** Debug(Engine SHARED)는 Shipping 정적 링크 결함을 원리상 못 낸다. Windows CI 러너의 TEMP 는 8.3 짧은 이름(`RUNNER~1`)이라 경로를 글자로 비교하면 틀린다. 빨간 CI 는 다음 결함을 숨긴다. 스킵은 실패보다 조용하다.
- **`SW_ENABLE_DEADLOCK_DETECTION` 은 매일 도는 `header-self-contained.yml` 이 `RunBuildWarnings.py --define SW_ENABLE_DEADLOCK_DETECTION --fail-on error` 로 지킨다.** push 마다 돌지 않으니 이 옵션을 건드린 변경은 같은 명령을 로컬에서 먼저 돌린다.
- **CI 실패는 `Scripts/dev/CiFailureReport.py` 가 주석으로 올린다**(테스트 · 구성 실패의 vcpkg 포트 로그 · 크래시 스택 `Bin/Saved/Logs/crash_*.stack.txt`). 정기 잡도 같다 — 헤더 단독 컴파일 잡의 빌드 오류(`build`), 퍼징 잡의 새니타이저 보고 · 스택 · 재현 입력(`fuzz`).
  퍼징 실패는 libFuzzer 가 짧은 입력에 찍는 `Base64:` 줄을 풀어 `SW_FUZZ_TARGET=<대상> LoaderFuzzer <파일>` 로 재현하고, 고친 뒤 그 입력을 대상의 씨앗(`LoaderFuzzTargets.cpp`)에 더한다. `Base64:` 가 비고 재현 입력이 빈 파일(`crash-da39a3ee…`)이면 입력 밖의 죽음이다 — 새니타이저 스택이 `exit` · 하네스 `stop` 아래면 종료 경로(정적 소멸 순서)를 고친다.
  저장소 주인 계정으로 로그인한 `gh` 가 있으면 잡 로그 전문을 받는다 — `gh api --allow-escape-sequences repos/sswgame/LearningTemplate/actions/jobs/<잡 id>/logs > job.log`. 진 테스트 주석의 제목: `[Timeout 180.0 s / 180 s]` 는 ctest 가 한도에서 죽인 것(멈춤이거나 느려짐), `[exit 1 after …]` 는 스스로 진 것, `[no exit line after …]` 는 종료 줄 없이 끝난 것(크래시). `ci-jobs <실행 id> --annotations` 의 잡 표는 실패한 단계의 경과 시간 · 종료 코드와 ctest Timeout 수를 보인다.
- **리눅스 CI 에서만 드러나는 결함 모양.** libstdc++ 의 `file_clock` 은 기원이 2174 년이라 파일 시각을 다른 시계로 옮길 때 기원을 맞춘다. 경로 구분자는 POSIX 쪽으로 정규화한다. 헤드리스(서버) 입력에는 게임패드 슬롯과 창 포커스가 없다. 테스트가 자식 셸을 띄우면 `exec` 를 붙인다(dash 의 `sh -c` 는 SIGTERM 에 먼저 죽어 143). 단조 시계로 잰 캐시 TTL 은 느린 빌드에서 임대를 풀어 버리므로 서버 틱 시각을 쓴다. epoll 전송은 멈춘 동안 읽지 않으므로 전송 모델마다 테스트 기대를 나눈다. 경합 검출기는 비 const `operator[]` 와 공유 읽기 버퍼를 쓰기로 보므로 읽기 경로는 const 로 둔다.
- **Windows STL containers 잡은 sw 할당자와 기록 해시에 기대는 테스트를 건너뛴다**(`MemoryTagTest` 둘, `DestructionDamageTest.EventLogHash…`). 같은 테스트가 기본 컨테이너 구성(Windows Debug 잡)에서 가끔 지는지는 2026-10-20 ~ 26 메인 CI 의 실패 실행 열세 개 로그에서 찾았으나 진 적이 없었다.
