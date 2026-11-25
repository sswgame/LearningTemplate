# Test/Scenario — 시나리오

그림 · 입력 · 씬이 돌아야 하는 확인은 `Content/**/automation/*.scenario.xml` 시나리오다. 단계는 적은 순서대로 돌고, 이미지는 백엔드별 골든과 견준다. 이 폴더는 시나리오를 CTest 항목으로 올린다. 단위 테스트는 [Test/README](../README.md), 지킬 규칙은 [Testing](../../docs/agents/Testing.md), 실패 추적은 스킬 `scenario-debug`.

## 형식

```xml
<Scenario name="engine.debughud" fixedDelta="0.0166667" timeoutFrames="300" startAfter="ScenePlaying" startTimeoutFrames="900" input="exclusive">
	<Launch variable="gv_firstScene" value="game/playground/maps/fogshowcase.scene.xml"/>  <!-- 기동 때만 정해지는 값(체인은 부모 App 이 자식 명령줄로 옮긴다) -->
	<AllowLog id="Audio.DeviceLost" reason="CI 러너에 오디오 장치가 없다"/>           <!-- 이 시나리오에서 나와도 되는 로그 id -->
	<DevCommand line="hud off"/>
	<WaitUntil watch="DebugHUD.Shown" equals="0" timeoutFrames="60"/>                  <!-- 조건이 설 때까지 -->
	<Press slot="Key.LeftControl"/>
	<Tap slot="Key.F3"/>                                                                 <!-- 판정 아래 입력은 읽기가 다음 프레임으로 넘긴다 -->
	<Screenshot file="hud.ppm"/>
	<ExpectImage file="hud.ppm" golden="hud" region="0,0,0.3,0.45"/>                    <!-- 백엔드별 골든과 비교 -->
	<ExpectImagesDiffer first="off.ppm" second="on.ppm" region="0,0,1,1" minDiffering="0.02"/> <!-- 같은 실행의 두 장이 다른지(음성 대조) -->
	<ExpectImagesDiffer first="full.ppm" second="lod.ppm" minDiffering="0" maxDiffering="0.01"/> <!-- 비용을 줄이는 값이 그림을 깨지 않는지(상한) -->
	<Pass/>
</Scenario>
```

- 시나리오 이름 · 속성은 단위 테스트 `ScenarioFilesTest` 가 앱을 띄우지 않고 모두 검사한다.
- 장치 능력이 없어 그 그림을 그리지 않는 백엔드는 `<SkipUnless watch="RHI.MaterialTessellation" reason="…"/>` 로 건너뛴다(감시 값 0 이면 Skipped, 못 읽으면 실패). 예: `automation/displacement`.
- 키트 · 에디터 확장에 기대는 시나리오는 루트에 `requiresModule="GF_EditorPlugin_Admin"`(쉼표로 여럿)을 적는다. 그 모듈이 꺼진 빌드에서는 건너뛰고 정적 검사도 이름을 보지 않는다.
- 단계 종류는 그 기능의 모듈이 제 .cpp 에서 `SW_AUTOMATION_STEP` 로 등록한다(키트 · 게임 포함, 예: GF_OpenWorldWestern 의 `<FeedHorse horse food>`). 모듈을 내리면 빠지고 핫 리로드는 같은 이름을 갈아 끼운다.
- 판정 색: 스테이지가 돌았는지 보는 클리어 · 표시 색에 마젠타를 쓰지 않는다(마젠타는 오류 · 빠진 텍스처 표시다). 지오메트리 · 테셀레이션 시나리오는 청록.
- 종료 코드: 0 통과 · 10 실패 · 11 읽기 오류 · 12 시간 초과 · 13 건너뜀 · 15 처리하지 않은 로그. 표 하나 `Config/Engine/ProcessExitCodes.json` → C++ `ProcessExitCode` · CMake `SW_EXIT_CODE_<이름>`.

## 대기

