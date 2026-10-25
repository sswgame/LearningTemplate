# 검증과 측정

> 질문: 무엇을 돌려야 일을 끝낸 것이고, 측정값은 어떻게 읽는가?
> 독자: 변경을 검증하거나 성능을 재는 사람.
> 결론: 손대기 전에 1절의 검증 규칙을 따르고, 성능 주장은 2절의 방법으로 Release 에서 잰 값으로만 한다. 3절은 검증할 때의 함정, 4절은 CI 실패 읽는 법이다.

한 모듈에만 해당하는 함정은 그 모듈 코드의 헤더 주석에, 테스트를 쓰는 규칙은 [Test README](../Test/README.md) 에 있습니다. 언제 어디까지 돌리는지는 [작업 방식](11_Workflow.md) 3절입니다.

## 1. 손대기 전에 — 검증 규칙

숫자 기준선(테스트 수 · 창 수 · 시간)은 적어 두지 않는다. 금방 낡는다. **비교가 필요하면 바꾸기 전과 후를 그 자리에서 측정한다.**
통과 기준은 "전부 통과, 스킵은 Dev 전용 케이스 · 그 플랫폼에 없는 타깃뿐" 이다. 시나리오(`-scenario`)는 종료 코드 0 만으로 통과가 아니다 — 아래 "시나리오 통과 기준" 이다.

개발 기본 빌드(`cmake --build --preset Ninja-Debug`)는 테스트 실행 파일을 짓지 않는다. 테스트까지 지으려면 `--target all AllTests` 를 붙인다.
`ctest` 는 실행 파일 테스트 앞에 셋업 항목 `BuildTestBinaries` 를 스스로 돌려 둘을 먼저 짓는다(빌드 없이 돌리려면 `-FS SWTestBinaries` — 픽스처 이름이다. CI 의 Test 단계가 그렇게 돌려 Build 단계 뒤 두 번째 링크가 없다).
`py -3 -m Scripts test` 도 먼저 짓는다(`--no-build` 로 건너뛴다). CI · 검증 프리셋(`CI-*`, `*-Shipping*`, `Ninja-Debug-ASAN`)의 `cmake --build --preset` 은 테스트까지 짓는다.

```powershell
cmake --build --preset Ninja-Debug            # 경고 0 이어야 한다(테스트 제외 — 테스트까지는 --target all AllTests)
cmake --build --preset Ninja-Shipping         # Debug 가 숨기는 결함이 여기서 드러난다(테스트까지)
ctest --preset Ninja-Debug-lint               # 컨벤션 · include 순서 · 린트 자기 테스트
# 커밋 훅(py -3 -m Scripts lint) · full-build · Scripts test 의 출력은 요약이다 — 통과는 한 줄 · 끝 몇 줄, 실패만 상세. 전부는 --verbose (빌드 · 테스트는 로그 파일 경로도 알린다)
py -3 Scripts/lint/gate/CheckDataDrivenStructure.py --summary  # 데이터 주도 구조 위반(코드의 에셋 경로 · 조용한 폴백 · 버린 읽기 결과) 영역별 표 — 기준선은 0 으로 수렴해야 하고, 형식으로 막힌 종류는 게이트에서 뺀다
# -j 는 값 없이 준다(CMake 3.29+: 이 PC 의 스레드 수). 테스트 프리셋은 CTEST_PARALLEL_LEVEL="" 로 같은 뜻이다(CMakePresets.json 의 parallel).
# 항목끼리는 병렬로 돌 수 있다 — 직렬은 빌드 셋업 · Load · QA 셋, GPU 항목은 GPU 슬롯(gpuSlots)만큼.
# 예외: AppTest 의 hostgpu 조각 · Race 와 QaGoldenImages 는 AppBin 잠금으로 서로 직렬이다(에디터 시나리오가 고정 이름 폴더에 쓴다). 푸는 계획: docs/plans/HostGpuIsolation.md.
ctest --test-dir build/Ninja-Debug -L nogpu -j --output-on-failure
ctest --test-dir build/Ninja-Shipping -L nogpu -j --output-on-failure

# CI 가 못 도는 부분(CI 러너는 nogpu 만) — GPU 있는 PC 에서 일을 끝내기 전에 반드시 돌린다. 단계마다 무엇을 도는지는 11_Workflow.md 3절 표.
# AppSmokeTest 가 네 백엔드 + 에디터(dx12 · gl)를 띄워 종료 코드 0 · 로그 [Error] 0건을 본다.
ctest --test-dir build/Ninja-Shipping -L hostgpu -j --output-on-failure   # 기능 시나리오 — Shipping 이 기본
ctest --test-dir build/Ninja-Debug -L race -j --output-on-failure         # 레이스 묶음(Debug 전용 — DataRaceDetector 는 SW_DEBUG 에서만)
ctest --test-dir build/Ninja-Debug -L hostgpu -j --output-on-failure      # Debug 전체 — 큰 단계 끝에 한 번

# ASan (Windows)
cmake --preset Ninja-Debug-ASAN
cmake --build --preset Ninja-Debug-ASAN
ctest --test-dir build/Ninja-Debug-ASAN -L nogpu -j

# 정적 분석 — 게이트가 아니라 판단 자료다(Scripts/docs/TrapsLint.md "함정 · 계약" 의 clang-tidy 참고)
py -3 Scripts/lint/report/RunClangTidy.py [--filter Core] [--clang-tidy <경로>]

# 눈으로 봐야 할 때 — 종료 코드 0, 로그에 [Error] 0건
cd build/Ninja-Debug/Bin
./App.exe -gv_profileFrames=40 -dx12 -EnableEditor
./App.exe -gv_profileFrames=40 -dx11 -EnableEditor
./App.exe -gv_profileFrames=40 -vk   -EnableEditor
./App.exe -gv_profileFrames=40 -gl   -EnableEditor
```

**시나리오 통과 기준.** 시나리오는 종료 코드 0 이고 처리하지 않은 `[Warning]` · `[Error]` 줄이 없을 때만 통과다. 처리하지 않은 줄이 하나라도 남으면 실행기가 15(`UnhandledLog`)로 끝나며 미확정(14)보다 항상 앞선다.
줄이 처리됐다는 것은 ① 그 시나리오의 `<ExpectLog id="…">`(로그 id 가 있는 줄) 또는 `<ExpectLog contains="…">`(id 없는 줄)가 선언한 줄이거나 ② 로그 id(`SW_LOG_WARNING_ID` · `SW_LOG_ERROR_ID`)가 붙은 줄이 `Resource/engine/automation/allowedlogs.json` 의 항목(id · 시나리오 하나 · 사유 · 처리 계획 문서)에 있다는 뜻이다. **선언은 줄을 정확히 가린다** — id 가 있는 줄은 `id` 로만, id 없는 줄은 `contains` 가 본문 전체와 같을 때만 가리고(부분 문자열은 다른 줄까지 가려 규칙을 무력화하므로 안 된다) `count="0"` 은 선언이 아니다. **id 없는 줄은 목록으로 가릴 수 없다** — 소스에 id 를 붙이거나 고친다.
목록의 칸이 비었거나 `TODO` 이면 모든 시나리오가 읽기 오류(11)로 선다. 호스트 테스트 스위트(`hostgpu`)는 같은 목록의 `suite` 항목을 쓴다. 보고에는 이렇게 쓴다 — 허용 목록에 없는 Warning · Error 가 하나라도 있으면 "통과" 가 아니라 "실패(UnhandledLog)" 다.
**시한과 종료 코드 12.** 프레임 시한(`timeoutFrames`)은 시나리오 시계를 붙잡은 프레임(`WaitUntil` · 시계 잡기 조건)을 세지 않으므로, 실행기가 시나리오 한 편의 벽시계 상한(`wallTimeoutSeconds`, 적지 않으면 150 초, 양수만)을 따로 본다. 넘으면 12(`TimedOut`)로 끝나고 보고를 쓴다.
이유에 어느 단계에서 얼마나 붙잡혔는지가 든다. 프로세스 시한(`AppTestUtil::kScenarioTimeoutSeconds` 180 초)이 먼저 죽이면 보고가 없으므로 이 상한이 그보다 짧아야 한다. 부하 시나리오(`<Load>`)는 측정 창 그물이 따로 있어 이 상한을 보지 않는다. 프레임 자체가 멈춘 행(교착)은 행 감시 스레드가 상한 + 유예(5 초) 뒤에 잡는다 — 미리 채워 둔 스냅샷으로 보고(`"hang": true`)를 쓰고 종료 코드 12 로 프로세스를 끝낸다. 시한 순서(감시 + 유예 < 시계 보류 170 초 < 프로세스 시한 180 초)는 `AutomationTimeLimits.h` 의 `static_assert` 가 지킨다.
`startAfter="Immediately"` 도 시작 차단 조건(로딩 화면 · 감시 울타리)이 풀린 뒤에 시작한다(씬이 플레이 중일 필요만 없다). 끝내 풀리지 않으면 시작 시한 뒤 12 로 끝난다.
**진 실행의 로그.** 로그 게이트는 시작한 시나리오 모두에 돌아 처리하지 않은 줄을 보고의 `unhandledLogs` 에 싣는다. 이미 진 실행(10 · 12 …)의 결과 숫자는 바뀌지 않는다 — 15 는 통과했을 때만 된다.
**보고 필드.** `name` · `result` · `exitCode` · `frames` · `reason` · `failures[]` · `unconfirmed[]` · `unhandledLogs[]` · `allowedLogs[]` · `metrics[]` · `records[]` 에 더해 실행 환경 칸이 있다 — `backend` · `game` · `startedAt`(지역 시각) · `startedAtUnixMs` · `wallMilliseconds` · `wallLimitMilliseconds`(부하는 0) · `lastStep`(끝난 자리 `시나리오 · 백엔드 · step N 단계 · f프레임`) · `reproduce`(같은 실행을 다시 띄우는 App 명령줄).
읽는 쪽이 있으니 필드는 더하기만 하고 `records[]` 는 끝에 둔다. `reproduce` 는 App 의 원본 인자를 그대로 옮기고(보고 · 녹화 경로와 `crash-reporter` 는 뺀다), 이름에 token · secret · password · apikey · credential · auth · license 가 든 인자의 값은 `REDACTED` 로 가린다. 원본에 없는 게임 · 실제 백엔드 · 시나리오 · `<Launch>` 변수만 채운다. 크래시 · 행 뒤 덤프에는 컨텍스트 키 `Scenario`(같은 자리 한 줄)와 빵부스러기(최근 단계 전환)가 남는다.
**시나리오를 직접 `App.exe` 로 조립하지 말고 `py -3 -m Scripts scenario <이름 조각 또는 경로> [--backend dx12|dx11|vulkan|gl|all] [--config Release|Shipping|Debug] [--app <App.exe>] [--screenshot-dir <폴더>]` 를 쓴다.** 시나리오 파일의 `<Launch>`(`-gv_firstScene` 같은 기동 변수)와 게임 · `-unattended` · `-scenario-report` 를 그대로 붙이고(손으로 조립하다 `-gv_firstScene` 을 빠뜨리면 읽기 오류 11이다), 백엔드마다 GPU 칸을 잡아 한 판씩 돌려 종료 코드를 뜻으로 풀고 `failures[]` · `unconfirmed[]` · `unhandledLogs[]`(줄 그대로)를 한 표로 보인다. 하나라도 통과가 아니면 종료 코드 1, 이름이 모호하거나 선언을 인자로 풀 수 없으면(부하 `<Load>` · `<묶음>.<n>` 일원 · 배포 빌드에 개발 변수) 이유와 함께 2 이다. `--dry-run` 은 조립한 명령만 찍는다. `--app-arg=<인자>`(반복 가능)는 `-gv_deferred=1` · `-EnableEditor` 같은 변형 인자를 맨 뒤에 더하고, 조립이 정하는 인자(`-scenario` · `-game` · 백엔드 스위치 · `<Launch>` 변수 · 에디터 시나리오의 에디터 인자)와 겹치면 오류로 멈춘다. 인자의 글(`-unattended` · `-scenario=` · `-EnableEditor` …)의 철자는 엔진 인자 표 `Source/Core/Predefined/ArgumentList.xxx`, 산출물 · 에디터 상태 폴더 이름은 `Source/Engine/Automation/AutomationScenario.h` 의 상수가 정본이고 이 명령과 C++ `AppScenarioTest` 가 같은 곳을 읽는다(두 벌을 두지 않는다). `<Launch>` 가 없는 시나리오는 "기동 변수가 필요 없다" 는 선언으로 보고 변수 없이 돈다(게임 · 백엔드 · 보고 인자만 붙는다 — 시나리오 대부분이 그렇고 `AppScenarioTest` 도 같게 돈다). `--screenshot-dir` 은 이번 판의 산출물만 `<폴더>/<백엔드>/` 에 모은다: 실행기는 `Saved/Automation/<시나리오 name>/` 을 비우지 않으므로 이 명령이 실행 전에 그 폴더를 비운다.
시나리오를 돌린 뒤에는 `py -3 -m Scripts scenario-logs --check` 로 목록에 있으나 더 나오지 않는 항목(지운다)과 15 로 끝난 보고를 센다. 처음 목록을 채울 때는 `--regen` 이 초안(사유 · 계획이 `TODO`)을 만든다. 절차와 이유는 [GPU 검증](plans/GpuVerification.md) 4절이다.

**단위마다 이 순서로 한다.** 테스트 먼저(이전 코드에서 지는 것을 확인) → 고침 → 변이 검사(고침을 되돌리면 테스트가 지는가) →
Debug(`nogpu` · `race`) · Shipping(`hostgpu`) → 커밋. 변이로 죽지 않는 테스트는 아무것도 지키지 않는다.

**에디터 패널에는 단위 테스트가 없다.** 컴파일이 통과해도 화면이 비어 있을 수 있다. 패널 · 위젯을 건드렸으면 실행해서 본다.
화면을 볼 수 없으면 `-gv_editorPanelDump=N` — N 번째 ImGui 프레임에 창 하나당 한 줄(이름 · 크기 · 정점 수 · 상태)을 남긴다.
**보이는데 정점이 0 인 패널**이 빈 패널이다. 의미 있는 신호는 "내용 없는 패널 0개" 쪽이고, 창 수는 패널이 늘면 바뀐다.
도구 패널까지 보려면 `-gv_editorOpenPanel=all` 을 같이 준다(모두 열고 · 저장된 도킹을 쓰지 않고 · 첫 크기 900×620, 그 실행의
레이아웃은 저장하지 않는다).

```powershell
cd build/Ninja-Debug/Bin
./App.exe -gv_profileFrames=60 -dx12 -EnableEditor -gv_editorOpenPanel=all -gv_editorPanelDump=40 > before.log
# ... 고친 뒤 같은 명령으로 after.log 를 떠서 비교한다
```

**기본 실기동은 테스트 씬을 연다.** Empty 팩 `data/gamesettings.xml` 의 `startMap` 이 `game/empty/maps/editortest.scene.xml` 이다(배포본도 같다).
그 씬의 메시는 `_meshID` 가 비어 **화면에 기하가 없다** — 픽셀 비교에는 벤치 큐브(`-gv_benchMeshes=N`)를 쓴다.
다른 씬은 `"-gv_editorStartupScene=<경로>"` 로 연다. **PowerShell 은 점이 든 인자를 쪼갠다 — 따옴표로 감쌀 것.**
씬 · 프리팹 에셋은 **엔진 직렬화기로 만든다**(손으로 쓴 XML 은 깨진다).

**렌더 결과는 픽셀로 측정한다.** `-gv_screenshot=out.ppm`(`-gv_screenshotFrame=N`)은 톤맵까지 든 최종 화면이다. 눈으로 보지 말고
배경과 다른 픽셀 수 · 채널 평균을 센다. `-gv_viewMode=<이름>`(Lit · Unlit · Wireframe 등 `RenderViewMode` 의 열거자 이름, 숫자는 오류)는 에디터 없이도 고른다.
**벤치는 `-gv_benchAnimate=0` 이어야 결정적이다**(네 백엔드 0 바이트 차이). 애니메이션을 켠 채 차이를 주장하려면 같은 조건
두 판으로 잡음 바닥을 먼저 측정한다.

```powershell
./App.exe -dx12 -gv_benchMeshes=8 -gv_benchAnimate=0 -gv_profileFrames=20 -gv_viewMode=Wireframe "-gv_screenshot=wire.ppm"
```

**사진만 찍고 끝낼 때는 `-gv_screenshotQuit`.** `-gv_screenshot` · `-gv_screenshotEditor` 는 사진을 쓰고도 앱을 끝내지 않는다(`-gv_profileFrames=N` 이나 시나리오가 끝내거나, 호출자가 표준 입력으로 `quit` 을 보내야 한다).
호출자가 둘 다 하지 않으면 시한(124)으로 죽는다. `-gv_screenshotQuit=1` 을 더하면 요청한 사진(둘을 함께 주면 둘 다)을 모두 쓴 프레임에 종료 코드 0 으로 끝나고, 파일을 쓰지 못했으면 14 로 끝난다.
기본은 끄기다 — `-gv_profileFrames` 와 같이 쓰는 측정이 사진 뒤에 일찍 끝나면 프로파일이 잘린다. 사진 인자 없이 이것만 주면 끝낼 근거가 없어 아무 일도 하지 않는다.

```powershell
./App.exe -dx12 -gv_screenshotQuit=1 "-gv_screenshot=shot.ppm"    # 사진 뒤 스스로 끝난다
```

**함정**

- 백엔드는 `-dx11 / -dx12 / -vk / -gl` 로 고른다. 명시한 백엔드를 쓸 수 없으면 폴백하지 않고 에러로 선다(`RHIBackendUtil::findCommandLineBackend`
  한 자리 — `-gv_rhiBackend=Vulkan`(열거자 이름 또는 숫자, 모르는 이름이면 기동이 멈춘다)도 같은 판정을 지난다). 실제로 뜬 백엔드는 로그 `Initializing RHI with backend:` 로 확인한다.
- 에디터는 `-EnableEditor` 를 줘야 뜬다. 없이 돌린 검증은 에디터 OFF 검증이다 — 로그로 실제로 로드됐는지 본다.
- 테스트 바이너리는 모든 구성에서 `build/<프리셋>/TestBin/` 에 있고 작업 디렉터리는 `Bin/` 이다. 모듈 DLL 은 `Bin/Modules/`, 서드파티 DLL 은 `Bin/` 한 벌, Dev PDB 는 `Bin/Symbols/` 에 있다.
  `Bin/` 에 남은 옛 자리 산출물(테스트 exe · 모듈 DLL · PDB)과 꺼진 모듈의 DLL 은 configure 가 지운다(`sw_removeStaleBinaryOutputs`) — 실행 중이라 못 지운 것은 경고로 남는다.
