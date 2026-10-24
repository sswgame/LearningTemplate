# Linux 와 GPU 검증 구조의 남은 일 (2026-10-18)

> 상태: 구현중 · 선행: 없음 · 영역: quality · 옛번호: C-41, E-8 · 진행: 1/3

C-41 의 순서는 결정 기록 5-12 `C-41` 행입니다. 네 단계 가운데 CI 상태 알림, 셰이더 전 백엔드 확인, 요구 백엔드 정책, 소프트웨어 렌더러 묶음은 들어갔습니다.
이 문서는 남은 한 가지를 구현 단위로 나눕니다. 앱 러너의 건너뜀(V1)은 `AppTestUtil::runAppOnBackend` 로 들어갔습니다. GPU 러너와 자체 호스트 러너는 하지 않는다는 결정은 그대로입니다.

## 1. CI 소프트웨어 렌더러 단계의 보고 전용 해제

### 1.1 지금 구조

`ci.yml` 의 `Test on software renderer` 단계는 `continue-on-error: true` 입니다. 실패는 주석(annotation)으로만 보이고 잡은 초록입니다.
`Test/TestPolicy.json` 의 `softwareRenderSeconds`(120)는 추정값입니다.

2026-10-20 확인(main 의 초록 실행 39163263777 · 39162795196 · 39160434493): 단계 시간은 Windows 2~4 초, Linux 6 초(설치 4~6 초)지만 세 번 모두 진 것입니다. `continue-on-error` 가 단계 결론을 success 로 보이게 할 뿐 주석 단계가 돌았으니(`outcome == 'failure'`) 초록이 아닙니다. 주석: Linux 는 `RHIDeviceTest.DeviceCreationAllBackends`(DX11 · DX12 백엔드 없음 로그를 예상 밖 로그로 침)와 `gl` 백엔드가 서지 못함, Windows 는 `BackendSweepVisitsEveryBackendThatStandsUp`(검증 레이어 없음 · GL 4.5 / KHR_debug 없음 등 16 줄을 예상 밖 로그로 침). 그래서 V2 는 해제하지 않았습니다. 케이스를 고치거나 의도한 로그를 `SW_TEST_DEFENSIVE_SCOPE` 로 감싼 뒤 다시 셉니다.

고친 것(Windows Debug 빌드 · 로컬 확인, CI 미확인 — `RHIDeviceTest.*` 39/39, `BackendSweep*` 는 목록 없이 · `--required_backends=dx12` 로 모두 통과): 근본 원인은 두 가지였습니다. (1) `DeviceCreationAllBackends` 가 이 호스트에 런타임이 없는 백엔드(리눅스의 DX)를 그대로 만들었다 — `RHIAvailability` 로 거른다. (2) 소프트웨어 렌더러 실행이 요구하지 않는 백엔드(Windows 러너의 Vulkan · GL)까지 세워 보며 환경 탓 경고를 쏟았다 — `--required_backends=` 가 있으면 그 목록만 세운다(`test::isBackendUnderTest`, `RHITestDevice` 가 따른다). 로그를 감싸 가리지 않았습니다.
2026-10-20 두 번째 확인(수정이 든 main 실행 39343813870 · 39342751204 · 39339645876): Windows(WARP)는 세 번 모두 통과(단계 3 · 3 · 4 초). 리눅스는 세 번 모두 졌고(단계 8 · 5 · 3 초) 원인은 설치 패키지가 아니라 llvmpipe 가 `GL_ARB_gl_spirv` 를 내지 않아 OpenGL 백엔드가 서지 못하는 것입니다(주석: `OpenGL 백엔드를 쓸 수 없습니다 — GL_ARB_gl_spirv 없음`). 소프트웨어 렌더러의 한계라 `softwareRequiredBackends.linux` 에서 `gl` 을 뺐습니다(`gl` 은 GPU 있는 PC 의 hostgpu 가 봅니다). 이 수정은 CI 로 확인하지 못했습니다.
남은 것: 수정이 든 다음 main 실행부터 Windows · Linux 가 연속 세 번 통과하면 `continue-on-error` 를 지우고 `softwareRenderSeconds` 를 그 시간에 여유를 둔 값으로 고칩니다. 지금까지 통과한 연속 실행은 없습니다(Windows 만 3 회).

