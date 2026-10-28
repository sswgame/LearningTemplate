# Linux 와 GPU 검증 구조의 남은 일 (2026-10-18)

> 상태: 참고 · 선행: 없음 · 영역: quality · 옛번호: C-41, E-8 · 진행: 해당없음

2026-10-28: 기능별 테스트 · 시나리오(`Test/` · `Resource/**/automation/`)를 새 프레임워크를 만들려고 전부 지웠다. 이 문서의 테스트 · 시나리오 · `ctest` · `AppTestUtil` 언급은 옛 구조를 가리키며, 새 틀이 정해지면 다시 쓴다.

 셰이더 전 백엔드 확인, 요구 백엔드 정책, 소프트웨어 렌더러 묶음, 앱 러너의 건너뜀(V1, `AppTestUtil::runAppOnBackend`)은 들어갔습니다. GPU 러너와 자체 호스트 러너는 하지 않는다는 결정은 그대로입니다.

## 1. 소프트웨어 렌더러 묶음

`Test/TestPolicy.json` 의 `softwareRenderCases`(라벨 `swrender`)는 GPU 없는 환경에서 소프트웨어 렌더러(`SW_RHI_SOFTWARE_ADAPTER=1`)로 도는 호스트 케이스입니다. 이 묶음을 CI 러너에서 돌리고 보고 전용을 풀던 일은 CI 와 함께 없앴습니다.

고친 것: (1) `RHIDeviceTest.DeviceCreationAllBackends` 가 런타임 없는 백엔드(리눅스의 DX)를 만들었다 — `RHIAvailability` 로 거른다. (2) 소프트웨어 렌더러 실행이 요구하지 않는 백엔드까지 세워 환경 탓 경고를 쏟았다 — `--required_backends=` 가 있으면 그 목록만 세운다(`test::isBackendUnderTest`). 로그를 감싸 가리지 않았다. (3) 리눅스는 llvmpipe 가 `GL_ARB_gl_spirv` 를 내지 않아 OpenGL 백엔드가 서지 못한다(소프트웨어 렌더러의 한계) — `softwareRequiredBackends.linux` 에서 `gl` 을 뺐다(`gl` 은 GPU 있는 PC 의 hostgpu 가 본다).

## 2. 단위

V1 만 남았고 들어갔습니다. CI 결과를 기다리던 V2 · V3 는 CI 를 지우면서 함께 지웠습니다.

### 2.1 배포본(Shipping) 검증의 건너뜀 선언 (`wt/shipscen`, 빌드 대기)

Debug 빌드 · 테스트는 아직 돌리지 않았다. 병합 뒤 Shipping 으로 다시 확인한다.

- `AppScenarioTest`: 시나리오의 `<Launch variable="gv_…">` 또는 호출자의 `-gv_` 인자가 있는데 개발 빌드가 아니면 `AppTestUtil::runScenarioArgument` 가 App 을 띄우지 않고 `Skipped` 로 돌려주고, `test::skipPartIfFeatureMissing` 이 `[ PARTSKIP ] …needs DevelopmentBuild…` 줄을 남긴다. 확인: Shipping `AppTest.exe` 출력에 이 줄이 나오고 `LOAD ERROR` 종료 11 이 없다.
- 원본 리소스를 읽는 케이스 21개(EngineTest 20 · EditorTest 1)는 `SW_TEST_REQUIRES_FEATURE( SourceResources )` 를 선언한다. 확인: Shipping `EngineTest.exe --host_suites=exclude` · `EditorTest.exe` 실패 0, 요약에 `SourceResources 21`.
- `GlobalVariableTest.MultithreadedStringReadWriteThreadSafety` 종료 조건을 "겹친 읽기 100 이상"으로 바꿨다.

새 케이스가 `Resource/game/<게임>/` 원본이나 게임 컴포넌트를 읽으면 같은 선언을 붙여야 한다(빠뜨리면 Shipping `ctest -L nogpu` 가 실패로 보고한다).

## 3. 시나리오 문턱의 근거 (2026-10-25)

