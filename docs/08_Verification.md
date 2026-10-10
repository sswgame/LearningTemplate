# 검증과 측정

변경이 맞는지 확인하는 방법과 성능을 재는 방법을 모은 문서입니다. 무엇을 돌려야 일을 끝낸 것으로 보는지, 측정값을 어떻게 읽는지,
테스트를 쓸 때 무엇을 조심하는지를 다룹니다. 한 모듈에만 해당하는 함정은 그 모듈 README 의 "함정 · 계약" 절에 있습니다.

## 1. 손대기 전에 — 검증 규칙

숫자 기준선(테스트 수 · 창 수 · 시간)은 적어 두지 않는다. 금방 낡는다. **비교가 필요하면 바꾸기 전과 후를 그 자리에서 잰다.**
통과 기준은 "전부 통과, 스킵은 Dev 전용 케이스 · 그 플랫폼에 없는 타깃뿐" 이다.

개발 기본 빌드(`cmake --build --preset Ninja-Debug`)는 시험 실행 파일을 짓지 않는다. 시험까지 지으려면 `--target all AllTests` 를 붙인다.
`ctest` 는 실행 파일 시험 앞에 셋업 항목 `BuildTestBinaries` 를 스스로 돌려 둘을 먼저 짓는다(빌드 없이 돌리려면 `-FS BuildTestBinaries`).
`py -3 -m Scripts test` 도 먼저 짓는다(`--no-build` 로 건너뛴다). CI · 검증 프리셋(`CI-*`, `*-Shipping*`, `Ninja-Debug-ASAN`)의 `cmake --build --preset` 은 시험까지 짓는다.

```powershell
cmake --build --preset Ninja-Debug            # 경고 0 이어야 한다(시험 제외 — 시험까지는 --target all AllTests)
cmake --build --preset Ninja-Shipping         # Debug 가 숨기는 결함이 여기서 드러난다(시험까지)
ctest --preset Ninja-Debug-lint               # 컨벤션 · include 순서 · 린트 자기 시험
ctest --test-dir build/Ninja-Debug -L nogpu --output-on-failure
ctest --test-dir build/Ninja-Shipping -L nogpu --output-on-failure

# CI 가 못 도는 부분 — GPU 있는 PC 에서 일을 끝내기 전에 반드시 돌린다.
# AppSmokeTest 가 네 백엔드 + 에디터(dx12 · gl)를 띄워 종료 코드 0 · 로그 [Error] 0건을 본다.
ctest --test-dir build/Ninja-Debug -L hostgpu --output-on-failure
ctest --test-dir build/Ninja-Shipping -L hostgpu --output-on-failure

# ASan (Windows)
cmake --preset Ninja-Debug-ASAN
cmake --build --preset Ninja-Debug-ASAN
ctest --test-dir build/Ninja-Debug-ASAN -L nogpu

# 정적 분석 — 게이트가 아니라 판단 자료다(Scripts/README.md "함정 · 계약" 의 clang-tidy 참고)
py -3 Scripts/lint/report/RunClangTidy.py [--filter Core] [--clang-tidy <경로>]

# 눈으로 봐야 할 때 — 종료 코드 0, 로그에 [Error] 0건
cd build/Ninja-Debug/Bin
./App.exe -gv_profileFrames=40 -dx12 -EnableEditor
./App.exe -gv_profileFrames=40 -dx11 -EnableEditor
./App.exe -gv_profileFrames=40 -vk   -EnableEditor
./App.exe -gv_profileFrames=40 -gl   -EnableEditor
```

**단위마다 이 순서로 한다.** 시험 먼저(이전 코드에서 지는 것을 확인) → 고침 → 변이 검사(고침을 되돌리면 시험이 지는가) →
Debug · Shipping · hostgpu → 커밋. 변이로 죽지 않는 시험은 아무것도 지키지 않는다.

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
그 씬의 메시는 `_meshId` 가 비어 **화면에 기하가 없다** — 픽셀 비교에는 벤치 큐브(`-gv_benchMeshes=N`)를 쓴다.
다른 씬은 `"-gv_editorStartupScene=<경로>"` 로 연다. **PowerShell 은 점이 든 인자를 쪼갠다 — 따옴표로 감쌀 것.**
씬 · 프리팹 에셋은 **엔진 직렬화기로 만든다**(손으로 쓴 XML 은 깨진다 — [Scene README](../Source/Engine/Scene/README.md) "함정 · 계약" 참고).

**렌더 결과는 픽셀로 잰다.** `-gv_screenshot=out.ppm`(`-gv_screenshotFrame=N`)은 톤맵까지 든 최종 화면이다. 눈으로 보지 말고
배경과 다른 픽셀 수 · 채널 평균을 센다. `-gv_viewMode=<0|1|2>`(Lit · Unlit · Wireframe)는 에디터 없이도 고른다.
**벤치는 `-gv_benchAnimate=0` 이어야 결정적이다**(네 백엔드 0 바이트 차이). 애니메이션을 켠 채 차이를 주장하려면 같은 조건
두 판으로 잡음 바닥을 먼저 잰다.

```powershell
./App.exe -dx12 -gv_benchMeshes=8 -gv_benchAnimate=0 -gv_profileFrames=20 -gv_viewMode=2 "-gv_screenshot=wire.ppm"
```

**함정**

- 백엔드는 `-dx11 / -dx12 / -vk / -gl` 로 고른다. 명시한 백엔드를 쓸 수 없으면 폴백하지 않고 에러로 선다(`RHIBackendUtil::findCommandLineBackend`
  한 자리 — `-gv_rhiBackend=Vulkan`(열거자 이름 또는 숫자, 모르는 이름이면 기동이 멈춘다)도 같은 판정을 지난다). 실제로 뜬 백엔드는 로그 `Initializing RHI with backend:` 로 확인한다.
- 에디터는 `-EnableEditor` 를 줘야 뜬다. 없이 돌린 검증은 에디터 OFF 검증이다 — 로그로 실제로 로드됐는지 본다.
- 테스트 바이너리는 모든 구성에서 `build/<프리셋>/TestBin/` 에 있고 작업 디렉터리는 `Bin/` 이다. 모듈 DLL 은 `Bin/Modules/`, 서드파티 DLL 은 `Bin/` 한 벌, Dev PDB 는 `Bin/Symbols/` 에 있다.
  `Bin/` 에 남은 옛 자리 산출물(테스트 exe · 모듈 DLL · PDB)과 꺼진 모듈의 DLL 은 configure 가 지운다(`sw_removeStaleBinaryOutputs`) — 실행 중이라 못 지운 것은 경고로 남는다.