- 셰이더 소스(.hlsl/.hlsli)를 고쳤으면 다시 쿠킹한다. 빌드는 HLSL 을 다시 쿠킹하지 않는다 — `App.exe --cook-shaders` 뒤에 재고 비교한다.
  "최신" 은 셰이더마다 판정한다: `.hlsl` 을 고치면 그 셰이더, `.hlsli` 를 고치면 **그 헤더를 (전이로라도) 연 셰이더만** 다시 구워진다(스탬프에 소스별로 연 헤더와 해시가 적힌다). 공용 헤더 하나가 모든 셰이더를 낡게 하지 않는다.
  Shipping 쿠킹은 `cook.stamp` 의 내용 해시로 검증하고 어긋나면 빌드를 세운다(`App --cook-all --verify-shaders`). 스탬프 판이 바뀐 뒤 첫 쿠킹은 모두 다시 굽는다.
  쿠킹(`ShaderCookDriver::cookRequests`)은 요청 × 포맷의 컴파일 · 리플렉션을 엔진 워커(`engine::runParallel`)로 나눠 돌리고 합치기는 요청 순서대로 직렬로 한다 — 바이너리 · `reflection.manifest` · `cook.stamp` 는 직렬로 돌 때와 바이트까지 같다.
  끝줄의 `compile` · `reflection` 시간은 작업마다 잰 것의 합이라 병렬이면 `total` 보다 길다(쿠킹 속도를 재는 값). 쿠킹은 최신 바이너리도 매번 읽어 리플렉션을 다시 뽑는다 — 이전 매니페스트에서 가져오지 않는 이유는 결정 기록 5-5.
- GPU 확인은 이미 통과가 증명된 항목을 다시 돌리지 않는다. 통과 증거(`verify-batch` 결과 · 증거 파일 · `NEXT.md` 의 통과 기록)가 있고, 그 증거 커밋 이후 항목이 쓰는 코드 · 셰이더 · 에셋 · 시나리오가 안 바뀌었음(`git diff <증거커밋>..HEAD -- <관련 경로>` 가 빔)을 둘 다 보였을 때만 건너뛴다. 증거가 모호하거나, 고쳐진 항목이거나, GPU 에서 처음 도는 항목이면 돌린다. 보고에는 건너뜀 표(항목 · 증거 커밋 · 바뀐 경로 없음)를 따로 둔다.
- 비결정적 실패는 한 번 재현으로 "확정" 이라 부르지 않는다. 3~5 회 다시 돌려 재현율을 본다. 간헐 실패를 보면
  `--output-on-failure` 출력 전체를 파일로 남긴다(요약만 남으면 어느 케이스인지 모른다).
- 직접 실행으로 통과해도 CTest 에서 질 수 있다(병렬 부하 · 시한). 끝내기 전 검증은 CTest 로 한다. 멈추면 `LastTest.log` 의 마지막 `[ RUN ]` 을 본다.
- 테스트 실행 파일은 `AllTests` 를 지어야 최신이다. 옛 exe 로 돌리면 모듈 ABI 불일치로 진다.
- 옛 대소문자 이름의 CMake 빌드 폴더(`EditorUiTest` 같은 것)가 남아 있으면 `-Wnonportable-include-path` 경고가 난다 — 그 폴더를 지우고 다시 구성한다.

## 2. 측정 · 프로파일

- **물리 벤치: `PhysicsBenchTest`**(Release) — 먼 이동 바디가 있는 step p50 1124~2468 → 319~330 us(없는 step 은 319~328 us 그대로).
- **에디터 모듈의 계측은 `SW_EDITOR_PROFILE_SCOPE`**(`Editor/Common/EditorProfile.h`) — 엔진 `SW_PROFILE_SCOPE` 는 엔진 창구(`engine::getSubsystem<FrameProfiler>()`)를
  불러 에디터 모듈에서 쓸 수 없다. 같은 표에 쌓이고 이름은 엔진이 복사해 든다(모듈을 다시 올려도 매달리지 않는다). `GT.Editor.updateUI` 의 하위 구간은
  `GT.Editor.waitDrawSnapshot · newFrame · frameSteps · endFrame(render · platformWindows · captureDrawSnapshot)` 이고, `frameSteps` 는 단계마다 `GT.Editor.step.<id>`(commands · assetReload · panels · selfTest · windowTitle · screenshot)로 나뉜다. 패널별 시간은 `-gv_editorPanelTimes=N`.
- **Hierarchy 는 화면 밖의 접힌 루트를 빈자리로 둔다**(이어진 것은 빈자리 하나) — 큐브 8000 Release `GT.Editor.updateUI` p50 4.2~4.7 → 1.3~1.4 ms, Hierarchy 패널
  평균 3.4~4.1 → 0.63~0.65 ms(큐브 100 에서는 0.12 ms). 남은 몫은 루트 8000 개를 도는 것(열림 상태 조회) — 더 줄이려면 열린 루트 목록을 들고 보이는 범위만 계산한다.
  빈자리 높이는 접힌 루트 줄(프레임 높이 + 줄 간격)과 같아야 스크롤이 튀지 않는다(`hierarchy.offscreenRootsKeepTheirPlace`).
- **AppSmokeTest 의 Unknown 태그 상한(8 KB)은 파일 수에도 걸린다** — 태그 없는 호출자(App 스플래시)가 공유 캐시(`ResourcePaths` 경로 캐시)의
  재해시를 일으키면 그 버킷 배열이 Unknown 으로 센다. 데이터 파일 몇 개를 더하자 18 KB 가 넘었다. 공유 캐시는 넣는 자리에서 자기 태그를 건다.
- **성능은 Release 로 측정한다.** Debug 는 레이스 검출기 · 이터레이터 프록시로 컨테이너 코드를 과장한다(668 vs 87 us). 이전 · 이후 바이너리를 같은 스크립트로
  **번갈아** 2~3 회 측정한다(`git stash -u` → 빌드 → 복사 → `stash pop` → 빌드). 아침 기준선과 오후 결과를 비교하면 기계 상태가 결과로 읽힌다.
- **측정 명령**(Release · VSync 꺼짐, 측정하는 동안 다른 빌드를 막는다):
  - 묶어서 한 번에 — `py -3 -m Scripts verify-batch --owner <이름> [--config Release] [--only <id>…]`(`--list` · `--check` · `--dry-run`). 아래 명령과 ctest · 테스트 필터를 `Test/QA/VerifyBatch.json` 의 항목(종류 · 구성 · GPU 잠금 · 한계 · 결과를 옮길 문서)으로 구성별로 이어 돌려 `build/verify-batch/<시각>/result.md` 에 표 하나(항목 · 값 · 한계 · 통과/실패 · 측정 환경 · 날짜)를 남긴다. 새 측정은 그 파일에 항목을 더한다(한 명령으로 안 도는 것은 `need_command`). 한 구성 안에서는 `--jobs N`(기본 2)개까지 겹쳐 돈다: 빌드 · 쿠킹 · 골든 · 측정 · GPU 독점 항목은 언제나 혼자, GPU 공유 항목끼리는 순서대로, 잠금 없는 `ctest` · `test` 항목과 GPU 공유 항목은 겹친다(이유는 [결정 기록](09_Decisions.md) 검증 묶음 행). 결과 표 · `result.json` 은 겹침과 무관하게 매니페스트 순서이고 항목 출력은 `logs/<id>.log` 로 따로 간다. `--jobs 1` 은 한 번에 하나씩 옛 순서 그대로이며, 겹친 항목이 CPU 경합으로 시간 제한에 걸린 것 같으면 `--jobs 1 --only <id>` 로 다시 돈다.
  - 전 · 후 비교 — `py -3 -m Scripts perf-compare --app <후>/App.exe [--app-before <전>/App.exe] [--cases <이름…>] [--runs N]`. 케이스마다 A · B 를 번갈아 재고
    구간 · `[Bench]` 값의 중앙값 표를 낸다. `--app-before` 가 없으면 같은 바이너리의 `-gv_` 스위치 A/B 케이스만 돈다.
  - 이 기계의 기준과 견주기 — `py -3 -m Scripts perf`(프레임 p50 · p99).
  - 씬 로드 — `py -3 -m Scripts scene-load-bench --app <Release>/App.exe [--count 8000] [--runs 5] [--asset-report]`. 원본 XML 을 먼저 여러 번, 쿠킹한 뒤 쿠킹본을 여러 번 잰다.
  - 모프 · 스킨 풀 준비는 `RT.Deform.preparePool` 아래 `RT.Deform.rebuildMorphPool` · `uploadSkinPalettes` · `rebuildVertexAnimationPool` 구간으로 나눠 본다.
- **측정 기계**: i5-8500(6 코어 6 스레드). 게임 · 렌더 스레드 + 워커 넷이 코어를 나눠, 나눠도 벽시계가 잘 안 준다 — 틱이 쓰는 CPU 총량이 벽시계를 정한다.
  프로파일러가 없으면 `Ninja-Release`(PDB 있음 — 줄 표) + `py -3 -m Scripts stacks <pid>`(DbgHelp 샘플러). ICF 로 함수가 남의 이름으로 보인다.
- **표 읽기.** 열은 avg · p50 · p99 · min · max · per_frame 이고 카운터 값은 per_frame 열에 있다(시간 열 0 을 "죽은 경로" 로 읽지 말 것). 평균이 히치를 가린다 —
  p50 · p99 · 최악 프레임을 본다. 백분위는 옥타브 × 8 칸 히스토그램의 아래 끝(±9 %)이다. 구간 표는 스레드마다 **일한 시간**이고, 프레임이 빨라졌는지는
  `[Profile] wall N frames … us/frame` 와 `startup N ms` 로 본다. 중첩 합이 바깥보다 크면 표부터 의심한다.
- **캐시 적중률은 측정 창 끝의 `[CacheStats]` 줄로 본다**(`-gv_profileFrames`). 캐시마다 한 줄 — 이름 · `items`(항목 수) · `hit` · `miss` · `rate`(적중률) · `clear`(통째로 비운 횟수)이고,
  hit / miss / clear 는 워밍업 뒤 측정 창 시작부터의 값이다. `no counters` 는 조회가 없어 세지 않는 캐시(오디오 · 현지화 어댑터)다. 캐시를 늘리거나 상한을 정할 때 이 줄의 숫자를 근거로 쓴다
  (규칙: [결정 기록](09_Decisions.md) "캐시는 `CacheStats` 레지스트리에…"). 센 값은 `Core/Diagnostics/CacheStats.h` 의 이름별 원자 카운터이고, 모든 `IAssetCache` 는 생성자에서 `CacheTracking` 으로 칸을 올리거나 세지 않는 사유를 적는다(`no counters (<사유>)`). `ResourcePath` / `ResourcePathMissing` 줄은 경로 캐시의 적중과 음성 결과 적중이다.
- **`RT.Frame` = `RT.BeginFrame`(펜스 대기 = GPU 백프레셔) + `RT.ExecutePacket` + `RT.Present`.** `GT.Packet.submit` 이 크면 GT 가 RT 를 기다린다. `GT.Frame` 은
  `Engine::update` 만 재고 게임 모듈은 `App::run` 의 `GT.Game.update` · `GT.Game.fixedUpdate` · `GT.Editor.updateUI` 다. 2026-09-13 이전 RT 수치는 실제보다 작다.
- **대기 지점 스코프**(측정만 한다 — 이 값이 크면 그 대기를 비동기로 바꾸는 근거가 된다). 이름은 `<스레드>.<영역>.<동작>` 이다.

  | 스코프 | 위치 | 재는 것 |
  | --- | --- | --- |
  | `GT.Material.waitIdle` | `Engine::submitRenderPacket` | 새 머티리얼 첫 사용 때 GT 가 `RenderThread::waitIdle`(RT + GPU)을 기다린 시간 |
  | `RT.Upload.waitIdle` | `VulkanOneShotCommands::endSubmitAndWait` | Vulkan 업로드 · 리드백의 제출 락 + `vkQueueWaitIdle`(스레드는 호출한 쪽) |
  | `RT.Upload.flush` | `D3D12RHIDevice::flushPendingUploads` | DX12 구조 버퍼 갱신 복사 리스트 닫기 · 제출(위와 견주는 값 — 기다리지는 않는다. 텍스처 전송은 이 스코프를 거치지 않고 제출 때 큐에 바로 나간다) |
  | `RT.PSO.create` | `FrameRendererPSO.cpp` 의 `createPipelineStateTimed` | 패스 · 머티리얼 변형 · 파티클 PSO 의 동기 생성(셰이더 컴파일 포함) |
  | `GT.Load.sync` | `AssetLoadBlock::startLoad` 의 `OnCallingThread` 분기 | 프리팹 · 머티리얼 · 텍스처 · 메시 · 셰이더 동기 로드(호출 스레드가 GT 가 아니면 이름과 달리 그 스레드 몫) |
  | `GT.Settings.save` | `UserSettingsManager::saveUserFile` | 사용자 설정 쓰기(교체 재시도 sleep 포함, 서비스 묶이기 전에는 재지 않는다) |

  시동은 로그 한 줄 `Startup phases: hostScope · engine · window · modules · finalize+show · total | subsystems: 이름=ms …`(서브시스템은 1 ms 이상만, 오래 걸린 순)로 본다. `Test/Load/Baselines.json` 의 `startup_ms` 와는 별개다.
- **병목은 씬 크기에 따라 뒤집힌다.** 큐브 2000 은 GPU 대기, 8000 은 게임 스레드다. 어느 쪽을 깎을지는 재고 나서 정한다. GT 가 병목이면 RT 구간이 늘어 보여도 경합일 뿐이다.
- **타임라인은 Tracy 로 본다**(`-gv_tracy=1` + 같은 판 0.14.1 뷰어). 표(`-gv_profileFrames`)는 구간마다 접은 숫자라
  "어느 스레드가 무엇을 기다렸나" 는 Tracy 의 스레드 타임라인으로 본다. 계측은 `SW_PROFILE_SCOPE` 하나가 둘 다에 남긴다. GPU 줄도 쿼리는 한 벌이다
  (엔진 타임스탬프 → Tracy 수동 GPU 컨텍스트). DX11 · Vulkan 은 GPU 시계를 컨텍스트를 열 때 한 번만 맞춰(큐를 기다린다) 긴 실행에서 GPU 줄이 조금씩 밀린다.
- **GPU 비용은 `GPU.<패스>` 타임스탬프로 나눈다.** 패스를 지워서 나누면 타깃 사슬이 바뀌어 답이 뒤집힌다(추정 38 us, 실측 123 us). `RT.BeginFrame` 은 GPU 시간의 대리값이 아니다.
- **재기 전에 VSync 가 꺼졌는지 본다** — 1/RT.Frame 이 주사율과 같으면 VSync 다. DXGI 는 스왑체인 생성과 `ResizeBuffers` **둘 다**에 `ALLOW_TEARING` +
  `Present( 0, DXGI_PRESENT_ALLOW_TEARING )`(짝이 안 맞으면 `INVALID_CALL`, `RHI/DX/RHIDxgiTearing.h`). Vulkan 은 present 모드. CLI 는 `-vsync`.
- **셰이더를 고쳤으면 재기 전에 `App.exe --cook-shaders`.** 빌드는 HLSL 을 다시 쿠킹하지 않는다.
- **런타임 UI 벤치**(2026-10-07, Release · DX12 · VSync 꺼짐 · 600 프레임 × 세 번, i5-8500): `-gv_benchUiWidgets=10000`(엔진 `UIBenchScreen` — 스크롤 밖이 대부분) ·
  `-gv_benchUiChurn=M`(프레임마다 글 M 칸) · `-gv_benchUiMarkers=K`(Empty 벤치 큐브에 화면 마커). 글 10 칸/프레임: `GT.UI.Paint` p50 12.6 → 1.6 ms(자르기 밖 자식 컬링),
  `GT.UI.Layout` 0.11 ms(30 위젯), `RT.Canvas` Upload 0.05 + Draw 0.013 ms. 바뀜 없음(대조군): `GT.UI.Paint` 2.1 → 0.12 ms(바뀌지 않은 트리 출력 재사용), 업로드 p50 0.
  마커 500 + 위젯 1 만: Layout 0.36 · Paint 2.1 ms. 2026-10-10(보이는 자식 이분 탐색 · 그리기 목록 통째 복사 뒤, 세 번): 글 10 칸/프레임 `GT.UI.Paint` p50 0.29~0.36 · p99 0.66~0.79 ms,
  `GT.UI.Layout` p50 0.06 ms, `RT.Canvas.Upload` p50 0.04 ms. 같은 날 다른 PC(워커 14) 세 번: 전 `GT.UI.Paint` p50 0.26~0.29 · Layout 0.06 ms(합 0.32~0.35) →
  후(바뀐 그리기 목록 맞바꾸기 · 빈 위젯 캐시 건너뛰기 · `Widget::asPanel`) Paint p50 0.23 · p99 0.52 ms, Layout 0.06 ms(합 0.29 ms). 걷기 분해(임시 구간):
  걷기 ~180 us(보이는 위젯 1150 · 캐시 일괄 860 — 이어 붙이기 ~90 · 자르기 검사 ~20), 트리 출력 → 프레임 목록 30 us, 같은 내용 비교 8 us, 프레임 목록 복사 55~65 us(→ 0, 맞바꾼다). 구간 `GT.UI.*` · `RT.Canvas.*`, 카운터 `UI.LayoutWidgets` · `UI.PaintWidgets` · `UI.CanvasQuads`(값은 per_frame 열).
- **디버그 HUD**(2026-10-11, Empty · DX12 · 600 프레임): 꺼진 상태 `GT.DebugHUD`(셸 맵 액션 하나 + 켜짐 비교) Release p50 · p99 0 us(1 us 미만, 세 번), 할당 0
  (Debug `-gv_profileAllocSites` 에 HUD 자리 없음, alloc/frame 12.5). 켜짐(기본 섹션 넷): 섹션 갱신 `GT.DebugHUD.Sections` Release 17~36 us · Debug 84 us(4 Hz 라 프레임당 그 1/15),
  Debug alloc/frame 12.5 → 14.6.