사진 지표를 단언하는 시나리오(`<ExpectImage>`)의 문턱이 첫 측정 전의 어림값인 채 커밋되어 어림값과 잰 값이 데이터에서 구분되지 않았습니다. 근본 원인은 측정 전에 숫자를 시나리오에 적는 흐름이 있고 출처가 형식에 없는 것입니다.

- **형식:** 모든 `ExpectImage` 는 `measured="YYYY-MM-DD 백엔드:값 …"`(잰 값) 또는 `provisional="이유"`(어림값) 가운데 정확히 하나를 가져야 합니다. 아니면 시나리오 읽기 오류입니다(`AutomationThresholdBasis`).
- **어림값의 취급:** 틀리면 실패이고, 맞으면 통과(0)가 아니라 미확정(14, `AutomationOutcome::Unconfirmed`)입니다. 보고 JSON 의 `unconfirmed[]` 와 `[Scenario] UNCONFIRMED` 줄이 목록이고 `AppScenarioTest` 는 실패로 세지 않고 `[AppTestUtil] UNCONFIRMED` 로 알립니다.
- **확정하는 법:** GPU 실행의 `metrics` 줄(`metric(영역) 파일 = 값`)을 보고 `provisional` 을 `measured="날짜 all:값"` 으로 바꾸고 문턱도 맞춥니다.
- **문턱 규칙(잰 값 기준):** `atLeast` 는 잰 값의 70 % 아래, `atMost` 는 잰 값의 1.5 배 위(0 이 기준이면 0.001), 부호 있는 값은 0 과 잰 값 사이 절반쯤. 백엔드 사이 차이는 1e-3 아래라 여유가 먹지 않습니다. 근거는 `measured` 속성과 시나리오 머리 주석에 둡니다. **결함의 값은 `measured` 에 넣지 않습니다**(한 백엔드만 다른 값이면 결함의 증거이므로 고친 뒤 다시 재서 더한다).
- **지금 어림값(`provisional`, 확정 대기):** `editor/screenrectview`(단언 3, Release 캡처 어림), 에디터 · 엔진 `debughud` `emptyscene` `flowterrain` `loadingscreen` `meshmodifiers` `sceneviewgameview` `screen` `terrainbrush` `viewmodes` `terrainedits` `runner` `uidemo`(단언 33, GPU 통과만 확인).

잰 값으로 옮긴 것(2026-10-25 GPU 확인, Release, 측정 커밋 `00a6fc842`, 네 백엔드):

| 시나리오 | 지표(영역) | 잰 값(백엔드 사이 차이) | 문턱 |
|---|---|---|---|
| `sky` | `meanRedMinusBlue`(전체) · `meanLuma` | -0.008603 (1e-6) · 0.59539 | atMost -0.004 · atLeast 0.40 |
| `vfxgpulights` | `differingFraction`(바닥) | dx11 0.01258 · dx12 0.01251 · vk 0.01245 · **gl 4.6e-05(결함, 기록 안 함)** | atLeast 0.009 |
| `useroptions` | `differingFraction` 렌더 스케일 · 시야 거리 · 후처리 0 · 색각 | 0.0886~0.0892 · 0.6649~0.6651 · 0.5724~0.5728 · 0.2489~0.2500 | atLeast 0.06 · 0.45 · 0.40 · 0.17 |
| `fog` | `meanLuma` 먼 띠 · 가까운 띠 | 0.47206 · 0.12611 | atLeast 0.35 · atMost 0.19 |
| `realtimegi` | `meanLuma`(큐브 옆면) | 0.10303 | atLeast 0.07 |
| `modulepass` | `meanRedMinusBlue`(전체) | 0.21575 | atLeast 0.15 |
| `killcam` | `meanLuma`(전체) | 0.42197~0.42203 | atLeast 0.30 |
| `motionblur` | `differingFraction` 움직인 켬 · 멈춘 켬 | 0.14398~0.14403 · 0 | atLeast 0.07 · atMost 0.001 |
| `clothshowcase` | `differingFraction` | 0.085762 | atLeast 0.06 |
| `shippingframe` | `meanLuma`(전체) | 0.16349 | atLeast 0.11 |
| `interactionhighlight` | `differingFraction` 외곽선 · 감각 · 반대 영역 둘 | 0.0352 · 0.6942 · 0 · 0 | atLeast 0.025 · 0.50 · atMost 0.001 · 0.001 |

