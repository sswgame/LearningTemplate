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
- **문턱을 정하는 규칙(잰 값 기준):** 켜져 있어야 하는 쪽의 하한(`atLeast`)은 잰 값의 70 % 아래, 꺼져 있어야 하는 쪽의 상한(`atMost`)은 잰 값의 1.5 배 위(0 이 기준이면 0.001), 부호가 있는 값(`meanRedMinusBlue`)은 0 과 잰 값 사이 절반쯤에 둡니다.
  백엔드 사이 차이는 1e-3 아래라 여유가 그것을 먹지 않습니다. 근거는 `measured` 속성(날짜 · 백엔드별 값)과 시나리오 머리 주석(측정 커밋 · 구성)에 둡니다.
  **결함의 값은 `measured` 에 넣지 않습니다** — 한 백엔드만 다른 값이면(예: `useroptions` 시야 거리의 dx12 0.307, `vfxgpulights` 의 gl 4.6e-05) 그 값은 결함의 증거이므로 적지 않고, 고친 뒤 다시 재서 더합니다.
- **지금 어림값(`provisional`):** 아래 목록이 확정 대기입니다.

| 시나리오 | 단언 수 | 근거 |
|---|---|---|
| `editor/screenrectview` | 3 | Release 캡처로 어림한 값, 문턱 미측정 |
| 에디터 · 엔진 `debughud` `emptyscene` `flowterrain` `loadingscreen` `meshmodifiers` `sceneviewgameview` `screen` `terrainbrush` `viewmodes` `terrainedits` `runner` `uidemo` | 33 | GPU 통과만 확인, 잰 값 기록 없음 |

잰 값(`measured`)으로 옮긴 것은 아래와 같습니다. 2026-10-25 열은 GPU 확인(Release, 측정 커밋 `00a6fc842`, 네 백엔드 dx11 · dx12 · vk · gl)의 `metrics` 줄이고, 문턱은 위 규칙으로 정했습니다.

| 시나리오 | 지표(영역) | 잰 값(백엔드 사이 차이) | 문턱 |
|---|---|---|---|
| `sky` | `meanRedMinusBlue`(전체) | -0.008603 (1e-6) | atMost -0.004 (처음 어림값 -0.02 는 흰 구름이 평균을 끌어올려 틀렸다) |
| `sky` | `meanLuma`(전체) | 0.59539 (1e-6) | atLeast 0.40 |
| `vfxgpulights` | `differingFraction`(바닥) | dx11 0.01258 · dx12 0.01251 · vk 0.01245 · **gl 4.6e-05(결함, 기록 안 함)** | atLeast 0.009 (처음 0.02 는 틀렸다) |
| `useroptions` | `differingFraction` 렌더 스케일 · 시야 거리 · 후처리 0 · 색각 | 0.0886~0.0892 · 0.6649~0.6651(**dx12 0.3073 은 DX12 깊이 클립이 꺼진 결함의 값이라 기록 안 함, 고침**) · 0.5724~0.5728 · 0.2489~0.2500 | atLeast 0.06 · 0.45 · 0.40 · 0.17 |
| `fog` | `meanLuma` 먼 띠 · 가까운 띠 | 0.47206~0.47207 · 0.12611 | atLeast 0.35 · atMost 0.19 |
| `realtimegi` | `meanLuma`(큐브 옆면) | 0.10303 (1e-7) | atLeast 0.07 |
| `modulepass` | `meanRedMinusBlue`(전체) | 0.21575~0.21576 | atLeast 0.15 |
| `killcam` | `meanLuma`(전체) | 0.42197~0.42203 | atLeast 0.30 |
| `motionblur` | `differingFraction` 움직인 켬 · 멈춘 켬 | 0.14398~0.14403 · 0(네 백엔드) | atLeast 0.07 · atMost 0.001 |
| `clothshowcase` | `differingFraction`(천이 움직였는가) | 0.085762~0.085763 | atLeast 0.06 |
| `shippingframe` | `meanLuma`(전체) | 0.16349 (정확히 같다) | atLeast 0.11 |
| `interactionhighlight` | `differingFraction` 외곽선 · 감각 · 반대 영역 둘 | 0.03519~0.03522 · 0.69418~0.69421 · 0 · 0 | atLeast 0.025 · 0.50 · atMost 0.001 · 0.001 |