- **텍스처 임포트 압축**(2026-10-10, Release App `--import-textures`, 워커 14 개 PC, 세 번): 띠 병렬 압축(`BandCompressJobInternal`) 전 → 후로
  BC7 512² 밉 10 단(`engine/textures_raw/random/grass.jpg`) 61.7~62.6 → 8.1~8.3 s, BC3 1024² 밉 11 단(`f00_000_face_00`) 17 → 2 ms,
  BC3 2048² 밉 12 단(`f00_001_body_00`) 70~72 → 9~10 ms, 원본 39 장 전체 3 분 35 초 → 40 초. 결과 DDS 는 스탬프 해시까지 바이트가 같다(스탬프를 비우고 전부 다시 임포트해
  `git status` 와 `import.stamp` 대조). 측정하는 법: 그 원본의 스탬프 줄만 지우고 `App --import-textures` — 로그 `Compressed WxH (… bands on N workers): T ms`.
  띠를 64 → 16 줄로 잘게 나눠도 그대로였다(BC7 512² 8.2~8.4 s). BC7 은 BC1 · BC3 보다 수천 배 느리다 — 무거운 것은 BC7 원본 수다.
  그 위에서 일괄 임포트(`AssetImportStampUtil::importAll`)가 **원본 파일도 워커와 나눠 임포트한다**(2026-10-25, 텍스처 · 모델 · 지형). 원본 폴더마다 사슬(같은 결과 자리에 쓰는 파일의 묶음)을 원자 카운터로 가져가는 동적 분배이고,
  텍스처의 띠 압축은 그 안에서 중첩된다(기다리는 스레드가 남의 일을 돕는다). 해시 계산 · 대조 · 임포트는 한 파일의 일이고, 요약의 문제 목록 · 카운터와 `import.stamp` 는 파일 순서대로 직렬로 합쳐 직렬로 돌 때와 같다
  (`AssetImportParallelTest.ParallelImportMatchesSerialImport`). 몸 메시를 읽는 장비 메시(`body_surface`)는 의존 없는 모델이 끝난 뒤 직렬로 임포트한다. 임포터 로그의 줄 순서는 파일 사이에서 섞인다.
  재는 법: 스탬프를 비우고 `App --import-textures` · `--import-models` 를 돌려 전체 시간(텍스처 약 1,080 · 모델 175)을 병렬 전후로 견주고, 끝난 뒤 `git status` 에 바이트 변화가 없어야 한다(병렬 전후의 측정값은 아직 없다).
  에디터 핫 리로드의 원본 임포트는 같은 임포트를 **백그라운드 잡에서 바뀐 원본만** 돌린다(2026-10-25, 결정 기록 5-11). 시나리오가 `Resource/` 의 모델 · 텍스처 · 높이장 원본을 쓴 뒤 결과를 읽으려면
  임포트가 끝난 것을 알아야 하므로 탐침 `Editor.AssetImportBusy`(0 이 될 때까지)와 `Editor.AssetImportFinishedCount`(시작 값보다 커질 때까지)를 `WaitUntil` 로 기다린다. 사본 폴더를 새로 만든 시나리오의 시작은
  감시 울타리가 사본이 일으킨 임포트까지 기다려 막는다. 검증: `EditorTest --test_filter=AssetHotReloadImportTest.*`(범위 임포트 · 워커 · 취소 · 재요청 · 에디터 스레드 적용).
- **벤치 스위치**(`Source/Games/Empty/BenchScene.cpp`): `-gv_benchMeshes=N` · `-gv_benchLights=N` · `-gv_benchGround=1` · `-gv_benchMovePercent=%` · `-gv_benchInstanced=1` ·
  `-gv_benchTickMovers=N`(틱 **안** 세터 — 실제 게임플레이 경로) · `-gv_benchSpawnChurn=N` · `-gv_benchMeshVariants` · `-gv_benchMeshShapes=N` ·
  `-gv_benchMaterialChurn*` · `-gv_benchAnimate=0`(컴퓨트 회전과 `update` 사인파를 **둘 다** 멈춘다), `-gv_deferred=1`, `-gv_useRenderThread=0`.
  틱 경로는 `-gv_tickBatch=0`(타입별 일괄 틱을 끈다 — 같은 바이너리 A/B)과 표의 `GT.Scene.tick.objectRange`(청크 CPU 합) · `pendingSlotApply` · 카운터
  `objectEntries` · `batchElements` · `pendingSlots` · `queuedWrites` 로 가른다. "무버 8000" 은 `-gv_benchMeshes=8000 -gv_benchTickMovers=1` 이다(`TickMovers=N` 은 큐브마다 N 개).
- **`-gv_profileFrames=N` 은 프레임 수다** — VSync 가 꺼진 가벼운 장면은 1500 fps 라 90000 프레임이 1 분에 끝난다(시간 상한은 없다). 시간으로 측정하려면
  `-gv_profileSeconds=S`(먼저 닿는 쪽이 끝낸다 — soak 이 쓴다).
- **프레임당 힙 할당**은 `-gv_profileFrames` 보고의 `alloc/frame`, 콜스택은 `-gv_profileAllocSites=N`(Debug App — 횟수는 최적화와 무관, 시간은 같이 측정하지 말 것).
- **씬 로드 측정**: `Scripts/dev/MakeStressScene.py` 로 큰 씬(도형 섞기) → `-gv_firstScene=<씬>`(또는 Empty 팩 `data/gamesettings.xml` 의 `startMap`) → `[SceneLoad]` 줄. Dev 는 `Cooked/` 를 마운트하지
  않으므로 쿠킹 효과는 `--cooked-dir=<repo>/Resource` 로 쿠킹하고 재고 지운다. 쿠킹본을 읽었는지는 `[SceneDocument] Loaded … from binary` 줄로 본다(`[SceneLoad]` 줄은 늘 논리 경로를 찍는다).
  기준값(`py -3 -m Scripts scene-load-bench --count 8000`, Release dx12, 5 회 가운데 값, Empty 팩): XML 은 파싱 25.6 ms · 인스턴스화 86.8 ms, 쿠킹본은 파싱 6.2 ms · 인스턴스화 26.7 ms 입니다. 로드 중 `GT.Frame` 최악은 XML 18.3 ms · 쿠킹본 14.1 ms 인데 판 사이 변동이 크다(3 회 측정에서는 XML 29.9 ms). 쿠킹본을 읽었는지는 하니스 표 아래 source 줄(`binary …scene.bin`)로 본다 — `[SceneLoad]` 줄은 쿠킹본이어도 `.scene.xml` 을 찍는다.
- **벤치가 상태를 공유하면 단계 순서를 측정한다.** 손대지 않은 대조군이 움직이면 하니스를 의심한다. 벤치 메시가 공유라 배치 결함을 가린 적이 있다 — 씬에서 온 메시로도 본다.
- **GPU 업로드 비용은 호출당이다**(DX12 ~3.3 us) — 쪼개면 느려진다. 구간을 배열로 묶어 한 번에.
- **워커가 쓴 데이터를 다른 코어가 읽으면** 캐시 이동이 항목당 일(~40 ns)보다 비싸다. 쓰는 스레드와 읽는 스레드를 같게 둔다.
- **워커는 공유 카운터 · 비트필드에 쓰지 않는다.** `fetch_add` 한 줄이 병렬 플러시를 직렬보다 느리게 했다. "하나라도" 플래그는 프레임에 한 번 쓰고 읽기만 한다.
- **도구**: `RunDuplicateCode.py --filter <dir> --no-headers`(머리말에 "합칠 대상 아님" 목록), `RunEngineLayerGraph.py`, `Scripts/dev/RunBackendSmoke.py`(4 백엔드 평균 RGB),
  Release 로 읽는 `TaskManagerBenchTest` · `GameObjectBenchTest` · `ContainerBenchTest` · `NetReplicationBenchTest`(클라이언트 16 × 엔티티 1000), `Test/TestFramework/Harness/TestBench.h`.
- **틱 디스패치 벤치**(`GameObjectBenchTest`, Release, `--load_suites=only` 로 `Bin` 에서 직접): `SmallGroupTickKnee`(무버 16 ~ 8000 개) · `PrerequisiteChainDepthSweep`(사슬 깊이 1 ~ 64) · `PrerequisiteFanOut`(리더 하나 + 팔로워 8000) · `PrerequisiteSpawnChurn`(팔로워 4000 에 프레임마다 하나 만들고 지움) · `FollowerOrderingGroupVersusPrerequisite` · `IntraObjectPhaseOrdering`. 작은 그룹은 직렬이 병렬보다 빨랐다(16 개 1 us 대 19 us, 512 개 13 대 47) — 손익분기는 약 1000 개(항목당 ~25 ns)라서 `TaskManager::runParallelByCost` 가 앞 항목을 직접 돌아 재고 고른다(`TaskManagerTest.RunParallelByCost*` 가 덮개 · 무거운 항목의 분할 · 가벼운 일의 직렬을 지킨다). 사슬 깊이 64 는 1375 → 267 us, 스폰 변동 + 선행 조건 4000 개는 프레임당 559 → 69 us(선행 조건 없는 변동은 42 ~ 46 us). 선행 조건 경로의 정확성은 `TickPrerequisiteTest` 의 `FollowerChurnKeepsOrderUnderIncrementalStageUpdates`(무작위 변동 300 프레임) · `FirstFollowerAddedIncrementallyAppliesTransformsBeforeItsStage` 가 지킨다.
- **타입별 컴포넌트 질의 벤치**(`TypedComponentBenchTest`, Release, `Bin` 에서 `../TestBin/EngineTest.exe --test_filter=TypedComponentBenchTest.* --load_suites=only --host_suites=all`): 오브젝트 1만 · 10만 × 정확한 타입 수 50 · 200 · 500 에서 `forEachComponentOfType`(버킷) · `ComponentRegistry::getAll` · 오브젝트 훑기를 말단 · 기반 타입으로 견주고(질의 한 번 ns), 만들고 지우기 비용 · 잠금 한 쌍 · `sizeof( Component )` 를 찍는다. 결과(2026-10-19, Release, 같은 PC, 5 판 p50): 버킷 순회는 옛 훑기보다 말단에서 70 ~ 2000 배, 기반에서 약 4 ~ 15 배 빠르다(10만 오브젝트 기반 2.7 ms → 0.18 ms). 검사 없는 등록형 목록보다는 말단이 느리다(10만 · K 50 에서 2.0 배): 컴포넌트마다 읽는 삭제 대기 표시와 K 에 비례하는 버킷 훑기 때문이고, 버킷 하나에 `isDerivedFrom` 약 2 ns, K 500 에서 약 1 µs 다. 오브젝트 하나 만들고 지우기는 p50 433 ns, 경합 없는 `std::shared_mutex` 배타 잠금 한 쌍은 약 4.3 ns 다. 게임별 버킷 수 K(600 프레임 시점)는 Empty 3 에서 Shooter3D 19 · WildWest 21 까지라 K 50 줄이 실제 게임에 해당한다. K 가 수백인 씬에서 자주 묻는 질의는 조회 타입마다 버킷 목록을 캐시하는 안(타입 레지스트리 세대로 무효화)이 먼저 필요하며, 측정으로 필요가 확인될 때만 넣는다. 앱 측정(2026-10-20, 독점 Release, 5 판 교대, 7-c 직전과 그 뒤 453 커밋을 함께 견준 값이라 7-c 만의 효과는 아니다): 큐브 20,000 개 `GT.Frame` p50 1310 → 1179 µs 로 퍼짐 안, `tickCostChurn` churn500 `GT.Game.update` p50 294 → 327 µs(+11 %, 원인이 7-c 인지 그 뒤 커밋인지 가르지 못함), 비동기 씬 로드 중 조회는 idle 과 같아(p50 400 ~ 500 ns) 로드 워커가 조회를 막지 않고, `resolveComponent<T>` 의 타입 확인은 핸들당 약 3.5 ~ 4.5 ns 다. 측정 때는 라운드마다 매니저 · 풀 · 레지스트리를 새로 만든다(best-of-N 반복은 자라는 컨테이너의 오염을 거르지 못한다).
- **측정 라운드마다 매니저 · 풀 · 레지스트리를 새로 만듭니다.** best-of-N 반복은 자라는 컨테이너가 만든 오염을 거르지 못합니다. 같은 값이 연달아 세 번 나올 때까지 수치를 믿지 않습니다.
- **한 번만 일어나는 이벤트(백엔드 교체 프레임의 최대값 등)는 3 회 이상 되풀이해 이상치를 거릅니다.** 프레임 1 에만 일어나는 일(업로드 · PSO 생성)은 워밍업(`FrameProfileSession::kWarmupFrames`) 뒤의 측정 창 밖입니다.
  측정 창 안에서 전량 재생성을 일으키려면 시나리오 단계 `<SwapBackend backend="other"/>` 를 워밍업보다 뒤의 프레임에 둡니다. 히치 최적화는 평균이 아니라 최악 프레임으로 판정합니다.
- **성능 워크로드는 움직여야 합니다.** 정지한 씬은 "바뀐 것 없음" 빠른 경로만 측정합니다. 워크로드가 지나지 않은 경로는 "검증됨" 이 아니라 "실행된 적 없음" 으로 보고,
  최적화 대상은 프로파일 표에서 고르고 전후 숫자를 커밋 메시지에 남깁니다. 새 프로파일 구간을 넣을 때는 그 상위 구간도 함께 넣습니다. 하위 구간의 합과 전체의 차가 대개 진짜 병목입니다.
- **렌더 성능은 디스크에서 읽은 씬으로 배치 수를 먼저 셉니다.** 인스턴스 수와 배치 수가 비슷하면 인스턴싱이 꺼진 것입니다.
- **측정 표에는 측정한 PC 이름과 CPU 를 적습니다.** 다른 빌드가 도는 동안 잰 값은 오염되므로, 측정하는 동안은 다른 빌드를 막습니다(아래 "빌드 속도" 의 슬롯 잠금).

### 영역별 측정값 (계획 문서 · 후보의 기준선)

계획 문서 · 후보 절이 근거로 삼는 측정값입니다. 다시 잴 때는 같은 명령과 구성으로 잽니다.

- **증분 쿠킹 단계별 시간**(증분 쿠킹 K0, 2026-10-20, Release App(`main` 33c8734fa), PC `SW` · Ryzen 7 6800H(워커 14) · 전원 연결, 다른 빌드 · 실행 없음, 세 번, 단위 초, 활성 게임 Empty):
  깨끗한 실행 = `Saved`(기록 · DDC), 쿠킹 폴더, 팩 폴더, 셰이더 `bin` 산출물을 지움. 바뀐 것 없는 실행 = 바로 이어 같은 명령 다시. 셰이더는 컴파일러 디스크 캐시(`Resource/cache/shaders`)까지 지운 것이 "차가운" 실행이다.
  명령은 `App --cook-shaders`, `App --cook-scenes --cooked-dir=<폴더>`, 팩 쿠킹(당시 파이썬 `Scripts cook --packs-only`, 시동 약 0.5 s 포함. 이 명령은 지금 없다).

  | 단계 | 깨끗함(세 번) | 바뀐 것 없음(세 번) | 비고 |
  |---|---|---|---|
  | 셰이더 6180 개 | 251.7 · 252.2 · 253.4 | 2.33 · 2.31 · 2.32 | 컴파일 캐시만 켠 깨끗한 실행 5.59 · 5.36 · 5.28 |
  | 씬 7 개(시동 포함) | 0.75 · 0.73 · 0.76 | 0.46 · 0.47 · 0.48 | 시동 약 0.4 s 가 든다 |
  | 프리팹 98 개 | 0.26 · 0.25 · 0.36 | 0.12 · 0.12 · 0.12 | |
  | 레지스트리 12 개 | 0.095 · 0.081 · 0.075 | 0.045 · 0.045 · 0.045 | |
  | VAT | 0(굽는 대상 0) | 0 | Empty 의 군중 표에 (메시, 클립)이 없다. 잰 값이 아니라 안 돌았다는 뜻 |
  | 내비메시 1 개 | 0.045 · 0.049 · 0.048 | 0.039 · 0.040 · 0.040 | |
  | 종료 꼬리(모듈 내림) | 0.21 · 0.27 · 0.22 | 0.22 · 0.21 · 0.21 | |
  | `--cook-scenes` 전체 | 1.36 · 1.38 · 1.46 | 0.89 · 0.88 · 0.89 | |
  | 팩 13 개(약 118 MB) | 12.4 · 12.1 · 12.1 | 1.70 · 1.68 · 1.68 | 바뀐 것 없으면 `.packrec` 로 다시 쓰지 않는다 |

  씬 단계는 쿠킹 폴더에 쓴 파일의 수정 시각(`.scene.bin` → `.prefab.bin` → `assetregistry.txt` → `.navmesh`)으로 나눴다. 쿠커 안에 단계별 타이머가 없어서 시동 · 종료 몫이 씬 줄과 꼬리에 섞인다(코드 변경 없음이 조건).
  바뀐 것 없는 실행의 로그는 씬 7 · 프리팹 98 · 레지스트리 12 · 내비메시 1 이 모두 `reused from the DDC` 다.
  **바이트 같음**: 깨끗한 실행 세 번과 바뀐 것 없는 실행 세 번의 쿠킹 폴더(`.bin` 105 · `.txt` 12 · `.navmesh` 1), 팩 13 개(`.packrec` 포함), 셰이더 산출물 6196 개의 SHA-256 이 전부 같다.
  셰이더는 차가운 실행 세 번과 컴파일 캐시를 켠 실행 세 번도 서로 같다(줄바꿈은 LF 로 맞춰 견줌 — `cook.stamp` 는 쓸 때 LF, 저장소는 CRLF).
  팩 줄은 항목별 압축 · CRC 를 병렬로 돌리기 전 값이다(2026-10-25 병렬 압축, 결정 기록 5-2). 병렬 뒤 값은 같은 명령의 요약 로그 한 줄로 잰다 — `Pack cook: N packs written or reused ... in X ms - read ..., compress W ms wall / C ms cpu, layout ..., hash ..., write ..., other ...`.
  `compress` 의 벽시계 / CPU 합이 병렬 효과이고, 바뀐 것 없는 실행(1.7 s)은 `hash` 가 거의 전부다(출력 118 MB 해시, 사본 복사를 없앴다). 병렬 뒤 팩 바이트가 직렬과 같음은 `PackLayoutTest.ParallelCompressionGivesTheSerialBytes`(매니페스트 `pack-cook-parallel-tests`)가 지킨다.
  병렬 뒤 첫 값(2026-10-25 통합 I, Release `SW_GAMES=ALL`, `-cook-all` 한 번씩, 새 워크트리라 기록 · DDC 없음 — 위 표와 데이터가 달라 직접 비교하지 않는다): 새 기록 14 팩 1850 ms(1235 파일 · 28 MB, compress 465 ms 벽시계 / 1183 ms CPU = 2.5 배, hash 201, other 1110), 바뀐 것 없음 988 ms(hash 154, other 833 — 출력 읽기 · DDC). 해시 시간은 `PackCooker`(입력) + `CookStore`(출력)가 잰다.
  읽는 법: 이득은 셰이더 컴파일 캐시(252 → 5 s)와 팩 증분(12 → 1.7 s)에서 나온다. 씬 · 프리팹 · 레지스트리 · 내비메시는 합쳐도 깨끗한 실행 1.4 s 라 DDC 로 줄어든 몫은 0.5 s 안팎이다(이 데이터 크기에서).