- 셰이더 소스(.hlsl/.hlsli)를 고쳤으면 다시 쿠킹한다. 빌드는 HLSL 을 다시 쿠킹하지 않는다 — `App.exe --cook-shaders` 뒤에 재고 비교한다.
  Shipping 쿠킹은 `cook.stamp` 의 내용 해시로 검증하고 어긋나면 빌드를 세운다(`Scripts/generate/CookAssets.py --verify-shaders`).
- 비결정적 실패는 한 번 재현으로 "확정" 이라 부르지 않는다. 3~5 회 다시 돌려 재현율을 본다. 간헐 실패를 보면
  `--output-on-failure` 출력 전체를 파일로 남긴다(요약만 남으면 어느 케이스인지 모른다).
- 직접 실행으로 통과해도 CTest 에서 질 수 있다(병렬 부하 · 시한). 끝내기 전 검증은 CTest 로 한다. 멈추면 `LastTest.log` 의 마지막 `[ RUN ]` 을 본다.

## 2. 측정 · 프로파일

- **물리 벤치: `PhysicsBenchTest`**(Release) — 먼 이동 바디가 있는 step p50 1124~2468 → 319~330 us(없는 step 은 319~328 us 그대로).
- **에디터 모듈의 계측은 `SW_EDITOR_PROFILE_SCOPE`**(`Editor/Common/EditorProfile.h`) — 엔진 `SW_PROFILE_SCOPE` 는 엔진 서비스 표(`engine::getFrameProfiler`)를
  불러 에디터 모듈에서 쓸 수 없다. 같은 표에 쌓이고 이름은 엔진이 복사해 든다(모듈을 다시 올려도 매달리지 않는다). `GT.Editor.updateUi` 의 하위 구간은
  `GT.Editor.waitDrawSnapshot · newFrame · commandsAndWatchers · panels · endFrame(render · platformWindows · captureDrawSnapshot)`, 패널별 시간은 `-gv_editorPanelTimes=N`.
- **Hierarchy 는 화면 밖의 접힌 루트를 빈자리로 둔다**(이어진 것은 빈자리 하나) — 큐브 8000 Release `GT.Editor.updateUi` p50 4.2~4.7 → 1.3~1.4 ms, Hierarchy 패널
  평균 3.4~4.1 → 0.63~0.65 ms(큐브 100 에서는 0.12 ms). 남은 몫은 루트 8000 개를 도는 것(열림 상태 조회) — 더 줄이려면 열린 루트 목록을 들고 보이는 범위만 계산한다.
  빈자리 높이는 접힌 루트 줄(프레임 높이 + 줄 간격)과 같아야 스크롤이 튀지 않는다(`hierarchy.offscreenRootsKeepTheirPlace`).
- **AppSmokeTest 의 Unknown 태그 상한(8 KB)은 파일 수에도 걸린다** — 태그 없는 호출자(App 스플래시)가 공유 캐시(`ResourceUtil` 경로 캐시)의
  재해시를 일으키면 그 버킷 배열이 Unknown 으로 센다. 데이터 파일 몇 개를 더하자 18 KB 가 넘었다. 공유 캐시는 넣는 자리에서 자기 태그를 건다.
- **성능은 Release 로 잰다.** Debug 는 레이스 검출기 · 이터레이터 프록시로 컨테이너 코드를 과장한다(668 vs 87 us). 이전 · 이후 바이너리를 같은 스크립트로
  **번갈아** 2~3 회 잰다(`git stash -u` → 빌드 → 복사 → `stash pop` → 빌드). 아침 기준선과 오후 결과를 견주면 기계 상태가 결과로 읽힌다.
- **측정 기계**: i5-8500(6 코어 6 스레드). 게임 · 렌더 스레드 + 워커 넷이 코어를 나눠, 나눠도 벽시계가 잘 안 준다 — 틱이 쓰는 CPU 총량이 벽시계를 정한다.
  프로파일러가 없으면 `Ninja-Release`(PDB 있음 — 줄 표) + `py -3 -m Scripts stacks <pid>`(DbgHelp 샘플러). ICF 로 함수가 남의 이름으로 보인다.
- **표 읽기.** 열은 avg · p50 · p99 · min · max · per_frame 이고 카운터 값은 per_frame 열에 있다(시간 열 0 을 "죽은 경로" 로 읽지 말 것). 평균이 히치를 가린다 —
  p50 · p99 · 최악 프레임을 본다. 백분위는 옥타브 × 8 칸 히스토그램의 아래 끝(±9 %)이다. 구간 표는 스레드마다 **일한 시간**이고, 프레임이 빨라졌는지는
  `[Profile] wall N frames … us/frame` 와 `startup N ms` 로 본다. 중첩 합이 바깥보다 크면 표부터 의심한다.
- **`RT.Frame` = `RT.BeginFrame`(펜스 대기 = GPU 백프레셔) + `RT.ExecutePacket` + `RT.Present`.** `GT.Packet.submit` 이 크면 GT 가 RT 를 기다린다. `GT.Frame` 은
  `EngineLoop::tick` 만 재고 게임 모듈은 `App::run` 의 `GT.Game.update` · `GT.Game.fixedUpdate` · `GT.Editor.updateUi` 다. 2026-09-13 이전 RT 수치는 실제보다 작다.
- **병목은 씬 크기에 따라 뒤집힌다.** 큐브 2000 은 GPU 대기, 8000 은 게임 스레드다. 어느 쪽을 깎을지는 재고 나서 정한다. GT 가 병목이면 RT 구간이 늘어 보여도 경합일 뿐이다.
- **타임라인은 Tracy 로 본다**(`-gv_tracy=1` + 같은 판 0.14.1 뷰어, `Source/Engine/Profiling/README.md`). 표(`-gv_profileFrames`)는 구간마다 접은 숫자라
  "어느 스레드가 무엇을 기다렸나" 는 Tracy 의 스레드 타임라인으로 본다. 계측은 `SW_PROFILE_SCOPE` 하나가 둘 다에 남긴다. GPU 줄도 쿼리는 한 벌이다
  (엔진 타임스탬프 → Tracy 수동 GPU 컨텍스트). DX11 · Vulkan 은 GPU 시계를 컨텍스트를 열 때 한 번만 맞춰(큐를 기다린다) 긴 실행에서 GPU 줄이 조금씩 밀린다.