이전에 잰 값으로 옮긴 것은 `ditherappear` 5 · `light2dcookie` 6 · `themepark/shadow` 5 입니다(기존 주석의 첫 측정 값).
탐침 단언(`Expect probe=… equals/atLeast`)은 상태 값이라 이 형식 밖입니다. 개수처럼 코드에서 정해지는 값을 손으로 적던 틈(`editorstartup` 의 패널 수)은 `wt/editorsc` 가 대표 패널 id 질의(`Editor.HasPanel`)로 바꿨습니다.

### 3.1 같은 GPU 확인에서 진 것 넷 (2026-10-25, 측정 커밋 `00a6fc842`)

- **`useroptions` 시야 거리의 dx12(0.307, 나머지 0.665) — 원인을 찾아 고쳤다(매니페스트 id `usersettings-scenario`).** DX12 의 PSO 서술(`D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{}`)이 0 으로 시작해 `RasterizerState.DepthClipEnable` 이 FALSE 였다. DX11 은 TRUE 로 적고
  Vulkan(`depthClampEnable` 끔) · GL(`GL_DEPTH_CLAMP` 끔)은 기본이 자르기라, DX12 만 가까운 · 먼 평면 밖 기하를 지우지 않고 깊이 1 로 눌러 그렸다. `gv_viewDistanceScale` 0.05 가 먼 평면을 100 m → 5 m 로 당겨도 DX12 에서만 먼 바닥이 남아 바뀐 화소가 절반이었다.
  `D3D12RHIResourceFactoryPipeline.cpp` 에서 `DepthClipEnable = TRUE` 로 고쳤다. 확인: `useroptions` 시나리오가 네 백엔드 모두 distance 0.665 근처로 나오고(문턱 0.45), 고친 뒤 dx12 값을 `measured` 에 더한다.
  같은 모양: 0 으로 시작하는 D3D12 서술의 다른 칸은 DX11 이 적는 칸(`FillMode` · `CullMode` · 깊이 바이어스)을 모두 적고 있었다. 컴퓨트 PSO 는 래스터라이저가 없다. 이 틈은 형식으로 막지 못했다(D3D12 서술이 0 시작) — 래스터라이저 칸을 한 함수가 한 번에 정하게 하는 정리가 남았다.
- **`ShippingSceneMatchesGoldenImage`(dx12, 32565 화소 초과) — 의도된 변경이고 골든이 낡았다. 골든을 다시 뜨면 된다.** 근거는 아래 넷이다.
  (1) 골든 `shippingframe_dx12.ppm.z` 는 2026-10-22 08:23(`f38748169`) 한 번만 떴고 그 뒤 고친 기록이 없다. 그날 08:22 의 Shipping 캡처와 중앙 상자 앞면 값이 같다((88, 77, 63)).
  (2) `f81daa9ff`(2026-10-23, H7)가 **포워드 림을 pow2 × 0.15 에서 pow3 × 0.12 로 약하게** 바꿨다. 커밋 글이 "포워드 화면의 림이 약해져 벤치 골든(opaque · transparent × 네 백엔드)을 다시 떴다" 라고 적고 Shipping 첫 화면 골든은 뜨지 않았다(Shipping 에서만 도는 케이스라 그 자리에서 안 돌았다).
  (3) 이 장면은 포워드이고 방향광 하나(`_intensity` 1.35 · `_ambient` 0.28)뿐이다. 림은 `키 라이트 색 × 림` 을 알베도 없이 더하고(`Lighting::computeRimLight`), 카메라 쪽으로 향한 앞면과 윗면은 둘 다 림 인수가 1 이라 바뀐 몫이 0.03 × 키 색이다. 실제로 앞면이 (88, 77, 63) → (80, 70, 56) 로 어두워졌고(R 이 G · B 보다 더 줄어 키 색 방향이다), 밝은 윗면은 톤 매핑이 눌러 (188, 178, 162) → (187, 177, 161) 로 1 만 변했다.
  달라진 화소는 모두 상자 하나(550~729 × 511~718, 37074 화소)이고 그 밖(배경 · 스프라이트)은 채널 차이가 0 이다(골든과 이번 캡처를 직접 견줬다).
  (4) 네 백엔드의 이번 캡처는 평균 휘도가 17 자리까지 같다(0.16348558525975887) — DX12 만의 결함이 아니라 공유 조명 식의 변화다.
  판정: 코드를 고칠 일이 없다. 절차(매니페스트 id `shipping-golden-dx12`, 통합이 Shipping 한 번에): `build/Ninja-Shipping/Bin` 에서 `SW_UPDATE_GOLDEN=1` 로 `AppTest.exe --test_filter=AppSmokeTest.ShippingSceneMatchesGoldenImage` 를 돌려 `Test/AppTest/Golden/shippingframe_dx12.ppm.z` 를 뜨고, 같은 환경변수 없이 한 번 더 돌려 통과를 확인한다.
  뜬 뒤 그림을 PNG 로 바꿔 눈으로 본다(상자 앞면이 어둡고 윗면이 밝으며 배경 (27, 33, 39) 이 그대로여야 한다). **끝남(2026-10-25 통합 I)**: 다시 떴고 변수 없이 통과, 앞면 (80, 70, 56) · 배경 (27, 33, 39) 로 위 판정과 같다. 다른 백엔드의 Shipping 골든은 저장소에 없다(`dx12` 만 있다) — 그 구성을 돌리려면 같은 절차로 `shippingframe_<백엔드>.ppm.z` 를 더 뜬다.