- **지형 LOD 교체**(`TerrainBenchTest.LODSweepWorstFrame`, Release, 쇼케이스 지형을 600 프레임 동안 가로지름, 3 회): 교체 프레임 111 개에서 updateLODs(청크 메시) p50 0.1 ms · 최악 4.0 ~ 5.1 ms,
  GPUScene 수집 p50 0.95 ms · 최악 5.0 ~ 5.2 ms 입니다. 기준은 GT 2 ms 입니다.
- **지형 높이 쓰기**(2026-10-15, **Debug** — Release 는 아직, 257 × 257 · 256 m · 청크 32 칸, 9 · 5 회 p50): 5 × 5 샘플을 쓴 뒤 물리 틱이 지형 전체 한 바디(청크 256 칸)면 1416 ms,
  청크마다 바디면 17.0 ms 입니다(`TerrainBenchTest.ColliderRebuildAfterBrushWrite`, 쓰지 않은 틱 0.1 ~ 0.16 ms). `writeHeights` 의 쓰기 프레임은 청크 메시를 넷까지만 다시 만들어
  한 변 9 샘플 2.6 ms · 257 샘플 25 ms 이고, 미룬 청크 전체는 257 샘플에서 262 ms 입니다(`WriteHeightsRectSizes` — 나누기 전에는 257 샘플 쓰기 한 번이 288 ms).
- **애니메이션 갱신 — 본 수 × 캐릭터 수**(`SyntheticRigBenchTest.EvaluateByBonesCharactersAndMode`, 2026-10-17, Release, PC `SW` · Ryzen 7 6800H, 다섯 번의 중앙값, 단위 us):
  `EditorTest.exe --load_suites=only --test_filter=SyntheticRigBenchTest.*` 를 `Bin` 에서 돌린다. 데이터는 대표 합성 리그(`game/empty/rigs_raw/`, 클립 둘 · 1 초 · 모프 둘),
  캐릭터 절반씩 clip_00 · clip_01 을 엇갈린 시각에서 재생, `AnimationSystem::evaluate` 한 프레임(워커 병렬, 캐릭터 1000 은 60 프레임이라 p99 ≈ 최댓값).
  own = 캐릭터마다 포즈, +rig = 다리 TwoBoneIK · 사슬 SpringChain, crowd = 군중 포즈 공유. GPU 스키닝(`GPU.Compute`)은 재지 않았다 — 런타임 .mesh 를 App 벤치 씬에 싣지 않았다.

  | 리그(본) | x1 own | x100 own p50 · p99 | x1000 own p50 · p99 | x1000 +rig p50 · p99 | x1000 crowd p50 · p99 |
  |---|---|---|---|---|---|
  | biped 20 | 3 | 132 · 178 | 551 · 921 | 1251 · 1904 | 204 · 379 |
  | quadruped 60 | 8 | 250 · 1797 | 1490 · 4575 | 4590 · 10243 | 218 · 336 |
  | biped 150 | 20 | 326 · 449 | 3223 · 3969 | 7193 · 10434 | 225 · 285 |
  | multileg 300 | 38 | 613 · 1932 | 8066 · 16659 | 13773 · 40555 | 257 · 922 |

  캐릭터 하나 직렬은 본당 약 130 ns 로 본 수에 선형이다. 캐릭터 1000 own 은 본·캐릭터당 21 ~ 27 ns 인데 본 150 → 300 에서 2.5 배(선형이면 2 배)이고,
  300 본 +rig 의 p99 가 p50 의 3 배(41 ms)다. quadruped 60 의 실행 사이 흔들림이 크다(x1000 own p50 1223 ~ 3162). crowd 는 버킷 수(클립 2 × 위상 4)에 묶여 본 · 캐릭터 수와 거의 무관하다.
- **파괴 첫 프레임**(`FractureBenchTest.ShowcaseBeginPlay`, Release, beginPlay + 첫 틱 — 첫 물리 스텝 앞에서 상태와 잎 셰이프를 만든다): 잎 셰이프를 시작 때 모두 지으면(`-gv_fractureShapeWarmMicroseconds=-1`) p50 19.4 ms, 기본(틱마다 약 0.57 ms 예산으로 나눠 지음)은 p50 7.7 ms 입니다(`PerfCompare.py` fractureWarm, 3 회).
  대신 셰이프가 준비된 뒤 벽이 깨지는 프레임이 p50 2.4 → 3.2 ms 로 늘었다(`fractureDefer`, 7 회 재측정 1.33 배 — 첫 프레임 12 ms 절감과 맞바꾼 값으로 유지한다).
  쇼케이스 파괴물 여섯 · 잎 312 개이고, 200 조각 벽 하나의 볼록 껍질이 약 11 ms(잎당 약 55 us)입니다. 깨지는 프레임은 200 조각 벽에서 4 ~ 7 ms(그중 사건 처리 2 ~ 4 ms)입니다.
- **구조 리팩토링 이동(S12 ~ S22) 전후**(Release dx12, 7 회 가운데 값, 전은 S12 직전 커밋 `6a86561ef` · 후는 그 뒤 main, 두 App 을 번갈아 돌림): 벤치 큐브 8000 에서 `GT.GPUScene.build` p50 360 → 327 us · p99 720 → 589 us, `GT.Frame` p99 3932 → 3145 us.
  `-EnableEditor` 에서 `GT.Editor.updateUI` p50 655 → 655 us(전의 `commandsAndWatchers + panels` 3 + 163 us 대 후의 `frameSteps` 163 us), `RT.Frame` max 1039 → 1160 us 입니다.
  3 회 측정에서는 에디터 `RT.Frame` max 가 945 → 11157 us 로 보였으나 일회성 히치였다 — p99 · max 는 7 회 이상으로 본다.
- **UDP 소켓 계층**(`NetUDPBenchTest`, 호스트 스위트, 서버 20 Hz 200 B · 클라이언트 30 Hz 40 B, 서버와 클라이언트가 한 PC, Release 3 회 가운데 값, 다른 빌드로 CPU 20 ~ 46 % 부하, Windows):
  클라이언트 500 은 받기 10.7k · 보내기 10.7k pkt/s, 서버 스레드 14 %, 패킷당 6.6 us, RTT p50 48 · p99 48 ms 입니다.
  클라이언트 2000 은 42.1k · 42.5k pkt/s, 54 %(51 ~ 82 %), 패킷당 6.5 us, RTT p50 68 · p99 70 ms, 커널 버림 0 입니다. RTT 는 대부분 같은 PC 의 클라이언트 드라이버 지연입니다.
- **온라인 부하 봇 참고값**(2026-10-06, Debug, `--local-server` — 봇과 서버가 한 스레드라 서로의 시간을 잰다): `quick_check` 봇 300 이 4200 동작을 28 초에 오류 0 으로 끝냈고,
  chat_send p50 377 ms · p99 1081 ms, 채팅 알림 185548 건이었습니다. 판단 근거로 쓸 값은 Release 서버 한 대 측정입니다(후보 `EpollAndRealStoreVerification`).
- **`DynamicBitset` 연산**(2026-10-17, `DynamicBitsetBenchTest`, Release, PC SW · Ryzen 7 6800H, 전 · 후 바이너리를 번갈아 세 번, 판 15 개 p50 의 가운데 값, ns):
  `--load_suites=only --test_filter=DynamicBitsetBenchTest.*` 를 `Bin` 에서 돌린다. 비트당 연산은 비트당 ns, 나머지는 한 번당 ns 다.

  | 연산 · 비트 수 | 전 | 후 | `std::bitset` |
  |---|---|---|---|
  | `to_string` 비트당 · 8 / 65536 | 5.4 / 5.2 | 1.0 / 0.6 | 0.1 / 0.0 |
  | `parse` 비트당 · 1024 / 65536 | 0.9 / 3.3 | 0.2 / 0.2 | 0.0 / 0.0 |
  | `set(pos)` · `test(pos)` 비트당 · 65536 | 3.3 · 2.7 | 2.0 · 1.4 | 0.8 · 0.0 |
  | `&=` · 1024 / 65536 | 11.1 / 525 | 4.6 / 106 | 2.6 / 106 |
  | `&` · `<<` · `>>` · 65536 | 681 · 631 · 525 | 244 · 256 · 263 | 144 · 338 · 194 |
  | `==` · 8 / 65536 | 1.6 / 481 | 2.0 / 63 | 0.4 / 88 |
  | 사본 연산(`&` · `~` · `<<`) · `resize` · 8 | 45 ~ 49 | 45 ~ 50 | 0.2 |

  격차의 원인: 사본을 돌려주는 연산 · `resize` 는 작은 크기에서 힙 할당(약 45 ns)이 전부다. `count` · `any` · `all` 은 8 비트에서 2 ns(DLL 호출),
  `push_back` 은 비트당 3 ns(수작업 0.6 ns)다. 호출부(직렬화 `to_string` · `parse` · `set` · `test`)가 쓰지 않아 두었다.
- **기믹 회로 상태**(2026-10-20, `GimmickStateBenchTest`, Release, PC SW · Ryzen 7 6800H, 세 번 중 최대): `EngineTest.exe --load_suites=only --test_filter=GimmickStateBenchTest.*` 를 `Bin` 에서 돌린다(두 케이스 5.4 s).
  쇼케이스 씬(`gimmickshowcase.scene.xml`)의 회로 5 개와 `common/prefabs/gimmicks` 의 프리팹 13 개(회로가 든 것)를 60 Hz 로 0 · 600 · 36000 스텝 돌리고 `FixedLayout::write( state )` 바이트와 조각 수(`kChunkPayloadBytes` 로 나눔),
  `saveState`(목록 용량을 이어 쓰는 호출) 한 번의 시간과 sw 할당 수를 줄마다 찍는다. 시계 해상도(100 ns)보다 한 번이 짧아 표본 하나를 2000 번 부르고 표본 5 개의 중앙값을 낸다.

  | 스텝 | 가장 큰 회로의 노드 수 | 가장 큰 바이트 | 조각 수 | `saveState` 한 번 | 할당(10000 호출) |
  |---|---|---|---|---|---|
  | 0(첫 틱 전, 상태가 비어 있음) | 0 | 15 | 1 | ≤ 60 ns | 0 |
  | 600 | 8 | 243 | 1 | ≤ 60 ns | 0 |
  | 36000 | 8 | 244 | 1 | ≤ 60 ns | 0 |

  가장 큰 회로는 쇼케이스의 `ShowcaseLogic`(노드 8)이고 프리팹은 `Elevator` 101 바이트(노드 2) · `LockedDoor` · `KeycardDoor` 91(노드 3)이 가장 크다. 모두 한 조각이고 `kMaxCircuitBytes`(3,920) 의 약 6 % 안이다.
  36000 스텝(10 분)에서 Delay 의 절대 스텝 번호가 varuint 를 늘려 600 스텝보다 1 바이트씩 커질 뿐 조각 수는 변하지 않는다. `crt` 열(CRT 힙 요청)은 Release 에서 재지 못해 -1 이다. Debug 의 시간은 단언 때문에 의미가 없다.
- **Vulkan VSync 프레임 놓침**(2026-10-06 조사, Release 큐브 100, 165 Hz): 46 회 중 4 회에서 600 프레임 창 안에 한 프레임이 12 ~ 24 ms 였고, DXGI(DX12 · DX11)는 같은 조건에서 회당 한 주기 이하였습니다.
  평소 Vulkan 은 `vkAcquireNextImageKHR` 에서 약 5.2 ms 를 기다립니다(DXGI 는 Present 안에서 기다린다). 느린 프레임은 둘로 갈렸습니다.
  하나는 이미지가 한 주기 늦게 돌아온 것(acquire 약 10 ms)이고, 다른 하나는 GPU 일이 0.3 ms 인 프레임의 펜스가 13 ms 뒤에 신호된 것(창 모드 Vulkan 프레젠트가 합성기를 거치는 경로)입니다. 그 프레임에 엔진 CPU 일은 없었습니다.

### 빌드 속도

- **빌드 시간 기준선은 `Scripts/dev/RunBuildBaseline.py`** — 풀 빌드(별도 폴더) · 헤더 하나 수정 · `.cpp` 하나 수정 · 워크트리 콜드(구성 + 빌드)를 각 3 회 측정해서
  중앙값 표를 내고 머리에 PC 이름 · CPU 를 적는다. 다른 빌드가 돌면 멈춘다. 헤더별 누적 파싱 시간 상위는 `Scripts/lint/report/RunIncludeCost.py`(전 TU `-ftime-trace`, PCH 없이)다.
  표는 같은 PC 의 전후만 견준다. 측정하는 동안은 빌드 슬롯을 독점으로 잡아(`py -3 -m Scripts shared-lock acquire build --owner <이름>`, 공유 슬롯 보유자가 모두 놓을 때까지 기다린다) 다른 에이전트의 빌드를 막는다. 남은 단계는 [빌드 속도 계획](plans/BuildSpeed.md)에 있다.
- **CPU 시간 나눔**(빌드 속도 계획을 세울 때의 Debug `.ninja_log`, 명령 하나당 한 번 센 값이라 시점이 섞였을 수 있다): 일반 컴파일 2,435 s(2,413 TU, 약 85 %), PCH 생성 370 s(86 개, 13 %),
  링크 23 s(85 개, 1 % 미만), 리플렉션 코드젠 40 s(28 개, 1 %)였다. 모듈별 컴파일은 Engine 688 TU 805 s, GameFramework 833 TU 671 s, Editor 143 TU 123 s, Core 84 TU 72 s, 테스트 614 TU 732 s(26 %)였다.
  16 스레드 기준 풀 빌드의 이론 하한은 약 170 ~ 180 초였다. 가장 느린 TU 는 `Engine.cpp` 6.5 초이고, `Physics/Jolt/*` 래퍼가 5.3 ~ 6.7 초로 보인 것은 다른 빌드와 겹쳐 잰 값이었다(조용할 때 각 1.9 ~ 2.5 초).
  `GameObjectManager.cpp` 를 PCH 없이 `-ftime-trace` 로 재면 전체 2.16 초 중 헤더 파싱(`Total Source`)이 1.22 초(57 %)였다.
- **기준선**(2026-10-10, PC `SW` · AMD Ryzen 7 6800H 논리 코어 16, 커밋 `0d054352a`(= main `8653de727` + 스크립트 수정), `Ninja-Debug`, 타깃 `all`, 테스트 제외):

  | 시나리오 | 1 회(s) | 2 회(s) | 3 회(s) | 중앙값(s) |
  |---|---:|---:|---:|---:|
  | 풀 빌드 | 125.9 | 131.5 | 131.7 | **131.5** |
  | 헤더 하나 수정(`Core/Container/vector.h`) | 131.6 | 132.7 | 130.7 | **131.6** |
  | `.cpp` 하나 수정(`GameObjectManager.cpp`) | 7.4 | 7.2 | 7.0 | **7.2** |
  | 워크트리 콜드(구성 + 빌드) | 141.2 | 146.8 | 142.9 | **142.9** |

  헤더 시나리오가 풀 빌드와 같다 — Core 기본 헤더 하나를 고치면 사실상 전 TU 를 다시 컴파일한다(2 단계 헤더 다이어트의 근거).
  워크트리 콜드가 풀 빌드보다 느린 것은 구성 시간이 더해지기 때문이다. Release 는 측정하지 않았다(풀 1 회 192.3 s 만 있다).