- **GPU 비용은 `GPU.<패스>` 타임스탬프로 나눈다.** 패스를 지워서 나누면 타깃 사슬이 바뀌어 답이 뒤집힌다(추정 38 us, 실측 123 us). `RT.BeginFrame` 은 GPU 시간의 대리값이 아니다.
- **재기 전에 VSync 가 꺼졌는지 본다** — 1/RT.Frame 이 주사율과 같으면 VSync 다. DXGI 는 스왑체인 생성과 `ResizeBuffers` **둘 다**에 `ALLOW_TEARING` +
  `Present( 0, DXGI_PRESENT_ALLOW_TEARING )`(짝이 안 맞으면 `INVALID_CALL`, `RHI/DX/RHIDxgiTearing.h`). Vulkan 은 present 모드. CLI 는 `-vsync`.
- **셰이더를 고쳤으면 재기 전에 `App.exe --cook-shaders`.** 빌드는 HLSL 을 다시 쿠킹하지 않는다.
- **런타임 UI 벤치**(2026-10-07, Release · DX12 · VSync 꺼짐 · 600 프레임 × 세 번, i5-8500): `-gv_benchUiWidgets=10000`(엔진 `UiBenchScreen` — 스크롤 밖이 대부분) ·
  `-gv_benchUiChurn=M`(프레임마다 글 M 칸) · `-gv_benchUiMarkers=K`(Empty 벤치 큐브에 화면 마커). 글 10 칸/프레임: `GT.Ui.Paint` p50 12.6 → 1.6 ms(자르기 밖 자식 컬링),
  `GT.Ui.Layout` 0.11 ms(30 위젯), `RT.Canvas` Upload 0.05 + Draw 0.013 ms. 바뀜 없음(대조군): `GT.Ui.Paint` 2.1 → 0.12 ms(바뀌지 않은 트리 출력 재사용), 업로드 p50 0.
  마커 500 + 위젯 1 만: Layout 0.36 · Paint 2.1 ms. 2026-10-10(보이는 자식 이분 탐색 · 그리기 목록 통째 복사 뒤, 세 번): 글 10 칸/프레임 `GT.Ui.Paint` p50 0.29~0.36 · p99 0.66~0.79 ms,
  `GT.Ui.Layout` p50 0.06 ms, `RT.Canvas.Upload` p50 0.04 ms. 같은 날 다른 PC(워커 14) 세 번: 전 `GT.Ui.Paint` p50 0.26~0.29 · Layout 0.06 ms(합 0.32~0.35) →
  후(바뀐 그리기 목록 맞바꾸기 · 빈 위젯 캐시 건너뛰기 · `Widget::asPanel`) Paint p50 0.23 · p99 0.52 ms, Layout 0.06 ms(합 0.29 ms). 걷기 분해(임시 구간):
  걷기 ~180 us(보이는 위젯 1150 · 캐시 일괄 860 — 이어 붙이기 ~90 · 자르기 검사 ~20), 트리 출력 → 프레임 목록 30 us, 같은 내용 비교 8 us, 프레임 목록 복사 55~65 us(→ 0, 맞바꾼다). 구간 `GT.Ui.*` · `RT.Canvas.*`, 카운터 `Ui.LayoutWidgets` · `Ui.PaintWidgets` · `Ui.CanvasQuads`(값은 per_frame 열).
- **텍스처 임포트 압축**(2026-10-10, Release App `--import-textures`, 워커 14 개 PC, 세 번): 띠 병렬 압축(`BandCompressJobInternal`) 전 → 후로
  BC7 512² 밉 10 단(`engine/textures_raw/random/grass.jpg`) 61.7~62.6 → 8.1~8.3 s, BC3 1024² 밉 11 단(`f00_000_face_00`) 17 → 2 ms,
  BC3 2048² 밉 12 단(`f00_001_body_00`) 70~72 → 9~10 ms, 원본 39 장 전체 3 분 35 초 → 40 초. 결과 DDS 는 스탬프 해시까지 바이트가 같다(스탬프를 비우고 전부 다시 임포트해
  `git status` 와 `import.stamp` 대조). 재는 법: 그 원본의 스탬프 줄만 지우고 `App --import-textures` — 로그 `Compressed WxH (… bands on N workers): T ms`.
  띠를 64 → 16 줄로 잘게 나눠도 그대로였다(BC7 512² 8.2~8.4 s). BC7 은 BC1 · BC3 보다 수천 배 느리다 — 무거운 것은 BC7 원본 수다.
- **벤치 스위치**(`Source/Games/Empty/BenchScene.cpp`): `-gv_benchMeshes=N` · `-gv_benchLights=N` · `-gv_benchGround=1` · `-gv_benchMovePercent=%` · `-gv_benchInstanced=1` ·
  `-gv_benchTickMovers=N`(틱 **안** 세터 — 실제 게임플레이 경로) · `-gv_benchSpawnChurn=N` · `-gv_benchMeshVariants` · `-gv_benchMeshShapes=N` ·
  `-gv_benchMaterialChurn*` · `-gv_benchAnimate=0`(컴퓨트 회전과 `update` 사인파를 **둘 다** 멈춘다), `-gv_deferred=1`, `-gv_useRenderThread=0`.
- **`-gv_profileFrames=N` 은 프레임 수다** — VSync 가 꺼진 가벼운 장면은 1500 fps 라 90000 프레임이 1 분에 끝난다(시간 상한은 없다). 시간으로 재려면
  `-gv_profileSeconds=S`(먼저 닿는 쪽이 끝낸다 — soak 이 쓴다).
- **프레임당 힙 할당**은 `-gv_profileFrames` 보고의 `alloc/frame`, 콜스택은 `-gv_profileAllocSites=N`(Debug App — 횟수는 최적화와 무관, 시간은 같이 재지 말 것).
- **씬 로드 측정**: `Scripts/dev/MakeStressScene.py` 로 큰 씬(도형 섞기) → `-gv_firstScene=<씬>`(또는 Empty 팩 `data/gamesettings.xml` 의 `startMap`) → `[SceneLoad]` 줄. Dev 는 `Cooked/` 를 마운트하지
  않으므로 쿠킹 효과는 `--cooked-dir=<repo>/Resource` 로 쿠킹하고 재고 지운다. `[SceneLoad]` 가 `.xml` 을 가리키면 쿠킹본을 안 읽은 것이다.
- **벤치가 상태를 공유하면 단계 순서를 잰다.** 손대지 않은 대조군이 움직이면 하니스를 의심한다. 벤치 메시가 공유라 배치 결함을 가린 적이 있다 — 씬에서 온 메시로도 본다.
- **GPU 업로드 비용은 호출당이다**(DX12 ~3.3 us) — 쪼개면 느려진다. 구간을 배열로 묶어 한 번에.
- **워커가 쓴 데이터를 다른 코어가 읽으면** 캐시 이동이 항목당 일(~40 ns)보다 비싸다. 쓰는 스레드와 읽는 스레드를 같게 둔다.
- **워커는 공유 카운터 · 비트필드에 쓰지 않는다.** `fetch_add` 한 줄이 병렬 플러시를 직렬보다 느리게 했다. "하나라도" 플래그는 프레임에 한 번 쓰고 읽기만 한다.
- **도구**: `RunDuplicateCode.py --filter <dir> --no-headers`(머리말에 "합칠 대상 아님" 목록), `RunEngineLayerGraph.py`, `Scripts/dev/RunBackendSmoke.py`(4 백엔드 평균 RGB),
  Release 로 읽는 `TaskManagerBenchTest` · `GameObjectBenchTest` · `ContainerBenchTest` · `NetReplicationBenchTest`(클라이언트 16 × 엔티티 1000), `Test/TestFramework/TestBench.h`.