### 1.2 해법

- `py -3 -m Scripts ci-jobs --annotations` 로 main 의 최근 실행에서 `swrender` 주석을 봅니다. Windows(WARP) 와 Linux(lavapipe, xvfb) 두 잡이 연속 세 번 이상 초록이면 `continue-on-error` 를 지웁니다.
- 같은 실행들에서 단계 시간을 읽어 `softwareRenderSeconds` 를 고칩니다. 가장 긴 값에 여유를 두고, 그 근거(실행 번호와 측정값)를 `_softwareRenderCasesReason` 에 적습니다.
- 초록이 아니면 지는 케이스를 고치는 것이 먼저입니다. 케이스를 목록에서 빼는 것은 소프트웨어 렌더러의 한계(예: WARP 픽셀 검증)일 때만 하고 이유를 적습니다.

## 2. 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| V2 | 지는 케이스 수정(들어감, 로컬 확인) → 리눅스 `gl` 원인 확인(llvmpipe 에 GL_ARB_gl_spirv 없음, 정책에서 뺌, CI 미확인) → 수정이 든 CI 가 연속 세 번 통과한 뒤 CI 주석과 단계 시간 확인 뒤 보고 전용 해제, 초 수정 | `.github/workflows/ci.yml`, `Test/TestPolicy.json` | 없음(빌드 없음) | 다음 main CI 실행 | 필요합니다. CI 단계 시간을 세 번 이상 읽습니다 |

| V3 | WARP 픽셀 테스트의 CI 실패 가르기(옛 `E-8`) — V2 로 `continue-on-error` 를 지운 뒤 `RenderPassGPUTest.*` 를 `softwareRenderCases` 에 더해 러너의 WARP 판 · 창 없는 세션 탓인지 주석으로 가른다. 이 PC 의 WARP(`SW_RHI_SOFTWARE_ADAPTER=1`, DX12 · DX11)에서는 2026-10-06 에 `RenderPassGPUTest.*` 71 개가 모두 통과했다 | `Test/TestPolicy.json`, `.github/workflows/ci.yml` | V2 | CI 의 `swrender` 주석 | 필요합니다. CI 단계 시간으로 `softwareRenderSeconds` 를 다시 잡는다 |

V2 는 판단이 거의 없어 `mech-worker` 에 맡길 수 있습니다. V3 은 V2 가 초록인 뒤라야 의미가 있다 — 보고 전용 단계에 지는 케이스를 더하면 V2 의 "연속 세 번 초록" 이 늦어진다.

### 2.1 배포본(Shipping) 검증의 건너뜀 선언 (`wt/shipscen`, 빌드 대기)

GPU 확인(main d679388a5 의 Shipping)에서 나온 거짓 실패 세 가지를 구조로 막았다. 코드는 들어갔고 **Debug 빌드 · 테스트는 아직 돌리지 않았다**(동시 빌드 칸이 차 있었다). 병합 뒤 Shipping 으로 다시 확인한다.

- `AppScenarioTest`: 시나리오의 `<Launch variable="gv_…">`(파서가 `gv_` 만 허용) 또는 호출자의 `-gv_` 인자가 있는데 개발 빌드가 아니면 `AppTestUtil::runScenarioArgument` 가 App 을 띄우지 않고 `Skipped` 로 돌려준다. 테스트 틀의 `test::skipPartIfFeatureMissing` 이 케이스 보고에 `[ PARTSKIP ] <시나리오>: <이유> - needs DevelopmentBuild, not in this build (Shipping)` 줄과 요약의 "Skipped parts inside cases … DevelopmentBuild N" 을 남긴다. 확인: Shipping `AppTest.exe` 의 `AppScenarioTest` 출력에 이 줄과 개수가 나오고 `LOAD ERROR` 종료 11 이 없다.
- 원본 리소스를 읽는 케이스 21개(EngineTest 20 · EditorTest 1)는 `SW_TEST_REQUIRES_FEATURE( SourceResources )` 를 선언한다(Shipping 에서 건너뜀, Debug · Release 에서는 그대로 돈다). 확인: Shipping 의 `EngineTest.exe --host_suites=exclude` · `EditorTest.exe` 가 실패 0, 요약에 `SourceResources 21`.
- `GlobalVariableTest.MultithreadedStringReadWriteThreadSafety`: 종료 조건을 쓴 횟수 상한에서 "겹친 읽기 100 이상"으로 바꿨다.