- **아키텍처 이음매 0 단계 기준선**(2026-10-25, PC `SW` · Ryzen 7 6800H 논리 코어 16, main `eb2a1fa59`, 새 워크트리, 타깃 `all`(테스트 제외), 빌드 칸 독점 · 다른 빌드 없음, [ArchitectureSeams](plans/ArchitectureSeams.md) 2 절).
  단계 분류는 빌드 전후 `.ninja_log` 차이를 명령 해시로 한 번씩 센 값이다(DLL 과 임포트 라이브러리, 코드젠의 여러 출력을 한 명령으로). 링크 비율 = DLL · EXE 링크 시간 합 ÷ 증분 벽시계(링크는 증분의 마지막 직렬 단계라 벽시계 몫으로 본다).

  | 시나리오 | Debug 벽시계(s) | Debug 명령 · TU | Debug 링크(s, 비율) | Lean 벽시계(s) | Lean 명령 · TU | Lean 링크(s, 비율) |
  |---|---:|---|---:|---:|---|---:|
  | 구성(빈 폴더) | 17.1 | | | 13.4 | | |
  | 콜드 풀 빌드(2 회) | 270.7 · 262.4 → **266.5** | TU 2,733(CPU 3,911 s) · PCH 33(100 s) | 링크 90 개 CPU 205 s(5 %) | 238.6 · 234.0 → **236.3** | TU 2,117(CPU 3,469 s) · PCH 27 | 링크 14 개 CPU 43 s |
  | `Core/Compression/CompressionStream.cpp` | **13.9** | 68 · TU 1 + 리플렉션 코드젠 64(CPU 153 s) | 5.3(38 %) | **7.0** | 10 · 코드젠 6 | 3.2(45 %) |
  | `Engine/.../GameObjectManager.cpp` | **8.3**(1 차 4.5) | 3 · TU 1(2.3 s) | 4.3(52 %, 1 차 60 %) | **3.9** | 3 · TU 1 | 2.4(61 %) |
  | `Engine/.../FrameRenderer.cpp` | **9.4**(1 차 7.9) | 3 · TU 1(3.4 s) | 2.7(29 %, 1 차 34 %) | **6.0** | 3 · TU 1 | 2.3(39 %) |
  | `GameFramework/.../BehaviorTree.cpp` | **2.7** | 4 · TU 1 | 1.2(45 %) | **1.7** | 4 · TU 1 | 0.8(46 %) |
  | `Editor/.../EditorPanelManager.cpp` | **3.0** | 4 · TU 1 | 1.0(32 %) | **1.7** | 4 · TU 1 | 0.7(42 %) |
  | `Games/Shooter3D/Shooter3DGame.cpp` | **2.4** | 3 · TU 1 | 0.3(14 %) | 0.5 | Lean 은 Shooter3D 를 짓지 않음(활성 게임 Empty) | — |
  | 헤더 `Renderer/Frame/FrameRenderer.h`(1,344 줄) | **18.5** | 33 · **TU 27**(CPU 128 s) | 3.7(20 %) | **14.7** | 33 · TU 27 | 3.4(23 %) |
  | 헤더 `GameFramework/.../BehaviorTree.h`(작은 헤더) | **4.7** | 24 · TU 15 + 코드젠 2 | 1.8(39 %) | **4.1** | 13 · TU 7 | 1.2(30 %) |

  증분은 각 3 회 중앙값이다(두 번 잰 줄은 둘째 값 — 같은 PC 에서 증분이 회차마다 ±3 s 흔들렸다). `Engine.dll` 링크 한 번은 2.3 ~ 5.8 s(중앙값 약 2.7 s, 10-16 의 1.9 s 보다 길다)이고,
  `Engine.dll` 26 MB · `Engine.pdb` 125 MB, 링크는 `lld-link /debug /INCREMENTAL:NO` 에 GHASH 없음이다. 모든 증분에 `cmake.verify_globs`(0.2 ~ 0.8 s)가 붙는다.
  `FrameRenderer.h` 팬아웃 27 TU 의 내역: `FrameRenderer*.cpp` 분할 파일 16 개(클래스 자신) + 바깥 11 개(`Engine.cpp` · `EngineSubsystemSet.cpp` · `RenderThread.cpp` · `LiveShaderSubsystem.cpp` · `RenderPacketBuilder.cpp` · Capture 셋 · 에디터 셋 — `EditorViewportToolbar` · `TestRunnerPanel` · `EditorScenarioSteps`). 전부 직접 include 다.
  Core `.cpp` 하나가 13.9 s 인 것은 `Core_objects` 가 `ReflectionParser.exe` 에도 링크되고, 파서 실행 파일이 바뀌면 리플렉션 코드젠 명령 64 개(Lean 6 개)가 모두 다시 돌기 때문이다.
  Dev 기동(Debug `App.exe`, 게임 Empty, DX12, `-gv_screenshot=<경로> -gv_screenshotQuit=1`, `Startup phases:` total, 데우기 1 회 5,850 ms 뒤 5 회): 4,128 · 2,926 · 8,330 · 5,869 · 5,290 ms → **중앙값 5,290 ms**(거의 전부 `engine` 단계).
  서브시스템 중앙값: `ModuleTypes` 2,150 · `FrameRenderer` 815 · `Audio` 767 · `Resource` 396 · `RHI` 275 · `Reflection` 88 ms(`FrameRenderer` · `RHI` 는 한 회에 3,470 · 1,870 ms 로 튄다). 경고 · 오류 0.

- **OS 헤더를 PCH 에서 뺀 효과**(2026-10-11, PC `SW`, Debug, `EngineMinimal.h` 가 `Windows.h` 묶음을 더는 넣지 않음): PCH 없이 `-fsyntax-only` 한 번역 단위의 중앙값(5 회)이 `GameObjectManager.cpp` 2.63 → 2.18 s ·
  `Scene.cpp` 2.74 → 2.24 s · `AudioClip.cpp` 2.56 → 2.11 s · `AnimationClip.cpp` 2.66 → 2.06 s(약 −19 %, TU 하나에 −0.5 s). PCH 를 켠 개발 풀 빌드는 131.5 → 127 s(−3 %)로 작다 — 이득은 PCH 를 끄는 구성(Windows CI)에서 크다.
  `Scripts` 없이 측정하는 법: `compile_commands.json` 의 명령에서 `/Yu` · `/Fp` · PCH 강제 포함 · `/Fo` 를 빼고 `-fsyntax-only -include Source/Engine/pch.h` 를 붙여 5 회 돌린다.
- **`StdHeaders.h` 다이어트 효과**(2026-10-11, PC `SW`, Debug, 세 슬롯을 잡고 다른 빌드 없음): 스트림 · `<random>` · `<regex>` · `<future>` · `<variant>` · `<condition_variable>` · `<csignal>` ·
  `<queue>` · `<stack>` · `<bitset>` 을 빼고 쓰는 파일이 직접 include(`MathUtil::getRandom` 의 엔진은 `MathUtil.cpp` 로). PCH 없이 `-fsyntax-only` 5 회 중앙값: `GameObjectManager.cpp` 1.36 → 1.03 s ·
  `Scene.cpp` 1.34 → 1.19 s · `AudioClip.cpp` 1.17 → 0.94 s · `AnimationClip.cpp` 1.13 → 0.92 s(합 5.02 → 4.08 s, −19 %). 실제 컴파일(`-ftime-trace`, 1 회) 합 8.39 → 7.57 s, 헤더 파싱 비율 53 → 51 %.
  (위 OS 헤더 항목의 2.18 s 와 이 표의 1.36 s 차이는 그때 다른 빌드가 겹친 탓이다 — 같은 세션의 전후만 견준다.)
  헤더 시간의 바닥: UCRT `wchar.h` → clang `intrin.h` → `immintrin.h`(AVX-512 하위 헤더 전부)가 TU 하나에 약 0.25 s 다(`__SCE__` 가 아니면 늘 전부 읽는다 — 엔진이 피할 수 없다).
  명령 다루는 법: 정규식으로 토큰만 지운다(문자열을 다시 쪼개면 `-DSW_LOG_TAG=\"…\"` 의 따옴표가 깨진다). `-ftime-trace` 는 구문 검사만으로는 파일을 쓰지 않으므로 `/Fo<임시>.obj` 로 실제 컴파일한다.
- **킷 PCH**(2026-10-11, PC `SW`, Debug): 첫 시도(131.1 → 113.0 s, PCH 컴파일 154 → 94)는 실제로는 킷이 **PCH 없이** 지어진 결과였다 — `REUSE_PRECOMPILE_HEADERS_FROM`
  속성이 무시돼 킷 PCH 60 개를 안 지은 이득이 킷 TU 약 420 개의 헤더 파싱 손해보다 컸다. 진짜 공유(`sw_useSharedPch`)를 같은 날 별도 폴더(`build/Ninja-Debug-Bench`)에서
  `--clean-first` 교차 3 쌍으로 측정했다: 공유 117.5 · 115.2 · 111.7 s(중앙값 **115.2**) · PCH 없는 킷 137.3 · 135.8 · 133.2 s(중앙값 **135.8**) — −20.6 s(−15 %).
  에이전트 빌드가 겹친 측정은 편차가 ±40 s 라 쓸 수 없다(`busy_before` 로 겹침을 먼저 센다).
  `.ninja_log` 는 `--clean-first` 뒤에도 이전 실행 항목이 남아 CPU 합을 그대로 더하면 틀린다.
- **임포트 라이브러리를 바뀔 때만 갱신한 효과**(2026-10-16, PC `SW`, Debug, 다른 빌드 없음): `.cpp` 하나(`GameObjectManager.cpp`)를 고친 증분 빌드가 **13.9 s(86 단계) → 4.9 · 5.2 · 5.5 s(3 단계)**.
  이전 13.9 s 는 컴파일 1.6 s + `Engine.dll` 링크 1.9 s + 킷 · 게임 · 에디터 DLL 84 개 재링크(링크 풀 동시 4)였고, `Engine.lib` 는 바이트까지 같은데 수정 시각만 바뀌고 있었다(`Get-FileHash` 로 확인).
  `Engine.dll` 을 층으로 쪼개는 안은 링크가 1.9 s 뿐이라 이 목적으로는 이득이 없다.
- **PCH 에서 자주 바뀌는 헤더를 뺀 효과**(2026-10-16, PC `SW`, Debug, `Component.h` · `SceneComponent.h` · `GameObject.h` · `Scene.h` · `SceneManager.h` · `RHITypes.h` 를 `pch.h` 에서 뺌, 쓰는 파일 열한 개에 include 를 더함):
  헤더를 건드린 증분 빌드(컴파일한 TU 수) — `GameObject.h` 131.6 → **68.6 s**(618), `Component.h` → **76.9 s**(662), `RHITypes.h` → **47.5 s**(332), `Scene.h` → **14.0 s**(59). 이전 값 131.6 s 는 Core 헤더 기준선과 같은 전체 재컴파일이다.
  의존 TU 수(`/clang:-M`, 테스트 포함 3,230 중): `Component.h` 3,094 → 848, `GameObject.h` → 784, `RHITypes.h` → 512, `SceneComponent.h` → 403, `Scene.h` → 164, `SceneManager.h` → 143.
  비용: 풀 빌드(`--clean-first`)는 원래 PCH 176 · 161 s 대 줄인 PCH 185 · 214 · 198 s 로 **약 +15 %**(이 PC 가 이날 기준선 131 s 보다 느려 절대값은 다르다 — 같은 날 번갈아 잰 값끼리만 견준다).
  PCH 를 끈 CI 구성은 원래 이 헤더를 TU 마다 읽으므로 변화가 없다. Debug · Shipping `all AllTests` 경고 0 · 오류 0. 실패한 파일은 열한 개(include 누락)뿐이었다 — `ninja -k 0` 으로 한 번에 모았다.
  큰 팬아웃은 Reflection 헤더(`ReflectionTypes.h` · `TypeRegistry.h` · `ReflectionCore.h`, 후보 PCH 에서도 TU 1,150 ~ 1,250 개)다.
- **구성별 풀 빌드**(2026-10-11, PC `SW`, Debug, 킷 PCH 공유 뒤, `build/Ninja-Debug-Bench` 에서 `--clean-first`, 세 슬롯을 잡고 다른 빌드 없음):

  | 구성 | 회차(s) | 중앙값(s) |
  |---|---|---:|
  | 개발(PCH, 유니티 끔) | 117.5 · 115.2 · 111.7 | **115.2** |
  | PCH + 유니티 | 145.2 · 146.2 · 144.8 | **145.2** |
  | 유니티만(PCH 끔) | 150.0 · 144.6 | **147.3** |
  | 둘 다 끔 | 295.5 · 283.7 | **289.6** |
  `cmake --preset` · `--clean-first` 는 공유 vcpkg 설치 폴더를 다시 깔 수 있으니 `py -3 -m Scripts shared-lock acquire vcpkg` 를 쥐고 측정한다(다른 쪽이 설치 중이면 헤더가 사라져 빌드가 진다).
- **헤더 파싱 비용 상위 20 개는 `Scripts/lint/report/RunIncludeCost.py --preset Ninja-Debug --jobs 8` 로 뽑는다.**
  0 단계 집계(2026-10-10, 위와 같은 PC · 커밋, `RunIncludeCost.py --preset Ninja-Debug --jobs 8`, 다른 빌드와 겹쳐 돌았으므로 순위만 믿는다):
  TU 1936 개 중 헤더 파싱 합 2619.9 s / 컴파일러 합 3190.5 s = **82 %**(구문 검사만이라 진짜 빌드 비율보다 높다).
  테스트를 짓지 않은 트리라 테스트 TU 372 개는 코드젠 산출물이 없어 구문 검사가 졌다 — 테스트까지 넣으려면 `--target all AllTests` 로 지은 뒤 다시 돌린다.

  | 순위 | 누적(s) | TU | 평균(ms) | 헤더 |
  |---:|---:|---:|---:|---|
  | 1 | 2376.3 | 1323 | 1796.1 | `Source/Engine/pch.h` |
  | 2 | 1709.1 | 1426 | 1198.5 | `Source/Core/CoreMinimal.h` |
  | 3 | 891.4 | 1426 | 625.1 | `Source/Core/Common/StdHeaders.h` |
  | 4 | 682.6 | 1426 | 478.7 | `Source/Core/Common/Defines.h` |
  | 5 | 681.7 | 1426 | 478.1 | `Source/Core/Common/Types.h` |
  | 6 | 679.8 | 1426 | 476.7 | `MSVC/include/string_view` |
  | 7 | 666.9 | 1426 | 467.7 | `MSVC/include/__msvc_string_view.hpp` |
  | 8 | 593.8 | 1355 | 438.2 | `Source/Core/Common/PlatformOsHeaders.h` |
  | 9 | 590.2 | 1336 | 441.8 | `Source/Engine/EngineMinimal.h` |
  | 10 | 590.1 | 1426 | 413.8 | `MSVC/include/iosfwd` |
  | 11 | 587.3 | 1326 | 442.9 | `Source/Engine/Common/Common.h` |
  | 12 | 535.1 | 1426 | 375.2 | `MSVC/include/cwchar` |
  | 13 | 533.9 | 1426 | 374.4 | `WinSDK/ucrt/wchar.h` |
  | 14 | 509.6 | 1426 | 357.4 | `clang/intrin.h` |
  | 15 | 503.4 | 1426 | 353.0 | `clang/x86intrin.h` |
  | 16 | 483.8 | 1426 | 339.3 | `clang/immintrin.h` |
  | 17 | 333.8 | 1355 | 246.4 | `WinSDK/um/Windows.h` |
  | 18 | 205.9 | 1355 | 151.9 | `WinSDK/um/Unknwn.h` |
  | 19 | 164.7 | 1357 | 121.4 | `WinSDK/um/ole2.h` |
  | 20 | 145.6 | 1357 | 107.3 | `WinSDK/um/objbase.h` |
- **Jolt 백엔드 TU**(`Physics/Jolt/*.cpp` 다섯, 같은 PC, 컴파일 DB 명령 그대로 3 회 중앙값의 합): 자기 PCH 없이 11.1 s(그중 헤더 파싱 7.0 s, 엔진 `pch.h` 만 5.8 s) →
  `EngineJolt_objects` PCH 2.9 s + TU 2.6 s = 5.5 s. 한 파일 수정 뒤 다시 짓는 시간은 TU 하나당 1.9 ~ 2.5 s → 0.3 ~ 0.8 s 다.
  측정하는 법: `/clang:-ftime-trace` 를 `-c --` 앞에 넣는다(뒤에 두면 파일 이름으로 읽힌다). 다른 빌드가 돌 때 측정한 값은 1.5 배까지 흔들린다.

## 2-1. 부하 검증

부하 시나리오 × 하드웨어 프로필을 Release 로 여러 번 돌려 p50 · p99 · max 를 **예산**(제품 목표, `Test/Load/Budgets.json`)과 **기준선**(이 기계의 회귀, `Test/Load/Baselines.json`)에 견주고,
진 조합은 어디(CPU 게임 스레드 · 렌더 스레드 · GPU · 메모리 · IO · 서버)가 예산을 깼는지 적습니다. 결정과 근거는 [결정 기록](09_Decisions.md) 5-2 끝입니다.

```powershell
py -3 -m Scripts load list                                       # 시나리오 · 프로필 · 묶음(Test/Load/Suites.json)
py -3 -m Scripts load run renderbench --profile Mid LowEnd --runs 3
py -3 -m Scripts load suite baseline --runs 3 --record           # 이 기계의 기준선을 새로 뜬다
py -3 -m Scripts load scale renderbench --profile LowEnd         # 예산을 지키는 가장 높은 품질 단계
py -3 -m Scripts load trend renderbench --metric GT.Frame.p99_us # <빌드>/LoadResults/trend.jsonl
ctest --test-dir build/Ninja-Release -L load                     # 부하 스위트(벤치 · 클러스터) — 시간 예산은 Release 에서만 판정
```

- **시나리오.** 자동화 시나리오에 `<Load clients rampUpSeconds durationSeconds network meshes movers/>` 한 줄을 더한 것이 부하 시나리오입니다(`Resource/**/automation/load/` — `AppScenarioTest` 가 줍지 않는 폴더).
  엔진이 시작 프레임에 측정 창을 열고, 창이 닫히면 보고와 함께 끝납니다(`<Pass/>` 를 두지 않는다). 서버 쪽은 `Test/Load/Scenarios/*.load.xml` 의 `<LoadScenario driver="loadbot|cluster">` 가 같은 `<Load>` 를 씁니다 —
  `loadbot` 은 OnlineLoadBot(`--local-server`), `cluster` 는 EngineTest 의 부하 스위트 `NetSimLoadTest`(루프백 클러스터, 끝에 모든 클라이언트의 상태 해시가 서버와 같은지)입니다.
- **하드웨어 프로필**(`Test/Load/Profiles.json`): CPU 코어 · 메모리는 Job Object, 엔진 워커 수는 친화도를 따르고(로그 `Initialized with N worker threads`), VRAM · 디스크 · 회선 · 해상도 · 품질은 스위치입니다.
  소프트웨어 제한이라 결과에 `emulated` 가 붙고(표의 `*`), 기준선 키가 실제 기기 측정(`Native`)과 갈립니다. 클럭 · 캐시 · GPU 성능은 흉내 내지 않습니다 — GPU 숫자는 해상도 · 품질 차이만 반영합니다.
- **지표.** 프로파일 표(`GT.Frame` · `RT.Frame` · `GPU.Frame` · `RT.BeginFrame` · `GT.Packet.submit` …의 p50 · p99 · max), 히치 꼬리(`[Profile] hitch` — 최악 프레임 다섯과 16 · 33 · 100 ms 를 넘은 프레임 수),
  `alloc.per_frame`, `gpu_memory_mb`, `startup_ms`, 밖에서 뜬 `private_mb.max` · `handles.max` 와 소크 기울기(`private_mb_per_minute` · `handles_per_minute`, 워밍업 1/4 제외 최소제곱), 봇의 동작별 p99 · `error_percent`, 클러스터의 `tick_p99_us` · `converged`.