단계는 적은 순서대로 돈다. 같은 프레임의 판정 아래 적은 입력은 읽기가 다음 프레임으로 넘긴다. `<At frame>` 과 `<Wait>` 는 없다(쓰면 읽기 오류). 실패 판정 시한 말고는 고정 대기가 없다.

| 기다리는 것 | 단계 |
|---|---|
| 무언가 끝나기(로드 · 정착 · 도크 · 애니메이션 끝 · 물리 잠듦 · 입력 반영) | `<WaitUntil watch=…>`. 시한은 넘으면 실패일 뿐 통과를 정하지 않는다. 필요한 감시 값이 없으면 엔진 · 게임에 더한다 |
| 결정적 시뮬레이션의 사진 순간 | 가능하면 사건 조건(래그돌 잠듦 · N 번째 적중 · 파편 착지) |
| 사진 앞 정착 | `<Screenshot stability>`: `render`(기본 — 에셋 · 화면 덮개 + 렌더 정착), `assets`(카메라가 움직이는 장면), `none`(그 프레임 그대로). 사진이 아닌 단계 앞은 `<WaitRenderStable/>` |
| 시간 자체가 내용(누른 채인 구간 · 이동 거리 · "다음 프레임에" 단언 · 프레임 경계) | `<AdvanceSimulation frames="N"/>`. 루트에 `fixedDelta` 가 있어야 한다. 그리지 않고 진행하되 마지막 `_advanceDrawnFrames`(`Config/Dev/GoldenImage.json`) 프레임은 그린다 |
| 그린 프레임 자체가 내용(`<BeginFrameStats>` ~ `<EndFrameStats>` · `<Load>` 측정 창 · 에디터 UI 가 그려야 도는 입력) | `<AdvanceSimulation frames="N" render="true"/>` |
| 에디터가 멈출 때까지 | `<EditorWaitStable/>` (아래) |

- 렌더 정착 조건은 그 상태를 만든 기능이 세 값(준비됨 · 해당 없음 · 진행 중 + 진척)으로 등록한다(`SW_AUTOMATION_STABILITY_CONDITION`). 문턱 · 정지 시한 · 반복 실패 한도는 `Config/Dev/ScenarioRun.json`. 진척이 `_stallFrames` 동안 그대로면 `WaitStalled`, 한 체인에서 같은 대기 실패가 `_repeatedFailureLimit` 번이면 체인이 `RepeatedFailure` 로 멈춘다.
- `<WaitUntil>` 의 시한은 `timeoutFrames`(시나리오 프레임) · `timeoutSeconds`(게임 초) · `timeoutWallSeconds`(벽시계 초 — 자식 프로세스 종료 · 파일 · GPU 같은 바깥 일) 가운데 하나다. 무인 실행은 프레임이 실시간보다 빨리 돌아 바깥 일을 게임 초로 기다리면 금방 끝난다.
- 바깥 일(자식 프로세스 · 컴파일러 · 파일 감시 · 워커 비동기 로드 · GPU 회수 — `Scene.Transitioning` · `Assets.Pending`)을 따르는 감시 값은 `SW_AUTOMATION_WALL_WATCH` 로 등록한다. 그 값을 기다리는 `<WaitUntil>` 이 `timeoutWallSeconds` 가 아니면 로드 오류다.
- 시한이 통과를 정하는 단계("일정 시간 동안 일어나지 않음")는 만들지 않는다. 그런 판정은 프레임 · 사건으로 센다. 단계의 벽시계 시한(`timeoutWallSeconds` · `ExpectExitWithin` · `CloseWindow withinSeconds`)은 `gv_scenarioWallScale` 로 늘어난다.
- `timeoutFrames` 는 시나리오에 적은 프레임만 센다. 시계를 붙잡는 대기(`<WaitUntil>` · `<LoadScene>` · 입력 대상 자리 잡기 · 스크린샷의 에셋 대기)는 저마다 시한이 있고, 전체는 벽시계 `wallTimeoutSeconds` 가 묶는다. 마지막 단계가 시한 밖이면 읽기 오류. `<Pass/>` 가 끝 차단을 기다리는 프레임은 `kMaxFinishWaitFrames` 가 묶는다.
- `<WaitUntil>` 이 기다리지 않고 지나가면 로그에 `was already true` 가 남는다. 고정 대기를 조건 대기로 바꿀 때 부정형 단언을 가려내는 표지다.
- 게임 시간이 드는 구간(메워짐 · 젖음 · 노출 적응)은 목표 값 조건 대기로 기다리고, 그 구간만 `<Variable name="gv_timeScale" value="4"/>` … `value="1"` 로 감싼다. 사진과 그 앞 프레임은 배율 1. 시간을 멈춘 시나리오는 진행 길이를 가장 긴 갱신 주기의 배수까지 줄인다.