탐침 단언(`Expect probe=…`)은 상태 값이라 이 형식 밖입니다.

### 3.1 같은 GPU 확인에서 진 것 (2026-10-25, 측정 커밋 `00a6fc842`)

- **`useroptions` 시야 거리의 dx12(0.307, 나머지 0.665) — 고쳤다).** D3D12 PSO 서술이 0 으로 시작해 `DepthClipEnable` 이 FALSE 였다(DX11 · Vulkan · GL 은 자르기가 기본). `D3D12RHIResourceFactoryPipeline.cpp` 에서 TRUE 로 고쳤다. 확인: 네 백엔드 모두 distance 0.665 근처, 고친 뒤 dx12 값을 `measured` 에 더한다. 같은 모양(0 으로 시작하는 서술)은 형식으로 막지 못했다 — 래스터라이저 칸을 한 함수가 한 번에 정하게 하는 정리가 남았다.
- **`ShippingSceneMatchesGoldenImage`(dx12)** — 포워드 림을 약하게 한 `f81daa9ff` 로 골든이 낡았던 것이고 다시 떠 통과했다(끝남, 2026-10-25 통합 I). 다른 백엔드의 Shipping 골든은 저장소에 없다(`dx12` 만) — 그 구성을 돌리려면 `build/Ninja-Shipping/Bin` 에서 `SW_UPDATE_GOLDEN=1 AppTest.exe --test_filter=AppSmokeTest.ShippingSceneMatchesGoldenImage` 로 `shippingframe_<백엔드>.ppm.z` 를 더 뜬다.
- **`vfxgpulights` 의 gl(차이 비율 4.6e-05, 나머지 0.0125) — 고쳤다(`e3f348bd5`).** 원인은 행 순서다. GL 의 장면 깊이는 0 행이 화면 아래인데 `vfxsimulate` · `occlusiontiles` 는 D3D 처럼 0 행이 위라고 텍셀을 구해, 입자가 바닥을 지나 떨어지면서 빛이 사라졌다. 충돌 없는 효과는 GL 도 같은 값이라 라이트 쓰기 · 소비는 정상이었고, 충돌 셰이더의 y 를 GL 에서 뒤집는 실험으로 가렸다. `depthhistory.hlsl` 이 GL 에서 원본 깊이의 행을 뒤집어 히스토리를 D3D 순서로 만든다. 고친 뒤 dx12 0.01251 · dx11 0.01258 · vk 0.01245 · gl 0.01246 으로 같아졌다. 문턱 완화로 가리지 않았다.
- **R32F 이미지 형식 불일치 — 고쳤다(`e3f348bd5`).** 컴퓨트 RW 슬롯이 `rgba32f` 로 고정이라 R32F 깊이 히스토리와 GL 이미지 형식이 어긋났다(GL 명세는 이 읽기 값을 정하지 않는다). `SW_DECLARE_RW_TEXTURE2D` 가 GL 이미지 형식을 받고(`SW_RW_SLOT<n>_GL_FORMAT`, 기본 `rgba32f`) 슬롯 0 이 깊이 히스토리인 두 셰이더가 `r32f` 를 적는다. gl 라이트 없음의 주원인은 아니고 같이 있던 별개 결함이다.

## 4. 시나리오의 종료 코드와 처리하지 않은 로그 (2026-10-25)

시나리오가 종료 코드 0(이나 14)으로 끝나도 로그에 `[Warning]` · `[Error]` 가 남아 아무도 고치지 않았습니다. 근본 원인은 로그 판정이 줄 문구의 부분 문자열이고 허용 항목에 처리 계획이 없어 목록을 늘리는 것이 고치는 것보다 쌌던 점입니다. 형식으로 막았습니다.