- **판정.** 여러 판(기본 3)의 가운데 값으로만 견주고 최소..최대를 같이 적습니다. 기준선은 p50 15 % · p99 35 % + 바닥(150 us · 32 MB)을 넘으면 회귀이고, max · 히치 수는 기준선과 견주지 않고 예산으로만 봅니다.
  Release 가 아니면 숫자만 적고 판정하지 않습니다(`--allow-debug` 로 덮는다). 판마다의 출력 전체는 `<빌드>/LoadResults/logs/`, 결과는 `<빌드>/LoadResults/<시각>.json` 입니다.
- **병목 갈래**는 같은 판의 하위 구간 비율로 가릅니다 — `RT.BeginFrame` 이 `RT.Frame` 의 절반을 넘으면 GPU(펜스 대기), `GT.Packet.submit` 이 `GT.Frame` 의 절반을 넘으면 게임 스레드가 렌더 스레드를 기다린 것, 아니면 가장 무거운 `GT.*` 구간을 붙입니다.
- **측정하기 전에** 다른 빌드 · GPU 실행이 없는지 봅니다 — `py -3 -m Scripts shared-lock acquire gpu --owner <이름>`(독점 — 공유 보유자가 다 놓을 때까지 기다린다)을 쥐고 재며, 다른 에이전트의 빌드가 돌면 기다립니다. VSync 는 기본으로 꺼져 있습니다(`display.vsync`).
- **디스크 흉내는 장치 하나 · 큐 깊이 1 입니다**(고해상도 마감 대기 — `sleep_for` 는 Windows 타이머 주기 15.6 ms 로 반올림돼 1 ms 와 8 ms 를 구분하지 못했다). 기동은 대략 `Native 기동 + 열기 수 × 지연` 이고,
  열기 수는 결과의 `startup_file_opens`(로그 `startup … ms, … file opens`)입니다. 어디서 읽는지는 `--app-arg=-gv_ioLogReads=1` 로 돌려 판 로그의 `[IO] open <경로> bytes=… thread=…` 를 봅니다(측정 창 안의 줄이 히치 후보).
- **`--app-arg` 로 진단 스위치를 App 에 더 넘깁니다** — `-gv_ioLogReads=1`, `-gv_profileAllocSites=12`(Release 로도 콜스택이 나온다). 진단을 켠 판은 기준선을 남기지(`--record`) 않습니다.
- **aiskirmish 의 `alloc.per_frame` 은 워커 수가 아니라 측정 창의 프레임 수를 따라갔습니다** — 고정 델타라 프레임이 많을수록 게임 시간이 길어 유닛이 늘었다. 할당 자리를 견줄 때는 `wall.frames` 도 같이 봅니다.
- **aiskirmish 는 StarSkirmish 가 든 빌드가 필요합니다** — Release 프리셋은 활성 게임만 지으므로 `cmake --preset Ninja-Release -DSW_GAMES=StarSkirmish` 로 구성합니다. 없으면 그 조합은 건너뜀(77)입니다.
  OnlineLoadBot 은 Windows 에서 키트 DLL 사본과 함께 `ToolBin` 에 있고 작업 폴더는 `Bin` 입니다.

**첫 기준선**(2026-10-15, PC `SW` · AMD Ryzen 7 6800H 16 스레드 · Radeon 내장 GPU, Release dx12 · VSync 꺼짐, 커밋 `wt/loadtest`, 조합마다 3 판의 가운데 값(최소..최대), `*` 는 흉내 낸 프로필):

| 시나리오 × 프로필 | 워커 | GT p50 / p99 (us) | RT p99 | GPU p99 | 최악 프레임 (us) | 할당/프레임 | 사유 메모리 (MB) | GPU 메모리 (MB) | 기동 (ms) |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|
| renderbench(큐브 8000 · 무버) × Native | 14 | 983 / 1441 (1310..1703) | 1441 | 983 | 19618 (4736..20216) | 6 | 215 | 57 | 634 |
| renderbench × HighEnd* | 10 | 1048 / 1572 | 1703 | 1179 | 3362 (3200..3717) | 6 | 221 | 64 | 601 |
| renderbench × Mid* | 4 | 851 / 1310 (1310..1441) | 1441 | 917 | 2254 (2237..3561) | 6 | 204 | 48 | 6449 |
| renderbench × LowEnd* | 2 | 655 / 1441 (1441..1572) | 1835 (1835..2097) | 1179 | 2899 (2269..2943) | 6 | 186 | 32 | 6816 |
| aiskirmish(AI 두 진영) × HighEnd* | 10 | 786 / 1441 (1310..1441) | 1310 | 720 | 14386 (12567..14472) | 19 | 203 | 62 | 649 |
| aiskirmish × Mid* | 4 | 589 / 1048 | 1048 | 589 | 19192 (17608..20545) | 34 | 185 | 47 | 6329 |
| aiskirmish × LowEnd* | 2 | 458 / 786 (786..851) | 720 | 425 | 25153 (15918..26516) | 60 | 167 | 31 | 6893 |
| soak(큐브 2000, 10 분) × Native | 14 | 720 / 1179 | 1310 | 786 | 13608 | 6 | 196(+0.07 MB/분) | — | — |

| 서버 시나리오 × 프로필 | 지표(가운데 값, 최소..최대) |
|---|---|
| connect(봇 300 · 5 초 램프업 · 30 초) × HighEnd* · Mid* · LowEnd* | login p99 13.1 · 13.2 · 15.9 ms, chat_send p99 21.1 · 18.7 · 21.2 ms, matchmaking_queue p99 19.0 · 18.9 · 22.8 ms, 오류 0 %, 동작 4200, 사유 메모리 63 MB |
| replication(클라이언트 8 · lte · 12 초) × HighEnd* · Mid* · LowEnd* | 틱 p50 2.9 · 3.0 · 3.2 ms, p99 9.2 (8.5..10.1) · 9.3 · 10.3 ms, max 45 · 43 · 44 ms(서버와 클라이언트 월드 아홉을 한 스레드에서), 수렴 1(상태 해시 일치) |

  흉내는 클럭을 줄이지 않아 코어가 적은 프로필의 프레임이 오히려 짧습니다(경합이 준다). 이 표에서 프로필 차이로 읽을 수 있는 것은 워커 수 · 기동(디스크) · 히치 꼬리 · 메모리입니다.
  이 표의 Mid · LowEnd 기동은 디스크 흉내의 타이머 반올림이 부풀린 값입니다(아래 표).

**`C-29` 최적화 전후**(2026-10-15, 같은 PC · Release dx12, 조합마다 3 판의 가운데 값(최소..최대)): 디스크 흉내 정밀화, `.meta` 스캔을 도메인 셋으로, StarSkirmish 유닛 모델 미리 읽기(`MeshPreloadSet`),
`RTSWorld::findAutoTarget` 의 지역 목록을 스크래치로. 후 값은 `Baselines.json` 에 기록한 판입니다.

| 시나리오 × 프로필 | 기동 (ms) 전 → 후 | 파일 열기 | 최악 프레임 (us) 전 → 후 | 16 ms 넘은 프레임 | 할당/프레임 |
|---|---|---|---|---|---|
| renderbench × Native | 702 → 625 | 385 → 316 | 19536 (14065..20001) → 14278 (13711..14341) | 1 → 0 | 6 → 6 |
| renderbench × Mid* | 6209 → 1139 | 385 → 316 | 12170 (2572..14079) → 14132 (14111..14369) | 0 → 0 | 6 → 6 |
| renderbench × LowEnd* | 6906 → 3664 | 385 → 316 | 2628 (2185..2979) → 18667 (13465..19852) | 0 → 1 | 6 → 6 |
| aiskirmish × HighEnd* | 628 → 848 (618..1093) | 394 → 309 | 14404 (14007..14555) → 14089 (6521..14157) | 0 → 0 | 20 → 6 |
| aiskirmish × Mid* | 6461 → 1114 | 394 → 309 | 18632 (17173..19571) → 14301 (14281..14655) | 2 → 0 | 35 → 5 |
| aiskirmish × LowEnd* | 6976 → 3550 | 394 → 309 | 25805 (20784..26055) → 4136 (3909..4322) | 3 → 0 | 65 → 5 |

  최악 프레임의 13 ~ 20 ms 한 프레임은 디스크 · 이 변경과 무관합니다 — 측정 창 안 파일 열기가 0 이고, 디스크 흉내를 끈 LowEnd 도 14.0 ms 였으며, `.meta` 변경만 뺀 같은 빌드와 3 판씩 견준 가운데 값이 3.2 ms 로 같았습니다(판마다 나오다 말다 한다 — 후보 `RuntimeQualityAdjust` ②).
  기동의 나머지 열기는 개발 배치에만 있는 것입니다 — 키트 DLL 섀도 복사 61 · 모듈 매니페스트 70 · 셰이더 원본 해시 약 30(Shipping 은 셋 다 없다, 후보 `RuntimeQualityAdjust` ①).
  부하 스위트의 시간 예산(`SW_TEST_TIME_BUDGET`)은 같은 날 Release 세 번 중 최대 케이스 시간의 네 배입니다(`TaskManagerBenchTest` 3.2 s · `PackReadBenchTest` 3.1 s · `NetSimLoadTest` 2.5 s 가 가장 길다).
- **CI 야간 잡**(`.github/workflows/load.yml`, 리눅스 `CI-Release`)이 `ctest -L load` 와 묶음 `nightly`(봇 · 클러스터 — GPU 가 없어 App 드라이버 제외)를 돌리고 진 조합을 주석으로 올립니다(`CiFailureReport.py load`). 러너는 측정 기계가 아니므로 기준선은 알림뿐입니다. 첫 실행(2026-10-15, 실행 38813564584)은 connect × Mid, replication × Mid · LowEnd 가 통과했고 tick p99 는 약 15 ms 였습니다. 10-18 이후의 두 실행(39070069934 · 39167082107)은 결과 JSON 없이 "the load run did not finish" 로 졌습니다: 봇이 보고서를 남기지 못하면 `readJsonInternal`(필수 표용, 없으면 예외)이 멈춰 JSON 이 통째로 사라졌고, 이제 보고서 없음은 그 판의 실패로 기록됩니다. 봇이 보고서를 못 남긴 이유(러너에서 즉시 끝남)는 다음 야간 실행의 주석(`exit` 와 오류 줄)으로 읽습니다 — 로그를 못 읽어 추측입니다.

## 3. 검증할 때의 함정

테스트를 쓸 때의 규칙(스위트 이름, 도우미, 단언, 샤드, 골든 기준, EditorTest · CoreTest 의 제약)은 [Test README](../Test/README.md) "단언과 도우미" 와 "함정과 주의" 절이 원본입니다.
이 절은 변경을 확인하고 결함을 좁힐 때의 함정만 다룹니다.

- **`-gv_fixedFrameDelta=<초>` 는 프레임마다 그 시간만 흘린다**(벽시계 무시) — 시나리오 · 픽셀 비교 · 벤치 재현이 기계와 상관없이 같은 게임 시간을 본다.
  App 의 종료 코드는 `Engine::requestQuit( code )` 가 정한다(0 성공, 시나리오 10 실패 · 11 읽기 오류 · 12 시간 초과 · 13 건너뜀).
- **실기동 확인은 시나리오로 남긴다** — `Resource/<영역>/automation/*.scenario.xml` 을 두면 그 게임 프리셋의 `ctest -L hostgpu`(`AppScenarioTest`)가 백엔드마다 돈다
  엔진 · 활성 게임의 시나리오는 시나리오 × 백엔드마다 독립 케이스 `AppScenarioTest.<시나리오>.<백엔드>`(백엔드 `dx11` · `dx12` · `vk` · `gl`, 배포본 `default`)다.
  실행 시작에 폴더를 훑어 만들므로(`SW_TEST_CASE_GENERATOR`) 파일을 두면 케이스가 생기고 목록을 적지 않는다. 한 시나리오는 `--test_filter=AppScenarioTest.vfxgpulights.*`,
  한 백엔드는 `--test_filter=AppScenarioTest.*.gl` 로 돌리고, 하나가 져도 나머지는 돈다. 실패 메시지에는 시나리오 · 백엔드 · 종료 코드와 그 뜻(10 단언 실패 · 15 처리하지 않은 Warning/Error 줄 · 12 시간 초과 …)과
  보고의 `failures[]` · `unhandledLogs[]` 줄이 든다. 같은 이름의 시나리오가 둘이면 폴더까지 넣은 이름(`game_empty_automation_foo`)을 쓴다.
  같은 빌드가 지은 게임 팩(`Bin/Games/<게임>/`)의 시나리오는 `-game=<게임>` 으로 도는 `AppScenarioTest.<게임>.<시나리오>.<백엔드>` 다(Shipping 에는 없다. 빌드의 활성 게임은 뺀다 — `-game=<활성 게임>` 은 인자 없는 실행과 같게 풀려 활성 게임 케이스가 이미 같은 판을 돈다). 한 게임은 `--test_filter=AppScenarioTest.HarvestValley.*`.
  `<Record>` 를 가진 시나리오(엔진 · 활성 게임 · 게임 팩)의 백엔드끼리 기록 비교는 `AppScenarioTest.RecordsMatchAcrossBackends` 가 한 번 더 모든 백엔드로 돌아 본다.
  백엔드 루프가 있던 `AppScenarioTest.RecordedPlayReplaysOnEveryBackend` · `ChainedScenariosShareOneProcess` 와 `AppSmokeTest.EveryBackendStartsRendersAndExitsCleanly` · `EditorModeStartsAndExitsCleanly` ·
  `BackendSwapFollowsTheNewDevice` · `BackendSwapRebuildsGameBeforeEditor` · `BenchFrameMatchesGoldenImage` 도 `<케이스>.<백엔드>` 로 갈렸다(`SW_TEST_CASE_PER_PARAMETER`; 한 프로세스를 이어 쓰는 것은 체인 안의 단계끼리라 목적이 그대로다).
  프로세스를 띄우거나 비싼 항목 루프도 같은 매크로로 갈렸다: `ServerBootTest.EveryBuiltGameStartsAsADedicatedServer.<게임>`(목록이 비는 틈은 `BuiltGameListIsNotEmpty`),
  `LoaderFuzzTest.EveryLoaderSurvivesMutatedInput.<대상>`, `CrashReportTest.EveryCrashKindLeavesAReport.<방식>`. 저장소 파일을 훑는 `Every…` 데이터 검사(수 ms ~ 3 s)는 항목 이름이 이미 실패 메시지에 있어 한 케이스로 둔다.
  GPU 디바이스 케이스(`RHIDeviceTest` · `RenderPassGPUTest`, `RHIBackendSweep` 으로 백엔드를 돌던 것 110개)도 `<케이스>.<dx11|dx12|vk|gl>` 로 갈렸다 — `test::listRHIBackendParameter` 를 목록으로 쓰고,
  케이스 안의 `RHIBackendSweep` 이 그 케이스의 백엔드 하나로 범위를 좁히므로 본문은 그대로다(`--test_filter=RHIDeviceTest.*.vk` 로 한 백엔드, `RenderPassGPUTest.<케이스>.*` 로 한 케이스). 디바이스는 케이스마다 한 번 세우니
  총 생성 횟수는 이전(케이스 × 백엔드)과 같다. 백엔드끼리 결과를 견주는 일곱(`BackendSweepVisitsEveryBackendThatStandsUp` · `TransparentOrderMatchesAcrossBackends` · `FrameRendererParityAllBackends` ·
  `MaterialTexturesAreSampledLinearWrap` · `EngineTextureSlotsAreSampledLinearClamp` · `CanvasDrawsOnEveryBackend` · `MeshModifierGPUTest.ProgramMatchesCPU`)는 목적이 견줌이라 한 케이스로 둔다.
  `AppUITest.DemoScreenMatchesAcrossBackends` 도 같은 까닭으로 한 케이스다.
  에디터 시나리오(`EditorScenariosPassOn<백엔드>`)는 백엔드당 체인 하나(한 프로세스)라 케이스는 그대로고, 실패 메시지의 `chain sub-results:` 가 통과하지 못한 시나리오마다 결과 · 종료 코드 · `failures[]` · `unhandledLogs[]` 를 나열한다(단계별 보고 `chain_editor_<백엔드>.<단계>.json`).
  손 확인 목록을 후보 절에 적지 말고 시나리오를 쓴다 — 못 쓰는 것만 이유와 함께 손 확인으로. 값을 보려면 `Expect probe`(게임 .cpp 의
  `SW_AUTOMATION_PROBE` 한 줄), 그림은 `Screenshot` + `ExpectImage`(지표 값은 보고 JSON 에 늘 적힌다).
  "다르다 · 같다" 는 기준 캡처를 같은 시나리오에서 찍어 `differingFraction`(화소 비율)으로 쓴다 — `differentFrom`(평균 차)은 `near` 만 받는다.
  문서를 여는 시나리오는 `WaitUntil` 이 기본 문서와 **구별되는** 값을 기다려야 한다 — 기본 문서와 같은 값을 기다리면 파일이 뒤늦게 열려 클릭이 어긋난다.
  키 `Tap` 은 사람의 누름처럼 여러 프레임(`hold="6"`)을 누른다 — 한 프레임짜리 누름은 UI 층이 켜지기 전에 떼어져, 누른 키를 같은 프레임 뒤에 읽는 결함(일시정지를 연 Esc 가 Back 으로 읽혀 메뉴가 바로 닫힘, 2026-10-20)을 가린다.