- **`vfxgpulights` 의 gl(차이 비율 4.6e-05, 나머지 0.0125) — 원인을 아직 못 찾았다(매니페스트 id `vfx-gpu-lights-scenario`).** 이 시나리오는 gl 에서 진다(문턱 완화로 가리지 않았다). 코드로 가린 것:
  쿠킹된 `vfxsimulate_cs.spv` 를 직접 읽어 GL 판과 Vulkan 판을 견줬다 — 라이트 버퍼(u3)의 원소 배치(stride 80 · 오프셋 0/16/32/48/64)와 저장 수(1)가 같고 GL 바인딩은 SSBO 19(= 16 + 3)로 계약과 맞다.
  GL 실행 로그에는 GL 오류 · 경고가 없고, 불똥은 그려진다(캡처에 60 여 개의 점) — 시뮬레이션은 돈다. 라이트만 없다. 그래서 `Vfx.md` 의 후보 "R32 실수 RW 텍스처 읽기"는 주원인으로 보기 어렵다: 깊이를 못 읽어 충돌을 놓쳐도 불똥은 바닥 위에서 첫 0.5 초 동안 빛을 내야 하는데 gl 은 0 이다.
  남은 후보와 가리는 법(통합, GPU): ① 시뮬레이션 직후 라이트 버퍼의 GPU 구간을 비동기 되읽기(`AsyncBufferReadback`)로 읽어 반경 · 세기가 써졌는지 본다(써졌으면 그리드 · 조명 쪽, 안 써졌으면 컴퓨트 쪽이다).
  ② `-gv_lightCulling 1`(CPU 생산자)은 GPU 구간을 거절하고 오류를 남기므로 쓸 수 없다. ③ 같은 SPIR-V 가 Vulkan 에서 맞으므로 드라이버 컴파일 차이(NVIDIA GL SPIR-V 경로)라면 `resolveDepthCollision` 의 이른 반환 일곱 개를 한 출구로 바꾼 판(이 저장소가 이미 `evaluateCurve` 로 한 우회)을 패치로 시험한다.
  (낡은 바이너리 탓은 아니다 — 같은 실행의 dx11 · dx12 · vk 가 같은 소스의 새 바이너리로 맞는 값을 냈다. 이 판단은 그 세 값이 맞다는 데서 나온 추론이다.)
- **R32F 이미지 형식 불일치(발견, 영향 미확인).** 쿠킹된 컴퓨트 SPIR-V 의 RW 이미지는 모두 `Rgba32f` 로 선언되는데(`RWTexture2D<float4>`), 깊이 히스토리는 R32F 텍스처이고 GL 은 `glBindImageTexture` 에 `GL_R32F` 를 준다. GL 명세는 이 불일치의 읽기 값을 정하지 않는다(같은 불일치로 RGBA8 텍스처에 쓰는 `RHIDeviceTest.ComputeTextureUavWriteIsReadable` 가 GL 에서 통과하는지는 이번에 확인하지 못했다).
  이 틈이 가림 타일 · 깊이 충돌을 GL 에서 틀리게 만드는지는 GPU 로 가린다 — 고치려면 슬롯마다 `[[vk::image_format]]` 을 다르게 줘야 하는데 셰이더 소스라 패치로 넘긴다.

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