- **측정 라운드마다 매니저 · 풀 · 레지스트리를 새로 만듭니다.** best-of-N 반복은 자라는 컨테이너가 만든 오염을 거르지 못합니다. 같은 값이 연달아 세 번 나올 때까지 수치를 믿지 않습니다.
- **한 번만 일어나는 이벤트(백엔드 교체 프레임의 최대값 등)는 3 회 이상 되풀이해 이상치를 거릅니다.** 프레임 1 에만 일어나는 일(업로드 · PSO 생성)은 워밍업(`FrameProfileSession::kWarmupFrames`) 뒤의 측정 창 밖입니다.
  측정 창 안에서 전량 재생성을 일으키려면 `-gv_rhiSwapAtFrame` 을 워밍업보다 뒤로 줍니다. 히치 최적화는 평균이 아니라 최악 프레임으로 판정합니다.
- **성능 워크로드는 움직여야 합니다.** 정지한 씬은 "바뀐 것 없음" 빠른 경로만 잽니다. 워크로드가 지나지 않은 경로는 "검증됨" 이 아니라 "실행된 적 없음" 으로 보고,
  최적화 대상은 프로파일 표에서 고르고 전후 숫자를 커밋 메시지에 남깁니다. 새 프로파일 구간을 넣을 때는 그 상위 구간도 함께 넣습니다. 하위 구간의 합과 전체의 차가 대개 진짜 병목입니다.
- **렌더 성능은 디스크에서 읽은 씬으로 배치 수를 먼저 셉니다.** 인스턴스 수와 배치 수가 비슷하면 인스턴싱이 꺼진 것입니다.

### 빌드 속도

- **빌드 시간 기준선은 `Scripts/dev/RunBuildBaseline.py`** — 풀 빌드(sccache 없는 별도 폴더) · 헤더 하나 수정 · `.cpp` 하나 수정 · 워크트리 콜드(sccache 웜)를 각 3 회 재서
  중앙값 표를 내고 머리에 PC 이름 · CPU 를 적는다. 다른 빌드가 돌면 멈춘다. 헤더별 누적 파싱 시간 상위는 `Scripts/lint/report/RunIncludeCost.py`(전 TU `-ftime-trace`, PCH 없이)다.
  표는 같은 PC 의 전후만 견준다([빌드 속도 계획](plans/BuildSpeed.md) 0 단계). 재는 동안은 `LT-wt/.slot1` ~ `.slot3` 를 모두 잡아 다른 에이전트의 빌드를 막는다.
- **기준선**(2026-10-10, PC `SW` · AMD Ryzen 7 6800H 논리 코어 16, 커밋 `0d054352a`(= main `8653de727` + 스크립트 수정), `Ninja-Debug`, 타깃 `all`, 시험 제외):

  | 시나리오 | 1 회(s) | 2 회(s) | 3 회(s) | 중앙값(s) |
  |---|---:|---:|---:|---:|
  | 풀 빌드(sccache 없음) | 125.9 | 131.5 | 131.7 | **131.5** |
  | 헤더 하나 수정(`Core/Container/vector.h`) | 131.6 | 132.7 | 130.7 | **131.6** |
  | `.cpp` 하나 수정(`GameObjectManager.cpp`) | 7.4 | 7.2 | 7.0 | **7.2** |
  | 워크트리 콜드(구성 + 빌드, sccache 웜) | 141.2 | 146.8 | 142.9 | **142.9** |

  헤더 시나리오가 풀 빌드와 같다 — Core 기본 헤더 하나를 고치면 사실상 전 TU 를 다시 컴파일한다(2 단계 헤더 다이어트의 근거).
  워크트리 콜드가 풀 빌드보다 느린 것은 PCH 를 켠 Windows 빌드가 sccache 에 거의 맞지 않기 때문이다(`/Yu` 캐시 불가, `Scripts/setup/README.md`). Release 는 재지 않았다(풀 1 회 192.3 s 만 있다).
- **Jolt 백엔드 TU**(`Physics/Jolt/*.cpp` 다섯, 같은 PC, 컴파일 DB 명령 그대로 3 회 중앙값의 합): 자기 PCH 없이 11.1 s(그중 헤더 파싱 7.0 s, 엔진 `pch.h` 만 5.8 s) →
  `EngineJolt_objects` PCH 2.9 s + TU 2.6 s = 5.5 s. 한 파일 수정 뒤 다시 짓는 시간은 TU 하나당 1.9 ~ 2.5 s → 0.3 ~ 0.8 s 다.
  재는 법: `/clang:-ftime-trace` 를 `-c --` 앞에 넣는다(뒤에 두면 파일 이름으로 읽힌다). 다른 빌드가 돌 때 잰 값은 1.5 배까지 흔들린다.

## 3. 검증 · 테스트 쓰기

- **설정 파일 시험은 둘이다** — `ConfigFileSchemaTest`(EngineTest, `Config/` 의 엔진 · 게임 · 서버)와 `EditorConfigFileSchemaTest`(EditorTest, `Config/Editor`).
  둘 다 표에 없는 파일을 실패로 본다 — 새 설정 파일은 그 표와 `Scripts/common/ConfigCatalog.py` 에 한 줄씩.
- **바깥 서버가 있어야 하는 시험은 `SW_TEST_REQUIRES_ENVIRONMENT( 스위트, "변수", "까닭" );`** — 변수가 비면 그 스위트만 빠지고 `[ SKIP SUITE ]` 한 줄(실패도 "모두 건너뜀" 도 아님,
  호스트 스위트와 섞이지 않는다). PostgreSQL 은 `SW_TEST_POSTGRES_URL`. 구현마다 같은 케이스는 계약 매크로로(`SW_SERVICE_STORE_CONTRACT_SUITE` · `SW_SQL_DRIVER_CONTRACT_SUITE` ·
  `SW_EPHEMERAL_STORE_CONTRACT_SUITE`). 서버 없이 실패 길만 보려면 닿지 않는 주소(`host=127.0.0.1 port=1 connect_timeout=1`)를 준다 — 깨끗이 지고 멈추지 않아야 한다.