### 에디터 시나리오

- `<EditorWaitStable/>` 은 화면(창 크기 반영 · 도크 배치 끝 · 밀린 흉내 입력 없음 · 앞 단계 뒤 그린 화면이 두 번 연속 같은 배치) · 씬(전환 없음) · 에셋(씬이 기다리는 로드 · 업로드 · PSO 없음) · 끝 차단(`SW_AUTOMATION_FINISH_BLOCKER` — 비동기 일을 새로 만들면 그 모듈이 등록한다)이 모두 멈출 때까지 기다린다.
- 일어나지 않음을 보는 단언(`sameAs` · 실패 수 0)은 `<EditorWaitStable/>` 뒤에 둔다. 다른 프로세스의 답(넷플레이)은 받은 수 감시 값을 `Remember` 하고 바뀔 때까지 벽시계로 기다린다.
- 붙잡는 단계는 무엇을 기다리는지 한 줄을 넘긴다(`holdUntilReady` 의 필수 인자). 진행 중인 일에는 단계 시한이 없다. 할 일이 다 끝났는데 화면이 120 프레임 넘게 움직이면 진동하는 UI 결함으로 진다.
- 그림을 찍기 전에 `<Variable name="gv_editorUiScale" value="1.5"/>` 로 UI 배율을 고정한다.
- 에디터 실행(`-EnableEditor -scenario=…`)은 사용자 `Saved/Editor` 를 읽지도 쓰지도 않는다. 프로세스마다 `Saved/Automation/EditorState/editor_p<pid>` 를 쓴다(`-gv_editorStateDir` 로 바꾼다).
- `SW_EDITOR_SELF_TEST` 케이스는 `<EditorSelfTest pattern="*"/>`(`engine/automation/editor/selftests.scenario.xml`)로 `Scenario.editor` 체인에서 백엔드 넷과 로그 게이트를 받는다. 앞 일의 효과를 기다리는 케이스는 `EditorSelfTestStep::WaitStable` 를 돌려준다. `Continue` 는 프레임 자체가 내용일 때와 조건을 직접 볼 때만.

## 이미지 골든