| 종료 코드 | 이름 | 뜻 |
|---|---|---|
| 0 | `Passed` | 단언이 맞고, 처리하지 않은 Warning · Error 줄이 없다 |
| 10 | `Failed` | 단언 실패 |
| 11 | `LoadError` | 시나리오 · 허용 목록 읽기 오류(허용 목록의 칸이 비었거나 `TODO` 면 여기서 선다) |
| 12 · 13 | `TimedOut` · `Skipped` | 시간 초과 · 이 기계에서 못 돎 |
| 14 | `Unconfirmed` | 단언은 맞았으나 어림값 문턱(`provisional`)이 들어 있다 |
| 15 | `UnhandledLog` | 단언은 맞았으나 허용 목록에도 `<ExpectLog>` 에도 없는 Warning · Error 줄이 남았다. 14 보다 항상 앞선다 |

- **허용 목록은 하나입니다.** `Resource/engine/automation/allowedlogs.json` 의 항목마다 로그 id(`SW_LOG_WARNING_ID` · `SW_LOG_ERROR_ID`), 범위(`scenario` 나 `suite` 중 정확히 하나), 사유(`reason`), 처리 계획 문서 이름(`plan`)이 모두 있어야 읽힙니다. 비었거나 `TODO`, 와일드카드 id, 중복 항목은 읽기 오류(11)입니다. 시나리오 실행기와 테스트 케이스의 판정기(`CaseLogJudge` — 모든 스위트)가 같은 목록을 읽습니다.
- **로그 id 가 없는 줄은 목록으로 가릴 수 없습니다.** 소스에 id 를 붙입니다. 시나리오가 일부러 만드는 줄만 `<ExpectLog contains>` 로 선언합니다.
- **조립 문장 (2026-10-27 재측정).** id 없는 Warning · Error 호출 3524개 가운데 서식 구멍(`%#` 등)이 낀 조립 문장이 2975개라 한꺼번에 올릴 수 없다(GameFramework/Base 363 · Kits 342, Engine/Graphics 295, Editor/Common 292, Engine/Object 171 …). 구조로는 `CheckExpectLogPhrases` 의 `ExpectLog/AssembledLine` 이 시나리오가 `contains` 로 선언하는 id 없는 조립 문장을 실패로 센다(지금 시나리오 98곳 중 0건). 나머지는 시나리오가 그 줄을 내는 순간 실행기가 처리하지 않은 로그(15)로 세므로, 그때 해당 호출에 id 를 붙인다. 붙인 곳: `NetHost` 가 잠금 안에서 모았다가 `update` 가 쓰는 거절 경고 둘(`Net.ConnectionRefused` · `Net.ServerRefused`) — 글이 `%#` 한 구멍뿐이라 린트가 조립 문장으로 볼 수 없던 호출이고, 종류 열거로 id 가 소스에 글자로 남는다.
- **래칫:** 목록에 있으나 끝까지 도는 시나리오에서 더 나오지 않는 항목은 `py -3 -m Scripts scenario-logs --check` 가 실패로 셉니다. 처리하지 않은 로그(15)로 끝난 보고가 있어도 실패입니다.
- **처음 채울 때:** 시나리오 보고(`-scenario-report`)를 모은 뒤 `py -3 -m Scripts scenario-logs --regen` 이 id 있는 줄을 `reason` · `plan` 이 `TODO` 인 초안 항목으로 더하고 사람이 채웁니다. id 없는 줄은 표로 찍히고 소스 쪽을 먼저 고칩니다.
- **남은 일:** 허용 목록은 첫 GPU 검증 보고를 받아 채우므로 지금 비어 있습니다(Warning · Error 를 남기는 시나리오는 15 로 끝난다). `plan` 칸이 가리키는 문서가 실제로 있는지는 기계가 확인하지 않습니다.

## 5. 사용자 결정

없습니다.

## 6. 명령 없는 검증 항목 (2026-10-28)

검증 묶음의 명령 없는 항목은 이유가 둘로 갈린다: `manual`(사람 눈 · 귀 · 장비 · 외부 에셋이 있어야 한다 — 42 개) 과 `need_command`(사람이 필요 없는데 명령을 못 만들었다 — 0 개). 읽을 때 일회성(`pending`) 항목은 명령이 있거나 `manual` 이어야 하고, 명령 없는 항목은 어느 문서의 `매니페스트 id` 줄이 가리켜야 한다(주인 문서).