- **`--test_filter` 는 gtest 모양이다** — 패턴 사이는 `:` 도 쉼표도 되고, 첫 `-` 뒤는 모두 빼는 패턴이다(`A.*:B.*-A.X`; `-A.*,B.*` 는 둘 다 뺀다).
  고르는 패턴이 등록된 케이스 하나와도 맞지 않으면 실행이 진다(`TestFrameworkTest.FilterThatSelectsNothingFails`) — 0/0 통과는 확인한 줄 안다.
- **광선이 두 삼각형이 나누는 모서리를 정확히 지나면 Möller–Trumbore 가 양쪽을 다 놓칠 수 있다** — 같은 각도로 나뉜 합성 원기둥 두 겹에서 실제로 났다
  (`CharacterGeometryUtil::intersectRayTriangle` 은 무게중심 여유 1e-5 로 막는다). 합성 형상 시험은 분할 수를 서로 다르게 하고, 면 모양(다각형)이라 반지름이 면 가운데서
  `r · cos(π/n)` 로 준다는 것도 기댓값에 넣는다.
- **`-gv_fixedFrameDelta=<초>` 는 프레임마다 그 시간만 흘린다**(벽시계 무시) — 시나리오 · 픽셀 비교 · 벤치 재현이 기계와 상관없이 같은 게임 시간을 본다.
  App 의 종료 코드는 `EngineLoop::requestQuit( code )` 가 정한다(0 성공, 시나리오 10 실패 · 11 읽기 오류 · 12 시간 초과 · 13 건너뜀).
- **실기동 확인은 시나리오로 남긴다** — `Resource/<영역>/automation/*.scenario.xml` 을 두면 그 게임 프리셋의 `ctest -L hostgpu`(`AppScenarioTest`)가 백엔드마다 돈다
  (`Source/Engine/Automation/README.md`). 손 확인 목록을 백로그에 적지 말고 시나리오를 쓴다 — 못 쓰는 것만 이유와 함께 손 확인으로. 값을 보려면 `Expect probe`(게임 .cpp 의
  `SW_AUTOMATION_PROBE` 한 줄), 그림은 `Screenshot` + `ExpectImage`(지표 값은 보고 JSON 에 늘 적힌다).
- **App 의 종료 코드 77 = 이 기계 · 빌드가 그 RHI 백엔드를 못 돌린다**(`RHIInitResult` 의 `BackendNotBuilt` · `DriverUnsupported`). 시험 · `AppRun.py` 는 로그 문구가 아니라 이것으로 건너뛴다 —
  환경 탓으로 물러나는 새 경로는 백엔드가 `_initResult` 를 적어야 건너뜀이 된다(안 적으면 결함으로 진다).
- **백엔드마다 디바이스를 세우는 시험은 `test::RHIBackendSweep`** — `for ( test::RHITestDevice& device : sweep )`, 건너뛰기는 케이스가 `sweep.getReadyCount() == 0` 으로.
  오프스크린 · 한 프레임 도우미(`makeSingleTargetPsoDesc` · `beginOffscreenRenderPass` · `countPrimaryColorPixels` · `renderSceneFrame`)를 먼저 볼 것.
- **macOS 는 지원 대상이 아니다** — 코드는 10-04 에 지웠고 `CheckTargetMacros` 가 `SW_PLATFORM_MACOS` 를 막는다. 되살리려면 그 전 git 기록에서. `Vcpkg.cmake` 의 APPLE 갈래 ·
  레거시 스탬프 이관과 `arm64-osx` · `x64-osx` 트리플릿은 손대지 않는 파일이라 사용자가 정리한다.
- **ThreadSanitizer 는 이 리눅스 환경의 clang 18 에 런타임이 없다** — clang 으로 `-fsanitize=thread` 컴파일하고 링크만 gcc 의
  `/usr/lib/x86_64-linux-gnu/libtsan.so.2` 를 직접 붙이면 돈다(`setarch -R` 으로 ASLR 을 끈다). 일부러 만든 경합을 잡는 것까지 확인했다(2026-10-03,
  `NetworkThreadTest`). 잠금 · 스레드를 바꾼 뒤 그 시험만 이렇게 돌린다.
- **고정 틱 창 끝에서 "모두 같다" 를 보는 네트워크 시험은 서버 물리가 그 창 안에 가라앉는다고 가정한다** — 물리 궤적은 구성마다 달라 리눅스 Shipping 에서는
  파괴 벽의 사건이 480 틱 창 너머까지 와 수렴 단언이 졌다(Debug 는 269 틱에 멈춤). 수렴은 창 뒤 상한 안에서 같아질 때까지 돌려 본다(`ScenarioOptions::_settleTickLimit`).
  해시가 갈리면 먼저 사건마다 적용 전 · 후 해시를 서버 · 클라이언트에 찍어 같은 (오브젝트, 번호) 끼리 맞춰 본다 — 결정성 결함인지 시험 가정인지 한 번에 갈린다.
- **느린 시나리오는 조건마다 한 케이스로 쪼갠다** — 조각(`SHARDS`)은 스위트 안의 케이스를 번갈아 나누므로, 한 케이스에 조건 둘을 넣으면 한 조각이 둘 다 진다
  (`NetSimDestructionMatrixTest` 는 회선 둘을 두 케이스로 나눠 호스트 스위트에서 nogpu 로 왔다 — WSL Debug 케이스마다 17 초).
- **CoreTest 는 엔진을 쓰지 않는다** — include 경로로는 막을 수 없다(`TestFramework` 가 Engine 을 PUBLIC 링크, `TestFramework.h` → `EngineMinimal.h`).
  `CheckTestSuites` 규칙 6 이 CoreTest 파일의 직접 Engine · GameFramework · Editor include 와 `engine::` 호출을 막는다. 엔진 타입이 필요하면 지역 대역을 쓰거나 EngineTest 에.
- **레이어 때문에 지금 자리가 가장 낮은 합법 자리인 파일 셋**(`PackCompressionUtil` · `ObjectUndoUtil` · `GPULight.h`)은 README 에
  이유가 있다 — 다시 "잘못 놓였다" 로 옮기지 말 것.
- **호스트 스위트 케이스는 예상 밖 `[Error]` 로그 하나로 진다** — 아직 못 고친 엔진 Error 는 `SW_TEST_KNOWN_ERROR_LOG( 스위트, 문구, 이유 )` 로만 허용하고, 고치면 그 줄을
  지운다(실행 끝에 남은 선언이 출력된다). 골든 이미지는 `AppSmokeTest.BenchFrameMatchesGoldenImage`(`Test/AppTest/Golden`, `SW_UPDATE_GOLDEN=1` 로 다시 뜬다).