- 영역을 가로 160 화소 이하로 줄여 `golden/<시나리오>.<이름>.<백엔드>.ppm`(백엔드는 `dx12` · `vk` · `wgpu` …, Linux 는 `<백엔드>.linux.ppm`)과 견준다(`AutomationGoldenUtil`).
- 판정은 두 축 · 세 상한이다. 넓고 얕은 차이: 색 거리(R · G · B 차의 합) 12 이상인 화소 비율 ≤ `tolerance`, 평균 거리 ≤ `maxMeanDistance`. 좁고 깊은 차이: 거리 96 이상인 화소가 4 이웃으로 이어진 가장 큰 덩어리 ≤ `maxRegionPixels`. 기본 문턱은 `Config/Dev/GoldenImage.json`(칸이 없으면 이미지 단언이 진다)이고 `<ExpectImage>` 속성이 덮는다.
- 결과는 `Pass` · `PassNearLimit`(한 축이라도 상한의 `_nearLimitRatio` 를 넘음) · `Fail`(`AutomationGoldenUtil::judge`). 근접은 통과이고 보고 `nearLimitGoldens[]` 와 경고 `Automation.GoldenNearLimit` 로 알린다(로그 게이트가 세지 않는다).
- `-scenario-update-golden` 은 없거나 · 크기가 다르거나 · 근접이거나 · 진 골든만 다시 쓰고 옆에 메타 `*.golden.json`(커밋 · 백엔드 · 까닭 · 측정값 · 상한)을 쓴다. 보고의 `updatedGoldens` 그림을 확인한 뒤 커밋한다.
- `-scenario-record-missing-golden` 은 없는 골든만 쓰고 그 시나리오는 `MissingBaseline` 으로 진다. 두 깃발을 함께 주면 이미지 단언마다 진다.
- 기록 모드가 아닌 실행은 보고 `goldenSamples[]` 에 골든마다 측정값과 상한을 남긴다. 실행 사이 흔들림은 `py -3 Scripts/dev/scenario_bisect.py --golden-margins [보고 폴더…]`.
- 검증 체인 백엔드(쿠킹 표 `scenario_backends`)마다 골든이 있는지는 `ScenarioGoldensTest` 가 본다. 백엔드를 고르는 시나리오(`<SkipUnless>` · `<SwapBackend>`)는 하나 이상이면 된다.
- 개발 겹치기 화면(`UIScreen::isDevOverlay` — 디버그 HUD 등)은 캡처를 백버퍼로 옮긴 뒤에 그려 사진에 들지 않는다. 게임 UI(토스트 포함)는 든다. `<ExpectUI screens>` 는 게임 화면만 센다. 디버그 드로우 · 테스트 그림 겹치기(`CanvasOverlays`)는 아직 주 출력에 칠한다.
- 같은 시나리오 안에서 같은 파일을 두 번 찍지 못한다(읽을 때 거절). 파일 쓰기와 `<ExpectImage>` · `<CaptureEditorUI>` 의 마젠타 판정은 `<Pass/>` 에서 한 번에 한다.

## 로그 · 기록

- 로그 게이트: 시나리오 구간의 Warning · Error 는 `<ExpectLog>`(기대)나 `<AllowLog>`(허용, 사유 필수)가 없으면 실패다. id 없는 줄은 허용할 수 없으니 소스에 id 를 붙인다.
- 사건은 `<ExpectLog id="영역.사건">` 로만 센다. 세려는 줄은 소스에서 `SW_LOG_*_ID` 로 쓴다. `contains` 는 그 id 줄의 값(경로 · 수)을 좁힐 때만 붙인다. 개발 명령의 답은 `DevCommand.Reply` 줄이다.
- `<Record watch="…"/>` 는 값을 보고 `records[]` 에 남기고, 부모 App(`AutomationPerBackend`)이 통과한 백엔드들의 기록을 순번대로 견준다. 다르면 실행이 실패(10)이고 `record mismatch - <시나리오>: …` 가 남는다. `<Record>` 가 있는 시나리오는 `-scenario-backend-split=independent` 에서도 모든 백엔드에서 돈다.
- 산출물(사진 · 보고)은 `Saved/Automation/<시나리오>/<프로세스>/`. 무인 실행은 사용자 설정 파일을 읽지도 쓰지도 않고 사양 자동 프리셋을 쓰지 않는다.
- 실행마다 `[Scenario] <이름> timeline:` 줄(프레임 종류별 ms · 무거운 단계 · 가장 긴 대기)과 단계 사이 `stage reset took` 이 남는다. 무엇이 느린지는 새로 재지 말고 이 줄을 모은다.

## 단계 묶음