- **구조로 닫은 것:** 상태가 `측정대기` 인 계획 문서는 돌릴 명령이 있는 항목(`manual` · `need_command` 가 아닌 것)을 `매니페스트 id` 줄로 하나는 가리켜야 한다(`findMeasureWaitViolations`, `plans --check` · `CheckVerifyPending` 이 부른다). 사람 확인만 남은 문서는 `측정대기` 로 둘 수 없다 — 아무도 못 푸는 대기가 쌓이는 모양을 막는다.
- **`result_doc` 정리:** 옛 `docs/plans/NEXT.md 0000절`(232 곳)은 이미 실제 문서로 바뀌어 있었다. 다시 생기지 않게 `findDocReferenceProblems` 가 `result_doc` 이 인계 · 규칙 문서(`NEXT.md` 등)를 가리키거나 `N절` 번호의 제목이 문서에 없으면 `검증 묶음 --check` · `plans --check` 에서 거절한다(번호 없는 절 이름은 사람이 읽는 글이라 확인하지 않는다).
- **통과한 일회성 항목:** 통합 S 의 `app-scenarios-release` 통과(1300 s, 시나리오 Warning/Error 0)로 덮이는 `covered` 일회성 14 개 가운데 보고 · 사진 · 첫 측정값을 사람이 읽어야 닫히지 않는 것을 뺀 7 개(`ability-arena-ai-control-scenarios` · `game-pack-scenarios` · `assertdialog-scenario` · `standard-input-commands` · `scene-reference-scenario` · `sync-tab-object-diff-scenario` · `scenarios-new-q2`)의 항목과 문서의 대기 글을 지웠다. 남은 7 개(`module-pass-scenario` 문턱 확정 · `shooter-*-scenario` 넷 · `wildwest-mount-scenario` 사진 확인 · `usersettings-scenario` dx12 `measured` 더하기)는 통과만으로 닫히지 않는다. 그 밖의 일회성 test · measure 항목은 마지막 통합의 항목별 결과 표가 저장소에 없어(`build/검증 묶음` 는 낡은 한 판뿐) 통과를 확인하지 못했다.
- **`need_command` 둘을 닫았다 (명령 없는 `need_command` 는 이제 0):** `shooter-crowd-measure` 는 perf-compare 케이스가 이미 있었다(`crowdShare` — 벤치 KayKit 기사 30 명, 군중 공유 끔 대 켬). 항목에 `perf-compare --cases crowdShare --runs 3` 을 달았다(`--measure` 로 돈다, 실행은 통합 몫). `paired-animation-two-process` 는 러너가 아니라 씬이 없다: `RunDestructionNetPair`(Server `-gv_netListenPort` + App `-gv_netConnectAddress`, 로그 맞댐)는 같은 모양으로 재사용할 수 있지만 `PairedAnimationNetSessionComponent` 를 단 씬과 세션 시작 · 늦은 참가 · 끝을 구동하는 시나리오가 데이터에 없다. 씬 저작이 선행이라 `manual`(저작 작업)로 분리했다 — [Candidates-gameplay](Candidates-gameplay.md).
- **통합에서 확인 ):** 위에서 항목을 지운 시나리오 일곱 묶음이 통합의 `ctest -L hostgpu` 에서 실제로 통과로 오르는지 케이스 이름으로 본다 — `game/abilityarena/automation/ai` · `control`, 게임 팩 시나리오 전부(`AppScenarioTest.<게임>.*` × 네 백엔드), `engine/automation/editor/assertdialog` · `netconditions`(표준 입력 명령) · `scenereference` · `netsyncobjects`, 새 시나리오 일곱 `editor/audiographpanel` · `behaviortreepanel` · `worldcellspanel` · `reflectionprobebake` · `wildwest/autoplaymount` · `frontiertown/autoplayclock` · `meadowvillage/autoplayclock`. 하나라도 건너뜀이거나 빠져 있으면 지운 항목을 되살리지 말고 그 시나리오를 고친다.