- **Debug 의 단언 경로는 `test::ScopedAssertCapture` 안에서 시험한다**(멈추지 않고 센다).
- **모듈 코드 정리 시험에 이미지 전체를 범위로 주지 말 것** — Shipping 은 엔진까지 한 exe 다. 스텁 · vtable 하나씩으로 좁힌다.
- **큰 `reserve` 는 실패하지 않을 수 있다** — 상한 시험은 할당 바이트로 단언한다.
- **시간으로 움직이는 GPU 픽셀 단언은 `FrameRenderer::setAnimationTimeOverride` 로 시각을 고정한다**(음수 = 벽시계). 단위 큐브는 모프 위상이 π 근처에 몰려
  t ≈ π/2 + kπ 에서 변위가 다 함께 0 이 된다 — 벽시계로 찍으면 부하에 따라 간헐로 진다.
- **에디터 확장의 순서는 등록 순서가 아니라 `SW_EDITOR_*` 의 order 키다.** 등록이 빠지거나 순서가 바뀐 것은 `AppSmokeTest.EditorRegistriesKeepTheirOrder`
  (`-gv_editorRegistryDump=1`, hostgpu)만 잡는다 — 패널 · 메뉴를 고치면 hostgpu 를 돌릴 것.
- **`ScopedDefensiveTestLog` 범위 안의 로그는 메시지 앞에 `"[Expected Defensive Test] "` 가 붙는다** — 문구 비교는 startsWith 가 아니라 포함 여부로.
- **워크트리와 vcpkg 스탬프:** 스탬프 해시는 트립릿 파일(`cmake/Modules/Toolchain/Vcpkg/*-{windows,linux,osx}.cmake`)의 **바이트**로 계산한다. 작업 사본의
  줄끝이 체크아웃과 다르면(git 은 같은 파일로 본다) main 과 워크트리가 서로의 스탬프를 "매니페스트 변경" 으로 보고 configure 마다 vcpkg install 이 돈다.
  워크트리를 만들기 전에 `git diff --quiet` 로 같은지 보고, 다르면 그 파일을 지우고 `git checkout` 으로 다시 받는다.
- **진단 도구 표는 `docs/01_GettingStarted.md` §5** 가 정본이다(`-gv_dumpReflection=…`, `ReflectionParser --dump`, `-gv_dumpRenderGraph=1`, `'A' waits on 'B'`,
  `경로:줄:열: 이유`, `[SW_ASSERT]` stderr, `-gv_screenshot`). 시험 도우미도 거기 있다.
- **렌더 변경은 스크린샷까지 봐야 검증이다.** 로그 · 시험이 다 초록인데 화면만 찢어진 적이 있다. 정적 씬은 바이트까지 결정적이라 4 백엔드 전후 sha 로 비교한다.
  4 백엔드 대조는 Debug 로(Shipping 은 `SW_SHIPPING_RHI_BACKEND` 하나만 링크하고 `-vk` 를 거절한다).
- **픽셀 지표는 튼튼하게.** "특정 색 픽셀 수" 는 클리어 색 · 톤맵에 무너진다 — 모서리 기준 배경 제거 + 평균(R−B) 대소, 또는 "고유 색 ≥ 2". sin(시간) 구동
  지표는 "달라진 픽셀 수" 로. define 이 GPU 에 닿았는지는 PSO 디스크립터가 아니라 픽셀로 본다. 막히면 PPM 을 덤프한다.
- **`-gv_screenshot` 은 PrintWindow 가 아니다** — PPM 이 정본이다. `-gv_screenshotFrame=N` 으로 프레임을, `-gv_screenshotAttachment=<이름>` 으로 첨부를 고른다.
  "가끔 튄다" 는 한 장으로 안 잡힌다 — `-gv_screenshotCount=30 -gv_screenshotInterval=2` 로 연속으로 찍어(`_000` … 이 붙는다) 장을 나란히 본다.
- **"조용한 프레임" 버그는 벤치가 가린다**(회전 큐브라 매 프레임 dirty). 에디터 정지 화면으로 본다: `-EnableEditor -gv_benchMeshes=1 -gv_screenshot -gv_screenshotFrame=60`.
  그리는 것이 의심스러우면 `-gv_gpuCulling=0` · `-gv_drawMerge=0` · `-gv_vertexPool=0` 으로 경로를 하나씩 뗀다.
- **백엔드 교체는 헤드리스로 재현한다**: `-gv_rhiSwapAtFrame=N -gv_rhiSwapTo=<backend>`. 투명 큐브가 카나리아다. 텍스처 경로는 `-gv_defaultMaterial=engine/materials/benchtextured.material`.
  전역 변수는 `GlobalVariableInfo::setValueAsInt` 로 넣는다 — **C++ 대입은 변경 콜백을 부르지 않는다.**
- **실제 백엔드는 로그 `Initializing RHI with backend: <이름>` 으로 확인한다**(설정값에 덮여 "네 백엔드 확인" 이 DX12 네 번이던 적이 있다). "인자가 적혔는가" 는
  `CommandLineManager::isArgumentProvided` 로(`getArgument` 는 기본값이 있으면 안 적어도 true).
- **리사이즈는 창을 실제로 흔들어야 검증된다.** 블로킹을 걷을 때는 그것이 무엇을 가리던지 먼저 찾는다(에디터 모드가 DX12 DEVICE_HUNG 을 가렸다).
- **레이스 시험은 "돌았다" 가 아니라 결과를 대조한다**(직렬 1 판 · 병렬 3 판 평균 ±1 %, 검출률 ~7/8). 이 시험은 hostgpu 라 CI 가 못 돈다 — 병렬 기록을 건드리면
  로컬 hostgpu 를 여러 번 돌린다. 레이스는 deferred 파이프라인으로 돌려야 드러난다. 겨루는 시험은 상대가 **돌기 시작한 뒤** 겨루고 "겹친 횟수 ≥ N" 을 단언한다.
- **변이 검사 함정** — 조건을 지우는 변이는 방향에 따라 우연히 통과한다. 백업을 `Copy-Item` 으로 되돌리면 mtime 이 과거라 ninja 가 안 짓는다(touch). 변이한 코드를 태우는
  스위트가 어느 실행 파일에 있는지 먼저 본다. "깨끗하게 실패했는가" 까지 본다(부정 단언 메시지는 `SW_EXPECT_FALSE_MSG`). 성공 쪽이 조용한지도 본다. 저장소 밖
  `mutlib` 은 기준선을 돌리지 않는다 — 시험이 먼저 통과하는지 확인한다. 변이 뒤에는 전체를 다시 빌드한다(부분 빌드는 모듈 DLL ABI 가 섞인다).