새 케이스가 `Resource/game/<게임>/` 원본이나 게임 컴포넌트를 읽으면 같은 선언을 붙여야 한다. 빠뜨리면 Shipping `ctest -L nogpu` 가 그 케이스를 실패로 보고한다(조용한 통과는 없다).

## 3. 시나리오 문턱의 근거 (2026-10-25)

사진 지표를 단언하는 시나리오(`<ExpectImage>`)의 문턱이 첫 측정 전의 어림값인 채 커밋되어, 어림값과 잰 값이 데이터에서 구분되지 않았습니다(`sky` · `vfxgpulights` 의 어림값, 에디터 시작 시나리오의 틀린 개수). 근본 원인은 측정 전에 숫자를 시나리오에 적는 흐름이 있고 그 숫자의 출처가 형식에 없는 것입니다.

- **형식:** 모든 `ExpectImage` 는 `measured="YYYY-MM-DD 백엔드:값 …"`(잰 값) 또는 `provisional="이유"`(어림값) 가운데 정확히 하나를 가져야 합니다. 없거나 형식이 틀리면 시나리오 읽기 오류입니다(`AutomationThresholdBasis`). 옛 형식 리더는 두지 않았고 시나리오 데이터를 다시 썼습니다.
- **어림값의 취급:** 어림값 문턱은 틀리면 그대로 실패이고, 맞으면 통과(0)가 아니라 미확정(14, `AutomationResult::Unconfirmed`)으로 끝납니다. 보고 JSON 의 `unconfirmed[]` 와 `[Scenario] UNCONFIRMED` 줄이 목록입니다. `AppScenarioTest` 는 실패로 세지 않고 `[AppTestUtil] UNCONFIRMED` 로그로 알립니다.
- **확정하는 법:** GPU 실행의 `metrics` 줄(`metric(영역) 파일 = 값`)을 보고 `provisional` 을 `measured="날짜 all:값"` 으로 바꿉니다. 문턱 숫자도 그 값에 맞춰 정합니다.
- **지금 어림값(`provisional`):** 아래 목록이 확정 대기입니다. `sky` 2개 · `vfxgpulights` 1개는 `wt/gfxfix` 가 고친 무대 · 캡처 프레임 기준의 어림값으로 통합 G 에서 달았습니다.

| 시나리오 | 단언 수 | 근거 |
|---|---|---|
| `fog` · `realtimegi` · `modulepass` | 2 · 1 · 1 | 첫 측정 전 어림값 |
| `interactionhighlight` · `useroptions` · `killcam` | 4 · 4 · 1 | 측정 전 추정 |
| `editor/screenrectview` | 3 | Release 캡처로 어림한 값, 문턱 미측정 |
| `motionblur` | 1 | 멈춘 쪽 다른 화소 비율 기록 없음 |
| 에디터 · 엔진 `debughud` `emptyscene` `flowterrain` `loadingscreen` `meshmodifiers` `sceneviewgameview` `screen` `terrainbrush` `viewmodes` `terrainedits` `runner` `uidemo`, 게임 `clothshowcase` `shippingframe` | 35 | GPU 통과만 확인, 잰 값 기록 없음 |
| `sky` · `vfxgpulights` | 3 | 첫 측정 전 어림값(`wt/gfxfix` 의 해 46° · 밝은 바닥 뒤) |

잰 값(`measured`)으로 옮긴 것은 `ditherappear` 5 · `light2dcookie` 6 · `motionblur` 1 · `themepark/shadow` 5 입니다(기존 주석의 첫 측정 값).
탐침 단언(`Expect probe=… equals/atLeast`)은 상태 값이라 이 형식 밖입니다. 개수처럼 코드에서 정해지는 값을 손으로 적던 틈(`editorstartup` 의 패널 수)은 `wt/editorsc` 가 대표 패널 id 질의(`Editor.HasPanel`)로 바꿨습니다.