- **게임 로직은 기반 탐침으로 단언하고 결정성은 백엔드끼리 비교한다** — 입력은 액션 이름(`Action` · `ActionAxis`), NPC 일정은 `AdvanceGameTime` + `Schedule.*`, AI 는 `AI.State` + `ExpectVisited`,
  "N 초 안에" 는 `WaitUntil timeoutSeconds`. 시나리오 끝에 `<Record probe="Scene.TransformHash"/>` 같은 기록을 두면 `AppScenarioTest` 가 네 백엔드의 `records` 를 비교해 다른 줄을 로그로 낸다.
  녹화(`-scenario-record` · `-scenario-record-probes`)를 다시 돌려 같은 검사점이 나오는지는 `AppScenarioTest.RecordedPlayReplaysOnEveryBackend` 가 본다. 형식은 `Source/Engine/Automation/README.md`.
  시나리오는 선언하지 않은 `[Warning]` · `[Error]` 한 줄 · 빠진 현지화 키 하나로 진다(로그 게이트). 일부러 내는 줄은 그 시나리오에 `<ExpectLog id="…">`(id 없는 줄만 `contains="…"`; 린트 `CheckExpectLogPhrases` 가 id · 문구가 소스에 있는지 본다 — 문구가 바뀌어 `count="0"` 이 거짓 통과하는 것을 막는다), 고칠 수 없는 줄만 `Resource/engine/automation/allowedlogs.json` 에 이유와 함께 둔다.
  게이트에 걸린 줄은 실패 메시지의 `unexpected log:` 줄과 `Saved/Automation/<이름>_<백엔드>.log` 에 있다.
  창 제목에서 시나리오를 본다 — 시나리오 · 벤치 실행 중 제목은 `<게임 제목> — <시나리오> · <백엔드> · step <번호> <단계> · f<프레임>`(벤치는 `bench <gv_bench 인자> · <백엔드> · f<프레임>`)이다. 에디터도 같다 — 앞부분이 `<게임> — <씬>[*] — SW Editor` 이고 제목은 엔진(`AutomationRunTitleTracker`) 한 곳만 건다. 캡처한 화면의 제목으로 어느 실행인지 읽고, 로그는 `[Scenario] <이름> step <번호> <단계> frame <F>` · `finished <결과> frames <N>` 줄로 같은 정보를 남긴다.
  `materialpreview` 가 GL 체인에서 한 번 진 적이 있다(미리보기 평균 R-B 가 옛 값 — 이후 0/23 재현 없음). 다시 지면 탐침 `Editor.MaterialDocumentIsProbeAsset` · `Editor.MaterialSwitchPending` 의 값으로 문서가 안 바뀐 것인지 계산이 안 따라온 것인지 가른다.
- **시나리오 단계 속성은 앱 없이(nogpu) 먼저 본다** — `AutomationScenarioDataTest.EveryScenarioStepPassesItsValidation` 가 모든 `*.scenario.xml` 의 단계를 등록된 검사 함수(`_pValidate`)로 훑는다(새 시나리오는 자동으로 든다).
  에디터 · App 의 단계는 검사 함수만 ImGui · 렌더를 모르는 파일에 두고 `SW_AUTOMATION_STEP_VALIDATOR` 로 따로 올려(`EditorScenarioStepValidation.cpp` · `AutomationBackendSteps.cpp`) 모듈 없이 본다.
  모듈이 안 올라온 탐침 · 개발 명령 · `Variable` 의 변수 이름은 이름만 받아 주는 대역으로 올려 나머지 속성을 보므로 **이름이 있는지와 탐침의 `target` · `key` 요구는 린트 `CheckNameReferences` 가 소스를 읽어 본다**.
  테스트가 보지 못하는 것은 `[ScenarioData] not seen …` 로그 줄에 이유별로 남는다(통과로 세지 않는다). 준비 함수(`_pPrepare`)에서만 보는 것 — `Action` 의 입력 맵 액션 이름, 누를 수 없는 슬롯, `EditorKey` 의 ImGui 키 이름, `EditorExpectObject` 의 컴포넌트 타입 이름 — 은 GPU 검증의 시작 프레임이 본다.
- **에디터 시나리오 백엔드 사례는 한 체크아웃에서 하나씩 돌린다** — `AppScenarioTest.EditorScenariosPassOn<백엔드>` 는 리소스 트리(`assetmanage/probe.material` 이름 바꾸기 · 새 머티리얼 · `editor/automationtemp/`)를 고친다.
  같은 워크트리에서 두 백엔드를 동시에 돌리면 서로의 파일을 지우고 남겨 assetmanage · chainreloadgame(고아 에셋 경고) · flowterrain · flowimport 가 함께 진다(2026-10-20, 네 백엔드 모두 실패, 하나씩 돌리면 DX12 · Vulkan · DX11 통과).
  GPU 공유 칸의 병렬은 워크트리가 다를 때만 쓰고, 진 뒤에는 `git status` 로 리소스 트리를 되돌린다.
- **결과를 로그로 늦게 내는 백그라운드 작업은 시나리오 시계를 잡는다** — 저장 검증(`EditorAssetValidation`, 파이썬 몇 초)은 저장 뒤 파일을 늦게 읽는다. 시계가 앞서 가면 시나리오의 뒤 단계(임시 폴더 지우기)가 바뀐 트리를 읽히고,
  그 결과(`[prefab-guids] … neither is known`)가 다음 시나리오 창에 떨어져 로그 게이트로 진다(2026-10-20 GL 체인 flowimport → chainreloadeditor). 그런 작업은 `SW_AUTOMATION_CLOCK_HOLDER` 로 등록한다 —
  참인 동안 실행기가 시계를 멈추고 다음 시나리오를 시작하지 않는다(`[Scenario] <이름> held the clock for N frame(s)` 줄).
  답을 내는 백그라운드 색인도 같다 — 콘텐츠 브라우저의 참조 역색인은 보이는 패널의 색인이 마지막 `Resource/` 변경을 따라오기 전이면 `ContentBrowserReferenceIndex` 가 시계를 잡고,
  탐침 `Editor.ReferenceIndexReady` · `Editor.ReferenceCount` 와 이름 바꾸기는 따라온 색인만 쓴다. "한 번 만들었다" 를 준비로 보던 때는 `<Scratch>` 복사 전에 시작한 스캔이 답해
  `assetmanage` 가 `Editor.ReferenceCount` 0 · "fixed 0 referencing file(s)" 로 졌다(2026-10-23 Debug GL 체인 앞부분, 사본 폴더가 없던 실행 4/4 실패 · 남아 있던 실행 0/4 — 고친 뒤 0/5 · 0/3).
- **ThreadSanitizer 는 이 리눅스 환경의 clang 18 에 런타임이 없다** — clang 으로 `-fsanitize=thread` 컴파일하고 링크만 gcc 의
  `/usr/lib/x86_64-linux-gnu/libtsan.so.2` 를 직접 붙이면 돈다(`setarch -R` 으로 ASLR 을 끈다). 일부러 만든 경합을 잡는 것까지 확인했다(2026-10-03,
  `NetworkThreadTest`). 잠금 · 스레드를 바꾼 뒤 그 테스트만 이렇게 돌린다.
- **레이어 때문에 지금 자리가 가장 낮은 합법 자리인 파일 셋**(`ResourcePakCompressionUtil` · `ObjectUndoUtil` · `GPULight.h`)은 README 에
  이유가 있다 — 다시 "잘못 놓였다" 로 옮기지 말 것.
- **에디터 확장의 순서는 등록 순서가 아니라 `SW_EDITOR_*` 의 order 키다.** 등록이 빠지거나 순서가 바뀐 것은 `AppSmokeTest.EditorRegistriesKeepTheirOrder`
  (`-gv_editorRegistryDump=1`, hostgpu)만 잡는다 — 패널 · 메뉴를 고치면 hostgpu 를 돌릴 것.
- **워크트리와 vcpkg 스탬프:** 스탬프 해시는 트립릿 파일(`cmake/Modules/Toolchain/Vcpkg/*-{windows,linux,osx}.cmake`)의 **바이트**로 계산한다. 작업 사본의
  줄끝이 체크아웃과 다르면(git 은 같은 파일로 본다) main 과 워크트리가 서로의 스탬프를 "매니페스트 변경" 으로 보고 configure 마다 vcpkg install 이 돈다.
  워크트리를 만들기 전에 `git diff --quiet` 로 같은지 보고, 다르면 그 파일을 지우고 `git checkout` 으로 다시 받는다.
- **배포 구성(Shipping · Shipping-Server) 빌드 폴더의 `Bin/Packs` 는 활성 게임(empty) 팩만 굽는다** — 오래 쓴 빌드 폴더에는 예전에 구운 다른 게임 팩이 남아 있어,
  다른 게임의 씬 · 프리팹을 읽는 테스트가 그 낡은 팩으로 통과한다. 새 워크트리에서는 진다(2026-10-25 통합 I, `SerializationRoundTripTest` 63 건). 그런 케이스는 맨 앞에
  `SW_TEST_REQUIRES_FEATURE( SourceResources )` 를 두고, `SW_TEST_SKIP`(환경 건너뜀)보다 **먼저** 둔다 — 환경 건너뜀이 남은 스위트는 "아무것도 검증 안 함" 으로 실패한다.
  사람이 기억하지 않도록 `CheckTestSuites` 규칙 10 이 막는다 — 케이스 본문 · 같은 파일 상수에 `game/<활성이 아닌 실제 팩>/` 리터럴이 있고 선언이 없으면 린트가 거절한다(경로 문자열만 다루는 케이스는 게이트 안 데이터로 예외).
- **통합 검증에서 에디터를 한 번은 실제로 띄운다** — Release `-EnableEditor` 에디터 시나리오 하나 × 네 백엔드와 Debug 에디터 시나리오 하나(dx12). Shipping `hostgpu` 에는 에디터가 없고 Debug `nogpu` 는 ImGui 렌더러 백엔드를 띄우지 않아,
  에디터가 기동하자마자 멈추는 결함 둘(백엔드가 에디터 범위보다 먼저 상태 저장소를 찾음, 워커가 에디터 범위를 읽어 Debug 가드에 걸림)이 두 통합 동안 초록 뒤에 있었다(2026-10-25 통합 J).
  Debug 가드(`assertEditorThread`)의 단언 위치는 크래시 스택이 첫 프레임 뒤를 못 풀면 `CallStackCapture::capture` + `symbolize` 를 그 자리에 잠시 넣어 찾는다.
- **Debug App 이 크래시로 끝나면 `Bin/Saved` 에 크래시 묶음이 남고, 다음 `ServerTest` 샤드들이 같은 폴더를 동시에 묶다가 `manifest.json` 을 못 찾는 Error 로 진다**(`ServerBootTest.EveryBuiltGameStartsAsADedicatedServer`). 다시 돌리면 통과한다 — 크래시를 낸 실행 뒤의 첫 실패는 이것부터 의심한다(쓰기 위치 격리는 [HostGpuIsolation](plans/HostGpuIsolation.md)).
- **진단 도구**(`-gv_dumpReflection=…`, `ReflectionParser --dump`, `-gv_dumpRenderGraph=1`, `'A' waits on 'B'`,
  `경로:줄:열: 이유`, `[SW_ASSERT]` stderr, `-gv_screenshot`).
- **렌더 변경은 스크린샷까지 봐야 검증이다.** 로그 · 테스트가 다 초록인데 화면만 찢어진 적이 있다. 정적 씬은 바이트까지 결정적이라 4 백엔드 전후 sha 로 비교한다.
  4 백엔드 대조는 Debug 로(Shipping 은 `SW_SHIPPING_RHI_BACKEND` 하나만 링크하고 `-vk` 를 거절한다).
- **픽셀 지표는 튼튼하게.** "특정 색 픽셀 수" 는 클리어 색 · 톤맵에 무너진다 — 모서리 기준 배경 제거 + 평균(R−B) 대소, 또는 "고유 색 ≥ 2". sin(시간) 구동
  지표는 "달라진 픽셀 수" 로. define 이 GPU 에 닿았는지는 PSO 디스크립터가 아니라 픽셀로 본다. 막히면 PPM 을 덤프한다.
- **D3D12 디버그 레이어 경고는 Debug DX12 로그의 `D3D12 debug layer (severity S, id N)` 줄이다**(앞 16 개만 적고 나머지는 센다 — `IRHIDevice::getValidationMessageCount`). 렌더 변경 뒤 이 줄이 0 인지 본다.
  테스트는 그리기 전후의 수를 비교해 단언한다(`CanvasDrawsOnEveryBackend` · `GPUTimestampsMarkUnwrittenSlotsAllBackends`).
- **`-gv_screenshot` 은 PrintWindow 가 아니다** — PPM 이 원본이다. `-gv_screenshotFrame=N` 으로 프레임을, `-gv_screenshotAttachment=<이름>` 으로 첨부를 고른다.
  게임 화면은 `-gv_screenshotSettledFrames=N`(로딩 화면 · 페이드 · 에셋 읽기가 끝난 뒤 N 프레임, 120 초 안에 자리 잡지 않으면 오류 14)으로 찍는다 — 로딩 화면 최소 표시와 페이드는 실제 시간이라 빠른 기계에서 고정 프레임이 로딩 화면을 찍는다.
- **에디터 UI 는 `-gv_screenshotEditor=<경로>.png`**(`-gv_screenshotEditorFrame=N`) 나 시나리오 단계 `<CaptureEditorUI file="….png" maxMagentaFraction="0"/>` 로 찍는다. 주 출력이 아니라 ImGui 패널 · 플로팅 창까지 든 에디터 화면이고, 창 전경 · 가림과 무관하다(원리는 [에디터 UX 기반](plans/EditorUX.md) 6 절).
  "가끔 튄다" 는 한 장으로 안 잡힌다 — `-gv_screenshotCount=30 -gv_screenshotInterval=2` 로 연속으로 찍어(`_000` … 이 붙는다) 장을 나란히 본다.
- **"조용한 프레임" 버그는 벤치가 가린다**(회전 큐브라 매 프레임 dirty). 에디터 정지 화면으로 본다: `-EnableEditor -gv_benchMeshes=1 -gv_screenshot -gv_screenshotFrame=60`.
  그리는 것이 의심스러우면 `-gv_gpuCulling=false` · `-gv_drawMerge=false` · `-gv_vertexPool=false` 으로 경로를 하나씩 뗀다.
- **백엔드 교체는 시나리오로 재현한다**: `py -3 -m Scripts scenario swapother`(한 번 건너기 — 단계 `SwapBackend`, 시점은 단계의 프레임이 정한다). 투명 큐브가 카나리아다. 텍스처 경로는 `-gv_defaultMaterial=engine/materials/benchtextured.material`.
  전역 변수는 `GlobalVariableInfo::setValueAsInt` 로 넣는다 — **C++ 대입은 변경 콜백을 부르지 않는다.**
- **실제 백엔드는 로그 `Initializing RHI with backend: <이름>` 으로 확인한다**(설정값에 덮여 "네 백엔드 확인" 이 DX12 네 번이던 적이 있다). "인자가 적혔는가" 는
  `CommandLineManager::isArgumentProvided` 로(`getArgument` 는 기본값이 있으면 안 적어도 true).
- **리사이즈는 창을 실제로 흔들어야 검증된다.** 블로킹을 걷을 때는 그것이 무엇을 가리던지 먼저 찾는다(에디터 모드가 DX12 DEVICE_HUNG 을 가렸다).
- **레이스 테스트는 "돌았다" 가 아니라 결과를 대조한다**(직렬 1 판 · 병렬 3 판 평균 ±1 %, 검출률 ~7/8). 이 테스트는 hostgpu 라 CI 가 못 돈다 — 병렬 기록을 건드리면
  로컬 hostgpu 를 여러 번 돌린다. 레이스는 deferred 파이프라인으로 돌려야 드러난다. 겨루는 테스트는 상대가 **돌기 시작한 뒤** 겨루고 "겹친 횟수 ≥ N" 을 단언한다.
- **변이 검사 함정** — 조건을 지우는 변이는 방향에 따라 우연히 통과한다. 백업을 `Copy-Item` 으로 되돌리면 mtime 이 과거라 ninja 가 안 짓는다(touch). 변이한 코드를 태우는
  스위트가 어느 실행 파일에 있는지 먼저 본다. "깨끗하게 실패했는가" 까지 본다(부정 단언 메시지는 `SW_EXPECT_FALSE_MSG`). 성공 쪽이 조용한지도 본다. 저장소 밖
  `mutlib` 은 기준선을 돌리지 않는다 — 테스트가 먼저 통과하는지 확인한다. 변이 뒤에는 전체를 다시 빌드한다(부분 빌드는 모듈 DLL ABI 가 섞인다).
- **새 테스트만 걸러 돌리고 커밋하지 말 것.** 빌드가 깨진 채 돈 ctest 는 **옛 바이너리**로 통과한다. 빌드가 진 프리셋(DLL 잠김 — `Scripts/setup/AddDefenderExclusions.py`)에서
  테스트를 돌리지 말 것.
- **간헐 실패를 다룰 때** — `ConsoleLogOutput` 은 Error 만 flush 해서 ctest `--output-on-failure` 의 마지막 `[ RUN ]` 은 크래시 위치가 아니다. 같은 명령을 직접 돌려 파일로
  받는다. 테스트 실행 파일에도 크래시 핸들러가 있다(`Test/TestFramework/main.cpp`). 단독으로 통과하고 스위트에서만 지면 전역 상태 오염부터(VFS 는 `GlobalVfsScope`).
- **측정은 혼자, 기능 확인은 둘까지.** 빌드 · 스모크 · 벤치 · GPU 스위트를 마구 겹치면 가짜 실패(파일 잠김 · CMake 프로브 레이스 `LNK1107`)와 수치 오염이 난다.
  결정적 설정으로 기능만 보는 시나리오 · `hostgpu` 는 GPU 공유 잠금(`--shared`, 두 프로세스)으로 백엔드를 나눠 함께 돌려도 되고, 성능 측정 · 부하 기준선 · 장치 제거 실험은 독점으로 혼자 돌린다.
  단 같은 빌드 폴더(`Bin`)의 에디터 체인 둘을 함께 돌리지 않는다 — 시나리오 산출물(`Saved/Automation/<시나리오>/` 스크린샷 · 임시 에셋)과 리소스를 고치는 시나리오(`assetmanage`)가 겹친다.
  나눌 때는 빌드 폴더가 다른 둘(워크트리 둘, Debug 와 Shipping)이나 산출물이 겹치지 않는 실행을 짝짓는다.
  샤드를 동시에 돌려도(`ctest -j3`) 시간 제한이 가짜로 진다 — 의심되면 그 샤드 하나만 다시 돌린다. 백그라운드 셸은
  `taskkill /T` 로 트리째 내린다. 창을 만드는 GPU 테스트를 Bash 로 돌리면 타임아웃에 걸린 것처럼 보인다 — PowerShell `Start-Process`.
- **Shipping 에서만 나는 것** — Info 로그 · `SW_ASSERT` 가 사라지고(반응이 로그 한 줄뿐인 기능은 배포본에서 없는 것과 같다), 리소스는 팩에만 있고 `.hlsl` 원본이 없으며,
  쿠킹한 바이너리 씬만 읽는다(씬 · 프리팹 테스트는 `SceneDocument::saveBinary` 로 `.bin` 도 쓴다). 풀 블록을 힙으로 반납하는 손상은 Shipping 에서만 터진다 — 풀 자유 목록이
  LIFO 인 것을 써서 "다음 할당이 같은 주소" 로 검사하면 Debug 에서도 잡힌다.