- 리로드: `<RebuildModule name="SWGame" variant="probe-next"/>`(판 표 `Config/Dev/ModuleRebuildVariants.json` 으로 모듈을 다시 지어 갈아 끼운다 — 끝은 `Module.ReloadPending`), `<OverwriteConfig configFile="Editor/…json" from="…"/>`, `<RestoreOverwrites/>`. 바꾼 파일은 시나리오 끝에 늘 되돌린다. 결과 감시 값 `Reload.Count.<길>` · `Reload.FailedCount` · `Reload.StaleCount` · `Reload.LastMs.<길>`(Config · Asset · Shader · Module), `Shader.RecompilePassCount`.
- 씬 · 연출: `<LoadScene path="…" timeoutFrames="600"/>`, `<ExpectActiveScene path="…"/>`, `<PlaySequence object="이름"/>` · `<WaitSequence object="이름"/>`.
- 벤치마크: `<BeginFrameStats/>` … `<EndFrameStats file="bench.json"/>` 사이 벽시계 프레임 시간(평균 · p95 · p99 · fps)을 산출물 폴더에 쓴다. 예: `game.playground.benchmark`.
- 재생(게임 · 콘솔): `ScenarioPlayback::play( 경로, 선택 )` 또는 콘솔 `scenario.play <경로> [loop] [demo] [fixed]` · `scenario.stop`. 판정이 끝나도 프로세스를 끝내지 않는다. 시퀀서 항목 `Scenario`(`_scenario="….scenario.xml"`)가 시작 프레임에서 이 창구를 부른다. 행동 리플레이(`.swintent`)는 `<Intent>` · 개발 명령 `intent.play`.

## CTest 항목

| 항목 | 무엇 |
|---|---|
| `Scenario.<팩>` | 구성이 짓는 게임마다(활성 게임 + `SW_GAMES`, Release 는 `ALL`) `Content/game/<팩>/automation/*.scenario.xml` 체인. 팩 이름은 `Config/Game/<게임>.json` 의 `_packRoot`, 활성 게임이 아니면 `-game=<게임>`. 엔진 시나리오(`engine/automation/`)는 활성 게임 체인에 든다 |
| `Scenario.engine.rhiswap.<이름>` | `rhiswap/` — 네 백엔드가 있는 Windows |
| `Net.<이름>` | `netpair/`(라벨 `net`) — 실행기(`Scripts/net`)의 `CTEST_ITEMS` |
| `Scenario.<팩>.showcase` | `showcase/`(골든 `showcase/golden/`) |
| `Scenario.<팩>.desktop.<이름>` | `desktop/` — OS 전역 상태를 쓰거나 프로세스를 끝낸다, 하나씩 혼자. `desktop/editor/` 는 편집기로 빌드 트리 · 모듈 · `Config/Editor` 를 바꾼다(`…desktop.editor.<이름>`) |
| `Scenario.<팩>.sequence.<묶음>` | `sequence/<묶음>.1` · `.2` — 같은 사용자 설정 파일로 두 번 기동 |
| `Scenario.<팩>.editor` · `Scenario.editor` | `editor/` — 그 게임을 싣는 편집기 체인 · 엔진 편집기 체인 |
| `Scenario.<팩>.load.<이름>` | `load/`(엔진 `engine/automation/load/` 도) — `<Load durationSeconds="N"/>` 부하, 라벨 `load` 하나라 `ctest -L load` |