- **새 시험만 걸러 돌리고 커밋하지 말 것.** 빌드가 깨진 채 돈 ctest 는 **옛 바이너리**로 통과한다. 빌드가 진 프리셋(DLL 잠김 — `Scripts/setup/AddDefenderExclusions.py`)에서
  시험을 돌리지 말 것.
- **간헐 실패를 다룰 때** — `ConsoleLogOutput` 은 Error 만 flush 해서 ctest `--output-on-failure` 의 마지막 `[ RUN ]` 은 크래시 위치가 아니다. 같은 명령을 직접 돌려 파일로
  받는다. 시험 실행 파일에도 크래시 핸들러가 있다(`Test/TestFramework/main.cpp`). 단독으로 통과하고 스위트에서만 지면 전역 상태 오염부터(VFS 는 `GlobalVfsScope`).
- **검증은 한 번에 하나만 돌린다.** 빌드 · 스모크 · 벤치 · GPU 스위트를 겹치면 가짜 실패(파일 잠김 · CMake 프로브 레이스 `LNK1107`)와 수치 오염이 난다. 백그라운드 셸은
  `taskkill /T` 로 트리째 내린다. 창을 만드는 GPU 시험을 Bash 로 돌리면 타임아웃에 걸린 것처럼 보인다 — PowerShell `Start-Process`.
- **Shipping 에서만 나는 것** — Info 로그 · `SW_ASSERT` 가 사라지고(반응이 로그 한 줄뿐인 기능은 배포본에서 없는 것과 같다), 리소스는 팩에만 있고 `.hlsl` 원본이 없으며,
  쿠킹한 바이너리 씬만 읽는다(씬 · 프리팹 시험은 `SceneDocument::saveBinary` 로 `.bin` 도 쓴다). 풀 블록을 힙으로 반납하는 손상은 Shipping 에서만 터진다 — 풀 자유 목록이
  LIFO 인 것을 써서 "다음 할당이 같은 주소" 로 검사하면 Debug 에서도 잡힌다.
- **시험 도우미**: `test::makeTempPath` · `makeTempDirectory`(케이스 폴더, 끝나면 지우고 못 지우면 진다), `test::ScopedLogCollector`, `test::ScopedFailureCapture`,
  `test::ScopedDefensiveTestLog`, `SW_ASSERT_TRUE_MSG`, `test::runThisExecutableAsChild`, `test::RHITestDevice`(`kArrAllRHIBackend`), `test::RHITestImage`,
  `test::FakeRHIDevice`(병렬 기록 nogpu), `LitCubeScene` · `renderPresentCaptureOf` · `compareCaptures`(TestRenderPassGPU.cpp), 에디터 지역 서비스 `Test/EditorTest/EditorTestServices.h`, 네트워크 호스트 묶음 `test::LoopbackCluster`(`TestFramework/TestLoopbackCluster.h` — 루프백 +
  끝점마다 흉내, 손 시각 `step` · 호스트 스레드, CoreTest 도 쓴다. 씬 · 라우터까지 필요하면 `NetSimHarness`).
- **시험 실행기 규칙** — 필터로 고른 스위트의 케이스가 전부 스킵되면 실패다(`--allow_empty_suite`). 테스트는 `Bin` 에 쓰지 않는다. `RUN_SERIAL` 은 이유와 함께만.
  PowerShell 에서 쉼표가 든 `--test_filter` 는 따옴표로 감싼다(안 감싸면 앞 토큰만 먹고 오류도 없다).
- **단언 규칙** — 공개 API 는 단언 뒤에 진짜 if 가드를 둔다(Release 에서 단언이 사라진다). 그런 경로의 시험은 Debug 에서 skip 하고 Release · Shipping 에서 잰다.
  작업 스레드 안의 `SW_ASSERT_*` 는 그 람다만 끝낸다. future 시험에서 `wait()` 를 부르지 말 것(회귀가 정지가 된다) — `isValid()` 와 짧은 `waitFor`.
- **EditorTest 는 ImGui 를 링크하지 않고 정해 둔 Editor 파일만 링크한다**(`EditorAssetCommands.cpp` 는 빠져 있다). 시험할 로직은 ImGui 없는 함수로 꺼낸다 —
  `alignObjects( listObject, … )`, `EditorPlaySession::captureSnapshot( PlaySessionData& )`, `EditorSessionPolicy`. `EditorContext::get()` 을 읽는 함수는 시험할 수 없다.
- **할당 0 을 보는 시험은 여러 번 재어 최솟값을 본다**(누계는 프로세스 전체 값). 누수 시험은 `MemoryProfiler::getLiveAllocationCount` 로, 노드를 동시에 여럿 쥐고.
- **공유 시험 픽스처의 등록**(`makeMockComponentTypeInfo` 등)은 짝 `.cpp` 한 TU 에. 헤더에 두면 TU 마다 `TypeInfo` 가 중복 등록된다. 시험 본문(전역 스코프)에서는 `sw::` 로 한정한다.
- **골든 기준은 뜬 장치를 적는다**(`device` — GPU 이름 · 드라이버, 다음 `--record` 부터) — 다른 기계에서 지면 비교 메시지가 두 장치를 함께 찍는다.
  기계별 기준(`<백엔드>.<기계>.json`)은 만들지 않는다(사용자 결정 2026-10-06) — 다른 기계에서 진 기록이 생기면 그 기계에서 `--record` 해 드라이버 차이인지 회귀인지 가른다.
- **할당 실패 경로는 `Memory::injectAllocationFailures( count )`(Dev, 스레드 국소)로 시험한다** — 이 스레드의 다음 count 번 `allocate` · `allocateAligned` 가 nullptr.
- **낱개 파일 · 팩 경쟁을 시험하려면 리소스 루트를 임시 폴더로 바꾼다**(`ResourceUtil::exchangeRootFolderPath` — 검색 폴더 · 경로 캐시를 다시 짓고 앞 루트를 돌려준다).
- **빌드 출력을 `| head` 로 자르지 말 것** — 파이프가 닫히면 빌드가 중간에 죽고 낡은 바이너리가 남는다. 파일로 받은 뒤 본다.
- **Windows ASan 이 `stack-overflow` 만 내고 스택을 풀지 못하면** 후보 함수마다 `fopen` · `fflush` · `fclose` 로 호출을 한 줄씩 남기고(줄 수 상한을 둡니다) 되풀이되는 모양을 봅니다.
- **`FrameRenderer::executePacket` 은 패킷의 스냅샷을 옮겨 갑니다.** 테스트에서 같은 패킷을 두 번 내면 두 번째는 빈 프레임입니다. App 실기동으로 좁혀지지 않는 백엔드 교체 결함은 `RenderPassGPUTest.RendererSurvivesDeviceRecreate` 쪽으로 옮겨 재현합니다.
- **변경 전후 그림이 다르면 어느 쪽이 맞는지는 네 백엔드의 일치도로 가립니다.** 변경 전 그림이 백엔드끼리 이미 어긋나 있었다면 변경 전은 기준이 아닙니다.
  네 백엔드가 같은 증상을 내면 백엔드 코드보다 그 위의 공유 호출부를 먼저 보고, 한 백엔드만 다르면 그 백엔드의 산출물과 계약부터 봅니다. 같은 변경에서 한 백엔드만 깨지면 배리어 누락(레이스)을 먼저 의심합니다.