## 4. 시나리오의 종료 코드와 처리하지 않은 로그 (2026-10-25)

시나리오가 종료 코드 0(이나 14)으로 끝나도 로그에 `[Warning]` · `[Error]` 가 남아 있으면 아무도 고치지 않았습니다. 근본 원인은 로그 판정이 줄 문구의 부분 문자열이고 허용 항목에 범위 · 사유의 처리 계획이 없어, 목록을 늘리는 것이 고치는 것보다 쌌던 점입니다. 형식으로 막았습니다([결정 기록](../09_Decisions.md) 2026-10-25 "처리하지 않은 로그").

| 종료 코드 | 이름 | 뜻 |
|---|---|---|
| 0 | `Passed` | 단언이 맞고, 처리하지 않은 Warning · Error 줄이 없다 |
| 10 | `Failed` | 단언 실패 |
| 11 | `LoadError` | 시나리오 · 허용 목록 읽기 오류(허용 목록의 칸이 비었거나 `TODO` 면 여기서 선다) |
| 12 · 13 | `TimedOut` · `Skipped` | 시간 초과 · 이 기계에서 못 돎 |
| 14 | `Unconfirmed` | 단언은 맞았으나 어림값 문턱(`provisional`)이 들어 있다 |
| 15 | `UnhandledLog` | 단언은 맞았으나 허용 목록에도 `<ExpectLog>` 에도 없는 Warning · Error 줄이 남았다. 14 보다 항상 앞선다 |

- **허용 목록은 하나입니다.** `Resource/engine/automation/allowedlogs.json` 의 항목마다 로그 id(`SW_LOG_WARNING_ID` · `SW_LOG_ERROR_ID`), 범위(`scenario` 나 `suite` 중 정확히 하나), 사유(`reason`), 처리 계획 문서 이름(`plan`)이 모두 있어야 읽힙니다. 하나라도 비었거나 `TODO` 이면, 와일드카드 id 이거나 같은 항목이 두 번이면 읽기 오류(11)로 멈춥니다. 시나리오 실행기와 호스트 테스트 스위트(`HostLogJudge`)가 같은 목록을 읽어 예전 `SW_TEST_KNOWN_LOG` 는 없앴습니다.
- **로그 id 가 없는 줄은 목록으로 가릴 수 없습니다.** 소스에 `SW_LOG_WARNING_ID` · `SW_LOG_ERROR_ID` 를 붙여야 합니다. 시나리오가 일부러 만드는 줄만 시나리오의 `<ExpectLog contains>` 로 선언합니다.
- **래칫:** 목록에 있으나 끝까지 도는 시나리오에서 더 이상 나오지 않는 항목은 `py -3 -m Scripts scenario-logs --check` 가 실패로 셉니다(보고 `allowedLogs[]` 로 비교하며, 백엔드마다 다를 수 있어 보고 전체를 모아 본다). 처리하지 않은 로그(15)로 끝난 보고가 있어도 실패입니다.
- **처음 채울 때:** 시나리오를 돌려 보고(`-scenario-report`)를 모은 뒤 `py -3 -m Scripts scenario-logs --regen` 이 id 가 있는 줄을 `reason` · `plan` 이 `TODO` 인 초안 항목으로 더합니다. TODO 가 남으면 모든 시나리오가 읽기 오류로 서므로 사람이 사유와 계획을 채워야 합니다. id 가 없는 줄은 표로 찍히고 소스 쪽을 먼저 고칩니다.
- **남은 일:** 현재 로그를 모은 표는 `gpu-verifier` 의 첫 보고를 받아 채우므로 허용 목록은 비어 있습니다(그래서 Warning · Error 를 남기는 시나리오는 지금 15 로 끝납니다). `plan` 칸이 가리키는 문서가 실제로 있는지는 아직 기계가 확인하지 않습니다.

## 5. 사용자 결정

없습니다.