- 어느 항목도 띄우지 않는 시나리오가 있으면 Windows Release 구성이 멈춘다. 항목이 띄울 수 없는 것(디버거 · 브라우저)만 까닭 · 명령과 함께 `Config/Rules/Test/ManualScenarios.json` 에 적는다.
- 체인 하나가 시나리오 모두를 `-scenario=a,b,c -scenario-keep-going` 으로 돌리고, `-scenario-backends=all` 이 백엔드마다 자식 App 하나를 띄운다(창은 2×2 바둑판). 단계마다 세션이 빈 씬 → 전역 변수 되돌리기 → `<Launch>` → 게임 인스턴스를 새로 세운다(`AutomationSession::startStage`). 단계별 결과는 `Testing/Logs/Scenario.<팩>.<백엔드>.log` 의 `chain stage i/n`.
- 무거운 장면 체인은 `-scenario-backend-slots=1`(`swSequentialScenarioPacks`, 이 폴더 `CMakeLists.txt`)로 차례로 돈다.
- 넷 시나리오: 값 판정은 클라이언트 시나리오의 `<Expect>` · `<WaitUntil watch>`, 두 프로세스 비교는 `<Record>`, 서버 사건은 로그 id 의 수다. 로그 글을 정규식으로 판정하지 않는다(`Scripts/common/log_lines.py` 가 id · 수준 · JSON 본문만 낸다). 주소는 개발 명령 줄이 `{gv_onlineServerAddress}` · `{gv_gameServerAddress}` 로 받는다.
- 보이는 실행(`--show` · `SW_SHOW_WINDOWS=1`)은 디버그 HUD 맨 위에 Scenario 섹션을 그린다(`gv_debugHUDScenario` 로 끈다). 감시 값 `DebugHUD.Shown` · `SectionCount` 는 사용자 HUD 만 센다.

## 결정적 실행

- 시나리오 세션은 `DeterministicRun`(`Engine/Foundation/DeterministicRun.h`)의 `_bFinishAssetJobsEachFrame` 을 켜 요청한 에셋 · PSO 가 셰이더 캐시가 빈 첫 실행에서도 같은 프레임에 보이게 한다. 확인은 `Saved/DDC/Shader` 를 지운 ctest.
- `_bWaitForAnimationClips` 로 애니메이터 시작 클립을 `onBeginPlay` 에서 기다린다. 환경 시간 멈춤은 `-gv_environmentAnimate=0`.
- 물리는 구성마다 같은 비트다(Jolt `CROSS_PLATFORM_DETERMINISTIC=ON`, `Engine/Physics` TU 는 `sw_mark_deterministic_sources` 로 FMA 축약을 끈다). 렌더는 대상이 아니다 — 골든 문턱 안의 백엔드 · 구성 차이는 렌더 차이다.
- 소스 정책(개발 실행)은 셰이더 DDC(`Saved/DDC/Shader`) → 실시간 컴파일이고 쿠킹본을 읽지 않는다. 디버그 코드젠은 `-gv_shaderDebugCodegen=1`. 쿠킹본 정책(배포 · `-cooked-content`)은 팩만 읽는다. 소스 트리의 `shaders/bin` 은 낡은 잔재다.
- 로딩 화면은 씬 로드 알림(`SceneManager::onSceneLoadRequested`, 요청마다 `SceneLoadKind` 필수)에 붙어 뜨고 그동안 게임 시간을 멈춘다. 시나리오의 첫 프레임은 늘 월드의 첫 틱이다.

## 새 시나리오 · 기준 파일(`changed` 단계)

묶음 검증(`Scripts/dev/preflight.sh`)은 빌드 직후 `py -3 Scripts/dev/test_history.py changed --preset Ninja-Release` 로 `git diff origin/main...HEAD` 에서 (a) 바뀐 소스 · 테스트의 기능 폴더를 단위 항목에 `--test-source` 로 좁혀 돌리고, (b) 더해지거나 바뀐 시나리오를 하나씩 제 프로세스로 체인의 백엔드마다 돈다. 기준 파일은 없는 것만 쓰고(`--record-missing-snapshots` · `-scenario-record-missing-golden`) 표 하나(항목 · 새것/바뀐 것 · 실패 종류 · 까닭 · 갈래)를 낸다. 모든 백엔드에서 같은 단언으로 지면 `전제`, 한 백엔드만 지면 `백엔드 <이름>`. 지거나 처음 쓴 기준 파일이 있으면 끝 줄 `CHANGED_FAILED` 로 멈추고, 그림 · 글을 보고 커밋한 뒤 `--from changed` 로 잇는다. 에디터 폴더(`/Editor/`) 케이스는 이 단계가 고르지 않는다(편집기 체인이 돈다).