- **여러 번 띄우는 하니스는 프로세스를 이름(`taskkill /IM App.exe`)이 아니라 PID 로 내립니다.** 이름으로 내리면 방금 띄운 다음 회차를 죽입니다. 에디터를 켠 실행은 기동이 길어 짧은 종료 대기로는 "종료 중 멈춤" 오탐이 납니다.
  로그의 `[Error]` 를 세는 실행은 강제 종료(`taskkill /F`)하지 않습니다. 비동기 로거의 뒷부분이 사라져 "오류 0" 이 거짓이 되므로 `-gv_profileFrames` 로 스스로 끝나게 합니다.
- **레이스가 의심되면 추론하지 말고 드러나는 구성(`-gv_deferred=1`)으로 Debug 실행해 로그의 `Concurrent … access detected` 건수를 고치기 전후로 셉니다.** "도달 불가" 는 정적 추론이 아니라 진입 횟수 계측으로 확인합니다.
- **"사용처 0 · 죽은 코드" 판정은 테스트 파일까지 직접 grep 하고, 지우기 직전에 한 번 더 확인합니다.** 무관해 보이는 선행 실패는 `git log -S` 로 코드와 테스트 양쪽의 도입 커밋을 대조해 되돌려 본 뒤에만 "선행 실패" 라고 부릅니다.
- **렌더 검증 씬에는 메시 2 종과 머티리얼 2 종 이상을 넣습니다.** 하나짜리 씬은 다중 배치 · 바인딩 경로를 지나지 않습니다. GPU 정렬 · 압축 테스트는 물체마다 보이는 면의 색을 다르게 둡니다(같은 색 · 같은 알파의 층은 순서와 무관해 테스트가 헛돕니다).
- **셰이더 결과가 화면에 안 보이면 이 순서로 좁힙니다.** ① 게이트 값(PSO · CB · UAV 핸들, 인스턴스 수)을 찍는다 ② 셰이더가 무조건 눈에 띄는 일을 하게 한다 ③ CB 멤버를 하나씩 조건으로 건다 ④ 임계값을 쓰기 전에 그 값의 실제 범위를 확인한다.
  시간에 따라 움직이는 기능은 `-gv_screenshotFrame` 을 다르게 준 두 장으로 먼저 캡처 시점이 다른지 봅니다.

## 4. CI

- **CI 가 끝까지 돌게 하는 세 가지**(`.github/workflows/ci.yml`). ① main 은 `cancel-in-progress: false` — push 가 실행 시간보다 잦으면 끝나는 실행이 0 건이 된다(10-04 7 시간).
  ② vcpkg 바이너리 캐시는 구성 직후 `actions/cache/save` 로 저장한다 — `actions/cache` 의 post 저장은 잡이 성공할 때만 돌아, 시험 하나가 지면 1 시간 지은 포트를 버렸다
  (Configure 50~90 분이 매번). 키는 OS 별 하나(트리플릿이 OS 당 하나). ③ Windows 는 `SW_ENABLE_PCH=OFF` — sccache 는 clang-cl 의 `/Yu` · `/Fp` 를 캐시하지 못해 적중률 0 % 였다
  (`sccache --show-stats` 의 "Non-cacheable reasons: /Fp"). PCH 를 끄면 PCH 가 가리던 오류가 드러난다(템플릿 본문의 `-Wcovered-switch-default`) — PCH 를 끈 구성도 짓는다.
  작업 로그 · 아티팩트는 API 로 403 이라 진 시험은 주석(annotation)으로 올린다 — 실행 목록 · 잡 단계 · 주석은 로그인 없이 읽힌다(시간당 60 회 한도를 여럿이 나눠 쓴다).
- **리눅스 CI 는 ubuntu-22.04 의 `libclang-dev`(16 미만)다.** 파서에 새 libclang API 를 쓰면 리눅스 잡만 선다 — `CINDEX_VERSION` 으로 가른다. CI 러너 파이썬은 3.10 이라
  f-string 식 안의 백슬래시 · 여러 줄 식이 configure 를 죽인다(`CheckPythonMinimumVersion.py`). GH Windows 러너는 cp1252 라 한글을 print 하는 스크립트가 빌드째 죽는다
  (증상: `sccache stats: 0 hits, 0 misses`) — 진입점은 `Scripts/common` 을 **모듈 수준에서** import 한다(UTF-8 stdout, 게이트 `CheckScriptEntryPoints` —
  함수 안의 import 는 치지 않는다: 한 갈래에서만 끌어오면 다른 갈래의 print 가 죽는다). 재현은 `PYTHONIOENCODING=cp1252`(cp949 는 `—` 에서 죽는다).
- **CI 실패는 실패한 잡과 같은 프리셋으로 재현한다.** Debug(Engine SHARED)는 Shipping 정적 링크 결함을 원리상 못 낸다. Windows CI 러너의 TEMP 는 8.3 짧은 이름
  (`RUNNER~1`)이라 경로를 글자로 비교하면 틀린다. 빨간 CI 는 다음 결함을 숨긴다(55 런 연속 실패를 아무도 몰랐다) — 런 상태:
  `curl -s "https://api.github.com/repos/sswgame/LearningTemplate/actions/runs?per_page=30&branch=main"`. 스킵은 실패보다 조용하다.
- **`SW_ENABLE_DEADLOCK_DETECTION` 은 CI Windows Debug 잡이 `RunBuildWarnings.py --define SW_ENABLE_DEADLOCK_DETECTION --fail-on error` 로 지킨다.**
- **CI 실패는 `Scripts/dev/CiFailureReport.py` 가 주석으로 올린다**(시험 · 구성 실패의 vcpkg 포트 로그 · 크래시 스택 `Bin/Saved/Logs/crash_*.stack.txt`).
  작업 로그 · 아티팩트는 관리자 전용(API 403)이라 밖에서는 주석만 보인다.