- **빌드 출력을 `| head` 로 자르지 말 것** — 파이프가 닫히면 빌드가 중간에 죽고 낡은 바이너리가 남는다. 파일로 받은 뒤 본다.
- **Windows ASan 이 `stack-overflow` 만 내고 스택을 풀지 못하면** 후보 함수마다 `fopen` · `fflush` · `fclose` 로 호출을 한 줄씩 남기고(줄 수 상한을 둡니다) 되풀이되는 모양을 봅니다.
- **변경 전후 그림이 다르면 어느 쪽이 맞는지는 네 백엔드의 일치도로 가립니다.** 변경 전 그림이 백엔드끼리 이미 어긋나 있었다면 변경 전은 기준이 아닙니다.
  네 백엔드가 같은 증상을 내면 백엔드 코드보다 그 위의 공유 호출부를 먼저 보고, 한 백엔드만 다르면 그 백엔드의 산출물과 계약부터 봅니다. 같은 변경에서 한 백엔드만 깨지면 배리어 누락(레이스)을 먼저 의심합니다.
- **여러 번 띄우는 하니스는 프로세스를 이름(`taskkill /IM App.exe`)이 아니라 PID 로 내립니다.** 이름으로 내리면 방금 띄운 다음 회차를 죽입니다. 에디터를 켠 실행은 기동이 길어 짧은 종료 대기로는 "종료 중 멈춤" 오탐이 납니다.
  로그의 `[Error]` 를 세는 실행은 강제 종료(`taskkill /F`)하지 않습니다. 비동기 로거의 뒷부분이 사라져 "오류 0" 이 거짓이 되므로 `-gv_profileFrames` 로 스스로 끝나게 합니다.
- **레이스가 의심되면 추론하지 말고 드러나는 구성(`-gv_deferred=1`)으로 Debug 실행해 로그의 `Concurrent … access detected` 건수를 고치기 전후로 셉니다.** "도달 불가" 는 정적 추론이 아니라 진입 횟수 계측으로 확인합니다.
- **"사용처 0 · 죽은 코드" 판정은 테스트 파일까지 직접 grep 하고, 지우기 직전에 한 번 더 확인합니다.** 무관해 보이는 선행 실패는 `git log -S` 로 코드와 테스트 양쪽의 도입 커밋을 대조해 되돌려 본 뒤에만 "선행 실패" 라고 부릅니다.
- **렌더 검증 씬에는 메시 2 종과 머티리얼 2 종 이상을 넣습니다.** 하나짜리 씬은 다중 배치 · 바인딩 경로를 지나지 않습니다. GPU 정렬 · 압축 테스트는 물체마다 보이는 면의 색을 다르게 둡니다(같은 색 · 같은 알파의 층은 순서와 무관해 테스트가 헛돕니다).
- **셰이더 결과가 화면에 안 보이면 이 순서로 좁힙니다.** ① 게이트 값(PSO · CB · UAV 핸들, 인스턴스 수)을 찍는다 ② 셰이더가 무조건 눈에 띄는 일을 하게 한다 ③ CB 멤버를 하나씩 조건으로 건다 ④ 임계값을 쓰기 전에 그 값의 실제 범위를 확인한다.
  시간에 따라 움직이는 기능은 `-gv_screenshotFrame` 을 다르게 준 두 장으로 먼저 캡처 시점이 다른지 봅니다.
- **디바이스 손실(Vulkan `VK_ERROR_DEVICE_LOST`, DX12 `DEVICE_HUNG` · `DEVICE_REMOVED`)이 나면 이 세 단계를 먼저 합니다.** Debug 로 돌립니다.
  ① 결함 주소를 읽습니다. Vulkan 은 `<자리>: VkResult=-4 (VK_ERROR_DEVICE_LOST) — last pass put in the submission order: '<패스>'` 다음 줄의 `fault address 0: read-invalid at 0x…`(VK_EXT_device_fault)를,
  DX12 는 DRED 의 `PageFault VA=0x…` 와 그 아래 `PageFault VA is inside live/recently freed allocation 'SW Buffer <핸들> …'` 를 봅니다. 실행마다 같은 주소이고 주소가 서술자 힙 · 배열 시작 + 약 4 GB(0xFFFFFFFF × 서술자 크기)이면
  무효 bindless 번호로 서술자를 읽은 것입니다(`binding.hlsli` 의 `Bindless::textureSlot` 주석과 머리의 "bindless 입문" 절).
  ② 같은 명령을 `-gv_validateBindlessIndex=2` 로 다시 돌려 `[BindlessIndex]` 줄을 셉니다. `is not a live` 오류가 있으면 그 패스 · 필드가 원인 후보 1 순위이고, 없으면 `= absent` 목록에서 손실 패스의 필드를 고릅니다.
  ③ 후보 하나만 셰이더에서 바꿔(첨자를 배열 안으로 자르기, 값을 상수로) 3 회 이상 다시 돌려 재현율을 견줍니다. 셰이더는 개발 빌드에서 라이브 컴파일되므로 다시 빌드할 필요가 없고, 끝나면 `App --cook-shaders` 로 다시 굽습니다.
- **백엔드 교체 · 장치 제거는 `engine/automation/rhiswap/` 시나리오로 본다**(네 백엔드가 모두 있는 PC 에서만 통과해 AppScenarioTest 가 줍지 않는다). `swapbackends`(네 백엔드 건너기),
  `terrainedits`(저장하지 않은 지형 편집 · 씬 뷰 카메라가 교체를 넘는지), `removedevice`(개발 명령 `rhi.removeDevice` 로 DX12 장치를 제거하거나 Vulkan 장치를 손실 상태로 둔 뒤 프레임 · 종료가 멈추지 않는지 — `-dx12` · `-vk` 로 돌린다).
  교체 전후 그림을 견줄 때는 한 번 먼저 건너 도킹 배치를 안정시킨다 — imgui.ini 가 노드 크기를 정수로 저장해 첫 리로드에서 씬 뷰 너비가 1 픽셀 바뀔 수 있다.
- **미리보기 창 시나리오와 검증 레이어.** `editor/previewreopen`(머티리얼 · 프리팹 창을 연 채 8 번 여닫기)과 `editor/previewswap`(창을 연 채 `SwapBackend other` → `start`)는 GPU 오류를 직접 못 보고 로그로 본다.
  검증 레이어를 켜는 `-gv_` 스위치는 없다 — DX12 디버그 레이어와 Vulkan 검증 레이어는 Debug 빌드에서만 켜지므로(Release 에는 없다) 레이어 오류를 보려면 `build/Ninja-Debug` 로 돌린다. 오류는 `[Warning]` · `[Error]` 줄이라 시나리오의 로그 게이트가 지고,
  손으로 세려면 `Saved/Automation/<이름>_<백엔드>.log` 에서 `D3D12 debug layer` 와 `[Error]` 줄(Vulkan)을 찾는다. 미리보기 타깃의 그림은 `previewswap` 이 저장하는 `Saved/Automation/previewswap_*.ppm` 을 눈으로 본다.

## 4. CI

push 뒤 결과를 기다리지 않는 것은 [작업 방식](11_Workflow.md) 4절의 규칙입니다. 실패를 알게 되면 공개 API 로 실행, 잡, 실패 주석을 읽습니다.

**CI 단계 분류.** 단계마다 시간 예산이 달라 한 곳에 몰지 않습니다. GPU 가 필요한 것은 어느 CI 도 돌리지 않습니다(러너에 GPU · 창이 없음) — 로컬 `hostgpu` 가 맡습니다.

| 단계 | 언제 | 무엇 | 파일 | 예산 |
|---|---|---|---|---|
| 린트 | 모든 push · PR (문서만 바뀌어도) | 게이트 + 셀프테스트 전부(훅이 건너뛰는 트리 전체 게이트 포함 — 아래 "커밋 훅과 린트 잡의 나눔"), 린트가 지면 non-main 은 같은 실행의 빌드를 취소한다 | `ci.yml` `lint` | 약 1 분 |
| 빌드 · `nogpu` | push · PR, 린트와 동시에 시작(바뀐 파일이 문서뿐이면 `changes` 잡이 건너뛰게 한다) | 구성 열 개 — Windows Debug · STL · Shipping · 전용 서버 Shipping, Linux Debug · ASan · TSan · Shipping · 전용 서버 Shipping | `ci.yml` `build` | 15 ~ 30 분(가장 긴 잡이 전체 시간) |
| 매일 컴파일 검사 | 03:00 KST · 수동 | 헤더 단독 컴파일, `SW_ENABLE_DEADLOCK_DETECTION` 구성 컴파일 | `header-self-contained.yml` | 약 30 분 |
| 매일 퍼징 | 03:00 KST · 수동 | 로더 퍼저(ASan) | `fuzz.yml` | 최대 2 시간 |
| 야간 부하 | 04:00 KST · 수동 | Release 부하 스위트 · 봇 · 클러스터 | `load.yml` | 최대 2.5 시간 |

판단 기준: push 마다 돌려도 잡 시간을 늘리지 않는 것만 `ci.yml` 에 둔다. 전 트리를 다시 훑는 검사(14 분 이상)나 평소 어느 프리셋도 켜지 않는 구성은 매일 잡으로 보낸다 —
`SW_ENABLE_DEADLOCK_DETECTION` 확인이 Windows Debug 잡에 있을 때 그 잡이 32 분으로 가장 길었고 14 분이 이 단계였다(옮기면 약 18 분으로 줄 것으로 본다 — 다음 main 실행으로 확인). 새 검사를 더할 때는 위 표에서 줄을 고른다.

**커밋 훅과 린트 잡의 나눔.** 커밋 훅은 staged 파일로 볼 수 있는 게이트만 돈다. 파일 사이를 대조해 트리 전체를 읽어야 하는 무거운 게이트는 훅에서 빼고(`preCommitSkipReason`)
린트 잡이 push 마다 트리 전체로 본다 — `CheckNameReferences`(코드의 정의 ↔ 인자 · 시나리오 · 데이터의 쓰임, 거의 모든 커밋에서 6~7 s 로 훅의 바닥이었다) ·
`CheckKitNamespaces`(트리 전체의 같은 상태 표 · 로그 범주). 린트 잡(`RunLintSuite`)과 `ctest -L lint` 는 훅의 건너뜀과 상관없이 `LintCatalog` 의 목록 전부를 돌린다
(`TestPreCommitLint.testTreeWideCrossFileGatesAreLeftToCi` 가 목록에 남아 있는지 본다). 한계: 이 둘의 위반은 커밋이 아니라 push 뒤 CI 에서 처음 보인다 —
이름 · 상태 표를 바꾸는 커밋은 push 전에 `py -3 -m Scripts gate CheckNameReferences` 를 손으로 돌린다.

```powershell
py -3 -m Scripts ci-jobs --summary                 # main 의 가장 새로 끝난 실행 한 줄(실패한 잡과 연속 실패 수) — 세션 가드가 세션 시작에 부른다
py -3 -m Scripts ci-jobs --runs                    # main 의 최근 실행
py -3 -m Scripts ci-jobs <실행 id> --annotations   # 실패한 잡과 실패 주석
```

- **CI 가 끝까지 돌게 하는 세 가지**(`.github/workflows/ci.yml`). ① main 은 `cancel-in-progress: false` — push 가 실행 시간보다 잦으면 끝나는 실행이 0 건이 된다(10-04 7 시간).
  ② vcpkg 바이너리 캐시는 구성 직후 `actions/cache/save` 로 저장한다 — `actions/cache` 의 post 저장은 잡이 성공할 때만 돌아, 테스트 하나가 지면 1 시간 지은 포트를 버렸다
  (Configure 50~90 분이 매번). 키는 OS 별 하나(트리플릿이 OS 당 하나). ③ 모든 잡이 PCH 를 켠 채 짓는다(로컬과 같은 구성). PCH 가 가리는 include 누락은
  하루 한 번 도는 헤더 단독 컴파일 잡(`header-self-contained.yml`)이 잡는다.
  작업 로그 · 아티팩트는 API 로 403 이라 진 테스트는 주석(annotation)으로 올린다 — 실행 목록 · 잡 단계 · 주석은 로그인 없이 읽힌다(시간당 60 회 한도를 여럿이 나눠 쓴다).
- **리눅스 CI 는 ubuntu-24.04 의 `libclang-dev`(18)다.** 파서에 새 libclang API 를 쓰면 리눅스 잡만 선다 — `CINDEX_VERSION` 으로 가른다. 16 미만은 한정 없이 적은
  타입도 FQN(`sw::editor::Foo`)으로 철자하므로 타입 이름은 `TypeNameMap::normalize` 가 잎 이름으로 맞춘다(`ReflectionParserTest.NestedNamespaceStructPropertyUsesTheLeafName`). CI 러너 파이썬은 ubuntu-24.04 에서 3.12 이지만 최소 지원이 3.10(`kMinimumPythonVersion`)이라
  f-string 식 안의 백슬래시 · 여러 줄 식이 configure 를 죽인다(`CheckPythonMinimumVersion.py`). GH Windows 러너는 cp1252 라 한글을 print 하는 스크립트가 빌드째 죽는다
  (증상: 구성 단계의 파이썬 생성기가 `UnicodeEncodeError` 로 끝난다) — 진입점은 `Scripts/common` 을 **모듈 수준에서** import 한다(UTF-8 stdout, 게이트 `CheckScriptEntryPoints` —
  함수 안의 import 는 치지 않는다: 한 갈래에서만 끌어오면 다른 갈래의 print 가 죽는다). 재현은 `PYTHONIOENCODING=cp1252`(cp949 는 `—` 에서 죽는다).
- **CI 실패는 실패한 잡과 같은 프리셋으로 재현한다.** Debug(Engine SHARED)는 Shipping 정적 링크 결함을 원리상 못 낸다. Windows CI 러너의 TEMP 는 8.3 짧은 이름
  (`RUNNER~1`)이라 경로를 글자로 비교하면 틀린다. 빨간 CI 는 다음 결함을 숨긴다(55 런 연속 실패를 아무도 몰랐다) — 런 상태:
  `curl -s "https://api.github.com/repos/sswgame/LearningTemplate/actions/runs?per_page=30&branch=main"`. 스킵은 실패보다 조용하다.
- **`SW_ENABLE_DEADLOCK_DETECTION` 은 매일 도는 `header-self-contained.yml` 이 `RunBuildWarnings.py --define SW_ENABLE_DEADLOCK_DETECTION --fail-on error` 로 지킨다.** push 마다 돌리지 않으니 이 옵션을 건드린 변경은 같은 명령을 로컬에서 먼저 돌린다.
- **CI 실패는 `Scripts/dev/CiFailureReport.py` 가 주석으로 올린다**(테스트 · 구성 실패의 vcpkg 포트 로그 · 크래시 스택 `Bin/Saved/Logs/crash_*.stack.txt`).
  정기 잡도 같다 — 헤더 단독 컴파일 잡의 빌드 오류(`build`), 퍼징 잡의 새니타이저 보고 · 스택 · 재현 입력(`fuzz`, libFuzzer 가 짧은 입력에 찍는 `Base64:` 줄).
  퍼징 실패는 그 Base64 를 풀어 대상 이름과 함께 `SW_FUZZ_TARGET=<대상> LoaderFuzzer <파일>` 로 재현하고, 고친 뒤 그 입력을 대상의 씨앗(`LoaderFuzzTargets.cpp`)에 더한다.
  `Base64:` 가 비어 있고 재현 입력이 빈 파일(`crash-da39a3ee…`)이면 입력 밖의 죽음이다 — 새니타이저 스택이 `exit` · 하네스 `stop` 아래면 종료 경로(정적 소멸 순서)를 고친다.
  빈 입력 · 경계 입력은 `LoaderFuzzTest.EveryLoaderSurvivesBoundaryInput` 이 모든 대상에 먹인다.
  작업 로그 · 아티팩트는 관리자 전용(API 403)이라 밖에서는 주석만 보인다. 저장소 주인 계정으로 로그인한 `gh` 가 있으면 잡 로그 전문을 받는다 —
  `gh api --allow-escape-sequences repos/sswgame/LearningTemplate/actions/jobs/<잡 id>/logs > job.log`(TSan 보고의 두 스택 · 자식 프로세스 출력이 거기 있다).
  진 테스트 주석의 제목에 결과가 붙는다. `[Timeout 180.0 s / 180 s]` 는 ctest 가 한도에서 죽인 것(멈춤이거나 느려진 것)이고, `[exit 1 after …]` 는 스스로 진 것,
  `[no exit line after …]` 는 한도 전에 종료 줄 없이 끝난 것(크래시)이다. `py -3 -m Scripts ci-jobs <실행 id> --annotations` 의 잡 표는 실패한 단계의 경과 시간 ·
  종료 코드와 ctest Timeout 수를 보인다. 응답은 임시 폴더에 캐시해 끝난 실행을 다시 물으면 API 를 쓰지 않는다(익명 한도는 시간당 60 회).
- **리눅스 CI 에서만 드러난 결함 모양**(Windows 에서는 통과하던 것). libstdc++ 의 `file_clock` 은 기원이 2174 년이라 파일 시각을 다른 시계로 옮길 때 기원을 맞춰야 한다.
  경로 구분자는 POSIX 쪽으로 정규화한다. 헤드리스(서버) 입력에는 게임패드 슬롯과 창 포커스가 없다.
  테스트가 자식 셸을 띄우면 `exec` 를 붙인다 — dash 의 `sh -c` 는 SIGTERM 에 먼저 죽어 종료 코드 143 이 된다.
  단조 시계로 잰 캐시 TTL 은 느린 빌드에서 임대를 풀어 버린다 — 서버 틱 시각을 쓴다. epoll 전송은 멈춘 동안 읽지 않으므로 전송 모델마다 테스트 기대를 나눈다.
  경합 검출기는 비 const `operator[]` 와 공유 읽기 버퍼를 쓰기로 보므로 읽기 경로는 const 로 둔다. ASan 은 스택 객체 수명(OpenSSL 스택)과 테스트 지역 변수의 선언 순서를 잡는다.
- **Windows STL containers 잡은 sw 할당자와 기록 해시에 기대는 테스트를 건너뛴다**(`MemoryTagTest` 둘, `DestructionDamageTest.EventLogHash…`). 기본 컨테이너 구성에서 같은 테스트가 가끔 지는지는 후보 `NightlyCiFollowups` 에서 지켜본다.
